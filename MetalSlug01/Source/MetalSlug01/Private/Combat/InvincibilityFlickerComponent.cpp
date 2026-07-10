// ==========================================
// 复活无敌期视觉闪烁 Component 实现 【2026.07.14 P0 大厂架构】
//
// 架构:
//   HealthComponent (数据) → OnInvincibilityChanged (事件)
//      ↓
//   UInvincibilityFlickerComponent (视觉编排)
//      ├─ PrepareMaterials: 收集 MID, 验证协议
//      ├─ OnFlickerStarted/Stopped: BlueprintAssignable → BP Timeline
//      └─ 零跨边界: 不触碰武器, 不触碰 HealthComp 数据
// ==========================================
#include "Combat/InvincibilityFlickerComponent.h"

// BaseCharacter: ResolveHealthComponent / GetMesh
#include "Characters/BaseCharacter.h"

// HealthComponent: OnInvincibilityChanged 订阅
#include "Components/HealthComponent.h"

// Material / Mesh
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"  // FMaterialParameterInfo + GetAllScalarParameterInfo
#include "Components/SkeletalMeshComponent.h"


// ==========================================
// 1. 构造函数
// ==========================================
/**
 * UInvincibilityFlickerComponent 构造函数
 *
 * 大厂原则 - 零 Tick:
 *   - 视觉闪烁由 BP 的 Timeline 驱动 (60Hz), 不需要 Component Tick
 *   - 完全事件驱动: OnInvincibilityChanged → OnFlickerStarted → BP Timeline
 */
UInvincibilityFlickerComponent::UInvincibilityFlickerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// 不需要 SetIsReplicatedByDefault:
	//   - 服务器/客户端各自本地订阅 OnInvincibilityChanged
	//   - 双发保证: 服务器激活时调用 Broadcast, 客户端通过 Replicated OnRep 自动 Broadcast
	//   - 因此每台机器本地都能恰好触发一次 OnFlickerStarted (不需要跨网络)
}


// ==========================================
// 2. UE 生命周期
// ==========================================
void UInvincibilityFlickerComponent::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log,
		TEXT("[InvincibilityFlickerComponent] BeginPlay: Owner=%s, Parameter=%s, SlotFilter.Num=%d"),
		*GetNameSafe(GetOwner()),
		*FlickerMaterialParameterName.ToString(),
		TargetMaterialSlotIndices.Num());

	// ============================================================
	// 大厂原则 - 缓存 Owner (避开 v36 ~ v40.6 的 BP archetype 陷阱)
	//   - 不缓存 raw pointer: 改为 TWeakObjectPtr, 即使 BP 重置也安全
	//   - 不缓存到字段: 每次 GetOwner() + Cast (因为 BP archetype 会缓存失效)
	//   - 与 AIAttackComponent::ResolveOwnerCharacter 模式完全一致
	// ============================================================
	ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(GetOwner());
	if (!OwnerChar)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[InvincibilityFlickerComponent] BeginPlay: Owner 不是 ABaseCharacter (Owner=%s). "
				 "本组件必须挂在 ABaseCharacter 上."),
			*GetNameSafe(GetOwner()));
		return;
	}

	// ============================================================
	// 缓存 HealthComponent (真理源)
	//   - 真根因 v36: Owner 字段在 BP 子类被 nullify
	//   - 必须用 ResolveHealthComponent() — 走 BeginPlay 中自动修复路径
	// ============================================================
	UHealthComponent* HC = OwnerChar->ResolveHealthComponent();
	if (!HC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[InvincibilityFlickerComponent] BeginPlay: Owner 找不到 HealthComponent (Owner=%s). "
				 "检查 BP 是否挂载 HealthComponent."),
			*GetNameSafe(GetOwner()));
		return;
	}

	CachedHealthComponent = HC;

	// ============================================================
	// 订阅 OnInvincibilityChanged
	//   - 服务器/客户端本地订阅 (双发保证: 服务器 Activate 主动 Broadcast, 客户端 OnRep Broadcast)
	//   - 本组件的 HandleInvincibilityChanged 会被调用 1 次 / 每次状态变化
	// ============================================================
	HC->OnInvincibilityChanged.AddDynamic(this, &UInvincibilityFlickerComponent::HandleInvincibilityChanged);

	UE_LOG(LogTemp, Log,
		TEXT("[InvincibilityFlickerComponent] BeginPlay: 成功订阅 OnInvincibilityChanged. Owner=%s, HC=%s, "
			 "当前 bIsInvincible=%d"),
		*OwnerChar->GetName(),
		*HC->GetName(),
		HC->IsInvincible() ? 1 : 0);

	// 边缘 case: BeginPlay 时已经处于无敌状态（罕见, 但 Replicated 字段可能在订阅前已同步）
	// 这种情况下不会再次触发 OnRep, 我们手动触发一次
	if (HC->IsInvincible())
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[InvincibilityFlickerComponent] BeginPlay: 检测到已激活无敌期 (错过 OnRep), 手动触发 HandleInvincibilityChanged(true)"));
		HandleInvincibilityChanged(true);
	}
}


void UInvincibilityFlickerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// ============================================================
	// 大厂原则 - 防御型清理 (防御残留 Timer / 订阅 / 字段)
	// ============================================================

	// 强制重置 MID 闪烁值 = 0 (退出关卡 / Actor 销毁前, 防止残留)
	ResetFlicker();

	// 取消订阅 (避免 Owner 销毁后回调 crash)
	if (UHealthComponent* HC = CachedHealthComponent.Get())
	{
		HC->OnInvincibilityChanged.RemoveDynamic(this, &UInvincibilityFlickerComponent::HandleInvincibilityChanged);
	}
	CachedHealthComponent.Reset();

	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 3. 核心事件处理 — HealthComponent 状态变化
// ==========================================
/**
 * HandleInvincibilityChanged — HealthComponent 状态变化的本地响应
 *
 * 大厂原则 - 双发保证:
 *   - 服务器: HealthComp Activate/Expire 主动 Broadcast → 本回调 (服务器本地跑一次)
 *   - 客户端: HealthComp OnRep 触发 Broadcast → 本回调 (客户端跑一次)
 *   - 服务器/客户端各自独立启动 Timeline, 不跨网络 (视觉不需要同步, 每台机器本地播放)
 *
 * @param bIsNowInvincible true=进入无敌, false=退出无敌
 */
void UInvincibilityFlickerComponent::HandleInvincibilityChanged(bool bIsNowInvincible)
{
	UE_LOG(LogTemp, Log,
		TEXT("[InvincibilityFlickerComponent] HandleInvincibilityChanged: Owner=%s, bIsNowInvincible=%d"),
		*GetNameSafe(GetOwner()),
		bIsNowInvincible ? 1 : 0);

	if (bIsNowInvincible)
	{
		// ============================================================
		// 进入无敌期 — 准备材料 + 派发 BP 启动事件
		// ============================================================

		// 1. 第一次启动时准备 MID (懒加载 — Mesh 可能 BeginPlay 时还没准备好)
		if (!bMaterialsPrepared)
		{
			PrepareMaterials();
		}

		// 2. 派发 BP 启动事件
		//    - 即便 PrepareMaterials 失败, 也派发 OnFlickerStarted
		//    - BP 可在 OnFlickerStarted 内查 HasPreparedMaterials() 决定是否启动 Timeline
		//    - 大厂原则 - 显式失败: 协议不满足时让 BP 知道
		if (bMaterialsPrepared && FlickerMaterials.Num() > 0)
		{
			bIsFlickering = true;
			OnFlickerStarted.Broadcast();
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[InvincibilityFlickerComponent] HandleInvincibilityChanged(true): MID 未准备好, "
					 "无法启动闪烁. Owner=%s, bMaterialsPrepared=%d, FlickerMaterials.Num=%d. "
					 "检查: 1) BP 是否有 SkeletalMesh 2) 材质是否有 %s 参数"),
				*GetNameSafe(GetOwner()),
				bMaterialsPrepared ? 1 : 0,
				FlickerMaterials.Num(),
				*FlickerMaterialParameterName.ToString());
		}
	}
	else
	{
		// ============================================================
		// 退出无敌期 — 重置 MID + 派发 BP 停止事件
		// ============================================================

		// 1. 立即重置 MID = 0 (不依赖 BP, 保证视觉立即消失)
		ResetFlicker();

		// 2. 派发 BP 停止事件
		bIsFlickering = false;
		OnFlickerStopped.Broadcast();
	}
}


// ==========================================
// 4. Material 准备 — 协议收集与验证
// ==========================================
/**
 * PrepareMaterials — 收集所有目标 Slot 的 MID 并验证参数协议
 *
 * 大厂原则 (与 DissolveComponent 完全对称):
 *   - 遍历 SkeletalMesh 的 NumMaterials 个槽
 *   - 每个槽 CreateDynamicMaterialInstance (UE 复制当前材质 → MID)
 *   - 验证 MID 是否含 FlickerAmount 参数 — 协议验证 (零兜底)
 *
 * 协议不满足时:
 *   - 不报错崩游戏 (Material 操作失败不会致命)
 *   - 显式 Log(Warning) 报警, 让美术修材质
 *   - 后续 OnFlickerStarted 检测到 bMaterialsPrepared=false → 不派发 OnFlickerStarted
 *     → BP 不启动 Timeline → 自然不闪烁 (失败显式化)
 */
void UInvincibilityFlickerComponent::PrepareMaterials()
{
	// 幂等保证
	if (bMaterialsPrepared)
	{
		return;
	}
	bMaterialsPrepared = true;

	// 清空旧引用 (防止重复调用残留)
	FlickerMaterials.Empty();

	USkeletalMeshComponent* Mesh = GetOwnerSkeletalMesh();
	if (!Mesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[InvincibilityFlickerComponent] PrepareMaterials: Owner 没有 SkeletalMesh Component "
				 "(Owner=%s). 闪烁无法工作 — 请在 BP_BaseCharacter/BP_GruntAI 检查 Mesh 组件是否挂载."),
			*GetNameSafe(GetOwner()));
		return;
	}

	const int32 NumMaterials = Mesh->GetNumMaterials();
	UE_LOG(LogTemp, Log,
		TEXT("[InvincibilityFlickerComponent] PrepareMaterials: Mesh=%s, NumMaterials=%d, "
			 "SlotFilter.Num=%d, Parameter=%s"),
		*Mesh->GetName(),
		NumMaterials,
		TargetMaterialSlotIndices.Num(),
		*FlickerMaterialParameterName.ToString());

	if (NumMaterials == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[InvincibilityFlickerComponent] PrepareMaterials: Mesh=%s 没有 Material Slot — 不会闪烁."),
			*Mesh->GetName());
		return;
	}

	// ============================================================
	// 大厂原则 - Slot 过滤策略
	//   1) TargetMaterialSlotIndices 空数组 = 闪烁所有 Slot (默认)
	//   2) TargetMaterialSlotIndices 非空 = 只闪烁列表内 Slot 索引 (策划精确控制)
	//
	// 实现: 始终遍历所有 Slot, 但用 Filter 函数判断是否跳过
	//      (这样可以统计"所有 Slot 数"和"有效 Slot 数"的日志)
	// ============================================================

	int32 CollectedCount = 0;
	int32 ProtocolFailedCount = 0;
	int32 SlotOutOfRangeCount = 0;

	for (int32 i = 0; i < NumMaterials; i++)
	{
		// 1. 过滤: Slot 索引不在白名单 → 跳过
		if (TargetMaterialSlotIndices.Num() > 0 && !TargetMaterialSlotIndices.Contains(i))
		{
			continue;
		}

		// 2. 创建 MID
		UMaterialInstanceDynamic* DynMat = Mesh->CreateDynamicMaterialInstance(i, nullptr);
		if (!DynMat)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[InvincibilityFlickerComponent] PrepareMaterials: Slot %d 创建 MID 失败 "
					 "(Mesh=%s). 协议要求: 该 Slot 必须有有效材质."),
				i, *Mesh->GetName());
			continue;
		}

		// 3. 协议验证: MID 必须含 FlickerMaterialParameterName 参数
		if (!ValidateMaterialHasFlickerParameter(DynMat))
		{
			// 不加入 FlickerMaterials 数组 — 让 OnFlickerStarted 检测到数量不足 → 阻止闪烁启动
			ProtocolFailedCount++;
			continue;
		}

		FlickerMaterials.Add(DynMat);
		CollectedCount++;
	}

	// 报告验证结果
	if (CollectedCount == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[InvincibilityFlickerComponent] PrepareMaterials: 没有任何 Slot 通过协议验证 "
				 "(Owner=%s, Mesh=%s). 闪烁不会启动. "
				 "根因: 1) M_Character 材质蓝图缺少 ScalarParameter '%s'  2) Slot 过滤太严. "
				 "修复: 在材质蓝图加 ScalarParameter \"%s\" 并连到 Emissive 或 BaseColor."),
			*GetNameSafe(GetOwner()),
			*Mesh->GetName(),
			*FlickerMaterialParameterName.ToString(),
			*FlickerMaterialParameterName.ToString());
	}
	else if (ProtocolFailedCount > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[InvincibilityFlickerComponent] PrepareMaterials: 部分 Slot 通过验证 "
				 "(Owner=%s, Mesh=%s, 通过=%d, 失败=%d). 失败的 Slot 不会闪烁."),
			*GetNameSafe(GetOwner()),
			*Mesh->GetName(),
			CollectedCount,
			ProtocolFailedCount);
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[InvincibilityFlickerComponent] PrepareMaterials: 全部 %d 个 Slot 通过协议验证 "
				 "(Owner=%s, Mesh=%s)."),
			CollectedCount,
			*GetNameSafe(GetOwner()),
			*Mesh->GetName());
	}
}


/**
 * ValidateMaterialHasFlickerParameter — 协议验证 (零兜底)
 *
 * UE 5.6 验证方式:
 *   1. GetAllScalarParameterInfo() 拿所有标量参数
 *   2. 遍历比对 ParameterInfo.Name == FlickerMaterialParameterName
 *
 * 大厂原则:
 *   - 这是"协议约束执行点"
 *   - 不允许"SetScalarParameterValue 不存在参数也正常 no-op 然后静默" — 必须报警
 *
 * @return true=有效, false=缺失
 */
bool UInvincibilityFlickerComponent::ValidateMaterialHasFlickerParameter(UMaterialInstanceDynamic* Mat) const
{
	if (!Mat)
	{
		return false;
	}

	// UE 5.6 标准 API: GetAllScalarParameterInfo (TArray<FMaterialParameterInfo>, TArray<FGuid>)
	TArray<FMaterialParameterInfo> ScalarParams;
	TArray<FGuid> ScalarParamIds;  // [v40.8 P0 修复] GetAllScalarParameterInfo 需要 2 个参数 (UE 5.6)
	Mat->GetAllScalarParameterInfo(ScalarParams, ScalarParamIds);

	for (const FMaterialParameterInfo& Info : ScalarParams)
	{
		if (Info.Name == FlickerMaterialParameterName)
		{
			return true;
		}
	}

	// 失败 — 报警
	UE_LOG(LogTemp, Warning,
		TEXT("[InvincibilityFlickerComponent] 协议验证失败: MID=%s 缺少参数 '%s'. "
			 "修复: 在对应材质蓝图 (M_Character) 加 ScalarParameter '%s' 并把它连到 Emissive 或 BaseColor."),
		*Mat->GetName(),
		*FlickerMaterialParameterName.ToString(),
		*FlickerMaterialParameterName.ToString());
	return false;
}


// ==========================================
// 5. 公开 API — BP 调用
// ==========================================
/**
 * SetFlickerAmount — 设置所有 MID 的 FlickerAmount 参数
 *
 * BP Timeline 的 Update 回调中调用:
 *   - Timeline 每帧输出 0.0~1.0 → 调本方法 → UE SetScalarParameterValue
 *
 * 大厂原则 - 零兜底:
 *   - FlickerMaterials 为空 → Log(Warning) + return (BP 调用时序错)
 *   - 强类型: FlickerValue 不限制范围 (BP 自己保证 0~1)
 *
 * @param FlickerValue 闪烁值 (0=原色, 1=最亮 或 BP 定义的其他映射)
 */
void UInvincibilityFlickerComponent::SetFlickerAmount(float FlickerValue)
{
	if (FlickerMaterials.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[InvincibilityFlickerComponent] SetFlickerAmount: FlickerMaterials 为空 (BP 调用时序错?). "
				 "请检查: 1) OnFlickerStarted 事件是否绑定 2) 材质是否有 '%s' 参数. Owner=%s"),
			*FlickerMaterialParameterName.ToString(),
			*GetNameSafe(GetOwner()));
		return;
	}

	// 大厂原则 - 单一协议, 反复调用幂等
	for (UMaterialInstanceDynamic* Mat : FlickerMaterials)
	{
		if (Mat)
		{
			Mat->SetScalarParameterValue(FlickerMaterialParameterName, FlickerValue);
		}
	}
}


/**
 * ResetFlicker — 强制重置所有 MID 的 FlickerAmount = 0.0
 *
 * 调用场景:
 *   - OnFlickerStopped 时 C++ 自动调（不依赖 BP）
 *   - EndPlay 时强制清理
 *   - BP 主动调 (例如角色死亡时主动恢复正常皮肤)
 *
 * 大厂原则 - 幂等保证:
 *   - FlickerMaterials 为空 → no-op (安全)
 *   - 多次调用 → 全部 = 0.0 (幂等)
 */
void UInvincibilityFlickerComponent::ResetFlicker()
{
	for (UMaterialInstanceDynamic* Mat : FlickerMaterials)
	{
		if (Mat)
		{
			Mat->SetScalarParameterValue(FlickerMaterialParameterName, 0.0f);
		}
	}

	if (bIsFlickering)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[InvincibilityFlickerComponent] ResetFlicker: 强制所有 MID %s=0.0. Owner=%s"),
			*FlickerMaterialParameterName.ToString(),
			*GetNameSafe(GetOwner()));
	}
}


// ==========================================
// 6. 内部辅助
// ==========================================
USkeletalMeshComponent* UInvincibilityFlickerComponent::GetOwnerSkeletalMesh() const
{
	if (ACharacter* Char = Cast<ACharacter>(GetOwner()))
	{
		return Char->GetMesh();
	}
	return nullptr;
}
