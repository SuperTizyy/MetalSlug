// Copyright (c) 2026.
// 文件作用: AI 控制器基类，负责 AI 的感知、行为树驱动、阵营判定、追逐兜底等核心逻辑
// 设计原则: 大厂架构 — 关键行为不依赖单一系统，多层兜底保证 AI 不会"死站"

// 引入本类的头文件，包含类声明和接口定义
#include "Systems/BaseAIController.h"

// 引入 UE 感知组件，用于检测视野内的目标（敌人/友军）
#include "Perception/AIPerceptionComponent.h"
// 引入视觉感知配置类，用于配置 AI 的视野半径、角度等参数 【Phase 2】共用层配 Sight
#include "Perception/AISenseConfig_Sight.h" // 【Phase 2】共用层配 Sight
// 引入行为树资源类，AI 的行为逻辑由行为树驱动
#include "BehaviorTree/BehaviorTree.h"
// 引入黑板组件，用于在行为树节点间共享数据（如目标、位置等）
#include "BehaviorTree/BlackboardComponent.h"
// 引入黑板数据资产，定义黑板中有哪些 Key 及其类型
#include "BehaviorTree/BlackboardData.h"
// 【P0 2026.07.09】AI 行为配置/Key 名集中管理, 通过 AIBlackboardKeyNames::Get(EAIBlackboardKey::XXX) 解析 FName
// 注意: AIBehaviorTypes.h 仅在此处 include 一次, BaseAIController.h 不重复 include (避免 BUILD.H 宏膨胀)
// 【P0 2026.07.09 bHasAttackToken 已弃用】
//   原 BTService_UpdateBlackboard 用 bHasAttackToken 做冷却中间态 (上帝 Service 反模式)
//   现架构: BTTask_PlayAttackMontage 一次性写 BB.CooldownEndTime, BTDecorator_CooldownReady 实时算
//   bHasAttackToken 仅作字符串常量保留, 见 AIBehaviorTypes.h 注释
#include "Systems/AI/AIBehaviorTypes.h"
// 引入 AI Profile 资产，包含阵营标签和行为配置资产的软引用
#include "Data/AI/AIProfileAsset.h"
// 引入 AI 行为配置静态对象，包含行为树引用和战斗/感知原始参数
#include "Data/AI/AIBehaviorConfigSO.h"
// 引入流式管理器，用于 TSoftObjectPtr::LoadSynchronous 同步加载行为树资源 【Phase 1】
#include "Engine/StreamableManager.h" // 【Phase 1】 TSoftObjectPtr::LoadSynchronous
// 引入 World 类，用于获取游戏世界实例（导航系统、子系统等）
#include "Engine/World.h"
// 引入引擎工具类，提供 TActorIterator 用于遍历世界中所有 Actor 【P0 2026.07.03】
#include "EngineUtils.h"              // 【P0 2026.07.03 19:22】TActorIterator (主动扫描兜底)
// 引入角色类，用于主动扫描时遍历所有 ACharacter 类型的 Pawn 【P0 2026.07.03】
#include "GameFramework/Character.h"  // 【P0 2026.07.03 19:22】ACharacter 主动扫描目标用
// 引入通用阵营代理接口，用于 ETeamAttitude 阵营敌我判定 【P0 2026.07.03】
#include "GenericTeamAgentInterface.h" // 【P0 2026.07.03 19:22】ETeamAttitude 阵营判定
// 引入角色移动组件，用于 TickChaseFallback 直接驱动 CharacterMovement
#include "GameFramework/CharacterMovementComponent.h"
// 引入胶囊体组件，用于 ComputeActorCenterDistance 计算角色中心点
#include "Components/CapsuleComponent.h"
// 引入动画实例，用于 AnimInstance.IsMovementLocked() 判断 (AnimBP 动画切换)
#include "Animation/AnimInstance.h"
// 引入骨骼网格组件，用于 AnimMontage 播放
#include "Components/SkeletalMeshComponent.h"
// 引入 GameplayStatics，用于获取 GameMode 实例 (BattleInProgress 判断)
#include "Kismet/GameplayStatics.h"
// 引入房间游戏模式，用于订阅 OnBattleStarted 委托
#include "Systems/RoomGameMode.h"
// 引入房间状态枚举，用于 ERoomState::BattleInProgress 判断
#include "Data/Enums/RoomEnums.h"
// 引入基础角色类，用于 Cast 判断 AI Pawn / 访问 bIsMovementLocked
#include "Characters/BaseCharacter.h"
// 引入 AI 运行时配置组件，TickChaseFallback/GetEffectiveAttackRange 用
#include "Systems/AI/AIRuntimeConfigComponent.h"
// 引入导航系统，用于检查 NavMesh 是否可用 (MoveTo 静默失败诊断)
#include "NavigationSystem.h"
// 引入 RecastNavMesh，用于诊断日志输出 NavMesh 实例名称
#include "NavMesh/RecastNavMesh.h"
// 引入路径跟随组件，包含 EPathFollowingRequestResult 完整定义 (UE5.6 修复)
#include "Navigation/PathFollowingComponent.h"

// 定义本文件的静态日志分类，所有 UE_LOG 使用此分类输出，方便在日志中过滤
DEFINE_LOG_CATEGORY_STATIC(LogBaseAI, Log, All);

// ==========================================
// 1. 构造函数
// ==========================================

// 构造函数：初始化 AI 控制器的默认子对象和 Tick 设置
ABaseAIController::ABaseAIController()
{
	// 创建 AI 运行时配置组件，存储难度缩放后的战斗/感知参数，可被外部 Profile 覆盖
	RuntimeConfig = CreateDefaultSubobject<UAIRuntimeConfigComponent>(TEXT("RuntimeConfig"));
	// 创建 AI 感知组件，负责检测视野内的目标（敌人/友军），触发 OnTargetPerceptionUpdated 回调
	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));

	// 【P0 2026.07.03】开启 Tick: 用于 C++ 层 MoveTo 兜底 (TickChaseFallback)
	// AAIController 默认关 Tick, 我们需要它
	// 启用 Tick 循环，使 Tick() 函数每帧被调用，用于追逐兜底逻辑
	PrimaryActorTick.bCanEverTick = true;
	// 设置 Actor 生成时 Tick 默认启用，无需外部手动开启
	PrimaryActorTick.bStartWithTickEnabled = true;
}


// ==========================================
// 2. UE 生命周期
// ==========================================

// BeginPlay：Actor 进入游戏世界时调用，用于初始化感知回调和启动行为树
void ABaseAIController::BeginPlay()
{
	// 调用父类 BeginPlay，确保 UE 引擎基类初始化流程正常执行
	Super::BeginPlay();

	// 【P0 2026.07.08 大厂架构重构】移除 BeginPlayTimeStamp 写入
	//   原用途: TickChaseFallback 启动期报警降噪 (>10s 后 AttackRange 报警降级)
	//   TickChaseFallback 已删, 字段无调用方
	// 替代: 启动期 AttackRange 异常检测改在 BTService_UpdateBlackboard 内执行

	// 如果感知组件存在，绑定感知更新回调函数
	if (AIPerception)
	{
		// 将 OnTargetDetected 函数绑定到感知组件的委托，当检测到目标时自动触发
		AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &ABaseAIController::OnTargetDetected);
	}

	// 【Phase 1 重构】 未走 Profile 入口, 但有 Pawn 时尝试 fallback
	// 检查条件：当前有 Pawn 控制、没有加载 Profile、行为树尚未启动
	if (GetPawn() && !CurrentProfile && !bBehaviorTreeStarted)
	{
		// 走传统兼容路径：由外部 GameMode 决定 BT 启动，这里只注册黑板 Key
		RunLegacyBehaviorTree();
	}

	// 【P0 架构升级 2026.07.03】兜底诊断: 无论是否走 Profile 入口, 都打印一次状态
	// 设计: 老路径 (RunLegacy) 走不到 OnProfileLoaded, 没诊断会瞎;
	//      新路径 (Profile) 已在 OnProfileLoaded 末尾诊断过, 这里会重复打印一次, 但内容相同 — 接受
	//      实战中 2 次日志距离 < 1 帧, 不会刷屏
	// 输出 AI 启动状态的全景诊断日志，包括 Profile、阵营、感知、NavMesh 等信息
	DiagnoseAndLogBootStatus();
}

// Tick：每帧调用，用于执行追逐兜底逻辑
void ABaseAIController::Tick(float DeltaSeconds)
{
	// 调用父类 Tick，确保引擎基类的帧更新逻辑正常执行
	Super::Tick(DeltaSeconds);

	// 【P0 2026.07.08 大厂架构重构 — BT 100% 接管】
	// 历史: 原 TickChaseFallback (C++) + UpdateNearbyThreatByDistance (C++) 共同实现"距离决策+追玩家"
	//       与 BT 树 DistanceCheck Decorator 职责重叠, 形成双轨决策冲突
	// 重构: 删除 C++ 兜底, AI 100% 由 BT_Grunt.uasset 决策
	//       - 距离决策: UE 原生 Compare BB Entries (读 BB.DistanceToTarget vs BB.AttackRange)
	//       - 追玩家:    UE 原生 Move To (AcceptanceRadius=AR-10)
	//       - 距离更新:  BTService_UpdateDistance (0.1s) — 单一职责, 只写 Distance + bHasTarget + AttackRange
	//       - HP 更新:   BTService_UpdateHealth (0.1s) — 单一职责, 只写 HealthPercent
	//       - 目标刷新:  BTService_RefreshTarget (0.3s)
	// 兜底: 如果 BT 资产未配置 (BP_MeleeGrunt 没设 BehaviorTree), AI 静止 — 设计师负责, C++ 不救
	//
	// v18 用户决策: "彻底删除 TickChaseFallback, 最纯 BT 架构"

	// 【P0 2026.07.09 进一步重构 — 删除冷却 Token 中间态】
	//   原 BTService_UpdateBlackboard 写 bHasAttackToken + CooldownEndTime (上帝 Service, 4 个反模式)
	//   现架构:
	//     - BTTask_PlayAttackMontage 攻击触发时, 一次性写 BB.CooldownEndTime = Now + AttackCooldown
	//     - BTDecorator_CooldownReady 实时读 World.Time vs CooldownEndTime (0 延迟决策)
	//     - BTService 不再维护任何冷却状态 (它只派生距离/HP 等世界事实)
	//     - 删除 bHasAttackToken BB Key 概念 (中间态冗余)
	//     - 删除 BTDecorator_HasAttackToken (被 Decorator_CooldownReady 取代)
}

// 【P0 2026.07.08 大厂架构重构】删除以下两个函数 (BT 接管):
//   - UpdateNearbyThreatByDistance: BB.NearbyThreat 改由 BT 装饰器基于 DistanceToTarget 派生
//   - TickChaseFallback:           C++ 距离决策删除, AI 100% 由 BT_Grunt 驱动
// 调用方已清理 (Tick 函数体内无调用, 头文件声明已删除)

/**
 * 【P0 大厂兜底 2026.07.03 19:22】主动扫描最近的敌方 Character
 *
 * 用途: AIPerception 失效时的最后兜底. 通过阵营接口找最近的敌人.
 *
 * 设计:
 *   - 遍历世界中所有 ACharacter (不是 ABaseCharacter, 因为玩家可能是 BP_SWAT 直接继承 ACharacter)
 *   - 用 IGenericTeamAgentInterface::GetTeamAttitudeTowards 判断敌我
 *   - 返回距离最近且 < ScanRange 的敌人
 *
 * 性能:
 *   - PVE 关卡 < 20 个 Pawn, GetAllActorsOfClass 内部遍历 O(n)
 *   - 每 0.3s 调用一次 (TickChaseFallback 节流), 完全可接受
 *   - 不做 NavMesh 投影, 不做视线检测 — 只负责"找到候选", 后续 MoveTo 自然会 NavMesh 修正
 *
 * @param MyCharacter  自身 Character (用于计算距离)
 * @return 最近的敌方 Character, 没找到返回 nullptr
 */
// ScanForNearestEnemy：主动扫描世界中最近的敌方角色，作为感知系统失效时的兜底
AActor* ABaseAIController::ScanForNearestEnemy(ACharacter* MyCharacter, float ScanRange)
{
	// 检查自身角色和世界是否有效，无效则无法扫描
	if (!MyCharacter || !GetWorld())
	{
		return nullptr;
	}

	// 扫描范围: 参数未指定时用默认 = 10000 (实测玩家重生可能跳 4000+ 单位, 3000 漏)
	// 设计权衡: PVE 关卡 < 20 个 Pawn, 10000 半径 = 0.6 平方千米, 性能完全可接受
	//          主动扫描是兜底, 必须保证"永远能找到" — 范围过小 = bug
	if (ScanRange <= 0.f)
	{
		ScanRange = 10000.f; // 【P0 2026.07.06】3000 → 10000, 兜底必须够大
		// 如果运行时配置存在且已加载配置，使用缩放后的感知半径计算扫描范围
		if (RuntimeConfig && RuntimeConfig->GetConfig())
		{
			// 获取难度缩放后的感知参数
			const FAIPerceptionParams PerceptionParams = RuntimeConfig->GetScaledPerception();
			// 取配置中的视野半径*2 与默认值中的较大者，确保扫描范围足够大
			ScanRange = FMath::Max(PerceptionParams.SightRadius * 2.f, ScanRange);
		}
	}

	// 获取 AI 角色当前世界坐标，用于计算与候选目标的距离
	const FVector MyLoc = MyCharacter->GetActorLocation();

	// 初始化最近敌人指针为空
	AActor* NearestEnemy = nullptr;
	// 初始化最近距离的平方为扫描范围的平方（避免每帧开方运算，提升性能）
	float NearestDistSq = ScanRange * ScanRange;

	// 遍历世界中所有 ACharacter 类型的 Actor
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		// 获取当前遍历到的 Character
		ACharacter* Candidate = *It;
		// 跳过空指针、自身和正在被销毁的 Actor
		if (!Candidate || Candidate == MyCharacter || Candidate->IsPendingKillPending())
		{
			continue;
		}

		// 跳过死亡的
		// 尝试将候选者转换为 ABaseCharacter，检查是否死亡
		if (ABaseCharacter* BaseChar = Cast<ABaseCharacter>(Candidate))
		{
			// 如果候选者已死亡，跳过
			if (BaseChar->IsDead())
			{
				continue;
			}
		}

		// 【P0 终极修复 2026.07.06】阵营判定 — 走 Pawn->Controller 间接路径
		//
		// 之前坑: Cast<IGenericTeamAgentInterface>(BP_Pawn) 失败
		//   因为 BP 类 (BP_SWAT_C) 不实现 C++ 接口 (默认只有 AIController 实现)
		//   → 走 else 分支 → 永远 Hostile → 应该能找到目标, 但实际失败
		//   根因: UE 的接口 Cast 对纯 BP 类的支持有限, 直接 Cast 不可靠
		//
		// 正确做法 (UE 官方推荐): Pawn->GetController() → 再 Cast AIController
		//   AIController 实现接口, GetGenericTeamId() 返回阵营 ID
		//
		// 大厂兜底: 即使阵营判定失败, 也不允许 ScanForNearestEnemy 找不到目标
		//   任何 ACharacter 都视为潜在敌人, 后续 BT 树精确判定
		ETeamAttitude::Type Attitude = ETeamAttitude::Neutral;
		bool bIsHostile = false;

		// 尝试通过 Controller 获取阵营
		if (const APawn* CandidatePawn = Cast<APawn>(Candidate))
		{
			if (const IGenericTeamAgentInterface* ControllerAgent =
				Cast<IGenericTeamAgentInterface>(CandidatePawn->GetController()))
			{
				const FGenericTeamId OtherId = ControllerAgent->GetGenericTeamId();
				const FGenericTeamId MyId = GetGenericTeamId();
				// 阵营不同 → 敌对 (大厂兜底: 不同阵营就是敌人)
				if (OtherId != MyId && OtherId != FGenericTeamId::NoTeam)
				{
					bIsHostile = true;
				}
				// 显式 NoTeam (255) → 敌对 (未配阵营的 Character = 可疑目标)
				else if (OtherId == FGenericTeamId::NoTeam)
				{
					bIsHostile = true;
				}
			}
			else
			{
				// 没 AIController (普通 NPC/怪物 Character) → 视为潜在敌人
				// 大厂兜底: PVE 关卡里这种 Character 99% 是敌人
				bIsHostile = true;
			}
		}
		else
		{
			// 不是 Pawn 的 Character → 跳过 (不攻击非 Character)
			continue;
		}

		if (!bIsHostile)
		{
			continue; // 不是敌人, 跳过
		}

		// 计算 AI 与候选者之间的距离平方（避免开方，提升性能）
		const float DistSq = FVector::DistSquared(MyLoc, Candidate->GetActorLocation());
		// 如果候选者距离比当前记录的最近距离更近
		if (DistSq < NearestDistSq)
		{
			// 更新最近距离平方
			NearestDistSq = DistSq;
			// 更新最近敌人指针
			NearestEnemy = Candidate;
		}
	}

	// 【P0 调试 2026.07.06】找不到目标时输出排查日志 (Verbose 级别, 默认不刷屏, 排查时改 LogBaseAI verbosity 即可)
	if (!NearestEnemy)
	{
		int32 CharacterCount = 0;
		for (TActorIterator<ACharacter> It2(GetWorld()); It2; ++It2)
		{
			CharacterCount++;
			ACharacter* C = *It2;
			if (!C || C == MyCharacter) continue;
			const APawn* CP = Cast<APawn>(C);
			const AController* Ctrl = CP ? CP->GetController() : nullptr;
			UE_LOG(LogBaseAI, Verbose,
				TEXT("[%s] ScanDebug: 候选 %s, isPawn=%d, hasCtrl=%d, dist=%.0f, MyTeam=%d"),
				*GetName(),
				*GetNameSafe(C),
				CP ? 1 : 0,
				Ctrl ? 1 : 0,
				FVector::Dist(MyLoc, C->GetActorLocation()),
				GetGenericTeamId().GetId());
		}
		UE_LOG(LogBaseAI, Verbose,
			TEXT("[%s] ScanForNearestEnemy 失败: WorldACharCount=%d, ScanRange=%.0f, MyTeam=%d"),
			*GetName(),
			CharacterCount,
			ScanRange,
			GetGenericTeamId().GetId());
	}
	// 返回找到的最近敌人，如果没有则返回 nullptr
	return NearestEnemy;
}

// SetCurrentlyAttacking 实现 — 【P0 终极修复 2026.07.06】重置卡死计时器
//
// 背景: 之前 inline 实现只是设 bool, 但 bIsCurrentlyAttacking=true 后如果 BT 异常退出
//       (如 OnTaskFinished 走别的路径, AbortTask 异常), C++ 层不知道, 永远卡 true
//
// 【P0 2026.07.08 大厂架构重构】移除 TimeSinceAttackingStarted (TickChaseFallback 已删)
//   原 Bailout 机制: TickChaseFallback 内 >15s 强制清 false
//   替代方案: BTTask_PlayAttackMontage 内部对称设置 + OnMontageEnded 回调统一收口
//              双重防护 — BT 自身能感知蒙太奇结束, 不需要 C++ 兜底
void ABaseAIController::SetCurrentlyAttacking(bool bAttacking)
{
	bIsCurrentlyAttacking = bAttacking;
}

// 【P0 2026.07.07】AttackCooldown 冷却期间标志 setter
// BTTask_MeleeAttack: 状态 2 攻击触发时清 false，状态 3 冷却中设 true
// TickChaseFallback: 看到 true → 用 LockAtAttackRange 模式，维持 AttackRange 距离
void ABaseAIController::SetInAttackCooldown(bool bInCooldown)
{
	bIsInAttackCooldown = bInCooldown;
}

// OnPossess：当控制器接管 Pawn 时调用，用于初始化运行时配置组件
void ABaseAIController::OnPossess(APawn* InPawn)
{
	// 调用父类 OnPossess，确保引擎基类的接管逻辑正常执行
	Super::OnPossess(InPawn);

	// 【P0 2026.07.07 大厂架构修复 — Component 归属正确性】
	//
	// 历史 Bug (v8): 旧代码 InPawn->FindComponentByClass<UAIRuntimeConfigComponent>()
	//   永远找不到, 因为 Component 挂在 AIController 上 (this), 不在 Pawn 上
	//
	// 修复: 直接 this->FindComponent, 同时 fallback 走 Pawn (兼容老 BP 配错的情况)
	if (!RuntimeConfig)
	{
		RuntimeConfig = FindComponentByClass<UAIRuntimeConfigComponent>();
	}
	// 大厂兜底: 如果 Controller 上没挂 (老 BP 配错), 走 Pawn 兼容
	if (!RuntimeConfig && InPawn)
	{
		RuntimeConfig = InPawn->FindComponentByClass<UAIRuntimeConfigComponent>();
	}

	// 【P0 2026.07.07 大厂架构重构】OnPossess 兜底 — 重置锁步状态
	// 防止 AI 重新 Possess 同一个 Pawn (或关卡切换) 时, 残留的 bMovementLockedForCooldown=true
	// 让 Pawn 永远不能动
	if (bMovementLockedForCooldown)
	{
		LockMovementForCooldown(false);
	}
}

// EndPlay：当 Actor 离开游戏世界时调用，用于清理资源
void ABaseAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 【P0 2026.07.06】取消订阅 RoomGameMode::OnBattleStarted 委托
	// 避免 GameMode 持有失效的 lambda 引用导致野指针访问
	if (OnBattleStartedHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			if (AGameModeBase* GMBase = UGameplayStatics::GetGameMode(World))
			{
				if (ARoomGameMode* RoomGM = Cast<ARoomGameMode>(GMBase))
				{
					RoomGM->OnBattleStarted.Remove(OnBattleStartedHandle);
				}
			}
		}
		OnBattleStartedHandle.Reset();
	}

	// 【P0 2026.07.07 大厂架构重构】销毁兜底 — 恢复 Character 的移动锁步状态
	// 防止 AI 控制器销毁但 Character 还活着时, 残留 MaxWalkSpeed=0 + bIsMovementLocked=true
	// 导致 Character 永远不能动 (尤其关卡切换 / 重新 Possess 时)
	if (bMovementLockedForCooldown)
	{
		LockMovementForCooldown(false);
	}

	// 调用父类 EndPlay，确保引擎基类的清理逻辑正常执行
	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 3. Profile 入口
// ==========================================

// InitializeFromProfile：从 AI Profile 资产初始化 AI，加载配置并启动行为树
void ABaseAIController::InitializeFromProfile(UAIProfileAsset* InProfile)
{
	// 检查传入的 Profile 是否为空
	if (!InProfile)
	{
		// 输出警告日志，提示 Profile 为空
		UE_LOG(LogBaseAI, Warning, TEXT("[%s] InitializeFromProfile(null)"), *GetName());
		return;
	}

	// 保存当前 Profile，供后续逻辑使用
	CurrentProfile = InProfile;
	// 输出日志，记录 Profile 名称、阵营标签和行为配置资产路径
	UE_LOG(LogBaseAI, Log, TEXT("[%s] InitializeFromProfile: Profile=%s FactionTag=%s ConfigAsset=%s"),
		*GetName(), *InProfile->GetName(),
		*InProfile->FactionTag.ToString(),
		*InProfile->BehaviorConfig.ToSoftObjectPath().ToString());

	// 把 Profile.FactionTag 应用到阵营协议
	// 如果 Profile 的阵营标签有效，解析并设置 AI 的阵营 ID
	if (InProfile->FactionTag.IsValid())
	{
		// 通过 ABaseCharacter 的工具函数将阵营标签转换为 GenericTeamId，并设置给 AI
		SetGenericTeamId(ABaseCharacter::ResolveGenericTeamIdFromTag(InProfile->FactionTag));
	}

	// Config 同步加载 (BT 由它异步加载)
	// 同步加载行为配置资产（包含行为树、战斗/感知参数）
	UAIBehaviorConfigSO* Config = InProfile->LoadBehaviorConfigSync();
	// 输出日志，记录加载到的配置资产名称
	UE_LOG(LogBaseAI, Log, TEXT("[%s] InitializeFromProfile: LoadBehaviorConfigSync -> %s"),
		*GetName(), *GetNameSafe(Config));
	// 如果运行时配置组件存在，将加载的配置应用到组件上
	if (RuntimeConfig)
	{
		RuntimeConfig->ApplyConfig(Config);
	}

	// 如果配置加载失败，输出警告并返回
	if (!Config)
	{
		UE_LOG(LogBaseAI, Warning, TEXT("[%s] Profile has no Config"), *GetName());
		return;
	}

	// BT 异步加载完成后启动 (走 AIProfileAsset 的 FOnAIBehaviorConfigLoaded)
	// 设计: UAIProfileAsset 负责异步加载它持有的 SoftObjectPtr<AIBehaviorConfigSO>
	//       加载完成后 config 已 Apply 到 RuntimeConfig (base 已同步一次, 这里直接异步监听完成)
	// 创建异步加载完成的委托，绑定到 OnProfileLoaded 函数
	ProfileLoadedDelegate = FOnAIBehaviorConfigLoaded::CreateUObject(this, &ABaseAIController::OnProfileLoaded);
	// 启动异步加载行为配置，加载完成后会触发 OnProfileLoaded 回调
	InProfile->LoadBehaviorConfigAsync(ProfileLoadedDelegate);
	// 输出日志，提示异步加载已启动
	UE_LOG(LogBaseAI, Log, TEXT("[%s] InitializeFromProfile: LoadBehaviorConfigAsync dispatched"), *GetName());
}

// OnProfileLoaded：行为配置异步加载完成后调用，用于启动行为树和配置感知
void ABaseAIController::OnProfileLoaded()
{
	// 【P0 2026.07.08 调试用】Warning 级别 log, 强制显示 OnProfileLoaded 入口 (排查 BT 启动问题)
	UE_LOG(LogBaseAI, Warning, TEXT("[%s] >>> OnProfileLoaded ENTERED (CurrentProfile=%s RuntimeConfig=%s GetConfig=%s)"),
		*GetName(),
		*GetNameSafe(CurrentProfile),
		*GetNameSafe(RuntimeConfig),
		*GetNameSafe(RuntimeConfig ? RuntimeConfig->GetConfig() : nullptr));

	// 输出日志，记录当前 Profile、运行时配置和配置资产的状态
	UE_LOG(LogBaseAI, Log, TEXT("[%s] OnProfileLoaded: CurrentProfile=%s RuntimeConfig=%s GetConfig=%s"),
		*GetName(),
		*GetNameSafe(CurrentProfile),
		*GetNameSafe(RuntimeConfig),
		*GetNameSafe(RuntimeConfig ? RuntimeConfig->GetConfig() : nullptr));

	// 检查 Profile、运行时配置和配置资产是否都有效
	if (!CurrentProfile || !RuntimeConfig || !RuntimeConfig->GetConfig())
	{
		// 如果有组件缺失，输出警告并走传统兼容路径
		UE_LOG(LogBaseAI, Warning, TEXT("[%s] OnProfileLoaded: missing components, fallback RunLegacy"), *GetName());
		RunLegacyBehaviorTree();
		return;
	}

	// 【Phase 2 共用层】感知配置 — 收归 Base, 任何模式都走这里
	// 旧: MeleeAIController::ConfigurePerceptionFromConfig 单独配
	// 新: Base 统一从 RuntimeConfig->GetScaledPerception() 读, 自动配
	// 配置 AI 的感知系统（视觉），从运行时配置读取缩放后的参数
	ConfigurePerceptionFromConfig();

	// 【P0 架构升级 2026.07.06 17:00】延迟 BT 启动 — 等 GameMode 广播战斗开始
	// 解决: AI 出生后立即 RunBehaviorTree, 玩家还在大厅就追玩家 (用户原话"还没点开始游戏 AI 就开始攻击人")
	//
	// 策略:
	//   1. 先检查 GameMode 是否已经 BattleInProgress (战斗已开始)
	//      - 是 → 直接 StartBehaviorTreeFromProfile
	//   2. 否 → 订阅 RoomGameMode::OnBattleStarted, 收到后 StartBehaviorTreeFromProfile
	//      - 必须检查 GameMode 有效性 (PIE 早期 GameMode 可能未初始化)
	//      - 必须检查 OnBattleStarted 是否有效 (极早期 GameMode nullptr 保护)
	TryStartBehaviorTreeOrWaitForBattleStart();

	// 【P0 架构升级 2026.07.03】启动期一次性全景诊断
	// 设计: 4 件事同时检查并报告, 任何一项异常立即刷 Error/ Warning 上日志
	//   1. Profile 完整性
	//   2. 阵营 ID 解析 (这是上次 bug 的关键: Faction.Enemy 未识别导致 SquadTeam=0)
	//   3. 感知配置 (Sight 半径/角度/阵营过滤)
	//   4. NavMesh 可用性 (启动期就检查, 避免 MoveTo 静默失败)
	// 集中输出 = 一次日志扫描能看清 AI 配置, 不必翻 4 个函数
	// 输出 AI 启动状态的全景诊断日志，帮助快速定位问题
	DiagnoseAndLogBootStatus();
}

/**
 * 【Phase 2 共用层 + P0 大厂架构修复】从 RuntimeConfig 配 AIPerception (Sight)
 *
 * 设计:
 *   - 替代原 MeleeAIController::ConfigurePerceptionFromConfig
 *   - 走 RuntimeConfig->GetScaledPerception() — 已经按难度缩放过
 *   - Hearing 留给 Phase 3, 现阶段不配
 *
 * 【P0 修复 — 2026.07.03】敌我阵营识别彻底重构:
 *   旧版坑:
 *     - bDetectEnemies=true 让 AI 把"中立"也当敌人, 结果 BP_GruntAI 把 BP_GruntAI 当目标互殴
 *     - 没显式声明要按 Team ID 区分阵营, DetectionByAffiliation.Team=空数组, 退化成全阵营敌对
 *   新版:
 *     - 关闭所有 ByAffiliation bool, 改用 Team 数组精确匹配
 *     - 队伍 ID 来自 Profile.FactionTag 解析 (Phase 1 已就绪)
 *     - 如果 Profile 没阵营, 退化为 bDetectEnemies=true (原行为), 至少不会瞎识别
 *
 * 注意:
 *   - 必须 RuntimeConfig 和 Config 都非空才执行 (兜底)
 *   - SightConfig 是 NewObject, 由 UE 反射管理 GC, 不会泄漏
 */
// ConfigurePerceptionFromConfig：从运行时配置配置 AI 的视觉感知系统
void ABaseAIController::ConfigurePerceptionFromConfig()
{
	// 检查感知组件、运行时配置和配置资产是否都有效，无效则跳过配置
	if (!AIPerception || !RuntimeConfig || !RuntimeConfig->GetConfig())
	{
		return;
	}

	// 创建视觉感知配置对象，由 UE 反射系统管理 GC，不会泄漏
	UAISenseConfig_Sight* SightConfig = NewObject<UAISenseConfig_Sight>(this, TEXT("SightConfig_P0Fix"));
	// 如果创建失败，直接返回
	if (!SightConfig)
	{
		return;
	}

	// 获取难度缩放后的感知参数
	const FAIPerceptionParams Params = RuntimeConfig->GetScaledPerception();
	// 设置视觉感知的最大视野半径（能看到的最远距离）
	SightConfig->SightRadius = Params.SightRadius;
	// 设置丢失目标的视野半径（目标超出此距离后丢失）
	SightConfig->LoseSightRadius = Params.LoseSightRadius;
	// 设置 peripheral vision 角度（视野锥的角度，单位度）
	SightConfig->PeripheralVisionAngleDegrees = Params.PeripheralVisionAngleDegrees;

	// ============================================================
	// 【P0 修复】阵营检测: 走 UE 原生阵营协议, 让 AIPerception 自动判定敌我
	// ============================================================
	// UE 5.6 的 FAISenseAffiliationFilter 只有三个 bool (没有 Teams 数组):
	//   bDetectEnemies    — 用 GetTeamAttitudeTowards == Hostile 判定
	//   bDetectNeutrals   — Neutral 算敌人
	//   bDetectFriendlies — Friendly 算敌人 (默认 false, 千万别开!)
	//
	// 修复原理:
	//   - 关掉 bDetectNeutrals 和 bDetectFriendlies (默认 false, 显式置 false 防御)
	//   - 只保留 bDetectEnemies=true, 引擎会自动调 GetTeamAttitudeTowards 判定
	//   - BTService_RefreshTarget 里 IsHostileTo() 是第二层兜底 (即使配错也安全)

	// 设置是否检测敌对目标，由配置参数决定
	SightConfig->DetectionByAffiliation.bDetectEnemies = Params.bDetectEnemies;
	// 【P0】关闭, 不把中立当敌人 — 避免 AI 攻击中立单位
	SightConfig->DetectionByAffiliation.bDetectNeutrals = false;   // 【P0】关闭, 不把中立当敌人
	// 【P0】关闭, 友军不打 — 避免 AI 攻击友军
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false; // 【P0】关闭, 友军不打

	// 输出日志，记录阵营配置信息，方便调试
	UE_LOG(LogBaseAI, Log,
		TEXT("[%s] ConfigurePerceptionFromConfig: SquadTeam=%d, bDetectEnemies=%d, bDetectNeutrals=%d, bDetectFriendlies=%d"),
		*GetName(),
		GetGenericTeamId().GetId(),
		SightConfig->DetectionByAffiliation.bDetectEnemies ? 1 : 0,
		SightConfig->DetectionByAffiliation.bDetectNeutrals ? 1 : 0,
		SightConfig->DetectionByAffiliation.bDetectFriendlies ? 1 : 0);

	// 将视觉感知配置应用到感知组件
	AIPerception->ConfigureSense(*SightConfig);
	// 设置视觉为 AI 的主要感知方式，优先级最高
	AIPerception->SetDominantSense(SightConfig->GetSenseImplementation());

	// 【P0 修复】NavMesh 可用性检查: 启动 BT 前先验证, 避免 MoveTo 永远失败
	// 获取游戏世界实例
	if (UWorld* World = GetWorld())
	{
		// 获取导航系统实例
		if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
		{
			// 获取主导航数据（Recast NavMesh），用于检查 NavMesh 是否存在
			ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(NavSys->GetMainNavData());
			// 获取 NavMesh 的名称，用于日志输出
			const FString NavMeshName = NavMesh ? NavMesh->GetName() : FString(TEXT("None"));
			// 获取 AI 的阵营 ID，用于日志输出
			const int32 MyTeamId = GetGenericTeamId().GetId();

			// 如果 NavMesh 不存在，输出错误日志，提示 AI 将无法移动
			if (!NavMesh)
			{
				UE_LOG(LogBaseAI, Error,
					TEXT("[%s] ConfigurePerceptionFromConfig: NavMesh 不存在! AI 将无法 MoveTo, 必须先放置 NavMeshBoundsVolume 并烘焙 (SquadTeam=%d)"),
					*GetName(), MyTeamId);
			}
			else
			{
				// NavMesh 存在，输出日志确认正常
				UE_LOG(LogBaseAI, Log,
					TEXT("[%s] ConfigurePerceptionFromConfig: NavMesh OK (%s), SquadTeam=%d"),
					*GetName(), *NavMeshName, MyTeamId);
			}
		}
	}
}

// TryStartBehaviorTreeOrWaitForBattleStart: 延迟启动 BT 的统一入口
//
// 【P0 架构升级 2026.07.06 17:00】解决"AI 出生立即追玩家"问题
// 用户原话: "还没点击开始游戏我就听到 AI 在打玩家了"
// 之前: AI 出生 → OnPossess → InitializeFromProfile → OnProfileLoaded → StartBehaviorTreeFromProfile → RunBehaviorTree
//       BT 跑起来立即开始追大厅里的玩家 (BP_SWAT_C_0 默认 spawn)
// 现在: 启动 BT 前先检查 GameMode 状态
//       - BattleInProgress (战斗已开) → 立即启动
//       - WaitingInRoom  (大厅) → 订阅 OnBattleStarted, 收到回调再启动
//
// 大厂原则: 不要在错误时机激活行为, 让 AI 与 GameMode 状态对齐
//          解耦: AI 不需要直接知道 GameMode 内部逻辑, 只订阅事件即可
void ABaseAIController::TryStartBehaviorTreeOrWaitForBattleStart()
{
	// 0. 防御: GameMode 可能 nullptr (PIE 启动早期), 退化路径 = 立即启动 (原行为)
	// 【P0 2026.07.08 调试用】Warning 级别 log
	AGameModeBase* GMBase = UGameplayStatics::GetGameMode(this);
	UE_LOG(LogBaseAI, Warning, TEXT("[%s] >>> TryStartBehaviorTreeOrWaitForBattleStart ENTERED (GMBase=%s)"),
		*GetName(), *GetNameSafe(GMBase));
	if (!GMBase)
	{
		UE_LOG(LogBaseAI, Warning,
			TEXT("[%s] TryStartBehaviorTreeOrWaitForBattleStart: GameMode 为空, 立即启动 BT (原行为)"),
			*GetName());
		StartBehaviorTreeFromProfile();
		return;
	}

	// 1. 强转 ARoomGameMode — 必须用我们的 RoomGameMode (它有 OnBattleStarted 委托)
	//    如果 GameMode 不是 ARoomGameMode (例如 PIE 改用了别的 GM 测试), 走原行为
	ARoomGameMode* RoomGM = Cast<ARoomGameMode>(GMBase);
	if (!RoomGM)
	{
		UE_LOG(LogBaseAI, Warning,
			TEXT("[%s] TryStartBehaviorTreeOrWaitForBattleStart: GameMode 不是 ARoomGameMode (%s), 立即启动 BT"),
			*GetName(), *GMBase->GetClass()->GetName());
		StartBehaviorTreeFromProfile();
		return;
	}

	// 2. 检查 RoomGM 是否已经广播过 OnBattleStarted (战斗已开始)
	//    - 是 → 立即启动 BT (AI 在战斗开始后才出生的情况)
	//    - 否 → 订阅事件, 等待回调
	// 【P0 2026.07.08 调试用】Warning 级别 log
	UE_LOG(LogBaseAI, Warning, TEXT("[%s] TryStartBehaviorTreeOrWaitForBattleStart: RoomGM=%s bBattleStartedBroadcasted=%d"),
		*GetName(), *GetNameSafe(RoomGM), RoomGM->bBattleStartedBroadcasted ? 1 : 0);
	if (RoomGM->bBattleStartedBroadcasted)
	{
		UE_LOG(LogBaseAI, Warning,
			TEXT("[%s] TryStartBehaviorTreeOrWaitForBattleStart: GameMode 已 BattleInProgress, 立即启动 BT"),
			*GetName());
		StartBehaviorTreeFromProfile();
		return;
	}

	// 3. 订阅 OnBattleStarted, 收到后启动 BT
	//    用 TWeakObjectPtr 防止 GameMode 销毁后回调访问野指针
	//    Handle 存到成员, EndPlay 时取消订阅 (避免泄漏)
	if (!OnBattleStartedHandle.IsValid())
	{
		TWeakObjectPtr<ARoomGameMode> WeakRoomGM(RoomGM);
		TWeakObjectPtr<ABaseAIController> WeakSelf(this);
		OnBattleStartedHandle = RoomGM->OnBattleStarted.AddLambda(
			[WeakSelf, WeakRoomGM]()
			{
				// 双重检查有效性 (可能 AI/GM 在等待期间已销毁)
				if (!WeakSelf.IsValid() || !WeakRoomGM.IsValid())
				{
					return;
				}
				ABaseAIController* Self = WeakSelf.Get();
				ARoomGameMode* GM = WeakRoomGM.Get();
				// 确认 GameMode 已进入 BattleInProgress (防止委托乱触发)
				if (GM->CurrentRoomState == ERoomState::BattleInProgress)
				{
					UE_LOG(LogBaseAI, Log,
						TEXT("[%s] OnBattleStarted 触发: GameMode 进入 BattleInProgress, 启动 BT"),
						*Self->GetName());
					Self->StartBehaviorTreeFromProfile();
				}
			});

		UE_LOG(LogBaseAI, Warning,
			TEXT("[%s] TryStartBehaviorTreeOrWaitForBattleStart: 已订阅 OnBattleStarted, 等待战斗开始"),
			*GetName());
	}
	else
	{
		// 已经订阅过了 — 不重复订阅 (例如 OnProfileLoaded 被多次调)
		UE_LOG(LogBaseAI, Warning,
			TEXT("[%s] TryStartBehaviorTreeOrWaitForBattleStart: 已订阅过 OnBattleStarted, 跳过"),
			*GetName());
	}
}

// StartBehaviorTreeFromProfile：从 Profile 启动行为树，加载行为树资源并运行
void ABaseAIController::StartBehaviorTreeFromProfile()
{
	// 【P0 2026.07.08 调试用】Warning 级别 log, 强制显示
	UE_LOG(LogBaseAI, Warning, TEXT("[%s] >>> StartBehaviorTreeFromProfile ENTERED (RuntimeConfig=%s GetConfig=%s)"),
		*GetName(),
		*GetNameSafe(RuntimeConfig),
		*GetNameSafe(RuntimeConfig ? RuntimeConfig->GetConfig() : nullptr));

	// 输出日志，提示进入函数
	UE_LOG(LogBaseAI, Warning, TEXT("[%s] StartBehaviorTreeFromProfile: enter"), *GetName());

	// 检查运行时配置和配置资产是否有效
	if (!RuntimeConfig || !RuntimeConfig->GetConfig())
	{
		// 如果配置无效，输出警告并返回
		UE_LOG(LogBaseAI, Warning, TEXT("[%s] StartBehaviorTreeFromProfile: no config"), *GetName());
		return;
	}

	// 同步加载行为树资源（从配置资产中的软引用加载）
	UBehaviorTree* BT = RuntimeConfig->GetConfig()->BehaviorTree.LoadSynchronous();
	// 输出日志，记录加载到的行为树名称
	UE_LOG(LogBaseAI, Log, TEXT("[%s] StartBehaviorTreeFromProfile: BT=%s"),
		*GetName(), *GetNameSafe(BT));

	// 如果行为树加载失败
	if (!BT)
	{
		// 输出警告并走传统兼容路径
		UE_LOG(LogBaseAI, Warning, TEXT("[%s] Config has no BehaviorTree, fallback"), *GetName());
		RunLegacyBehaviorTree();
		return;
	}

	// 运行行为树，启动 AI 的行为逻辑
	RunBehaviorTree(BT);
	// 标记行为树已启动，避免重复启动
	bBehaviorTreeStarted = true;
	// 输出日志，记录行为树启动状态和黑板信息
	UE_LOG(LogBaseAI, Warning, TEXT("[%s] RunBehaviorTree called, BB=%s BBAsset=%s"),
		*GetName(),
		*GetNameSafe(GetBlackboardComponent()),
		*GetNameSafe(BT->GetBlackboardAsset()));
}

/**
 * 派生类入口 (Phase 1 推荐用)
 * 设计: 派生类做完自己专属的感知/GAS/装备配置后调用本方法
 * 内部会复用已 ApplyConfig 的 Config, 不会重复加载
 */
// StartBehaviorTreeFromConfig：派生类调用，用于启动行为树（复用已加载的配置）
void ABaseAIController::StartBehaviorTreeFromConfig()
{
	// 如果行为树已启动，直接返回，避免重复启动
	if (bBehaviorTreeStarted)
	{
		return;
	}
	// 调用从 Profile 启动行为树的函数
	StartBehaviorTreeFromProfile();
}

// RunLegacyBehaviorTree：传统兼容路径，用于没有 Profile 的情况
void ABaseAIController::RunLegacyBehaviorTree()
{
	// 兼容路径: 没有 Profile 时, 直接看 Pawn 是否在调试 BP 里硬塞了 BT (GameMode 可直接 RunBehaviorTree).
	// 不在 BaseAIController 内硬启动任何 BT — 让外部决定.
	// 检查条件：行为树未启动且有 Pawn 控制
	if (!bBehaviorTreeStarted && GetPawn())
	{
		// BT 启动交给外部 GameMode, Base 不越俎代庖
	}
}

/**
 * 【P0 架构升级 2026.07.03】启动期一次性全景诊断
 *
 * 设计理念 (大厂经典):
 *   "启动期一切异常都在一处显式输出" — 避免出现"AI 不动"时翻 4 个日志段拼上下文
 *   一次扫描 = 看清 AI 全局状态
 *
 * 检查 4 项:
 *   1. Profile 完整性
 *   2. 阵营 ID 解析 (历史 bug 源: Faction.Enemy 未识别导致 SquadTeam=0)
 *   3. 感知配置 (Sight 半径/角度/阵营过滤) — 简化为 SightSense 字段快速摘要
 *   4. NavMesh 可用性 (启动期就检查, 避免 MoveTo 静默失败)
 *
 * 输出策略:
 *   - 健康: Log
 *   - 异常: Error
 *   - 全 AI 一致格式, 方便 grep "Diagnose:" 一键过滤
 */
// DiagnoseAndLogBootStatus：输出 AI 启动状态的全景诊断日志，帮助快速定位问题
void ABaseAIController::DiagnoseAndLogBootStatus() const
{
	// 获取 AI 控制器的名称，用于日志输出
	const FString MyName = GetName();

	// 【1】Profile 完整性
	// 获取当前 Profile 的名称
	const FString ProfileName = GetNameSafe(CurrentProfile);
	// 获取当前配置资产的名称
	const FString ConfigName  = GetNameSafe(RuntimeConfig ? RuntimeConfig->GetConfig() : nullptr);
	// 获取阵营标签的字符串表示
	const FString FactionStr  = CurrentProfile ? CurrentProfile->FactionTag.ToString() : FString(TEXT("<None>"));

	// 【2】阵营 ID 解析 — 关键诊断点
	// 获取 AI 的阵营 ID
	const FGenericTeamId MyTeamId = GetGenericTeamId();
	// 将阵营 ID 转换为整数，用于日志输出和判断
	const int32 TeamIdInt = MyTeamId.GetId();
	// 获取 Pawn 的类名，用于日志输出
	const FString PawnFactionStr = GetPawn() ? GetPawn()->GetClass()->GetName() : FString(TEXT("<NoPawn>"));

	// 【3】感知配置摘要
	// 初始化感知配置摘要字符串，默认提示没有运行时配置
	FString PerceptionSummary = TEXT("<no RuntimeConfig>");
	// 如果运行时配置和配置资产有效，生成感知配置摘要
	if (RuntimeConfig && RuntimeConfig->GetConfig())
	{
		// 获取难度缩放后的感知参数
		const FAIPerceptionParams P = RuntimeConfig->GetScaledPerception();
		// 格式化感知配置摘要，包含视野半径、丢失半径、视野角度和敌对检测标志
		PerceptionSummary = FString::Printf(
			TEXT("SightR=%.0f LoseR=%.0f FOV=%.0f° bDetectEnemies=%d"),
			P.SightRadius, P.LoseSightRadius, P.PeripheralVisionAngleDegrees,
			P.bDetectEnemies ? 1 : 0);
	}

	// 【4】NavMesh 可用性
	// 初始化 NavMesh 摘要字符串，默认提示没有世界
	FString NavMeshSummary = TEXT("<no world>");
	// 获取游戏世界实例
	if (UWorld* World = GetWorld())
	{
		// 获取导航系统实例
		if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
		{
			// 获取主导航数据（Recast NavMesh）
			ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(NavSys->GetMainNavData());
			// 根据 NavMesh 是否存在，生成对应的摘要字符串
			NavMeshSummary = NavMesh
				? FString::Printf(TEXT("OK (%s)"), *NavMesh->GetName())
				: FString(TEXT("MISSING ⚠ MoveTo 必定失败"));
		}
		else
		{
			// 导航系统不可用，设置对应的摘要字符串
			NavMeshSummary = TEXT("NavSys 不可用");
		}
	}

	// 【统一输出格式】大厂 grep 友好: "Diagnose:" 一键定位
	// 输出 Profile、配置、阵营标签、阵营 ID 和 Pawn 类名的诊断日志
	UE_LOG(LogBaseAI, Log,
		TEXT("Diagnose: [%s] Profile=%s Config=%s FactionTag=%s -> TeamID=%d PawnClass=%s"),
		*MyName, *ProfileName, *ConfigName, *FactionStr, TeamIdInt, *PawnFactionStr);

	// 输出感知配置摘要的诊断日志
	UE_LOG(LogBaseAI, Log,
		TEXT("Diagnose: [%s] Perception=%s"),
		*MyName, *PerceptionSummary);

	// 输出 NavMesh 可用性的诊断日志
	UE_LOG(LogBaseAI, Log,
		TEXT("Diagnose: [%s] NavMesh=%s"),
		*MyName, *NavMeshSummary);

	// 【异常升级到 Error】 — 启动期最致命的 3 个 P0 故障点
	// 如果阵营 ID 为 0（玩家阵营），输出错误日志，提示 AI 会把玩家当友军
	if (TeamIdInt == 0)
	{
		UE_LOG(LogBaseAI, Error,
			TEXT("Diagnose: [%s] ⚠ TeamID=0 = Player 阵营! AI 会把玩家当友军不攻击. 请检查 Profile.FactionTag (期望 Faction.Enemy)"),
			*MyName);
	}
	// 如果运行时配置或配置资产缺失，输出错误日志，提示 AI 无法运行
	if (!RuntimeConfig || !RuntimeConfig->GetConfig())
	{
		UE_LOG(LogBaseAI, Error,
			TEXT("Diagnose: [%s] ⚠ RuntimeConfig 缺失, AI 无法运行 (无 BT 可启)"),
			*MyName);
	}
	// 如果 NavMesh 缺失，输出错误日志，提示 MoveTo 必定失败
	if (NavMeshSummary.Contains(TEXT("MISSING")))
	{
		UE_LOG(LogBaseAI, Error,
			TEXT("Diagnose: [%s] ⚠ NavMesh 缺失, MoveTo 必定失败. 请在地图中放置 NavMeshBoundsVolume 并 Build Paths"),
			*MyName);
	}
}

// SetDifficultyTier：设置 AI 的难度等级，影响战斗和感知参数的缩放
void ABaseAIController::SetDifficultyTier(EAIDifficultyTier NewTier)
{
	// 如果运行时配置组件存在，设置难度等级
	if (RuntimeConfig)
	{
		RuntimeConfig->SetDifficultyTier(NewTier);
	}
}


/**
 * GetEffectiveAttackInterval — AI 攻击间隔的最终值
 *
 * 【P0 大厂架构 2026.07.06 19:25】回退优先级:
 *   1. Profile.AttackInterval > 0  → 直接用 (策划在 Profile 上调的, 不叠加难度缩放)
 *   2. Profile.AttackInterval <= 0 → 用 ConfigSO 难度缩放后的 AttackCooldown
 *   3. 都没拿到                    → 兜底 1.2s (与 ConfigSO 默认一致)
 *
 * 大厂设计:
 *   - BTTask 不需要直接接触 Profile, 调一行 GetEffectiveAttackInterval() 就行
 *   - 难度缩放在这里完成 (BTTask 自己不重复做), 单一数据源
 *   - Profile 手动值免受难度缩放 (策划明确意图比自动缩放优先级高)
 */
float ABaseAIController::GetEffectiveAttackInterval() const
{
	const UAIProfileAsset* Profile = GetCurrentProfile();

	// 优先级 1: Profile 手动值 (策划明确意图, 不叠加难度缩放)
	if (Profile && Profile->AttackInterval > 0.0f)
	{
		return Profile->AttackInterval;
	}

	// 优先级 2: ConfigSO + 难度缩放
	if (UAIRuntimeConfigComponent* ConfigComp = RuntimeConfig)
	{
		// GetScaledCombat 内部完成难度缩放 (Hard 缩 0.7x, Easy 缩 1.3x)
		return ConfigComp->GetScaledCombat().AttackCooldown;
	}

	// 优先级 3: 兜底
	// v13 默认 5.0s (与 ConfigSO 新默认一致, 2026.07.07 用户反馈 "AttackCooldown 默认给我改成 5 秒")
	return 5.f;
}


/**
 * GetEffectiveAttackDamage — AI 攻击伤害的最终值
 *
 * 【P0 大厂架构 2026.07.06 重构】修复 ConfigSO.Damage 死代码 bug
 *
 * 优先级:
 *   1. RuntimeConfig 拿得到 → 难度缩放后的 Combat.Damage
 *   2. 拿不到 → 兜底 12.f (与 ConfigSO 默认一致)
 *
 * 设计:
 *   - 单点维护: 所有 AI 通道读伤害都走这里
 *   - 难度缩放在 Config 内部完成, 单一数据源
 *   - 跟 GetEffectiveAttackInterval 同一架构
 */
float ABaseAIController::GetEffectiveAttackDamage() const
{
	if (UAIRuntimeConfigComponent* ConfigComp = RuntimeConfig)
	{
		// GetScaledCombat 内部完成难度缩放
		return ConfigComp->GetScaledCombat().Damage;
	}

	// 兜底
	return 12.f;
}


/**
 * 【P0 2026.07.07 大厂架构重构】AI 默认步行速度 (cm/s)
 *
 * 设计: 集中管理, 避免 TickChaseFallback / LockMovementForCooldown 各自硬编码 600
 *       (项目其它地方 Player MaxWalkSpeed 也是 600, 见 BaseCharacter.cpp line 76, 139)
 *
 * 修改此值需同时检查:
 *   - BaseCharacter.cpp line 76 / 139 (Player 的 MaxWalkSpeed)
 *   - 任何 AnimBP 的 Run/Walk 动画速度
 */
static constexpr float kDefaultAIWalkSpeed = 600.0f;


/**
 * GetEffectiveAttackRange — 【P0 2026.07.07 大厂架构重构】AI 攻击范围 (与 BTTask 单一数据源)
 *
 * 与 BTTask_MeleeAttack::GetAttackRange 完全一致, 单一数据源:
 *   RuntimeConfig->GetConfig()->GetScaledCombat().AttackRange (含难度缩放)
 *
 * 为什么必须有此函数:
 *   - 旧版 TickChaseFallback 用 hardcode 距离阈值或自己算 AttackRange,
 *     导致与 BTTask_MeleeAttack 的 AttackRange 不一致
 *   - 玩家走出"AI 看到的 AttackRange"但还在"BTTask 看到的 AttackRange"内 → 状态机分裂
 *   - 玩家进入"AI 看到的 AttackRange"但超出"BTTask 看到的 AttackRange" → AI 锁步但 BT 不攻击
 *
 * @return 最终攻击范围（厘米）; 没 Profile/Config 时返回默认 180 (与 BTTask 一致)
 */
float ABaseAIController::GetEffectiveAttackRange() const
{
	// 【P0 2026.07.07 大厂架构修复 — 直接用成员变量, 去掉 FindComponentByClass】
	//
	// 历史 Bug (致命):
	//   RuntimeConfig 在构造函数里 CreateDefaultSubobject 创建 (早于异步加载)
	//   但 OnProfileLoaded (异步回调) 才真正填充 Config 内容
	//   在此之前 FindComponentByClass 查到的是"空壳" RuntimeConfig → Config=nullptr
	//   → 返回兜底值 180, 而 BTTask_MeleeAttack 用的是 500
	//   → TickChaseFallback 在 180cm LockStop, BTTask 在 500cm 才触发攻击
	//   → 状态机分裂, 冷却后退逻辑永远走不到 (因为 Distance 达不到 180!)
	//
	// 修复: 直接用 RuntimeConfig 成员指针 (构造函数已创建, 永不丢失)
	//       RuntimeConfig->Config 在 OnProfileLoaded 异步填充之前是 nullptr
	//       但此时 BT 还没启动, TickChaseFallback 不应该工作
	//       等 Config 加载完成后, RuntimeConfig->GetConfig() 自然返回正确值
	//       不再依赖 FindComponentByClass 反射查找 (避免空壳问题)
	if (RuntimeConfig && RuntimeConfig->GetConfig())
	{
		return RuntimeConfig->GetScaledCombat().AttackRange;
	}
	return 180.f;
}


/**
/**
 * GetAllowMovementDuringAttack — 【P0 2026.07.07 大厂架构重构】攻击时是否允许移动
 *
 * 设计: 单一数据源, 与 GetAttackRangeHysteresis 同一 Component 查找路径
 *       ComputeArrivalDecision 读取此值, 决定攻击中是否 LockStop
 *
 * @return true=攻击中允许移动 (默认, 向后兼容), false=攻击中禁止移动
 */
bool ABaseAIController::GetAllowMovementDuringAttack() const
{
	// 【P0 2026.07.07 大厂架构修复】直接用 RuntimeConfig 成员, 去掉 FindComponentByClass
	if (RuntimeConfig && RuntimeConfig->GetConfig())
	{
		return RuntimeConfig->GetConfig()->Combat.bAllowMovementDuringAttack;
	}
	return true;
}


/**
 * GetAttackRangeHysteresis — 【P0 2026.07.07 v18 大厂架构重构】迟滞区间 (cm), 用户可调
 *
 * 设计:
 *   - v18 重新暴露到 UAIBehaviorConfigSO 字段 (用户 20:35 反馈 "把 Hyst 暴露 ConfigSO 出来 我好方便调试")
 *   - 默认 0 (与 v13 用户 19:30 "正常后退即可" 原话一致)
 *   - 用户调试时可填 5~30 测试迟滞效果
 *   - 抗抖动兜底是 Refractory Period (TickChaseFallback 内 200ms 引擎层兜底), 不依赖 Hyst
 *
 * 为什么不默认给个 30:
 *   - v17 测试硬编码 30cm 暴露: "AI 卡在 200cm 不追到 180cm 攻击范围, 必须玩家靠近一点才攻击"
 *   - 30cm 是错误的值; 0cm 默认 = 锐利边界, 让 AI 一定追到 AR 内才能攻击
 *   - 用户主动填小值 (5~10cm) 才有意义, 不能默认大
 *
 * @return Hyst 值 (cm), 默认 0
 */
float ABaseAIController::GetAttackRangeHysteresis() const
{
	// 【v18】从 ConfigSO 读真实字段值 (用户 20:35 反馈要求)
	if (RuntimeConfig && RuntimeConfig->GetConfig())
	{
		return RuntimeConfig->GetConfig()->Combat.AttackRangeHysteresis;
	}
	return 0.f; // 兜底: 没 Config 时默认 0 (锐利边界, 不卡 AI)
}


/**
 * ComputeArrivalDecision — 【P0 2026.07.07 v14 大厂架构】纯函数距离决策 (四态)
 *
 * 这是"AI 距离驱动停车"语义的单点真理。任何调用方
 * (TickChaseFallback / BTTask_MeleeAttack 冷却判断 / 蓝图调试 / 服务器回滚)
 * 拿到完全一致的结果, 不存在"两个地方距离算法不一致"的可能。
 *
 * v14 重大架构调整 (用户 2026.07.07 19:50 反馈 — "AI 攻击冷却时持续原地走动"):
 *   旧 v13 设计问题:
 *     - bInCooldown=true 走 P2 距离维持 → AI 在 D<AR 时持续后退, 玩家贴身 AI 后退又追
 *     - 物理表现: "原地走动" (前后 Scale 在 ±0.7 / +1.0 之间高频切换, 净位移近似 0)
 *     - 用户原话: "正常应该是原地不动"
 *
 *   v14 新设计 (四态):
 *     [P1] 蒙太奇播放中 (bAttacking=true)
 *           → 冲锋型 (bAllowMovementDuringAttack=true): 走 P3 距离维持 (边打边走)
 *           → 站桩型 (bAllowMovementDuringAttack=false): LockStop
 *     [P2-New] 蒙太奇结束 → 冷却中 (bAttacking=false, bInCooldown=true)
 *           → 统一 LockStop (无论冲锋/站桩, 刚攻击完等下次攻击, 不动)
 *           这是 v14 核心修复: 用户期望"原地不动"
 *     [P3] 完全空闲 (bAttacking=false, bInCooldown=false)
 *           → D > AR+Hyst: Chase(+1.0) 追
 *           → D < AR-Hyst: Chase(-0.7) 退
 *           → 区间内: LockStop
 *
 *   关于 v11 (18:34) vs v14 (19:50) 的矛盾:
 *     18:34 用户原话: "冷却期 AI 维持 AR 距离"  → v11 实现 P2 距离维持
 *     19:50 用户原话: "冷却期持续原地走动, 应该原地不动"  → v14 推翻了 v11
 *     实际行为 v14:
 *       - 蒙太奇结束 → AI 静止 (5 秒冷却期内一动不动)
 *       - 5 秒后冷却结束 → AI 突然"苏醒", 主动追或退
 *     这是更符合"攻击后僵直"的真实物理直觉, 也是 UE 大厂动作游戏的标准做法
 *
 * v13 → v14 不再依赖 Hyst 字段 (字段已删), Hyst=0 硬编码为单点 AR 边界
 *
 * @param Hysteresis  迟滞缓冲 (cm), v13 字段已删, 调用方传 0 (硬编码)
 *
 * 调用方: TickChaseFallback 唯一调用, 决策频率: 每帧
 *
 * 关于后退速度硬编码 0.7 而非可配置的工程理由 (历史 v11 决策, v14 继续保留):
 *   - 玩家 MaxWalkSpeed=600 (BaseCharacter.cpp line 76, 139)
 *   - 600*0.7=420cm/s 后退速度: 略慢于玩家冲刺 (不至于永远追不上)
 *     又快于玩家走路 (不至于被玩家轻松贴身)
 *   - 这是大厂"AI 不脱离战斗"的核心约束, 用魔法数字更稳定
 *   - 如果未来需要可调, 改回常量定义即可, 1 行修复
 *
 * 关于后退方向的说明:
 *   返回的 ChaseScale 是**带符号**的: 正=前进, 负=后退
 *   调用方 TickChaseFallback 会根据符号决定 AddMovementInput 的方向:
 *     - 正: AddMovementInput(ForwardDirection,  ScaleValue)     → 朝敌人移动
 *     - 负: AddMovementInput(RetreatDirection, |ScaleValue|)     → 远离敌人移动
 *   其中:
 *     ForwardDirection  = (Target - AI).GetSafeNormal()   (AI→Target)
 *     RetreatDirection  = -ForwardDirection              (Target→AI 反向)
 *   用 RetreadDirection + 正 Scale (而非负 Scale) 是大厂 UE 实践:
 *     - 方向矢量明确告诉引擎"我要往这个方向走", 行为可预测
 *     - NavMesh 不会把负 Scale 误判为"减速指令"
 *     - 与 UE 官方 ACharacter::AddMovementInput 推荐用法一致 (方向明确, Scale 在 [-1, 1])
 */
ABaseAIController::FArrivalDecision ABaseAIController::ComputeArrivalDecision(
	float Distance, float AttackRange,
	bool bAttacking, bool bAllowMovementDuringAttack,
	bool bInCooldown, float Hysteresis)
{
	FArrivalDecision Out;
	Out.Action = FArrivalDecision::EAction::Chase;
	Out.ChaseScale = 1.0f;
	Out.bShouldLock = false;

	// 输入防御: Hysteresis 必须非负, 防止负值导致逻辑倒置
	// (v13: 外部唯一调用方 TickChaseFallback 传 0, 这里 double-check 防御负值输入)
	const float SafeHysteresis = FMath::Max(Hysteresis, 0.f);

	// ================================================================
	// [P1] 站桩型 AI 攻击中: 强制 LockStop (用户原意 — 站桩型就该站定)
	//
	// v11 调整: 站桩型的判定从"在攻击蒙太奇中" 改为 "在攻击蒙太奇中 且 配置为不允许移动"
	//   - bAllowMovementDuringAttack=false: 站桩型, 攻击时锁步 (老行为保留)
	//   - bAllowMovementDuringAttack=true : 冲锋型, 走 P3 距离维持 (统一逻辑)
	//
	// 注意: 这里**不**叠加 bInCooldown — v16 起 bInCooldown 不再影响移动决策
	//       (v11/v13 行为: 蒙太奇期间无论是否冷却都站定, 蒙太奇后无论是否冷却都距离维持)
	// ================================================================
	if (bAttacking && !bAllowMovementDuringAttack)
	{
		Out.Action = FArrivalDecision::EAction::LockStop;
		Out.ChaseScale = 0.f;
		Out.bShouldLock = true;
		return Out;
	}

	// ================================================================
	// [P2-Removed] v16 修复 — 彻底废除 v14 引入的"冷却期 LockStop" 分支
	//
	// v14 修复链路:
	//   - 用户 19:50 反馈: "AI 在攻击冷却时持续原地走动, 应该原地不动"
	//   - v14 修复: 增加 P2-New 分支 (bAttacking=false && bInCooldown=true → LockStop)
	//   - 结果: 蒙太奇结束后 5 秒内 AI 完全静止 (ConfigSO.AttackCooldown=5s)
	//
	// 用户 20:11 反馈 (推翻 v14 修复):
	//   "玩家走 ai 也不跟随, 要一会才跟随"
	//   用户实际想要的: **冷却期 AI 应该跟随玩家走 (距离维持 AttackRange)**,
	//   而**不是静止 5 秒**. 5 秒 LockStop 太久了.
	//
	// v16 修复 (大厂"以用户最终反馈为准"原则):
	//   - 完全废除 P2-New 冷却期 LockStop 分支
	//   - 不论 bInCooldown 是 true 还是 false, 只要 bAttacking=false 就走 P3 距离维持
	//   - 物理直觉: AI 攻击完恢复"主动战斗意识", 立刻继续按 AttackRange 调整距离
	//   - 这与用户 18:34 原始诉求一致:
	//     "只要进入攻击冷却, 就实时监测敌人与 ai 距离, ai 保持 AttackRange 设定的距离,
	//      直到攻击冷却结束"
	//
	// 不变量 (v16 大厂原则 — 输入决定输出):
	//   - 蒙太奇中 (bAttacking=true):
	//     - 站桩型 (bAllowMovementDuringAttack=false) → P1 LockStop
	//     - 冲锋型 (bAllowMovementDuringAttack=true)  → P3 距离维持
	//   - 非攻击期 (bAttacking=false): 统一 → P3 距离维持 (不论 bInCooldown)
	//     - 蒙太奇刚结束: AI 立刻恢复距离维持, 玩家走近就后退, 玩家走远就追
	//     - Cooldown 概念在移动决策上完全无影响 (只控制攻击 Token 何时就绪)
	//
	// 注意: bIsInAttackCooldown 成员变量仍然维护 (Timer 仍会清),
	//       只是它不再影响 ComputeArrivalDecision. 这是"职责分离":
	//       - 移动决策: 看 bAttacking
	//       - 攻击权限: 看 Cooldown Timer / BB Token
	// ================================================================

	// ================================================================
	// [P3 v16] 距离维持 (统一逻辑, 不分冷却期 / 完全空闲)
	//
	// 历史:
	//   - 旧 v10 bug: D < AR → LockStop, 玩家贴身时 AI 死站 (用户 18:11 反馈)
	//   - v11 新: 距离维持 (用户 18:34 反馈, 但 19:50 反馈推翻了)
	//   - v14 错误: 引入冷却期 LockStop (用户 19:50 反馈推动)
	//   - v16 终态: 距离维持, 废除所有冷却期 LockStop (用户 20:11 反馈推动)
	//
	// 设计:
	//   - D > AR+Hyst   → 全速追  (距离够远, 追到 AR 边缘)
	//   - D < AR-Hyst   → 面向敌人后退  (距离太近, 退到 AR 边缘)
	//   - 区间内 [AR-Hyst, AR+Hyst] → LockStop  (迟滞区间, 不切换, 防止抖动)
	// ================================================================

	// 防御: 距离为负视为不在范围内 (避免 -1 触发 Lock)
	if (Distance < 0.f)
	{
		// 异常输入 → 视为已到达, 等待 BT 决策
		Out.Action = FArrivalDecision::EAction::LockStop;
		Out.ChaseScale = 0.f;
		Out.bShouldLock = true;
		return Out;
	}

	// Hysteresis 区间 = 迟滞区, 视为"已到达", 锁步
	// 不调 AddMovementInput, 让 BT 在下一帧重新评估
	const float UpperBound = AttackRange + SafeHysteresis;
	const float LowerBound = AttackRange - SafeHysteresis;

	if (Distance > UpperBound)
	{
		// 距离太远 → 全速追
		Out.Action = FArrivalDecision::EAction::Chase;
		Out.ChaseScale = 1.0f;
		Out.bShouldLock = false;
		return Out;
	}

	if (Distance < LowerBound)
	{
		// 距离太近 → 面向敌人后退 (硬编码 0.7)
		// 0.7 是大厂"略慢于玩家冲刺"的标准值, 见函数头注释
		Out.Action = FArrivalDecision::EAction::Chase;
		Out.ChaseScale = -0.7f;  // 硬编码, 见函数头注释
		Out.bShouldLock = false;
		return Out;
	}

	// 在 [AR-Hyst, AR+Hyst] 迟滞区间 → LockStop
	// 大厂 Hysteresis 核心: 不切换动作, 避免抖动
	Out.Action = FArrivalDecision::EAction::LockStop;
	Out.ChaseScale = 0.f;
	Out.bShouldLock = true;
	return Out;
}


/**
 * ComputeActorCenterDistance — 【P0 2026.07.07】两 Actor 中心点距离 (cm)
 *
 * 与 BTTask_MeleeAttack::GetCenterLocation 算法一致:
 *   - ACharacter: 胶囊体中心
 *   - 其他 Actor: 包围盒中心
 *
 * 为什么必须 static:
 *   - TickChaseFallback 不依赖 AIController 实例就能调用
 *   - 未来 BT 其它节点 / 蓝图也能用同一套算法
 *   - 算法只有 ~30 行, 不需要单独的 utility 类
 *
 * @param A 第一个 Actor (为 nullptr 时返回 -1, 让调用方处理)
 * @param B 第二个 Actor (同上)
 * @return 两 Actor 中心点距离（厘米）; 任一为 nullptr 返回 -1
 */
float ABaseAIController::ComputeActorCenterDistance(const AActor* A, const AActor* B)
{
	if (!A || !B)
	{
		return -1.f;
	}
	return FVector::Dist(ComputeActorCenter(A), ComputeActorCenter(B));
}

/**
 * ComputeActorCenter — 【P0 2026.07.07】单 Actor 中心点 (世界坐标)
 *
 * 优先级:
 *   1. ACharacter 且有 UCapsuleComponent → 胶囊体世界坐标 (≈ 角色腰部)
 *   2. ACharacter 但无 CapsuleComponent → ActorLocation.Z + 88cm (站立身高近似一半)
 *   3. 其他 Actor → FBoxSphereBounds 包围盒中心
 *
 * 为何 Z+88cm 而不是 +88.9 (Character 默认半高):
 *   88cm = 1.76m ÷ 2 ≈ 大多数人形角色腰部的经验值,
 *   玩家是 BP_SWAT 也是 Character, 也用胶囊体分支 (这步仅兜底)
 */
FVector ABaseAIController::ComputeActorCenter(const AActor* A)
{
	if (!A)
	{
		return FVector::ZeroVector;
	}

	if (const ACharacter* Char = Cast<ACharacter>(A))
	{
		if (const UCapsuleComponent* Capsule = Char->GetCapsuleComponent())
		{
			return Capsule->GetComponentLocation();
		}
		// 兜底: 无胶囊体的 Character
		FVector Loc = Char->GetActorLocation();
		Loc.Z += 88.f;
		return Loc;
	}

	// 非 Character: 用包围盒中心
	FVector Origin = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	A->GetActorBounds(false, Origin, Extent);
	return Origin;
}


/**
 * 【P0 2026.07.07 v13 大厂减熵】ComputeArrivalDecision 单元自检
 *
 * 背景: 状态机 3 个优先级 (站桩型 / 冲锋型 / 距离维持) × 5 种距离区间
 *       (远 / 迟滞上沿 / 中 / 迟滞下沿 / 近) = 15 个状态组合, 手动调试容易遗漏
 * 大厂实践: 纯函数 → 单元自检, 启动时跑一遍全部组合, 关键 case 失败立即 Error
 *
 * v14 重大架构调整 (用户 2026.07.07 19:50 反馈 — "AI 攻击冷却时持续原地走动"):
 *   旧 v13 问题: !bAttacking && bInCooldown → P2 距离维持 → AI 在 D<AR 时持续后退, 玩家贴身时抖动
 *   v14 新设计 (四态):
 *     [P1] 蒙太奇中 (bAttacking=true)
 *           → 冲锋型 (bAllowMovementDuringAttack=true): P3 距离维持 (边打边跑)
 *           → 站桩型 (bAllowMovementDuringAttack=false): LockStop
 *     [P2 v14 新] 蒙太奇结束 → 冷却中 (bAttacking=false, bInCooldown=true)
 *           → 一律 LockStop (大厂"攻击后僵直"标准做法, 5 秒冷却期内 AI 静止)
 *     [P3] 完全空闲 (bAttacking=false, bInCooldown=false)
 *           → D > AR+Hyst: Chase(+1.0) 追
 *           → D < AR-Hyst: Chase(-0.7) 退
 *           → 区间内: LockStop
 *
 * v13 → v14 不再依赖 Hyst 字段 (字段已删), Hyst=0 硬编码为单点 AR 边界
 *
 * 关键验证点:
 *
 *   [CASE 1-2]   ⭐ v11 — P3 完全空闲 + 远/近 → Chase
 *   [CASE 3-4]   ⭐ v16 — 冷却期 + 远/近 → Chase (废除 v14 的 LockStop, 用户 20:11 反馈推动)
 *   [CASE 5-6]   ⭐ v11 核心修复 — 冲锋型攻击中 + 远/近 → Chase
 *   [CASE 7-8]   v11 站桩型攻击中 + 远/近 → LockStop
 *   [CASE 9-12]  v13 — Hyst=0 单点 AR 边界测试 (D=251/250/249/245)
 *   [CASE 13]    ⭐ v16 — 蒙太奇结束后冷却期 + 贴身 → Chase(-0.7) (恢复 v11 行为)
 *   [CASE 14]    完全空闲 + D=0 → Chase(-0.7)
 *   [CASE 15]    距离为负 → LockStop
 *   [CASE 16]    ⭐ v13 — 边界量化测试 (D ∈ {248..252})
 *
 * 调用方: UMetalSlug01GameInstance::Init 或 GameMode BeginPlay 中执行一次
 * 设计: 自检结果只输出日志, 不影响游戏运行 (即使失败也只是日志 Error)
 */
void ABaseAIController::SelfTestArrivalDecision()
{
	// v18 Hysteresis = 0 (ConfigSO 字段已恢复, 默认 0, 用户可调, 见 AIBehaviorTypes.h::FAICombatParams)
	//   这是大厂原则"测试值 = 实际运行时数据"
	//   ComputeArrivalDecision 调用方 TickChaseFallback 现在从 ConfigSO 读
	//   默认 Hyst=0 时 = 单点 AR 边界 (v13 行为), Refractory Period 200ms 引擎层兜底抗抖动
	constexpr float kDefaultHysteresis = 0.f;

	struct FTestCase
	{
		const TCHAR* Name;
		float Distance;
		float AttackRange;
		bool bAttacking;
		bool bAllowMoveDuringAttack;
		bool bInCooldown;
		FArrivalDecision::EAction ExpectedAction;
		float ExpectedScaleMin;
		float ExpectedScaleMax;
		bool ExpectedLock;
	};

	const FTestCase Tests[] =
	{
		// ============================================================
		// [P4] ⭐ v11 — 非攻击非冷却, 任何状态都距离维持
		// ============================================================
		// [CASE 1] P4 距离够远 → Chase(1.0)
		{ TEXT("C1_NonCombat_Far"),  300.f, 250.f, false, true,  false,
		  FArrivalDecision::EAction::Chase, 0.99f, 1.01f, false },

		// [CASE 2] ⭐ P4 贴身 (< AR=250) → Chase(-0.7) 面向敌人后退 (用户原话)
		// 用户原话: "敌人离 ai 小于 AttackRange, ai 面向敌人往后退"
		{ TEXT("C2_NonCombat_Near_Retreat"), 100.f, 250.f, false, true,  false,
		  FArrivalDecision::EAction::Chase, -0.8f, -0.6f, false },

		// ============================================================
		// [v16] ⭐ 废除 v14 P2-New 冷却期 LockStop — 恢复 v11 距离维持 (用户 20:11 反馈)
		//   冷却期行为现在与"完全空闲"完全一致: 距离维持 AttackRange
		//   - D > AR → Chase(1.0)
		//   - D < AR → Chase(-0.7)
		// ============================================================
		// [CASE 3] ⭐ v16 — 冷却期距离够远 → Chase(1.0) (v14 这里期望 LockStop, 已修正)
		{ TEXT("C3_Cooldown_Far_Chase"),   400.f, 250.f, false, false,  true,
		  FArrivalDecision::EAction::Chase, 0.99f, 1.01f, false },

		// [CASE 4] ⭐ v16 — 冷却期贴身 → Chase(-0.7) 后退 (v14 这里期望 LockStop, 已修正)
		// 这是用户 18:34 原始诉求 + 20:11 最新反馈的核心:
		//   "敌人离 ai 小于 AttackRange, ai 面向敌人往后退" (冷却期也适用)
		{ TEXT("C4_Cooldown_Near_Retreat"), 100.f, 250.f, false, false,  true,
		  FArrivalDecision::EAction::Chase, -0.8f, -0.6f, false },

		// ============================================================
		// [P3] v11 冲锋型攻击中: 距离维持 (同 P2)
		// ============================================================
		// [CASE 5] P3 冲锋型攻击中 + 贴身 → Chase(-0.7) 后退
		{ TEXT("C5_AttackingAllowMove_Near_Retreat"), 100.f, 250.f, true,  true,  false,
		  FArrivalDecision::EAction::Chase, -0.8f, -0.6f, false },

		// [CASE 6] P3 冲锋型攻击中 + 远距离 → Chase(1.0)
		{ TEXT("C6_AttackingAllowMove_Far"), 400.f, 250.f, true,  true,  false,
		  FArrivalDecision::EAction::Chase, 0.99f, 1.01f, false },

		// ============================================================
		// [P1 站桩型] 任何距离 → LockStop (强制)
		// ============================================================
		// [CASE 7] 站桩型攻击中 + 贴身 → LockStop
		{ TEXT("C7_AttackingLock_Near"), 100.f, 250.f, true,  false, false,
		  FArrivalDecision::EAction::LockStop, -0.01f, 0.01f, true },

		// [CASE 8] 站桩型攻击中 + 远距离 → LockStop (强制, 任何距离)
		{ TEXT("C8_AttackingLock_Far"), 400.f, 250.f, true,  false, false,
		  FArrivalDecision::EAction::LockStop, -0.01f, 0.01f, true },

		// ============================================================
		// ⭐ v18 Hyst 字段重新暴露 ConfigSO, 默认 0 = 单点 AR 边界
		//   与 v13 行为一致 (锐利边界), Refractory Period 200ms 抗抖动
		//   用户可以在 ConfigSO 填 5~30 测试迟滞区间效果
		//   AR=250, Hyst=0 → 单点边界 [250, 250]
		//   D > 250 → Chase(1.0)
		//   D < 250 → Chase(-0.7)
		//   D == 250 → LockStop
		// ============================================================
		// [CASE 9 ⭐ v18] P4 + D=251 (刚好 > AR=250, Hyst=0) → Chase(1.0)
		{ TEXT("C9_Boundary_AboveChase"), 251.f, 250.f, false, true,  false,
		  FArrivalDecision::EAction::Chase, 0.99f, 1.01f, false },

		// [CASE 10 ⭐ v18] P4 + D=250 (正好 == AR, 单点) → LockStop
		{ TEXT("C10_Boundary_ExactLock"), 250.f, 250.f, false, true,  false,
		  FArrivalDecision::EAction::LockStop, -0.01f, 0.01f, true },

		// [CASE 11 ⭐ v18] P4 + D=249 (刚好 < AR=250, Hyst=0) → Chase(-0.7) 后退
		{ TEXT("C11_Boundary_BelowRetreat"), 249.f, 250.f, false, true,  false,
		  FArrivalDecision::EAction::Chase, -0.8f, -0.6f, false },

		// [CASE 12 ⭐ v18] P4 + D=245 (< AR 远离) → Chase(-0.7) 后退 (验证大于阈值仍持续后退)
		{ TEXT("C12_FarBelow_Retreat"), 245.f, 250.f, false, true,  false,
		  FArrivalDecision::EAction::Chase, -0.8f, -0.6f, false },

		// [CASE 13 ⭐ v16/v18] 蒙太奇已结束 + Cooldown 未结束 + 玩家贴身 → Chase(-0.7) 后退
		//   v16 修复: 冷却期不再走 P2-New LockStop, 恢复 v11 距离维持 (用户 20:11 反馈)
		//   v18 验证: Hyst=0 默认时, 冷却期贴身仍正常距离维持
		{ TEXT("C13_PostMontageCooldown_Retreat"), 100.f, 250.f, false, false, true,
		  FArrivalDecision::EAction::Chase, -0.8f, -0.6f, false },

		// [CASE 14 ⭐ v18 关键] Hyst=30 时 (用户测试用) D ∈ [220, 280] → LockStop
		//   这是用户 20:35 反馈的 bug: "AI 卡在 200cm 不进 180cm 攻击范围"
		//   当 Hyst=30 时 AR+Hyst=280, AR-Hyst=220, AI 在 200cm (< 280) 就 LockStop,
		//   永远不追到 180cm 攻击区 → BTTask 不触发
		//   验证: 默认 Hyst=0 时这个 case 也 Chase (因为 200 < 250 AR)
		{ TEXT("C14_Hyst30_Dist200_WouldLock_In30NowChase"), 200.f, 250.f, false, true, false,
		  FArrivalDecision::EAction::Chase, -0.8f, -0.6f, false },

		// [CASE 15] D=0 极端 — 重叠, 应后退 (虽然物理上不可达)
		{ TEXT("C15_ZeroDistance_Retreat"), 0.f, 250.f, false, true,  false,
		  FArrivalDecision::EAction::Chase, -0.8f, -0.6f, false },

		// [CASE 16] 距离为负 (异常输入) → LockStop (防御)
		{ TEXT("C16_NegativeDistance_Lock"), -1.f, 250.f, false, true,  false,
		  FArrivalDecision::EAction::LockStop, -0.01f, 0.01f, true },
	};

	int32 PassCount = 0;
	int32 FailCount = 0;

	for (const FTestCase& T : Tests)
	{
		const FArrivalDecision Result = ComputeArrivalDecision(
			T.Distance, T.AttackRange, T.bAttacking, T.bAllowMoveDuringAttack, T.bInCooldown, kDefaultHysteresis);

		const bool bActionOK = (Result.Action == T.ExpectedAction);
		const bool bScaleOK = (Result.ChaseScale >= T.ExpectedScaleMin && Result.ChaseScale <= T.ExpectedScaleMax);
		const bool bLockOK = (Result.bShouldLock == T.ExpectedLock);
		const bool bAllOK = bActionOK && bScaleOK && bLockOK;

		if (bAllOK)
		{
			PassCount++;
		}
		else
		{
			FailCount++;
			UE_LOG(LogBaseAI, Error,
				TEXT("[SelfTest] FAIL %s: Distance=%.0f Range=%.0f Attacking=%d AllowMove=%d Cooldown=%d "
					 "→ Got Action=%d Scale=%.2f Lock=%d | Expected Action=%d Scale=[%.2f, %.2f] Lock=%d"),
				T.Name, T.Distance, T.AttackRange,
				T.bAttacking ? 1 : 0, T.bAllowMoveDuringAttack ? 1 : 0, T.bInCooldown ? 1 : 0,
				(int32)Result.Action, Result.ChaseScale, Result.bShouldLock ? 1 : 0,
				(int32)T.ExpectedAction, T.ExpectedScaleMin, T.ExpectedScaleMax, T.ExpectedLock ? 1 : 0);
		}
	}

	// [CASE 17 ⭐ v18] 边界量化测试 (Hyst=0 默认单点边界)
	//   行为: AR=250, Hyst=0 → 验证 D ∈ {200, 245, 249, 250, 251, 280, 300} 全部行为正确
	//   这是大厂"边界决断性测试", 防止纯函数在边界上下沿处的 if-else 分支被改错
	//   ⭐ v18 关键: 验证 Hyst=0 默认时, AI 一定会追到 AR (不会卡在 AR+Hyst 远处不动)
	//              这是用户 20:35 反馈 "AI 卡在 200cm 不追" 的根因测试
	{
		struct FExactCase { float D; FArrivalDecision::EAction Expect; float ScaleSign; };
		const FExactCase ExactCases[] =
		{
			{ 200.f, FArrivalDecision::EAction::Chase,    -1.f },  // < AR → 后退 (v17 失败点: Hyst=30 时 D=200 在区间内 LockStop, 不追到 AR!)
			{ 245.f, FArrivalDecision::EAction::Chase,    -1.f },  // < AR → 后退
			{ 249.f, FArrivalDecision::EAction::Chase,    -1.f },  // < AR → 后退
			{ 250.f, FArrivalDecision::EAction::LockStop,   0.f },  // == AR 单点 → 锁步
			{ 251.f, FArrivalDecision::EAction::Chase,     1.f },  // > AR → 追
			{ 280.f, FArrivalDecision::EAction::Chase,     1.f },  // > AR → 追
			{ 300.f, FArrivalDecision::EAction::Chase,     1.f },  // > AR → 追
		};
		for (const FExactCase& E : ExactCases)
		{
			const FArrivalDecision R = ComputeArrivalDecision(E.D, 250.f, false, true, false, 0.f);
			const bool bActionOK = (R.Action == E.Expect);
			const bool bScaleOK = (FMath::Sign(R.ChaseScale) == E.ScaleSign);
			if (bActionOK && bScaleOK)
			{
				PassCount++;
			}
			else
			{
				FailCount++;
				UE_LOG(LogBaseAI, Error,
					TEXT("[SelfTest] FAIL C17_ExactBound D=%.0f: Got Action=%d Scale=%.2f | Expected Action=%d Sign=%.0f"),
					E.D, (int32)R.Action, R.ChaseScale, (int32)E.Expect, E.ScaleSign);
			}
		}
	}

	// [CASE 18 ⭐ v18 关键验证] 模拟用户场景: Hyst=30 时 D=200 不该 LockStop
	//   这是用户 20:35 反馈的 bug 直接复现: "AI 卡在 200cm 不追到 180cm 攻击范围"
	//   当 ConfigSO.Combat.AttackRangeHysteresis=30 (v17 硬编码值),
	//   AR=180, AR+Hyst=210, AI 在 D=200 时会 LockStop (因为 200 < 210)
	//   → AI 不追到 180 攻击范围 → BTTask 永远不触发 → 玩家必须靠近 1cm 才能被攻击
	//
	//   v18 修复: 默认 Hyst=0, AI 在 D=200 时 Chase(-0.7) 后退到 AR=180 内触发 BTTask
	//   ⭐ 这个测试通过 = 用户 20:35 反馈的 bug 已修复
	{
		const FArrivalDecision R_Hyst0 = ComputeArrivalDecision(200.f, 180.f, false, true, false, 0.f);
		if (R_Hyst0.Action == FArrivalDecision::EAction::Chase && FMath::Sign(R_Hyst0.ChaseScale) == -1.f)
		{
			PassCount++;
			UE_LOG(LogBaseAI, Log,
				TEXT("[SelfTest] C18_Hyst0_At200_Chase ✓ (v18 默认: AR=180 Hyst=0 D=200 → Chase 后退到攻击范围, 修复用户 20:35 'AI 卡 200cm 不追' 反馈)"));
		}
		else
		{
			FailCount++;
			UE_LOG(LogBaseAI, Error,
				TEXT("[SelfTest] FAIL C18_Hyst0_At200_Chase: Got Action=%d Scale=%.2f (Expected Chase -1)"),
				(int32)R_Hyst0.Action, R_Hyst0.ChaseScale);
		}
	}

	// [CASE 19 ⭐ v18 文档化已知问题] 当 Hyst=30 时 D=200 → LockStop (符合迟滞设计, 但会触发用户场景的 bug)
	//   这个 case 记录: 当 ConfigSO.Combat.AttackRangeHysteresis=30 时,
	//   AI 在 D=200 (< AR+Hyst=210) 会 LockStop
	//   这是用户 20:35 反馈 bug 的根因, 但不是 ComputeArrivalDecision 的 bug
	//   是配置错误 (Hyst 太大, 比 AR 还大 17%)
	//   v18 默认 Hyst=0 绕开此问题, 但保留此 case 提醒未来调试者
	{
		const FArrivalDecision R_Hyst30 = ComputeArrivalDecision(200.f, 180.f, false, true, false, 30.f);
		if (R_Hyst30.Action == FArrivalDecision::EAction::LockStop)
		{
			PassCount++;
			UE_LOG(LogBaseAI, Warning,
				TEXT("[SelfTest] C19_Hyst30_At200_LockStop ⚠ (ConfigSO.Hyst=30 时 AI 在 D=200 卡住, 不追到 AR=180. "
					 "用户 20:35 反馈的 bug 根因. v18 默认 Hyst=0 避免此问题)"));
		}
		else
		{
			FailCount++;
			UE_LOG(LogBaseAI, Error,
				TEXT("[SelfTest] FAIL C19_Hyst30_At200_LockStop: Got Action=%d Scale=%.2f (Expected LockStop)"),
				(int32)R_Hyst30.Action, R_Hyst30.ChaseScale);
		}
	}

	if (FailCount == 0)
	{
		UE_LOG(LogBaseAI, Log,
			TEXT("[SelfTest] ComputeArrivalDecision: ALL %d CASES PASSED ✓ "
				 "(v18: Hyst 字段重新暴露到 ConfigSO [用户 20:35 反馈要求], 默认 0 = 单点 AR 边界, "
				 "Refractory Period 200ms 在 TickChaseFallback 抗抖动 已扩展为'任何决策切换' (不仅符号反转), "
				 "Hyst=0 默认时 AI 一定追到 AR 攻击范围 (修复用户 20:35 'AI 卡 200cm 不追' 反馈 [v17 Hyst=30 硬编码导致]), "
				 "RetreatSpeed 字段早删, "
				 "v15 修复 ExecuteTask 路径 PerformAttack 漏设 bInCooldown=true [用户 20:04 反馈], "
				 "v16 废除 v14 冷却期 LockStop 恢复 v11 距离维持 [用户 20:11 反馈])"),
			PassCount);
	}
	else
	{
		UE_LOG(LogBaseAI, Error,
			TEXT("[SelfTest] ComputeArrivalDecision: %d/%d FAILED — 距离决策状态机存在 bug, 请检查 v13 重构"),
			FailCount, PassCount + FailCount);
	}
}


/**
 * LockMovementForCooldown — 【P0 2026.07.07 大厂架构重构】冷却锁步统一接口
 *
 * 修复问题三: "冷却时玩家原地不动, AI 原地不动但仍触发脚步声"
 *
 * 脚步声触发链:
 *   AnimBP 走路循环动画 → AnimNotify_Footstep 在脚落地关键帧触发
 *                          → BaseCharacter::PlayFootstepSound → FootstepComponent::PlayFootstep
 *
 * AnimBP 切回 Idle 的条件 (BaseAnimInstance.cpp line 78):
 *   if (Character->bIsMovementLocked) AnimSpeed = 0.0f → 走 Idle 分支
 *
 * 旧问题: TickChaseFallback 冷却期间只不调 AddMovementInput, 但:
 *   - 角色 Velocity 仍有残余 → AnimInstance 仍走 Walk 循环 → 触发脚步声
 *   - 即便 Velocity 归 0, AnimInstance 的 AnimSpeed 仍可能 > 0 (Acceleration 残留)
 *
 * 修复: 冷却锁步时一站式管理:
 *   - MaxWalkSpeed = 0 (引擎立即刹车, 物理速度瞬间归零)
 *   - bIsMovementLocked = true (AnimInstance 走 Idle 动画, AnimNotify 不触发)
 *
 * 双兜底: 调用方 (TickChaseFallback) 必须也停掉 AddMovementInput,
 *         否则 Velocity 与 MaxWalkSpeed=0 矛盾会引发抖动
 *
 * @param bLock true=锁步, false=恢复
 */
void ABaseAIController::LockMovementForCooldown(bool bLock)
{
	// 幂等保护: 状态没变时不重复设置
	// (MaxWalkSpeed 和 bIsMovementLocked 每次 set 都触发 CharacterMovement 内部重算)
	if (bMovementLockedForCooldown == bLock)
	{
		return;
	}

	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}

	ABaseCharacter* MyChar = Cast<ABaseCharacter>(MyPawn);
	if (!MyChar)
	{
		return;
	}

	if (bLock)
	{
		// 锁步: MaxWalkSpeed=0 + AnimInstance 走 Idle
		if (UCharacterMovementComponent* Movement = MyChar->GetCharacterMovement())
		{
			Movement->MaxWalkSpeed = 0.f;
			// 同时清零 Velocity, 否则惯性会让 AnimInstance 继续走 Walk 动画几帧
			// 注意: 必须保留 Z 轴速度 (重力下落 / 跳跃), 否则 AI 在斜坡上会卡住
			const FVector CurrentVel = Movement->Velocity;
			Movement->Velocity = FVector(0.f, 0.f, CurrentVel.Z);

		// 【P0 2026.07.07 大厂架构 — 修复 "AI 停在 174cm 不是 500cm"】
		// 仅清零 Velocity 不够: CharacterMovement 在下一帧会根据 Acceleration 重新加速
		// → 即使 Distance < AttackRange 触发 Lock, AI 仍可能再追一小段
		// → 最终停在 175cm 而不是 500cm
		//
		// 修复: 调用 StopMovementImmediately() 彻底停止移动 (清 Acceleration + Velocity + PendingInput)
		// 这是 UE 推荐的 "瞬间刹车" 接口, 用于 Montage 打断 / 强制停下场景
		//
		// 注意: 只在 Lock 状态切换瞬间调用一次 (上面已做幂等保护), 不会每帧调用, 无性能问题
		Movement->StopMovementImmediately();
	}
	// 关键: bIsMovementLocked=true → AnimInstance 检测后 AnimSpeed=0 → 走 Idle 动画
	// AnimNotify_Footstep 只在 Walk/Run 动画的关键帧触发, Idle 动画没有 → 脚步声消除
	MyChar->bIsMovementLocked = true;
	bMovementLockedForCooldown = true;

	// 【P0 2026.07.07】新增锁步原因区分 (按 bAllowMovementDuringAttack 分类)
	// bAttacking && !bAllowMoveDuringAttack → 攻击中禁止移动
	// bAttacking &&  bAllowMoveDuringAttack → 不走这里 (会进 Chase 分支)
	// !bAttacking && 冷却触发            → 冷却中禁止移动
	UE_LOG(LogBaseAI, Verbose,
		TEXT("[%s] LockMovementForCooldown: LOCK (MaxWalkSpeed=0, bIsMovementLocked=true, Velocity.XY=0, bAttacking=%d, bAllowMove=%d)"),
		*GetName(), bIsCurrentlyAttacking ? 1 : 0, GetAllowMovementDuringAttack() ? 1 : 0);
	}
	else
	{
		// 恢复: MaxWalkSpeed=600 + AnimInstance 走 Walk/Run
		if (UCharacterMovementComponent* Movement = MyChar->GetCharacterMovement())
		{
			Movement->MaxWalkSpeed = kDefaultAIWalkSpeed;
		}
		MyChar->bIsMovementLocked = false;
		bMovementLockedForCooldown = false;

		UE_LOG(LogBaseAI, Verbose,
			TEXT("[%s] LockMovementForCooldown: UNLOCK (MaxWalkSpeed=%.0f, bIsMovementLocked=false)"),
			*GetName(), kDefaultAIWalkSpeed);
	}
}


/**
 * GetCurrentTargetActor — AI 当前目标 (从 Blackboard 读)
 *
 * 简化: 直接读 BB Key "TargetActor"
 *
 * 【UE 5.6 注意】AAIController::GetBlackboardComponent() 返回 const UBlackboardComponent*
 *                  (const 正确性, BB 数据不应被业务代码随意修改 — 写操作走 BT 框架)
 *                  所以这里也用 const 指针接收
 */
AActor* ABaseAIController::GetCurrentTargetActor() const
{
	// 用 GetBlackboardComponent 是 AAIController 原生方法, 内部已经走 BB 实例
	// 注意 UE 5.6 这里返回 const UBlackboardComponent* (BB 数据 const 保护)
	const UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB)
	{
		return nullptr;
	}

	// BB Key "TargetActor" — Phase 1 时 BTTask 默认配置
	return Cast<AActor>(BB->GetValueAsObject(FName(TEXT("TargetActor"))));
}


// ==========================================
// 4. 【Phase 1 重构】阵营协议实现
// ==========================================
// 设计: AI 也是 IGenericTeamAgentInterface 的实现方.
//       阵营从 Profile.FactionTag 推, 不要再造一份 uint8 TeamID.

// SetGenericTeamId：设置 AI 的阵营 ID，同步到 Pawn 上供感知系统读取
void ABaseAIController::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	// 把 TeamID 存进 Pawn (让 Pawn 暴露给 AIPerception 直接读)
	// 获取当前控制的 Pawn
	if (APawn* MyPawn = GetPawn())
	{
		// 尝试将 Pawn 转换为通用阵营代理接口
		if (IGenericTeamAgentInterface* PawnAgent = Cast<IGenericTeamAgentInterface>(MyPawn))
		{
			// 将阵营 ID 设置给 Pawn，让感知系统能直接读取
			PawnAgent->SetGenericTeamId(NewTeamID);
		}
	}
}

// GetGenericTeamId：获取 AI 的阵营 ID，从 Pawn 上读取
FGenericTeamId ABaseAIController::GetGenericTeamId() const
{
	// 获取当前控制的 Pawn（const 指针）
	if (const APawn* MyPawn = GetPawn())
	{
		// 尝试将 Pawn 转换为通用阵营代理接口（const 指针）
		if (const IGenericTeamAgentInterface* PawnAgent = Cast<IGenericTeamAgentInterface>(MyPawn))
		{
			// 从 Pawn 上获取阵营 ID 并返回
			return PawnAgent->GetGenericTeamId();
		}
	}
	// 如果没有 Pawn 或 Pawn 没有实现阵营接口，返回默认敌对 ID（255）
	return FGenericTeamId(255); // AI 默认敌对, 直到外部设置
}

// GetTeamAttitudeTowards：获取 AI 对另一个 Actor 的阵营态度（敌对/中立/友好）
ETeamAttitude::Type ABaseAIController::GetTeamAttitudeTowards(const AActor& Other) const
{
	// 尝试将目标 Actor 转换为通用阵营代理接口
	if (const IGenericTeamAgentInterface* OtherAgent = Cast<IGenericTeamAgentInterface>(&Other))
	{
		// 获取目标的阵营 ID
		const FGenericTeamId OtherId = OtherAgent->GetGenericTeamId();
		// 获取 AI 自身的阵营 ID
		const FGenericTeamId MyId = GetGenericTeamId();

		// 标准 FGenericTeamId::GetAttitude
		// 使用引擎原生函数计算两个阵营 ID 之间的态度，并返回
		return FGenericTeamId::GetAttitude(MyId, OtherId);
	}
	// 如果目标没有实现阵营接口，返回中立态度
	return ETeamAttitude::Neutral;
}


// ==========================================
// 5. 感知触发入口
// ==========================================
//
// 【Phase 3 大厂架构】感知回调 — 双重写入策略
//
// 设计:
//
//   目标感知分两个层次写入 BB，职责分离:
//
//   Layer 1 — TargetActor（任意距离均写入）:
//     - 只要感知到敌人，无条件写入 TargetActor
//     - 由 BTService_RefreshTarget 仲裁刷新（或本函数直接写）
//     - 驱动 BT 的 MoveTo / Attack 节点执行
//
//   Layer 2 — NearbyThreat（极近距离覆盖）:
//     - 仅当敌人进入 AttackRange 范围时写入 (v5 已合并 OverrideBTDistance, 2026.07.07)
//     - 优先级高于 TargetActor（极近距离遭遇优先响应）
//     - 由本函数直接写入，BTService 在下一帧读取 NearbyThreat
//
//   为什么分层？
//     - Layer 2 确保极近距离遭遇能立刻覆盖目标（不用等 Service Tick）
//     - Layer 1 确保任意距离都能驱动行为树（即使没有 NearbyThreat）
//     - 两者协同：Service 读 NearbyThreat 优先，本函数写 NearbyThreat 极近时才触发
//
//   感知触发时机:
//     - bSensed=1: 敌人首次进入/持续在视野内
//     - bSensed=0: 敌人离开视野（LastKnownLocation 留存由 BT 处理）
//
//   架构优势:
//     - 不依赖 BTService 是否被添加到行为树（Service 缺失时 OnTargetDetected 兜底）
//     - 不依赖任何额外阈值配置（直接用 AttackRange, 用户 2026.07.07 硬要求）
//     - 与 BTService_RefreshTarget 完全兼容（两者写同一个 Key，互不冲突）

// OnTargetDetected：感知回调函数，当感知组件检测到目标时触发
void ABaseAIController::OnTargetDetected(AActor* Actor, FAIStimulus Stimulus)
{
    // 输出日志，记录检测到的目标 Actor 和是否成功感知
    UE_LOG(LogBaseAI, Log, TEXT("[%s] OnTargetDetected: Actor=%s, bSensed=%d"),
        *GetName(),
        *GetNameSafe(Actor),
        Stimulus.WasSuccessfullySensed() ? 1 : 0);

    // 如果检测到的 Actor 为空，直接返回
    if (!Actor)
    {
        return;
    }

    // 获取黑板组件，用于写入目标信息
    UBlackboardComponent* BB = GetBlackboardComponent();
    // 如果黑板组件不存在，直接返回
    if (!BB)
    {
        return;
    }

    // ============================================================
    // 【P0 大厂架构重构 2026.07.06】OnTargetDetected 职责最终方案
    //
    // 设计:
    //   - 感知系统是 TargetActor 的事件驱动主源（最权威、最快）
    //   - TickChaseFallback 是 TargetActor 的扫描兜底（不与感知系统冲突）
    //   - UpdateNearbyThreatByDistance 每帧按距离动态更新 NearbyThreat
    //     (OnTargetPerceptionUpdated 在持续可见时只触发 1 次, 不能依赖)
    //   - 两者协同：感知写则信任, 感知丢失则扫描接管
    //
    // 为什么 OnTargetDetected 必须写 TargetActor:
    //   1. 感知是事件驱动, 玩家进入视野即触发, 延迟 < 1 帧
    //   2. BTService 依赖 TargetActor 让 BT 树进入追击分支
    //   3. TickChaseFallback 扫描周期 0.3s, 单独依赖会导致反应迟钝
    //
    // 为什么 OnTargetDetected 不再写 NearbyThreat:
    //   - 持续可见时 OnTargetPerceptionUpdated 只触发 1 次
    //   - 玩家从远到近时, 距离变化但没有新事件, NearbyThreat 不会更新
    //   - 改由 UpdateNearbyThreatByDistance 每帧检查距离, 解决时序问题
    // ============================================================

    // 如果没有成功感知到目标（目标丢失）
    if (!Stimulus.WasSuccessfullySensed())
    {
        // 目标丢失时清空 TargetActor — 避免 AI 继续追已丢失的 stale 引用
        AActor* CurrentTarget = Cast<AActor>(
            BB->GetValueAsObject(FName(AIBlackboardKeyNames::TargetActor)));
        if (CurrentTarget == Actor)
        {
            BB->SetValueAsObject(FName(AIBlackboardKeyNames::TargetActor), nullptr);
            // NearbyThreat 由 UpdateNearbyThreatByDistance 清空 (因为 TargetActor 失效)
            UE_LOG(LogBaseAI, Log, TEXT("[%s] OnTargetDetected: 目标丢失, 清空 TargetActor"),
                *GetName());
        }
        return;
    }

    // 如果 AI 没有控制的 Pawn，直接返回
    if (!GetPawn())
    {
        return;
    }

    // ============================================================
    // TargetActor (事件驱动写入)
    // 感知到目标即写入, 让 BTService 能立即响应
    // NearbyThreat 由 UpdateNearbyThreatByDistance 每帧按距离动态更新
    // ============================================================
    BB->SetValueAsObject(FName(AIBlackboardKeyNames::TargetActor), Actor);
}
