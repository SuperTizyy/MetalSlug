// ==========================================
// 版权声明：在项目设置的描述页面填写您的版权信息。
// ==========================================
//
// @file WeaponAttachmentComponent.cpp
// @brief 武器网络复制 + 挂载 + SyncWeapon 子系统 — 实现
//
// 【大厂原则】
//   - 单一真理源: CurrentWeapon 在本组件, CharacterID/SpawnWeaponID 在本组件
//   - 零兜底: OnRep_CurrentWeapon 反查失败 → Log Error + return (Phase 1.2)
//   - 零兜底: FindWeaponAttachmentConfig 找不到精确 → Log Error + return nullptr (Phase 1.3)
//   - Owner 暴露: 通过 Owner->MeleeProfile / Owner->GetMesh() 等访问
//
// 【调用链】
//   PossessedBy (Server) → SyncWeaponFromProfile → SpawnAndEquipWeapon
//                                                        ↓
//                                                    SpawnActor + AttachToComponent
//                                                        ↓
//                                                    CurrentWeapon = NewWeapon (Replicated)
//                                                                             ↓
//                                                                  OnRep_CurrentWeapon (Client)
//                                                                             ↓
//                                                                  反查 WeaponID + 挂载
// ==========================================

// ==========================================
// 头文件包含区
// ==========================================
#include "Combat/WeaponAttachmentComponent.h"

// 包含 BaseCharacter 以访问 Owner->MeleeProfile / GetMesh() / GetController() 等
#include "Characters/BaseCharacter.h"

// 包含 BaseWeapon 以访问 IsA<>() / AttachToComponent
#include "Weapons/BaseWeapon.h"

// 包含 RoomGameMode 以访问 WeaponDataTable (运行时武器配置)
#include "Systems/RoomGameMode.h"

// 包含 AMyGameHUD 以访问 HUD 刷新
#include "UI/MyGameHUD.h"

// 包含 GameHUDWidget 以访问 UpdateWeaponIconFromID
#include "UI/Game/GameHUDWidget.h"

// 【v54 大厂架构重构 — UAIProfileAsset 已删除】这里不再 include Data/AI/AIProfileAsset.h
// 关卡预放 AI 走 ConfigSO (DA_AIBehaviorConfig_*.uasset)
// 数据流: AMeleeAIController::SetupMeleeAI 末尾 → SetMeleeConfig(UAIBehaviorConfigSO*)

// UE 反射: DOREPLIFETIME
#include "Net/UnrealNetwork.h"

// UE 控制器: APlayerController
#include "GameFramework/PlayerController.h"

// UE DataTable: FindRow 模板
#include "Engine/DataTable.h"

// UE 反射: LogCategory
#include "Logging/LogMacros.h"


// ==========================================
// 1. 构造函数
// ==========================================

/**
 * UWeaponAttachmentComponent 构造函数
 *
 * 目的: 不创建子组件 (本组件无子组件), 仅初始化默认值
 * 关键: 不启用 Tick (本组件不需要每帧 Tick, 所有逻辑都是事件驱动)
 */
UWeaponAttachmentComponent::UWeaponAttachmentComponent()
{
	// 不需要 Tick: 武器网络复制由 UE 引擎自动处理, 挂载由 OnRep 触发
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// 【2026.07.12 P0 修复】启用 Component 网络复制
	//
	// 根因: 旧构造函数只设 DOREPLIFETIME, 没调 SetIsReplicatedByDefault(true)
	//       → UE 默认 ActorComponent 不参与网络复制
	//       → CurrentWeapon 字段永远不复制到客户端
	//       → OnRep_CurrentWeapon 永远不触发
	//       → 客户端永远没武器 (客户端看不到任何 [OnRep_CurrentWeapon] 日志)
	//
	// 大厂原则 - 单一真理源:
	//   - SetIsReplicatedByDefault(true) 让 Component 本身参与复制
	//   - DOREPLIFETIME 注册 CurrentWeapon 字段
	//   - ReplicatedUsing = OnRep_CurrentWeapon 触发回调
	//   - 三层缺一不可, 这是 UE 5.6 硬约束
	//
	// 修复后: 服务器 SpawnAndEquipWeapon → CurrentWeapon = NewWeapon → 自动复制 → 所有客户端 OnRep 触发
	SetIsReplicatedByDefault(true);

	// 默认值 (已在头文件初始化, 这里不再重复)
	CurrentWeapon = nullptr;
}


// ==========================================
// 2. UE 生命周期
// ==========================================

/**
 * GetLifetimeReplicatedProps: 注册需要网络同步的 UPROPERTY
 *
 * 同步内容:
 *   - CurrentWeapon (武器指针, ReplicatedUsing 触发 OnRep)
 *   - CharacterID / SpawnWeaponID (字符串, 普通 Replicated)
 *
 * 【2026.07.12 P0 修复】Client 端 OnRep_CurrentWeapon 反查挂载配置依赖 CharacterID 和 SpawnWeaponID
 *                    如果这两个字段没复制, 客户端 OnRep 时拿到空字符串, 反查失败 → 武器不挂载
 */
void UWeaponAttachmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// CurrentWeapon: ReplicatedUsing = OnRep_CurrentWeapon
	// 服务器写 → 客户端 OnRep_CurrentWeapon 自动触发
	DOREPLIFETIME(UWeaponAttachmentComponent, CurrentWeapon);

	// CharacterID / SpawnWeaponID: 普通 Replicated
	// 服务器 PossessedBy / SpawnAIInternal 写入 → 客户端同步 → OnRep_CurrentWeapon 反查挂载
	DOREPLIFETIME(UWeaponAttachmentComponent, CharacterID);
	DOREPLIFETIME(UWeaponAttachmentComponent, SpawnWeaponID);
}


/**
 * GetOwnerCharacterChecked — v40.3 单一真理源: 永远走 GetOwner()
 *
 * 反模式 (v6-v40.2): TWeakObjectPtr<ABaseCharacter> OwnerCharacter BeginPlay 缓存
 *   - 缓存可能失效 (BeginPlay 未跑 / BP 子类清空字段)
 *   - 多个调用方裸用 OwnerCharacter.Get() → 拿到 null 时 Log Error + return
 *
 * 新架构 (v40.3):
 *   - 真理源 = GetOwner() (UE 标准 API, Component 必挂在 Actor 上, 永远非空)
 *   - 不缓存 (零兜底 + DRY + 不增加不确定性)
 *   - Cast 失败 Log Error + return nullptr (强制修复 BP 子类化挂载错误)
 *
 * @return 有效 Owner (失败返回 nullptr, 调用方必须 if 检查 + 走错误处理路径)
 */
ABaseCharacter* UWeaponAttachmentComponent::GetOwnerCharacterChecked(const UActorComponent* Self)
{
	if (!Self)
	{
		// Self 为空 = 静默 nullptr 返回 (componnet 自身已失效, 不强求)
		return nullptr;
	}

	AActor* OwnerActor = Self->GetOwner();
	if (!OwnerActor)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] GetOwnerCharacterChecked: GetOwner() 返回 nullptr. "
				 "Component 实例: '%s'. 组件未挂在 Actor 上, 致命错误."),
			*GetNameSafe(Self));
		return nullptr;
	}

	ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(OwnerActor);
	if (!OwnerChar)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] GetOwnerCharacterChecked: Owner '%s' (类型=%s) 不是 ABaseCharacter 派生. "
				 "本组件必须挂在 ABaseCharacter 子类上."),
			*OwnerActor->GetName(),
			*OwnerActor->GetClass()->GetName());
		return nullptr;
	}

	return OwnerChar;
}


/**
 * BeginPlay: 一次性诊断日志 (v40.3 不再缓存 Owner 字段)
 *
 * 历史 (v6-v40.2): 在此处缓存 OwnerCharacter TWeakObjectPtr
 * 现架构 (v40.3): 不缓存, 运行时全走 GetOwner() — BeginPlay 仅留诊断日志
 */
void UWeaponAttachmentComponent::BeginPlay()
{
	Super::BeginPlay();

	// 一次性校验 Owner (不缓存, 仅做启动期诊断 — 强制 BP 配置错误立即暴露)
	if (ABaseCharacter* Owner = GetOwnerCharacterChecked(this))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[WeaponAttachmentComponent] BeginPlay: Owner=%s, CharacterID='%s', SpawnWeaponID='%s', "
				 "CurrentWeapon=%s, bReplicates=%d"),
			*GetNameSafe(Owner), *CharacterID, *SpawnWeaponID,
			*GetNameSafe(CurrentWeapon), (int32)GetIsReplicated());
	}
}


/**
 * EndPlay: 清理 (无字段缓存, 无需清理 — 保留作为钩子)
 */
void UWeaponAttachmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// v40.3 删除 OwnerCharacter 清空 (字段已不存在)
	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 3. OnRep_CurrentWeapon — 客户端武器挂载回调
// ==========================================

/**
 * OnRep_CurrentWeapon — 武器指针改变时的网络复制回调
 *
 * 【Phase 1.2 修复 — 零兜底】反查 WeaponID 失败 → Log Error + return
 * 【Phase 1.3 修复 — 零兜底】FindWeaponAttachmentConfig 找不到精确 → Log Error + return nullptr
 *
 * 触发场景:
 *   - 服务器 PossessedBy → SpawnAndEquipWeapon → CurrentWeapon=新武器 → 复制到客户端
 *   - 客户端 OnRep_CurrentWeapon 触发 → 走反查 + 挂载逻辑
 *
 * 大厂修复架构:
 *   - 服务器和本地控制的角色已在 PossessedBy 中处理 → 跳过
 *   - 先脱挂旧武器 (复活场景 OldWeapon != null)
 *   - CharacterID 未就绪时静默跳过 (网络复制优先级)
 *   - 反查 WeaponID 失败 → Log Error + return (Phase 1.2)
 *   - 拒绝通配 fallback (Phase 1.3)
 */
void UWeaponAttachmentComponent::OnRep_CurrentWeapon(ABaseWeapon* OldWeapon)
{
	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] OnRep_CurrentWeapon: Old=%s, New=%s, Local=%d, Auth=%d"),
		*GetNameSafe(OldWeapon), *GetNameSafe(CurrentWeapon),
		Owner->IsLocallyControlled(), Owner->HasAuthority());

	// 服务器和本地控制的角色已经在 PossessedBy 中处理了, 跳过
	if (Owner->IsLocallyControlled() || Owner->HasAuthority())
	{
		return;
	}

	// 旧武器脱挂 (防御: 复活场景 OldWeapon != null)
	if (OldWeapon && IsValid(OldWeapon))
	{
		OldWeapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		UE_LOG(LogTemp, Log,
			TEXT("[WeaponAttachment] 脱挂旧武器: %s"), *OldWeapon->GetName());
	}

	if (!CurrentWeapon)
	{
		// 新武器为空, 服务器清空了武器, 客户端也已脱挂
		return;
	}

	// CharacterID 未就绪时静默跳过
	// 防御: CharacterID 通过 PlayerState 网络复制, 优先级可能低于 CurrentWeapon
	// 【2026.07.12 P0 修复】CharacterID/SpawnWeaponID 已 Replicated (跟 CurrentWeapon 同包同步)
	//                       这段防御已不需要 — 如果出现 CharacterID 空, 那是服务器未写, 不是网络时序问题
	if (CharacterID.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] OnRep_CurrentWeapon: CharacterID 为空, 服务器 PossessedBy/SpawnAIInternal 没写 SetSpawnLoadout. "
				 "Weapon=%s 不会挂载. 【大厂原则】零兜底, 强制修复 Spawn 链路."),
			*CurrentWeapon->GetName());
		return;
	}

	// 【v54.3 大厂重构 — Class 强类型真理源】删除整个 FString 反查块
	//
	// 旧 (v54.2 — 反查字符串): OnRep_CurrentWeapon 收到新 CurrentWeapon → 反查 WeaponDataTable 把 Weapon BP 翻译为 WeaponID 字符串 → 用字符串再去 FindWeaponAttachmentConfig
	//   - 2 层翻译 (BP → RowName → 配置), 字符串拼错永远不可见
	//   - 大部分工作冗余: 已经有 CurrentWeapon (强类型), 还要反查字符串有什么用?
	//
	// 新 (v54.3 — Class 强类型): 直接用 CurrentWeapon->GetClass() 强类型查 FindWeaponAttachmentConfig
	//   - 真理源: CurrentWeapon (Class) — UE 标准 API 拿到的, 永远最权威
	//   - 不需要回查 DT_WeaponInfo, 不需要任何字符串中转
	TSubclassOf<ABaseCharacter> OwnerClass = Owner->GetClass();
	TSubclassOf<ABaseWeapon> WeaponClass = CurrentWeapon ? CurrentWeapon->GetClass() : nullptr;
	FWeaponAttachmentConfig* AttachmentConfig = FindWeaponAttachmentConfig(OwnerClass, WeaponClass);

	// ============================================================
	// 【v35 零兜底改造】挂载配置缺失 → Log Error + Destroy 武器 + return
	//
	// 旧版 (v32-v34) 反模式:
	//   - SocketName/RelativeLocation/RelativeRotation 用兜底默认值 (TEXT("WeaponSocket_R") / ZeroVector / ZeroRotator)
	//   - AttachToComponent 用默认 Socket → 角色没这个 socket → attach 无效
	//   - SetActorRelativeLocation/Rotation 用 ZeroVector → 武器位置错乱 (挂在原点, 玩家看不到武器)
	//   - 注释自辩"使用默认 Socket" = 字面违反零兜底原则
	//
	// 大厂原则 (v35 零兜底):
	//   - 挂载配置缺失 = 配置错, 必须显式化
	//   - Log Error + Destroy 武器 + return, 强制用户在 UE 编辑器配 WeaponAttachmentDataTable
	//   - 玩家看到"武器没显示" > "武器位置错乱" (后者让 BP 配置错被永远掩盖)
	// ============================================================
	if (!AttachmentConfig)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] OnRep_CurrentWeapon: 挂载配置缺失! "
				 "OwnerClass=%s, WeaponClass=%s, Weapon=%s. "
				 "【v35 零兜底】不静默使用默认 Socket — Destroy 武器 %s. "
				 "【强制修复 — v37 单一真理源】挂载配置不在 BP 角色里 (v37 已删), "
				 "而是集中在 UE 编辑器 → BP_GM_RoomGameMode → Class Defaults → "
				 "Room|Data → Weapon Attachment DataTable 配 DT_WeaponAttachmentConfig. "
				 "DataTable 必须包含 (TargetCharacter + WeaponBlueprint) 精确匹配行."),
			*Owner->GetClass()->GetName(),
			CurrentWeapon ? *CurrentWeapon->GetClass()->GetName() : TEXT("nullptr"),
			CurrentWeapon ? *CurrentWeapon->GetName() : TEXT("nullptr"),
			CurrentWeapon ? *CurrentWeapon->GetName() : TEXT("nullptr"));
		if (CurrentWeapon)
		{
			CurrentWeapon->Destroy();
		}
		return;
	}

	// 从配置读取挂载参数 (唯一真理源, 不允许默认值)
	const FName SocketName = AttachmentConfig->SocketName;
	const FVector RelativeLocation = AttachmentConfig->RelativeLocation;
	const FRotator RelativeRotation = AttachmentConfig->RelativeRotation;

	// 将武器挂载到插槽
	FAttachmentTransformRules AttachmentRules = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
	CurrentWeapon->AttachToComponent(Owner->GetMesh(), AttachmentRules, SocketName);

	// 【v35 零兜底】总是调 setter, 不 IsNearlyZero 判断 (旧版"如果为零就跳过"也是兜底)
	CurrentWeapon->SetActorRelativeLocation(RelativeLocation);
	CurrentWeapon->SetActorRelativeRotation(RelativeRotation);

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] 挂载完成: Socket=%s, OwnerClass=%s, WeaponClass=%s"),
		*SocketName.ToString(),
		*Owner->GetClass()->GetName(),
		CurrentWeapon ? *CurrentWeapon->GetClass()->GetName() : TEXT("nullptr"));

	// ==========================================
	// 【v40.2 P0 注释】武器图标刷新不在此处触发
	//   根因: 旧版只挂武器, 不调 RefreshWeaponIconOnHUD
	//         → 客户端武器图标永远不显示
	//   修复点: 不在 OnRep 末尾触发 (服务器已在 PossessedBy 内通过 RefreshCharacterIcon 链路 → RefreshWeaponIconOnHUD 触发 Client_RefreshWeaponIcon RPC)
	//         客户端实现通过 "事件 + 缓存双轨制" (v40.2 新增 CharacterEvents::SetCachedWeaponIcon / GetCachedWeaponIcon) 保证 HUD 订阅时能补发
	//   大厂原则 - 单一真理源: 武器图标真理源 = WeaponDataTable 行 (服务器查表 RPC 推 Icon)
	// ==========================================
}


// ==========================================
// 4. EquipWeapon — 服务器换枪
// ==========================================

/**
 * EquipWeapon — 服务器装备武器 (简化接口, deprecated)
 *
 * 调用方: GameMode 直接指定 Class 的场景 (不走 DataTable)
 *
 * 流程:
 *   1. 仅服务器调用
 *   2. 如果手里已经有武器 → 销毁旧的
 *   3. SpawnActor 生成新武器
 *   4. 挂载到默认 Socket "WeaponSocket_L"
 *
 * 【v36 DEPRECATED】此接口违反大厂单一真理源 — 不查 DataTable, 不查 AttachmentConfig
 *   业务场景应该走 RequestWeaponSpawn (查表 + 挂载配置), 不要用 EquipWeapon.
 *   保留只是为了让 BP 不编译失败. 调用时打 Warning 提醒改用 RequestWeaponSpawn.
 */
void UWeaponAttachmentComponent::EquipWeapon(TSubclassOf<ABaseWeapon> WeaponClassToEquip)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[WeaponAttachment] EquipWeapon 是 deprecated 接口 (v36), 不查 DataTable 也不查 AttachmentConfig. "
			 "【大厂原则】单一真理源 = WeaponDataTable + WeaponAttachmentDataTable. "
			 "请改用 RequestWeaponSpawn(TSubclassOf<ABaseWeapon>) 走完整链路. Pawn=%s, WeaponClass=%s"),
		*GetNameSafe(GetOwnerCharacterChecked(this)), *GetNameSafe(WeaponClassToEquip));

	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] EquipWeapon: Owner 无效."));
		return;
	}

	// 只有服务器上帝有资格发枪
	if (!Owner->HasAuthority() || !WeaponClassToEquip)
	{
		return;
	}

	// 如果手里已经有武器, 先销毁旧的
	if (CurrentWeapon)
	{
		CurrentWeapon->Destroy();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 在世界中凭空召唤出这把武器
	CurrentWeapon = Owner->GetWorld()->SpawnActor<ABaseWeapon>(
		WeaponClassToEquip, Owner->GetActorLocation(), Owner->GetActorRotation(), SpawnParams);

	if (CurrentWeapon)
	{
		// 焊接到第三人称全身模型的 "WeaponSocket_L" 插槽
		CurrentWeapon->AttachToComponent(
			Owner->GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			FName("WeaponSocket_L"));
	}
}


// ==========================================
// 5. RequestWeaponSpawn — 公开入口 (关卡预放 AI)
// ==========================================

/**
 * RequestWeaponSpawn — 武器生成请求入口 (对外暴露)
 *
 * 调用方:
 *   - AMeleeAIController::SetupMeleeAI (OnPossess 末尾手动触发武器生成)
 *
 * 内部直接调 SpawnAndEquipWeapon, 复用完整链路 (查表/挂载/HUD)
 */
void UWeaponAttachmentComponent::RequestWeaponSpawn(TSubclassOf<ABaseWeapon> WeaponClass)
{
	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] RequestWeaponSpawn: WeaponClass=%s, Owner=%s"),
		*GetNameSafe(WeaponClass), *GetNameSafe(GetOwnerCharacterChecked(this)));

	// 零兜底: WeaponClass 必须非空
	if (!WeaponClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] RequestWeaponSpawn: WeaponClass 为空. "
				 "【v54.3 零兜底】拒绝生成武器. "
				 "【根因排查】1) 调用方 LoadSynchronous 失败 2) DT_WeaponInfo 反查失败 3) Config.LevelPlacedWeaponClass 未配."));
		return;
	}

	// 直接调内部方法, 复用完整链路 (挂载配置/HUD)
	SpawnAndEquipWeapon(WeaponClass);
}


// ==========================================
// 6. SpawnAndEquipWeapon — 服务器权威生成
// ==========================================

/**
 * SpawnAndEquipWeapon — 根据 WeaponID 查表并生成+装备武器
 *
 * 工业级规范: 指针与有效性校验贯穿全流程
 *
 * 流程:
 *   1. WeaponID 校验
 *   2. 幂等检查 (防止 PossessedBy + SetupMeleeAI 同帧重复生成)
 *   3. GM + WeaponDataTable 校验
 *   4. 查表 FWeaponInfo (WeaponID → WeaponBlueprint)
 *   5. 查表 FWeaponAttachmentConfig (CharacterID + WeaponID → Socket/Offset)
 *   6. LoadSynchronous 加载 WeaponBlueprint
 *   7. SpawnActor 生成武器
 *   8. 挂载到 Socket (SnapToTargetNotIncludingScale)
 *   9. 应用 RelativeLocation/Rotation/Scale
 *   10. 写 CurrentWeapon (触发 Replicated)
 *   11. 刷新 HUD 武器图标 (仅本地玩家)
 */
/**
 * 【v54.3 大厂架构重构 — 直接接受 Class, 删除 DT_WeaponInfo 中间层】SpawnAndEquipWeapon
 *
 * 用户原话 2026.07.16:
 *   "DefaultWeaponRowName 属性为什么不直接引用 DT_WeaponInfo 表的 WeaponBlueprint 内容啊"
 *   → 真理源直接传递 Class, 不需要 DT 反查
 *
 * 旧流程 (v54.2 — 中间层冗余):
 *   1. WeaponID 校验
 *   2. 幂等检查
 *   3. GM + WeaponDataTable 校验
 *   4. 查表 FWeaponInfo (WeaponID → WeaponBlueprint)         ← 删除
 *   5. 查表 FWeaponAttachmentConfig (CharacterID + WeaponID → Socket/Offset)
 *   6. LoadSynchronous 加载 WeaponBlueprint                  ← 删除 (调用方已 Load)
 *   7. SpawnActor 生成武器
 *   8. 挂载到 Socket
 *   9. 应用 RelativeLocation/Rotation/Scale
 *   10. 写 CurrentWeapon (触发 Replicated)
 *   11. 刷新 HUD 武器图标
 *
 * 新流程 (v54.3 — 真理源直接 Class):
 *   1. WeaponClass 校验
 *   2. 幂等检查
 *   3. 查挂载配置 FWeaponAttachmentConfig (PawnClass + WeaponClass → Socket/Offset)  — 保留 (UI 配置)
 *   4. SpawnActor 生成武器
 *   5. 挂载到 Socket
 *   6. 应用 RelativeLocation/Rotation/Scale
 *   7. 写 CurrentWeapon (触发 Replicated)
 *   8. 刷新 HUD 武器图标
 *
 * 大厂原则:
 *   - 真理源直接传递: 调用方 (RequestWeaponSpawn) 已经 LoadSynchronous 拿到 Class, 这里不再查 DT
 *   - 删除中间层冗余: DT_WeaponInfo 反查从武器生成路径完全删除 (DT 仍保留供 UI 查询武器图标/列表)
 *   - 编译期类型安全: TSubclassOf<ABaseWeapon> 拒绝错类型
 */
void UWeaponAttachmentComponent::SpawnAndEquipWeapon(TSubclassOf<ABaseWeapon> WeaponClass)
{
	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: Owner 无效. "
				 "【v40.3 零兜底】Component 已挂在已销毁 Actor 上? 检查 BP 子类化配置."));
		return;
	}

	// 【v54.3 显式优于隐式】WeaponClass 必须非空
	if (!WeaponClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: WeaponClass 为空, 拒绝生成武器. "
				 "【v54.3 零兜底】不静默跳过, 不生成默认武器. "
				 "【根因排查】1) 调用方 LoadSynchronous 失败 2) DT_WeaponInfo 反查失败 3) Config.LevelPlacedWeaponClass 未配."));
		return;
	}

	// 【幂等性检查】防止重复生成武器
	if (IsValid(CurrentWeapon))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: 已有武器 %s, 跳过生成 — WeaponClass=%s"),
			*CurrentWeapon->GetName(), *WeaponClass->GetName());
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] SpawnAndEquipWeapon: 开始生成武器: WeaponClass=%s, PawnClass=%s (真理源直接 Class, 跳过 DT_WeaponInfo 中间层)"),
		*WeaponClass->GetName(), *Owner->GetClass()->GetName());

	// 【v54.3 大厂重构】DT_WeaponInfo 反查已删除 — 武器 Class 由调用方直接传递
	//   旧 (v54.2): GM->WeaponDataTable->FindRow<FWeaponInfo>(FName(*WeaponID)) -> WeaponInfo->WeaponBlueprint.LoadSynchronous()
	//   新 (v54.3): 调用方已经传入 WeaponClass, 直接使用

	// 1. 查找武器挂载配置 (PawnClass + WeaponClass → Socket/Offset)
	FWeaponAttachmentConfig* AttachmentConfig = FindWeaponAttachmentConfig(Owner->GetClass(), WeaponClass);

	// 2. 在服务器上生成武器
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Owner;

	ABaseWeapon* NewWeapon = Owner->GetWorld()->SpawnActor<ABaseWeapon>(
		WeaponClass,
		Owner->GetActorLocation(),
		Owner->GetActorRotation(),
		SpawnParams
	);

	if (!NewWeapon)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: SpawnActor 失败."));
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] SpawnAndEquipWeapon: 武器生成成功: %s"), *NewWeapon->GetName());

	// 4. 【零兜底】挂载参数必须从配置读取
	//
	// 旧实现 (v22-v31.x) 反模式:
	//   - 找不到挂载配置 → 静默用默认值 'WeaponSocket_R'
	//   - 角色没这个 socket → attach 实际无效 → 武器浮在原点
	//   - trace 的两端坐标返回 0 → IsNearlyZero() true → 跳过整个 trace
	//   - 用户看到"动画有, 伤害无" — 根因被默认值掩盖
	//
	// 新架构 (v32 — 零兜底):
	//   - 严格依赖 FindWeaponAttachmentConfig 返回的配置
	//   - 配置缺失 → Log Error + Destroy 武器 + 不 attach
	//   - 这强制用户配 WeaponAttachmentDataTable (v37 真理源 = GM, 不是 BP 角色)
	if (!AttachmentConfig)
	{
		// 【v54.3 大厂重构 — Class 强类型】删除 WeaponID 中间层引用
		//   旧 (v54.2): CharacterID='%s', WeaponID='%s' (FString 字符串)
		//   新 (v54.3): CharacterID 已废弃 (改用 PawnClass), WeaponClass 直接传
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SpawnAndEquipWeapon: 挂载配置缺失 — PawnClass='%s', WeaponClass='%s' 没在 WeaponAttachmentDataTable 中找到. "
			     "【v32 零兜底】不静默用默认值 — 销毁武器 %s, 强制修复 UE 编辑器配置 "
			     "【v37 单一真理源】UE 编辑器 → BP_GM_RoomGameMode → Class Defaults → Room|Data → "
			     "Weapon Attachment DataTable 字段必须配 DT_WeaponAttachmentConfig)."),
			*Owner->GetClass()->GetName(), *WeaponClass->GetName(), *NewWeapon->GetName());
		NewWeapon->Destroy();
		return;
	}

	const FName SocketName = AttachmentConfig->SocketName;
	const FVector RelativeLocation = AttachmentConfig->RelativeLocation;
	const FRotator RelativeRotation = AttachmentConfig->RelativeRotation;
	const FVector RelativeScale = AttachmentConfig->RelativeScale;

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] SpawnAndEquipWeapon: 使用挂载配置: Socket=%s, Loc=%s, Rot=%s"),
		*SocketName.ToString(),
		*RelativeLocation.ToString(),
		*RelativeRotation.ToString());

	// 5. 将武器焊接到角色的插槽上
	FAttachmentTransformRules AttachmentRules = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
	NewWeapon->AttachToComponent(Owner->GetMesh(), AttachmentRules, SocketName);

	// 6. 应用相对位置、旋转、缩放偏移
	if (!RelativeLocation.IsNearlyZero())
	{
		NewWeapon->SetActorRelativeLocation(RelativeLocation);
	}
	if (!RelativeRotation.IsNearlyZero())
	{
		NewWeapon->SetActorRelativeRotation(RelativeRotation);
	}
	if (!RelativeScale.Equals(FVector(1.0f, 1.0f, 1.0f)))
	{
		NewWeapon->SetActorRelativeScale3D(RelativeScale);
	}

	// 7. 更新 CurrentWeapon 指针, 这会触发网络同步
	CurrentWeapon = NewWeapon;

	// 8. 刷新战斗 HUD 上的武器图标 (仅本地玩家)
	//
	// 【v54.3 大厂重构 — Class 强类型真理源】HUD 图标的更新由 CharacterIconComponent 独立负责
	//   - 旧 (v54.2): 这里 SpawnAndEquipWeapon 末尾直接调 HUD->UpdateWeaponIconFromID(FString WeaponID)
	//     (耦合! 武器生成 → HUD 图标 → 单一真理源分裂)
	//   - 新 (v54.3): SpawnAndEquipWeapon 只负责生成/挂载/复制, HUD 图标由 CharacterIconComponent::RefreshWeaponIconOnHUD 独立链路处理
	//     - CharacterIconComponent 读 Owner->GetSpawnWeaponID() (Replicated 字符串) → 查 DT_WeaponInfo → 拿 Icon
	//     - 跟武器生成路径完全独立 (大厂原则: 职责对等, 不交叉)
	//
	// 因此这里不再调 HUD.UpdateWeaponIconFromID, 否则会形成两条独立链路, 违背 v40.1 "职责单一"
}


// ==========================================
// 7. SetSpawnLoadout / GetSpawnWeaponID / Setter 接口
// ==========================================

/**
 * SetSpawnLoadout — 统一的武器/角色预填入口
 *
 * 大厂原则 - 显式赋值优于隐式跳过:
 *   - 无论是否为空都写入, 确保外部覆盖意图被尊重
 *   - 空字符串时应显式清空, 而非保留旧值
 */
void UWeaponAttachmentComponent::SetSpawnLoadout(const FString& InCharacterID, const FString& InWeaponID)
{
	// 【2026.07.12 P0 诊断】日志保留 — 排查 Spawn 链路不写入字段问题
	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] SetSpawnLoadout: InCharID='%s', InWeaponID='%s', "
			 "Old CharID='%s', Old WeaponID='%s'"),
		*InCharacterID, *InWeaponID, *CharacterID, *SpawnWeaponID);

	// 无论是否为空都写入, 不再跳过空字符串
	CharacterID = InCharacterID;
	SpawnWeaponID = InWeaponID;
}


/** Setter (用于 ResetSpawnWeaponID 等场景) */
void UWeaponAttachmentComponent::SetSpawnWeaponID(const FString& InWeaponID)
{
	SpawnWeaponID = InWeaponID;
}


/** Setter */
void UWeaponAttachmentComponent::SetCharacterID(const FString& InCharacterID)
{
	CharacterID = InCharacterID;
}


// ==========================================
// 8. SetMeleeConfig — ConfigSO 注入入口 (v54 改名, 原 SetMeleeProfile)
// ==========================================

/**
 * SetMeleeConfig — ConfigSO 直接注入武器配置 (v54 改名, 原 SetMeleeProfile)
 *
 * 设计: SetSpawnLoadout 只做字段写入, 不承担"来源解析"职责.
 *       本方法负责从 UAIBehaviorConfigSO 解析 DefaultWeaponRowName,
 *       然后调用 SetSpawnLoadout 写入.
 *
 * 【v54 大厂架构重构 — UAIProfileAsset 已删除】
 *   - 旧: SetMeleeProfile(UAIProfileAsset*) → 写 Owner->MeleeProfile (字段已删)
 *   - 新: SetMeleeConfig(UAIBehaviorConfigSO*) → 直接写 SetSpawnLoadout
 *   - Owner 不再持有 Profile 字段, Config 由调用方传入 (一次性使用)
 *
 * 【v49 大厂架构重构】
 *   - 删除 CharacterRowName 解析 (字段已废弃)
 *   - 武器挂载查表改用 Pawn->GetClass() 强类型, 不再需要 CharacterID 字符串
 *
 * 调用方:
 *   - AMeleeAIController::SetupMeleeAI 末尾 (关卡预放 AI 自举路径)
 *   - AZombieAIController::SetupZombieAI 末尾
 */
void UWeaponAttachmentComponent::SetMeleeConfig(UAIBehaviorConfigSO* InConfig)
{
	// 【2026.07.12 P0 修复】Owner 单一真理源 = GetOwner()
	//
	// 新架构 (单一真理源 - 不缓存, 直接问 Component 的真理源):
	//   UE 5.6 标准做法: Component 的真理源永远是 GetOwner() (Component 必挂在 Actor 上)
	//   不再用 WeakPtr 缓存 (缓存可能失效, 增加不确定性 — 这就是"隐性兜底")
	//   每次显式问 GetOwner(), 拿到的是 Component 真实挂载的 Actor (100% 准确)
	//   Cast 失败直接 Error + return (零兜底: 失败就是失败, 强制修复 BP 组件挂载)
	//
	// 【v36 重大修复 — 显式优于隐式】拒绝 WeaponID == NAME_None 的隐式容错
	//
	// 旧版 (v32-v35) 反模式:
	//   - Profile.WeaponID == NAME_None 时, WeaponIDStr 保持空字符串 FString()
	//   - 写入 Pawn.SpawnWeaponID = ""
	//   - OnPossess 末尾 GetSpawnWeaponID() 读空字符串 → 跳过武器生成
	//   - 注释自辩"留空 = 不生成武器" = 字面违反零兜底原则
	//
	// 新架构 (v54 — ConfigSO 替代 Profile):
	//   - Config->DefaultWeaponRowName == NAME_None → Log Error + return (拒绝写入)
	//   - 强制用户在 UE 编辑器 → DA_AIBehaviorConfig_XXX → Identity|LevelPlacedAI 配 DT_WeaponInfo 的 RowName
	//   - "留空 = 不生成武器" 改用专门的 bHasWeapon 布尔字段 (策划意图), 不混用 NAME_None
	//   - 大厂原则: WeaponID 字段语义是"必须有值", 不是"可空"
	//
	// 大厂原则 (与"单一真理源"一致):
	//   - 真理源 = GetOwner() (Component 的标准 API, 永远非空除非 Actor 已销毁)
	//   - 不缓存 (缓存失效 = 隐性兜底 = 隐藏 bug)
	//   - 不静默兜底 (Cast 失败立即 Log Error + return, 强制显式修复)
	ABaseCharacter* Owner = Cast<ABaseCharacter>(GetOwner());
	if (!Owner)
	{
		// v40.3 之前此处直接 Log Error — 已上移到 GetOwnerCharacterChecked 集中处理
		return;
	}

	if (!InConfig)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SetMeleeConfig: InConfig 为空. "
				 "【v54 零兜底】拒绝写入. 检查调用方 SetupMeleeAI/SpawnAIInternal 是否传入有效 ConfigSO."));
		return;
	}

	// 【v54.3 大厂架构重构 — 真理源直接 Class, 跳过 DT_WeaponInfo 中间层】拒绝 LevelPlacedWeaponClass.IsNull()
	//   旧 (v54.2): DefaultWeaponRowName (FName) → 查 DT_WeaponInfo[RowName] → 拿 WeaponBlueprint
	//   新 (v54.3): LevelPlacedWeaponClass (TSoftClassPtr<ABaseWeapon>) → LoadSynchronous → 直接 Class
	//   - 跳过 SpawnWeaponID 字符串中转 + 跳过 SpawnAndEquipWeapon 内 DT_WeaponInfo 反查
	//   - 单一真理源: Config.LevelPlacedWeaponClass 直接决定武器 BP
	if (InConfig->LevelPlacedWeaponClass.IsNull())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SetMeleeConfig: Config=%s 的 LevelPlacedWeaponClass 为空 (TSoftClassPtr null). "
				 "【v54.3 零兜底】拒绝生成武器. "
				 "【修复】UE 编辑器 → DA_AIBehaviorConfig_%s → LevelPlacedAI → LevelPlacedWeaponClass 字段必须配 BP_Weapon_*.uasset. "
				 "如果 AI 设计上不需要武器, 应该让字段 = null 是显式错误, 不该静默. "
				 "【大厂原则】配置错必须显式化, 不允许用 null 当 'no weapon' 标志位."),
			*InConfig->GetName(), *InConfig->GetName());
		// 拒绝写入 (Pawn.SpawnWeaponID 保持空, 后续链路会 Log Error, 强制修复)
		return;
	}

	// 【v54.3 大厂原则 — 真理源直接传递】LoadSynchronous 必须成功
	TSubclassOf<ABaseWeapon> WeaponClass = InConfig->LevelPlacedWeaponClass.LoadSynchronous();
	if (!WeaponClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] SetMeleeConfig: Config=%s 的 LevelPlacedWeaponClass.LoadSynchronous() 失败. "
				 "【v54.3 零兜底】资产可能已被删除或路径错. "
				 "【修复】UE 编辑器 → DA_AIBehaviorConfig_%s → LevelPlacedWeaponClass 字段重新选 BP."),
			*InConfig->GetName(), *InConfig->GetName());
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] SetMeleeConfig: Config=%s, DesiredWeaponClass=%s (真理源直接持 BP, 跳过 DT_WeaponInfo 中间层)"),
		*InConfig->GetName(), *WeaponClass->GetName());

	// 【v54.3 重构】直接调 RequestWeaponSpawn(Class) — 不再走 SpawnWeaponID 字符串中转
	RequestWeaponSpawn(WeaponClass);
}


// 【v54 大厂架构重构 — UAIProfileAsset 整个类已删除】SetMeleeProfile 函数整个删除
//   旧版签名: SetMeleeProfile(UAIProfileAsset*) — UAIProfileAsset 类不存在, 无法保留编译
//   新版签名: SetMeleeConfig(UAIBehaviorConfigSO*) — 见本文件上方 SetMeleeConfig 实现
//   所有调用方已迁移到 SetMeleeConfig


// ==========================================
// 9. SyncWeaponFromConfig — 从 ConfigSO 同步字段 (v54 改名, 原 SyncWeaponFromProfile)
// ==========================================

/**
 * SyncWeaponFromConfig — 从传入 ConfigSO 同步武器数据 (v54 改名, 原 SyncWeaponFromProfile)
 *
 * 【v54 大厂架构重构 — UAIProfileAsset 已删除】
 *   - 旧: SyncWeaponFromProfile() → 读 Owner->MeleeProfile 字段
 *   - 新: SyncWeaponFromConfig(UAIBehaviorConfigSO*) → 调方传入 Config, 不缓存字段
 *   - Owner 不再持有 Profile 字段, 不再缓存
 *
 * 调用方:
 *   - AMeleeAIController::SetupMeleeAI 末尾 (关卡预放 AI 自举路径)
 *   - AZombieAIController::SetupZombieAI 末尾
 *
 * 逻辑:
 *   1. 如果 SpawnWeaponID 为空, 从 Config.DefaultWeaponRowName 填充
 *   2. 诊断日志记录同步结果
 *
 * 【v49 大厂重构】删除 CharacterID 同步 (改用 Pawn->GetClass() 强类型查表)
 */
void UWeaponAttachmentComponent::SyncWeaponFromConfig()
{
	// 【v54 大厂架构】本函数保留实现兼容性, 但实际写入已在 SetMeleeConfig 完成
	// 这里不再从任何字段读取, 而是由外部直接调 SetMeleeConfig(InConfig) → 完成 SetSpawnLoadout
	// 保留为空实现是为了不破坏外部已有调用方
}


// 【v54 大厂架构重构 — Owner->MeleeProfile 字段已删除】SyncWeaponFromProfile 函数整个删除
//   旧版签名: SyncWeaponFromProfile() — 从已删除的 Owner->MeleeProfile 字段读
//   新版签名: SyncWeaponFromConfig() — 见本文件上方 SyncWeaponFromConfig 实现
//   所有调用方已迁移到 SyncWeaponFromConfig (因为 Owner->MeleeProfile 字段不存在)


// ==========================================
// 10. FindWeaponAttachmentConfig — 严格匹配 (Phase 1.3 零兜底)
// ==========================================

/**
 * FindWeaponAttachmentConfig — 在 WeaponAttachmentDataTable 中查找挂载配置 (v32 严格匹配 — 零兜底)
 *
 * 【Phase 1.3 修复 — 零兜底】删除通配 fallback
 *
 * 历史 (v22-v31.x) 反模式:
 *   - 注释自承"退而求其次, 记录一个通配配置作为 fallback"
 *   - 找不到精确匹配时, 静默返回"通配占位行" (通常是设计师留的占位行)
 *   - 武器用错位置/Socket → 玩家视觉上"武器位置飘了"但**没有任何 Log Error**
 *   - 通配 fallback 命中时**不打日志**, 只在 !FallbackMatch 时打 Warning
 *
 * 新架构 (Phase 1.3 — 零兜底):
 *   - 严格匹配: 仅匹配 TargetCharacter + WeaponBlueprint 都精确对齐的行
 *   - 找不到精确 → Log Error + return nullptr
 *   - 任何降级"通配兼容"被删除, 武器挂载配置错误必须显式化
 *
 * 大厂原则:
 *   - 配置缺失 → Log Error (不是 Warning)
 *   - 配置错误 = 大厂零兜底禁区, 必须强制修复 WeaponAttachmentDataTable
 */
FWeaponAttachmentConfig* UWeaponAttachmentComponent::FindWeaponAttachmentConfig(
	TSubclassOf<ABaseCharacter> InPawnClass,
	TSubclassOf<ABaseWeapon> InWeaponClass) const
{
	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] FindWeaponAttachmentConfig: Owner 无效. "
				 "【v40.3 零兜底】Component 必须挂在 ABaseCharacter 上. "
				 "如果连接到了非 ABaseCharacter 的 Actor 上, 请修复 BP 子类化 (BP_*.WeaponAttach 父类)."));
		return nullptr;
	}

	// 【v49 零兜底】参数校验
	if (!InPawnClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] FindWeaponAttachmentConfig: InPawnClass 为空. "
			     "【修复路径1】调用方应传入 Pawn->GetClass() (BP 强类型). "
			     "【修复路径2】OnRep_CurrentWeapon 路径应从 CurrentWeapon->GetClass() 反查."));
		return nullptr;
	}
	if (!InWeaponClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] FindWeaponAttachmentConfig: InWeaponClass 为空. "
			     "【修复路径】调用方应传入 CurrentWeapon->GetClass() (BP 强类型)."));
		return nullptr;
	}

	static const FString ContextString(TEXT("WeaponAttachmentLookup"));

	// 【v37 单一真理源】从 GM 拿 DataTable, 不再用组件字段 (字段已删)
	UDataTable* WeaponAttachmentDataTable = GetWeaponAttachmentDataTable();
	if (!WeaponAttachmentDataTable)
	{
		// GetWeaponAttachmentDataTable 内部已 Log Error, 这里不再重复
		return nullptr;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponAttachment] 查找配置: PawnClass=%s, WeaponClass=%s, Owner=%s"),
		*InPawnClass->GetName(), *InWeaponClass->GetName(), *Owner->GetName());

	TArray<FName> RowNames = WeaponAttachmentDataTable->GetRowNames();

	for (const FName& RowName : RowNames)
	{
		FWeaponAttachmentConfig* Config = WeaponAttachmentDataTable->FindRow<FWeaponAttachmentConfig>(RowName, ContextString);
		if (!Config)
		{
			continue;
		}

		// ============================================================
		// 【v49 大厂架构重构】按 Class 精确匹配 — 单一真理源
		//
		// 旧 (v32-v48): TargetCharacter 配 TSoftClassPtr, WeaponBlueprint 配 TSoftClassPtr
		//              → 实际匹配时按 IsChildOf 等逻辑, 容易错位
		//
		// 新 (v49): TargetCharacter Class == InPawnClass 且 WeaponBlueprint Class == InWeaponClass
		//         → BP 强类型比对, UE 编辑器强约束
		//         → 不允许 IsChildOf 模糊匹配 (避免"父类行匹配子类 Pawn"的不可预期行为)
		// ============================================================

		// 1. TargetCharacter 匹配 (Class 精确比对)
		if (Config->TargetCharacter.IsNull())
		{
			// TargetCharacter 留空 → 通配行 (大厂原则禁止, 但兼容旧配置)
			UE_LOG(LogTemp, Warning,
				TEXT("[WeaponAttachment] FindWeaponAttachmentConfig: RowName='%s' 的 TargetCharacter 留空, 视为通配行. "
				     "【v49 警告】大厂原则禁止通配 — 必须显式配 TargetCharacter."),
				*RowName.ToString());
		}
		else
		{
			UClass* TargetCharClass = Config->TargetCharacter.LoadSynchronous();
			if (!TargetCharClass)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[WeaponAttachment] FindWeaponAttachmentConfig: RowName='%s' 的 TargetCharacter 加载失败."),
					*RowName.ToString());
				continue;
			}
			if (TargetCharClass != InPawnClass)
			{
				continue;  // PawnClass 不匹配 → 跳过
			}
		}

		// 2. WeaponBlueprint 匹配 (Class 精确比对)
		if (Config->WeaponBlueprint.IsNull())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[WeaponAttachment] FindWeaponAttachmentConfig: RowName='%s' 的 WeaponBlueprint 留空. "
				     "【v49 警告】拒绝通配 — 必须显式配 WeaponBlueprint."),
				*RowName.ToString());
			continue;  // 武器必须显式配
		}

		UClass* ConfigWeaponClass = Config->WeaponBlueprint.LoadSynchronous();
		if (!ConfigWeaponClass)
		{
			continue;
		}
		if (ConfigWeaponClass != InWeaponClass)
		{
			continue;  // WeaponClass 不匹配 → 跳过
		}

		// 3. 精确匹配成功
		UE_LOG(LogTemp, Log,
			TEXT("[WeaponAttachment] 找到精确匹配配置: RowName='%s' (PawnClass=%s, WeaponClass=%s)"),
			*RowName.ToString(), *InPawnClass->GetName(), *InWeaponClass->GetName());
		return Config;
	}

	// 【Phase 1.3 零兜底改造】删除通配 fallback
	//
	// 历史 (v22-v31.x) 反模式:
	//   - 上面 for 循环找不到精确匹配 → 退到通配占位行
	//   - 通配 fallback 命中时不打日志, 武器位置错乱但没报警
	//   - 注释自辩"退而求其次" = 字面违反零兜底原则
	//
	// 新行为 (Phase 1.3):
	//   - Log Error + return nullptr
	//   - 调用方 (OnRep_CurrentWeapon / SpawnAndEquipWeapon) 拿到 nullptr → 武器不挂载
	//   - 玩家看到"武器没显示" > "武器位置错乱" (后者让 BP 配置错被永远掩盖)
	UE_LOG(LogTemp, Error,
		TEXT("[WeaponAttachment] 未找到精确匹配配置: PawnClass=%s, WeaponClass=%s. "
			 "Pawn=%s. "
			 "【v49 大厂原则】拒绝返回通配 fallback — 必须修复 WeaponAttachmentDataTable, "
			 "添加 (TargetCharacter=PawnClass + WeaponBlueprint=WeaponClass) 精确匹配行. "
			 "打开 GM_RoomGameMode Class Defaults → Room|Data → Weapon Attachment DataTable 找到 DT 配置."),
		*InPawnClass->GetName(), *InWeaponClass->GetName(), *Owner->GetName());
	return nullptr;
}

// =============================================================================
// 【v37 单一真理源】GetWeaponAttachmentDataTable — 从 GM 拿武器挂载配置 DataTable
//
// 真理源迁移 (v37 大厂重构):
//   - 旧 (v32-v36): UWeaponAttachmentComponent 内每个 BP 都需要配这个字段
//     - BP_BaseCharacter / BP_SWAT_C / BP_GruntAI 各自配一次
//     - 配置反模式: 3 个 BP 持有同一资产 → 用户实际踩坑: 全部忘配
//   - 新 (v37): 集中在 GameMode 配一次, 所有角色通过 GM 拿到
//     - BP_GM_RoomGameMode → Class Defaults → Room|Data → Weapon Attachment DataTable
//     - 找不到 / 为空 → Log Error + return nullptr (零兜底 — 强制修 GM)
// =============================================================================
UDataTable* UWeaponAttachmentComponent::GetWeaponAttachmentDataTable() const
{
	ABaseCharacter* Owner = GetOwnerCharacterChecked(this);
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] GetWeaponAttachmentDataTable: Owner 无效 (已 Destroy 或未初始化)."));
		return nullptr;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] GetWeaponAttachmentDataTable: World 无效. Pawn=%s"),
			*Owner->GetName());
		return nullptr;
	}

	ARoomGameMode* GM = World->GetAuthGameMode<ARoomGameMode>();
	if (!GM)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] GetWeaponAttachmentDataTable: AuthGameMode 不是 ARoomGameMode (类型=%s). "
				 "Pawn=%s. 【v37 单一真理源】武器挂载表只能在 ARoomGameMode 派生类中配."),
			*GetNameSafe(World->GetAuthGameMode()), *Owner->GetName());
		return nullptr;
	}

	UDataTable* ResultTable = GM->WeaponAttachmentDataTable;
	if (!ResultTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponAttachment] GetWeaponAttachmentDataTable: GM->WeaponAttachmentDataTable 为空! "
				 "Pawn=%s. 【v37 单一真理源】必须在 BP_GM_RoomGameMode 的 Class Defaults → Room|Data → "
				 "Weapon Attachment DataTable 字段配 DT_WeaponAttachmentConfig. "
				 "【零兜底】不许默认分配, 不许 fallback."),
			*Owner->GetName());
		return nullptr;
	}

	return ResultTable;
}