// ==========================================
// ANS_MeleeTraceState 实现 (v74 大厂架构) — 近战武器命中检测区间通知
// ==========================================
//
// 【大厂原则 — 责任链】
//   ANS_MeleeTraceState (本类)
//     → Weapon->StartDamageTrace(bIsHeavy)
//       → MeleeSwStrategy::StartTrace (内部激活 + 广播)
//         → Tick 检测 BoxTrace 缝合 → 命中广播
//
//   美术在蒙太奇时间轴上控制"何时开始/结束"
//   C++ 在 Strategy 内部处理"命中判定 + 伤害结算"
//   订阅方通过 OnTraceStateChanged 委托拿事件 (HUD/音效/命中反馈)
//
// 【零兜底】
//   - Mesh 无效 → Log Error + 拒绝启动
//   - 没找到武器 → Log Error + 拒绝启动
//   - 武器不是近战 (MeshType != Melee) → Log Error + 拒绝启动
// ==========================================

#include "Animation/ANS_MeleeTraceState.h"

#include "Characters/BaseCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"  // v82+ UMeshComponent 完整定义 (CurrentWeapon->GetMeshComponent 返回类型)
#include "Weapons/BaseWeapon.h"
#include "Weapons/WeaponDamageStrategy.h"


/**
 * UANS_MeleeTraceState 构造函数
 *
 * @brief  AnimNotifyState 构造 — 仅设置编辑器时间轴显示颜色
 * @note   醒目橙色 NotifyColor — 与 Footstep 等其他 ANS 区分, 美术一眼能识别
 */
UANS_MeleeTraceState::UANS_MeleeTraceState()
{
#if WITH_EDITORONLY_DATA
	// 蒙太奇编辑器默认区间长度 (美术可拖动调整)
	NotifyColor = FColor(220, 80, 40); // 醒目橙色 — 与 Footstep 区分
#endif
}


// ==========================================================
// 1. NotifyBegin — 进入区间
// ==========================================================

/**
 * NotifyBegin — 进入 trace 区间时引擎自动调用
 *
 * @brief  蒙太奇播放进入 ANS 区间起点时, 根据 EnterState 启动/停止 trace
 * @param  MeshComp        当前播放动画的骨骼网格组件
 * @param  Animation       触发该通知的动画序列
 * @param  TotalDuration   本次 ANS 区间总时长 (秒)
 * @param  EventReference  通知事件引用 (UE 5 新 API)
 * @note   【v85.3 大厂重构】所有进程 (服务器+客户端) 都执行相同逻辑, 伤害只在服务器结算
 * @note   【v93.2 母体复用】bIsMother=true 时走 StartMotherTrace/StopMotherTrace (零武器依赖)
 * @note   EnterState 三种语义:
 *         - Tracing: 启动 trace (最常用, 占区间大部分时长)
 *         - Idle:    主动停止 trace (美术用于"先停一下"的视觉表达)
 *         - Hit:     非法 — 命中状态由 TickDetection 自动设置, 不允许美术配置
 */
void UANS_MeleeTraceState::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	// 零兜底: Mesh 无效 → 拒绝
	if (!MeshComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ANS_MeleeTraceState] NotifyBegin: MeshComp 为空 — 拒绝启动 trace."));
		return;
	}

	// 取 Owner (角色 Pawn)
	AActor* OwnerActor = MeshComp->GetOwner();
	if (!OwnerActor)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ANS_MeleeTraceState] NotifyBegin: MeshComp 没有 Owner — 拒绝启动 trace."));
		return;
	}

	// 零兜底 — Owner 不是 ABaseCharacter
	ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(OwnerActor);
	if (!OwnerChar)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ANS_MeleeTraceState] NotifyBegin: Owner=%s 不是 ABaseCharacter — 拒绝启动 trace. "
			     "【v74 零兜底】只支持挂在角色 Mesh 上的蒙太奇."),
			*OwnerActor->GetName());
		return;
	}

	// ============================================================
	// 【v93.2 大厂架构 — 母体复用】bIsMother=true 早返回分支
	//   - 母体没有武器 → 不能走 CurrentWeapon 路径 (下面会因 CurrentWeapon=nullptr 被零兜底拒绝)
	//   - 必须在取武器检查之前分支, 否则母体永远走不到这里
	//   - 母体路径: 走 Owner->StartMotherTrace(bIsHeavy) (零武器依赖)
	// ============================================================
	if (OwnerChar->bIsMother)
	{
		// 母体分支: 根据 EnterState 决策 (镜像普通武器路径)
		switch (EnterState)
		{
		case EWeaponTraceState::Tracing:
			OwnerChar->StartMotherTrace(bIsHeavy);
			break;

		case EWeaponTraceState::Idle:
			OwnerChar->StopMotherTrace();
			break;

		case EWeaponTraceState::Hit:
			UE_LOG(LogTemp, Error,
				TEXT("[ANS_MeleeTraceState] NotifyBegin: 母体 EnterState=Hit 不合法 — 拒绝. "
				     "【v93.2 零兜底】Hit 由 MeleeSwStrategy 命中时自动设置."));
			break;

		default:
			UE_LOG(LogTemp, Error,
				TEXT("[ANS_MeleeTraceState] NotifyBegin: 母体未知 EnterState=%d — 拒绝."),
				static_cast<int32>(EnterState));
			break;
		}
		return;
	}

	// 取当前武器
	ABaseWeapon* CurrentWeapon = OwnerChar->GetCurrentWeapon();
	if (!CurrentWeapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ANS_MeleeTraceState] NotifyBegin: Owner=%s 没有当前武器 — 拒绝启动 trace. "
			     "【v74 零兜底】检查 SpawnWeaponID 是否配置."),
			*OwnerChar->GetName());
		return;
	}

	// 零兜底: 只处理近战武器 (其他类型由对应 ANS 处理)
	if (CurrentWeapon->GetMeshType() != EWeaponMeshType::Melee)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ANS_MeleeTraceState] NotifyBegin: Weapon=%s MeshType=%d 不是 Melee — 拒绝启动 trace. "
			     "【v74 零兜底】枪械用 ANS_RangedFire, 不要挂这个标签."),
			*CurrentWeapon->GetName(),
			static_cast<int32>(CurrentWeapon->GetMeshType()));
		return;
	}

	// ============================================================
	// 【v85.3 大厂架构重构】所有进程都调 StartDamageTrace
	//
	// 旧版 (v74-v85.2) 反模式:
	//   NotifyBegin 只让服务器调 StartDamageTrace → 客户端只能画静态 Box (不执行 trace)
	//   → 客户端看不到同步的 trace 视觉效果 (命中时的绿色 Box)
	//
	// 新版 (v85.3):
	//   所有进程 (服务器+客户端) 都调 StartDamageTrace
	//   MeleeSwStrategy::TickDetection 在所有进程都执行
	//   Server_ReportHit 的 HasAuthority() 守卫是防重复扣血的唯一机制
	//
	// 调用链 (大厂原则 - 责任链):
	//   1. 动画播放到 ANS 区间开始 → NotifyBegin → StartDamageTrace
	//   2. MeleeSwStrategy::TickDetection 跨帧执行 BoxTrace (两端都跑)
	//   3. 命中 → Server_ReportHit RPC → 服务器 ApplyDamage (HasAuthority 守卫)
	//   4. 动画结束/ANS 区间结束 → NotifyEnd → StopDamageTrace
	//
	// 大厂原则 - 零兜底:
	//   - 不区分服务器/客户端进程: 两端都执行相同的 trace 逻辑
	//   - 伤害计算只在服务器: Server_ReportHit 的 HasAuthority() 守卫保证
	// ============================================================

	// 根据 EnterState 决策 (美术可能配 Idle 做"主动关闭")
	switch (EnterState)
	{
	case EWeaponTraceState::Tracing:
		CurrentWeapon->StartDamageTrace(bIsHeavy);
		break;

	case EWeaponTraceState::Idle:
		CurrentWeapon->StopDamageTrace();
		break;

	case EWeaponTraceState::Hit:
		// Hit 是 TickDetection 命中时瞬态, 不能由 AnimNotifyState 设置
		UE_LOG(LogTemp, Error,
			TEXT("[ANS_MeleeTraceState] NotifyBegin: EnterState=Hit 不合法 — 拒绝. "
			     "【v74 零兜底】Hit 由 MeleeSwStrategy 命中时自动设置, 不允许美术手动配置."));
		break;

	default:
		UE_LOG(LogTemp, Error,
			TEXT("[ANS_MeleeTraceState] NotifyBegin: 未知 EnterState=%d — 拒绝."),
			static_cast<int32>(EnterState));
		break;
	}
}


// ==========================================================
// 2. NotifyEnd — 离开区间
// ==========================================================

/**
 * NotifyEnd — 离开 trace 区间时引擎自动调用
 *
 * @brief  蒙太奇播放离开 ANS 区间终点时, 关闭 trace (无论是否命中)
 * @param  MeshComp        当前播放动画的骨骼网格组件
 * @param  Animation       触发该通知的动画序列
 * @param  EventReference  通知事件引用 (UE 5 新 API)
 * @note   【v85.3 大厂重构】所有进程都调 StopDamageTrace, 客户端 trace 立即关闭
 * @note   StopDamageTrace 是幂等的 (蒙太奇自然结束时武器可能已切换/销毁, 静默 return 安全)
 */
void UANS_MeleeTraceState::NotifyEnd(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	// 零兜底: Mesh 无效 → 静默 (蒙太奇自然结束, 不能刷错)
	if (!MeshComp)
	{
		return;
	}

	AActor* OwnerActor = MeshComp->GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(OwnerActor);
	if (!OwnerChar)
	{
		return;
	}

	// ============================================================
	// 【v93.2 大厂架构 — 母体复用】bIsMother=true 时关闭母体 trace
	//   - 与 NotifyBegin 对称: 母体无武器 → 走 Owner->StopMotherTrace
	//   - 不论 EnterState 配置是 Tracing/Idle, NotifyEnd 都关闭 (蒙太奇自然结束语义)
	// ============================================================
	if (OwnerChar->bIsMother)
	{
		OwnerChar->StopMotherTrace();
		return;
	}

	ABaseWeapon* CurrentWeapon = OwnerChar->GetCurrentWeapon();
	if (!CurrentWeapon)
	{
		// 蒙太奇结束时武器可能已切换/销毁 — 静默 (StopDamageTrace 幂等)
		return;
	}

	// ============================================================
	// 【v85.3 大厂架构重构】所有进程都调 StopDamageTrace
	//
	// 旧版 (v74-v85.2) 反模式:
	//   NotifyEnd 只让服务器调 StopDamageTrace → 客户端永远不关 trace
	//   → 客户端 trace 永远开着 (射线跟着角色走)
	//
	// 新版 (v85.3):
	//   所有进程都调 StopDamageTrace
	//   MeleeSwStrategy::TraceState = Idle → TickDetection 不再执行
	//   → 客户端 trace 立即关闭
	// ============================================================
	CurrentWeapon->StopDamageTrace();
}


// ==========================================================
// 3. GetNotifyName — 蒙太奇编辑器显示
// ==========================================================

/**
 * GetNotifyName_Implementation
 *
 * @brief  返回蒙太奇编辑器时间轴上的显示名称, 便于美术识别不同 ANS 区间
 * @return FString 形如 "Melee Trace (Tracing Heavy)" — 包含状态和是否重击
 * @note   UE 5 AnimNotifyState 标准 API, 编辑器自动调用
 */
FString UANS_MeleeTraceState::GetNotifyName_Implementation() const
{
	// 编辑器时间轴上显示 "Melee Trace (Tracing)" / "Melee Trace (Idle)"
	const FString StateName = StaticEnum<EWeaponTraceState>()
		? StaticEnum<EWeaponTraceState>()->GetDisplayNameTextByValue(static_cast<int64>(EnterState)).ToString()
		: TEXT("Unknown");

	return FString::Printf(TEXT("Melee Trace (%s%s)"),
		*StateName,
		bIsHeavy ? TEXT(" Heavy") : TEXT(""));
}
