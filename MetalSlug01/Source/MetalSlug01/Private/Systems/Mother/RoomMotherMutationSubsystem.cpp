// Copyright (c) 2026. All Rights Reserved.
// URoomMotherMutationSubsystem — 生化模式母体变异业务权威调度

#include "Systems/Mother/RoomMotherMutationSubsystem.h"

#include "Systems/RoomGameMode.h"
#include "Systems/RoomGameState.h"
#include "Systems/Lifecycle/RoomLifecycleSubsystem.h"
#include "Systems/Spawn/RoomSpawnSubsystem.h"
#include "Characters/BaseCharacter.h"
#include "Components/HealthComponent.h" // v93.4 — MutateCharacterToMother Step 3.6 血量验证 (GetMax)
#include "Data/Config/PlayerConfigAsset.h" // v133.4.1 — Step 3.6 验 MotherMaxHealth 字段 (GetPlayerConfigAsset 返回值)

#include "Data/Faction/FactionTags.h" // v93.3 — MutateCharacterToMother Step 3.5 阵营验证 (IsOffense)

#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "AIController.h" // v108 — FilterCandidatesByPolicy 判 AI vs Player
#include "Math/UnrealMathUtility.h"

// ==========================================
// 【大厂原则 — 标准子系统钩子】
// ==========================================

bool URoomMotherMutationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 大厂原则: 子系统只在 Game World 实例化 (PIE / Standalone)
	// 避免在编辑器世界 / Preview 中创建浪费内存
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}


URoomMotherMutationSubsystem* URoomMotherMutationSubsystem::Get(const UObject* WorldContextObject)
{
	// 大厂原则 — UE 标准 Subsystem 访问模式 (镜像 v31.5 其他 Room Subsystem 风格)
	if (!WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		return World->GetSubsystem<URoomMotherMutationSubsystem>();
	}
	return nullptr;
}


// ==========================================
// 【依赖注入】
// ==========================================

void URoomMotherMutationSubsystem::InitializeSubsystem(ARoomGameMode* InGameMode,
                                                      URoomLifecycleSubsystem* InLifecycle,
                                                      URoomSpawnSubsystem* InSpawn)
{
	// 大厂原则 — 显式优于隐式: 参数校验失败立即 Log Error, 字段留空但不抛错
	// 目的: 调用方注入失败时, 调用链显式中断, 不让"半截初始化"扩散
	if (!InGameMode)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] InitializeSubsystem: GameMode 为空, 业务调度不可用. 【修复】检查 ARoomGameMode::InjectSubsystemConfigs 调用顺序."));
	}
	if (!InLifecycle)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] InitializeSubsystem: LifecycleSubsystem 为空, 倒计时回调无法接收."));
	}
	if (!InSpawn)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] InitializeSubsystem: SpawnSubsystem 为空, 母体 Pawn 重建不可用."));
	}

	GameMode = InGameMode;
	Lifecycle = InLifecycle;
	Spawn = InSpawn;

	UE_LOG(LogTemp, Log,
		TEXT("[MotherMutation] InitializeSubsystem: 依赖注入完成 (GameMode=%s Lifecycle=%s Spawn=%s)"),
		*GetNameSafe(InGameMode), *GetNameSafe(InLifecycle), *GetNameSafe(InSpawn));
}


void URoomMotherMutationSubsystem::SetRoundTransitionGuard(bool bInTransition)
{
	bIsInRoundTransition = bInTransition;
	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] 【v230】SetRoundTransitionGuard: bIsInRoundTransition=%s."),
		bInTransition ? TEXT("true") : TEXT("false"));
}


// ==========================================
// 【服务器权威入口 — 倒计时到期回调】
// ==========================================

void URoomMotherMutationSubsystem::HandleCountdownExpired()
{
	// 大厂原则 — 服务器权威: 本函数仅在服务器运行, 但内部仍 defensive 检查
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] HandleCountdownExpired: World 为空, 拒绝触发变异."));
		return;
	}

	// 【v230 大厂架构新增】小局转换守卫 — 防止时序竞态
	//   根因: OnRoundTransitionTimerExpired → StartNextZombieRound → RestartZombieRoundPlayers 正在执行时
	//   旧的 MotherMutationTimerHandle 回调可能触发 → 把人类又变回母体
	//   修复: bIsInRoundTransition=true 时拒绝触发变异
	if (bIsInRoundTransition)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] 【v230-严重】HandleCountdownExpired: bIsInRoundTransition=true, 拒绝触发变异. "
			     "【根因排查】母体变异倒计时在小局转换期间被触发 — RestartZombieRoundPlayers 应在末尾关闭 bIsInRoundTransition. "
			     "请检查 URoomSpawnSubsystem::RestartZombieRoundPlayers 末尾是否调用 SetRoundTransitionGuard(false)."));
		return;
	}

	// ==========================================
	// 【v230 大厂架构新增】诊断快照 — 追踪所有母体状态变化
	// ==========================================
	//
	// 目的: 当用户反馈"小局开始时有母体出现"时, 从日志里完整还原时序
	//   - 此时 bIsInRoundTransition 已关闭 (守卫正常), 但仍有母体出现
	//   - 说明根因不在时序竞态, 而是 RestartZombieRoundPlayers 本身的逻辑问题
	//
	// 日志格式:
	//   [MotherMutation] 【v230 诊断】HandleCountdownExpired 入口快照:
	//   - 当前时间: XX.XXs
	//   - 母体账本 MotherCharacters: N 个
	//   - 现场人类候选 (TActorIterator): M 个
	//   - bMotherMutationFired_Local: true/false
	//   - GameState.MotherMutationHasFired: true/false
	//   - 母体 Pawn 详情: [Pawn名, bIsMother, FactionTag]
	//
	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] 【v230 诊断】HandleCountdownExpired 入口快照:"));

	// 【v230 诊断】使用局部 RoomGS 指针 (作用域仅限此诊断块)
	if (ARoomGameState* DiagRoomGS = World ? World->GetGameState<ARoomGameState>() : nullptr)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[MotherMutation]   - 当前局时间: %.2fs, MotherMutationHasFired=%s, Remaining=%ds"),
			DiagRoomGS->GetServerWorldTimeSeconds(),
			DiagRoomGS->MotherMutationHasFired ? TEXT("true") : TEXT("false"),
			DiagRoomGS->GetMotherMutationRemainingSeconds());
	}

	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation]   - bMotherMutationFired_Local=%s, bIsInRoundTransition=%s"),
		bMotherMutationFired_Local ? TEXT("true") : TEXT("false"),
		bIsInRoundTransition ? TEXT("true") : TEXT("false"));

	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation]   - 母体账本 MotherCharacters.Num()=%d"), MotherCharacters.Num());

	// 遍历母体账本, 打印每个母体 Pawn 的详细信息
	for (int32 i = 0; i < MotherCharacters.Num(); ++i)
	{
		if (ABaseCharacter* Mother = MotherCharacters[i].Get())
		{
			UE_LOG(LogTemp, Display,
				TEXT("[MotherMutation]   - 母体账本[%d]: Pawn='%s' bIsMother=%s FactionTag='%s' Dead=%s"),
				i,
				*Mother->GetName(),
				Mother->bIsMother ? TEXT("true") : TEXT("false"),
				*Mother->FactionTag.GetTagName().ToString(),
				Mother->IsDead() ? TEXT("true") : TEXT("false"));
		}
		else
		{
			UE_LOG(LogTemp, Display,
				TEXT("[MotherMutation]   - 母体账本[%d]: 已销毁 (WeakPtr 过期)"), i);
		}
	}

	// 打印现场人类候选数量 (用 TActorIterator, 与 GetEligibleHumanTargets 同步)
	int32 LiveHumanCount = 0;
	int32 LiveMotherCount = 0;
	for (TActorIterator<ABaseCharacter> It(World); It; ++It)
	{
		ABaseCharacter* Char = *It;
		if (!Char || Char->IsPendingKillPending()) continue;
		if (Char->IsDead()) continue;
		if (Char->bIsMother || Char->GetClass()->GetName().Contains(TEXT("MuTi")))
		{
			++LiveMotherCount;
		}
		else
		{
			++LiveHumanCount;
		}
	}
	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation]   - 现场状态 (TActorIterator): 活人类=%d, 活母体=%d"),
		LiveHumanCount, LiveMotherCount);

	// 【v117.4 大厂架构修复】RAII 守卫 — 不论后续 return 路径如何, 都必须:
	//   1. 清空 GameState.MotherMutationStartTime/Duration (避免 UI 闸门挡空投倒计时)
	//   2. 启动 Lifecycle->StartAirdropCountdown() (业务规则不变: 母体变异倒计时结束 = 启动空投)
	// 根因:
	//   - 旧版 (v117.3) 把这两行写在函数末尾
	//   - 但 FilterCandidatesByPolicy 失败 / 候选空等 return 路径不会走到末尾
	//   - 实际日志 (Session 2026.08.03): 候选 1 个玩家 + Policy=1(AIOnly) → FilteredCandidates=0 → return
	//   - 母体变异字段不清空, UI 闸门永远挡空投倒计时, Text_AirdropCountdown 永远不显示
	// 大厂原则 — RAII 收口:
	//   - 这是 "母体变异倒计时结束" 业务的不可变事实 (invariant)
	//   - 必须无论变异成功与否都执行
	//   - 即使变异失败 (Filter 候选空), 游戏还要继续, 空投倒计时必须启动
	// 大厂原则 — 顺序约束:
	//   - 必须在 Lifecycle->StartAirdropCountdown() 之前 ResetMotherMutationCountdown
	//   - 原因: Lifecycle 内部校验 "MotherMutationRemainingSeconds > 0 拒绝启动"
	//   - 必须先 Reset 让字段归零, 才能通过校验
	ARoomGameState* GuardGameState = World->GetGameState<ARoomGameState>();
	if (GuardGameState)
	{
		GuardGameState->ResetMotherMutationCountdown();
	}
	if (Lifecycle)
	{
		Lifecycle->StartAirdropCountdown();
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MotherMutation] HandleCountdownExpired: Lifecycle 未注入, 无法启动空投倒计时. "
			     "【修复】检查 ARoomGameMode::InjectSubsystemConfigs."));
	}

	// 防御层 1: 单进程防重入
	if (bMotherMutationFired_Local)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] HandleCountdownExpired: 本局已触发母体变异, 拒绝重复触发. "
			     "【防御层 1】本地守卫生效 — 这是 UE 引擎某处重复调 Lifecycle 的回调."));
		return;
	}

	ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] HandleCountdownExpired: GameState 为空, 拒绝触发变异."));
		return;
	}

	// 防御层 2: 跨进程防重入 (GameState Replicated 字段)
	if (RoomGS->MotherMutationHasFired)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] HandleCountdownExpired: GameState.MotherMutationHasFired 已为 true, 拒绝重复触发. "
			     "【防御层 2】分布式守卫生效 — 这是 Lifecycle Subsystem 重复启动倒计时 SetTimer 残留."));
		return;
	}

	// 模式校验 — 大厂原则: 母体变异是生化模式独有, 刀战模式绝对不允许触发
	// 防御层 3: 即使 LifecycleSubsystem 配错 / 重复触发, 也能被这里挡住
	if (RoomGS->CurrentMatchMode != ERoomMatchMode::Zombie)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] HandleCountdownExpired: 当前模式=%d 不是 Zombie, 拒绝触发变异. "
			     "【防御层 3】刀战模式不应走到这里 — 检查 Lifecycle 启动倒计时条件."),
			(int32)RoomGS->CurrentMatchMode);
		return;
	}

	// 1. 收集候选
	TArray<ABaseCharacter*> Candidates = GetEligibleHumanTargets();

	// 大厂原则 — 零兜底: 找不到人 → 报错 + 退出 (不允许"静默跳过")
	if (Candidates.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] HandleCountdownExpired: 候选清单为空, 拒绝触发变异. "
			     "【根因排查】1) 玩家 Pawn 没生成 2) AI 全死 3) 全部已是母体 4) SpawnSubsystem 未初始化."));
		return;
	}

	// 【v108 大厂架构新增】按策略过滤候选 (AIOnly / PlayerOnly / Random)
	// 真理源: Lifecycle.CachedMotherSelectionPolicy ← GM.MotherSelectionPolicy (InitGame 一次性注入)
	EMotherSelectionPolicy Policy = EMotherSelectionPolicy::Random;
	if (Lifecycle)
	{
		Policy = Lifecycle->GetCachedMotherSelectionPolicy();
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MotherMutation] HandleCountdownExpired: Lifecycle 未注入, 默认走 Random 策略. "
			     "【修复】检查 ARoomGameMode::InjectSubsystemConfigs."));
	}
	TArray<ABaseCharacter*> FilteredCandidates = FilterCandidatesByPolicy(Candidates, Policy);

	// 大厂原则 — 零兜底: 策略过滤后候选空 → 报错 + 退出 (用户决策: 强制策划扩玩家/AI 数量)
	if (FilteredCandidates.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] HandleCountdownExpired: 策略过滤后候选空 (Policy=%d, 原候选=%d). "
			     "【根因】1) AIOnly 模式但全是玩家; "
			     "2) PlayerOnly 模式但全是 AI; "
			     "3) 候选 Pawn Controller 全部为空 (BP 配错)."),
			static_cast<int32>(Policy), Candidates.Num());
		return;
	}

	// 【v108 大厂架构新增】获取母体变异数量 (GM 注入, 默认 1)
	int32 MotherCount = 1;
	if (Lifecycle)
	{
		MotherCount = Lifecycle->GetCachedMotherMutationCount();
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MotherMutation] HandleCountdownExpired: Lifecycle 未注入, 默认 MotherCount=1."));
	}

	// 大厂原则 — 显式优于隐式: Clamp 校验 (Lifecycle 已 ClampMin=1, 但防御性再校验)
	MotherCount = FMath::Max(1, MotherCount);

	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] HandleCountdownExpired: 候选 %d → 策略过滤后 %d → 即将变异 %d 个母体 (Policy=%d)"),
		Candidates.Num(), FilteredCandidates.Num(), MotherCount, static_cast<int32>(Policy));

	// 3. 标记 (防御层 1 + 2 写入)
	bMotherMutationFired_Local = true;
	RoomGS->MarkMotherMutationFired();

	// 【v108 大厂架构重构】循环选 N 个目标, 每个选完从清单移除 (避免重复选同一人)
	int32 ActualMutated = 0;
	for (int32 i = 0; i < MotherCount; ++i)
	{
		// 实时检查候选清单 (循环中可能被前面变异导致死亡而失效)
		if (FilteredCandidates.Num() == 0)
		{
			// 大厂原则 — 零兜底 (用户决策): 候选不足 → Log Error + 中断循环 (强制策划扩玩家/AI)
			UE_LOG(LogTemp, Error,
				TEXT("[MotherMutation] HandleCountdownExpired: 候选不足 (期望=%d, 已变异=%d, 剩余=%d). "
				     "【根因】场景中活人不够, 部分母体未生成. "
				     "【修复】增加玩家/AI 数量, 或降低 GM.MotherMutationCount."),
				MotherCount, ActualMutated, FilteredCandidates.Num());
			break;
		}

		// 2. 选母体
		ABaseCharacter* Selected = SelectRandomTarget(FilteredCandidates);
		if (!Selected)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[MotherMutation] HandleCountdownExpired: 选母体失败 (i=%d/%d), 拒绝继续. "
				     "【根因】候选清单中有 nullptr 或角色刚好被销毁."),
				i + 1, MotherCount);
			break;
		}

		UE_LOG(LogTemp, Display,
			TEXT("[MotherMutation] HandleCountdownExpired: 第 %d/%d 个母体 选中 '%s' (%s)"),
			i + 1, MotherCount,
			*Selected->GetName(),
			*GetNameSafe(Selected->GetController()));

		// 4. 触发变异
		const bool bSuccess = MutateCharacterToMother(Selected);
		if (!bSuccess)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[MotherMutation] HandleCountdownExpired: MutateCharacterToMother 失败 (i=%d/%d, Target='%s'). "
				     "继续选下一个, 但本局已标记 MotherMutationHasFired, 不再重复触发."),
				i + 1, MotherCount, *Selected->GetName());
			// 大厂原则: 即使失败也继续 (失败原因通常与本次循环无关, 不阻塞)
		}
		else
		{
			++ActualMutated;
		}

		// 大厂原则 — 显式优于隐式: 选完即从清单移除 (避免重复选同一人)
		FilteredCandidates.Remove(Selected);
	}

	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] HandleCountdownExpired: 完成 (期望=%d, 实际变异成功=%d)"),
		MotherCount, ActualMutated);

	// 【v117.4 大厂架构修复】ResetMotherMutationCountdown + Lifecycle->StartAirdropCountdown
	//   已在函数入口通过 RAII 守卫集中处理 (任何 return 路径都会执行)
	//   - 这是 "母体变异倒计时结束" 业务的不可变事实, 不允许业务失败时跳过
	//   - 镜像 HandleZombieRoundEnd 行为: 母体变异结束 → 启动下一轮业务 (空投倒计时)
}


// ==========================================
// 【业务核心 — 收集候选清单】
// ==========================================

TArray<ABaseCharacter*> URoomMotherMutationSubsystem::GetEligibleHumanTargets()
{
	TArray<ABaseCharacter*> Candidates;

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] GetEligibleHumanTargets: World 为空, 返回空清单."));
		return Candidates;
	}

	// 大厂原则 — 单一真理源遍历 (v93.1 重构):
	//   - 玩家: GS->PlayerArray (UE 引擎自动 add)
	//   - AI 走 URoomSpawnSubsystem::GetAllBattleCharacters() (业务层唯一入口)
	//   - 不再直接 GetAllActorsOfClass (避免散查 + 业务层账本不统一)
	//
	// 为什么 AI 不用账本:
	//   - SpawnSubsystem.PendingAIQueue 大厅阶段已消费, 不持有运行时 AI 账本
	//   - GetAllBattleCharacters 是业务层唯一账本入口 (镜像 v28 大厅入队策略)
	//   - 选母体是每局 N 次 (N=玩家数+AI数), 走 GetAllBattleCharacters 完全可接受

	if (!Spawn)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] GetEligibleHumanTargets: SpawnSubsystem 未注入, 返回空清单. "
			     "【修复】检查 URoomMotherMutationSubsystem::InitializeSubsystem 是否被 ARoomGameMode::InjectSubsystemConfigs 调用."));
		return Candidates;
	}

	// 路径 A: 玩家 (走 PlayerState 真理源, 不通过 SpawnSubsystem — 玩家账本走 PS)
	for (TActorIterator<APlayerState> It(World); It; ++It)
	{
		APlayerState* PS = *It;
		if (!PS) continue;

		APlayerController* PC = Cast<APlayerController>(PS->GetOwner());
		if (!PC) continue;

		ABaseCharacter* Char = Cast<ABaseCharacter>(PC->GetPawn());
		if (!Char) continue;

		// 大厂原则 — 零兜底: 候选人必须满足以下所有条件
		if (Char->IsDead()) continue;        // 死的不要
		if (Char->bIsMother) continue;        // 已是母体的不要
		// (bIsHuman 检查省 — bIsMother=false 且 alive 即隐含 human)

		Candidates.Add(Char);
	}

	// 路径 B: AI + 玩家 Pawn (走 SpawnSubsystem 业务账本)
	// 注意: GetAllBattleCharacters 返回玩家+AI 全部, 玩家部分在路径 A 已加, 这里 unique
	TArray<ABaseCharacter*> AllBattleChars = Spawn->GetAllBattleCharacters();
	for (ABaseCharacter* Char : AllBattleChars)
	{
		if (!Char) continue;

		// 跳过玩家 Pawn (已在路径 A 加过了)
		if (Char->IsPlayerControlled()) continue;

		if (Char->IsDead()) continue;
		if (Char->bIsMother) continue;

		Candidates.Add(Char);
	}

	UE_LOG(LogTemp, Verbose,
		TEXT("[MotherMutation] GetEligibleHumanTargets: 找到 %d 个候选人类角色 (玩家+AI)"),
		Candidates.Num());

	return Candidates;
}


// ==========================================
// 【业务核心 — 随机选母体】
// ==========================================

ABaseCharacter* URoomMotherMutationSubsystem::SelectRandomTarget(const TArray<ABaseCharacter*>& Candidates)
{
	// 大厂原则 — 零兜底: 候选空 → 立即报错 + nullptr (不允许"默认选第 1 个")
	if (Candidates.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] SelectRandomTarget: 候选清单为空, 拒绝选母体."));
		return nullptr;
	}

	// 大厂原则 — 真随机: 用 FMath::RandRange (0, Num-1) 而不是 FMath::Rand() % Num (后者分布不均)
	const int32 Index = FMath::RandRange(0, Candidates.Num() - 1);

	ABaseCharacter* Selected = Candidates[Index];
	if (!Selected)
	{
		// 边缘情况: 选出的角色刚好在这中间被销毁 / 设了 IsDead (理论上不应该发生, 但大厂原则 — 显式优于假设)
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] SelectRandomTarget: 选中的角色已无效 (Index=%d, Candidates.Num=%d). "
			     "【根因】候选清单在筛选后到选中前, 角色被销毁. 走递归重选."),
			Index, Candidates.Num());
		return nullptr;
	}

	return Selected;
}


// ==========================================
// 【v108 大厂架构新增 — 策略过滤候选】
// ==========================================

TArray<ABaseCharacter*> URoomMotherMutationSubsystem::FilterCandidatesByPolicy(
	const TArray<ABaseCharacter*>& Candidates,
	EMotherSelectionPolicy Policy)
{
	TArray<ABaseCharacter*> Filtered;

	switch (Policy)
	{
	case EMotherSelectionPolicy::Random:
	{
		// Random: 不过滤, 全部候选都可被选中
		Filtered = Candidates;
		break;
	}

	case EMotherSelectionPolicy::AIOnly:
	{
		// AIOnly: 只保留 AI Pawn (Controller 是 AAIController 派生)
		Filtered.Reserve(Candidates.Num());
		for (ABaseCharacter* Char : Candidates)
		{
			if (!IsValid(Char))
			{
				continue;
			}
			AController* Ctrl = Char->GetController();
			if (!Ctrl)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[MotherMutation] FilterCandidatesByPolicy(AIOnly): '%s' Controller 为空, 跳过. "
					     "【根因】BP 配错 / Pawn 未成功 Possess."),
					*Char->GetName());
				continue;
			}
			if (Ctrl->IsA<AAIController>())
			{
				Filtered.Add(Char);
			}
		}
		break;
	}

	case EMotherSelectionPolicy::PlayerOnly:
	{
		// PlayerOnly: 只保留玩家 Pawn (Controller 是 APlayerController 派生)
		Filtered.Reserve(Candidates.Num());
		for (ABaseCharacter* Char : Candidates)
		{
			if (!IsValid(Char))
			{
				continue;
			}
			AController* Ctrl = Char->GetController();
			if (!Ctrl)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[MotherMutation] FilterCandidatesByPolicy(PlayerOnly): '%s' Controller 为空, 跳过. "
					     "【根因】BP 配错 / Pawn 未成功 Possess."),
					*Char->GetName());
				continue;
			}
			if (Ctrl->IsA<APlayerController>())
			{
				Filtered.Add(Char);
			}
		}
		break;
	}

	default:
	{
		// 防御性: 枚举值非法 (理论上不应发生, 但 UE UENUM 加新值时不写 case 会落到这里)
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] FilterCandidatesByPolicy: 未知策略=%d, 拒绝过滤. "
			     "【修复】检查 EMotherSelectionPolicy 枚举是否新增了值未处理."),
			static_cast<int32>(Policy));
		break;
	}
	}

	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] FilterCandidatesByPolicy: Policy=%d 候选 %d → 过滤后 %d"),
		static_cast<int32>(Policy), Candidates.Num(), Filtered.Num());

	return Filtered;
}


// ==========================================
// 【业务核心 — 母体变异】
// ==========================================

bool URoomMotherMutationSubsystem::MutateCharacterToMother(ABaseCharacter* Target)
{
	// ==========================================
	// 【v230 大厂架构新增】变异触发诊断快照
	// ==========================================
	//
	// 目的: 记录每次"小局开始时突然有母体出现"的问题
	//   - 触发链: HandleCountdownExpired → GetEligibleHumanTargets → SelectRandomTarget → MutateCharacterToMother
	//   - 此快照记录变异触发时刻的全部状态
	//
	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] 【v230 诊断】MutateCharacterToMother 触发! Target='%s'"),
		Target ? *Target->GetName() : TEXT("<null>"));

	if (Target)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[MotherMutation]   - Target 状态: bIsMother=%s bIsHuman=%s Dead=%s FactionTag='%s'"),
			Target->bIsMother ? TEXT("true") : TEXT("false"),
			Target->bIsHuman ? TEXT("true") : TEXT("false"),
			Target->IsDead() ? TEXT("true") : TEXT("false"),
			*Target->FactionTag.GetTagName().ToString());
	}

	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation]   - 触发时状态: bIsInRoundTransition=%s bMotherMutationFired_Local=%s"),
		bIsInRoundTransition ? TEXT("true") : TEXT("false"),
		bMotherMutationFired_Local ? TEXT("true") : TEXT("false"));

	// 大厂原则 — 显式优于隐式: 入参校验全开
	if (!Target)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutateCharacterToMother: Target=nullptr, 拒绝变异."));
		return false;
	}
	if (Target->IsDead())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutateCharacterToMother: Target='%s' 已死, 拒绝变异."),
			*Target->GetName());
		return false;
	}
	if (Target->bIsMother)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutateCharacterToMother: Target='%s' 已是母体, 拒绝二次变异."),
			*Target->GetName());
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutateCharacterToMother: World 为空, 拒绝变异."));
		return false;
	}

	// 大厂原则 — 业务分层 (v90 重构): Spawn 调度归 SpawnSubsystem, 本函数只负责
	//   1. 委派 Pawn 重建 (调 SpawnSys->MutatePawnToMother, 原地变 — 不回出生点)
	//   2. 母体账本 MotherCharacters.AddUnique
	//   3. 验证链 — 阵营 / 血量 (业务层强制让根因立刻可见)
	//
	// 【v99.1 大厂架构重构】母体 Pawn 业务字段 + 复活链真理源 + 视觉 RPC 全部下沉到 MutatePawnToMother
	//   - bIsMother / bIsHuman / PS->bIsMother / Multicast_PlayMutationFX 由 SpawnSubsystem 统一写入
	//   - 复活链直接调 MutatePawnToMother 不再漏写状态 / 漏发 RPC
	//
	// 旧 (v18-v88) 占位反模式:
	//   - 直接设 Target->bIsMother=true + 跳过 Pawn 重建
	//   - 玩家看到角色仍是人类 Mesh, 武器仍能开火, 完全没变异
	//   - 注释自述 "【占位】本架构下一阶段实现 Pawn 重建" — 跳过 = 重复架构
	//
	// 新 (v90) 单一真理源:
	//   - 实际 Pawn 销毁 + Spawn 新 BP_MuTi Pawn + 重新 Possess = SpawnSubsystem 唯一入口
	//   - MotherCharRowName 走 GM.MotherCharacterRowName (真理源, 业务可调)
	//   - 不允许业务层自己 Destroy + SpawnActor (违反 SRP / 集中调度)
	//   - 原地变 (业务核心: 母体在原地变, 不回到出生点)

	// ===== 防御层 4: SpawnSubsystem 注入校验 =====
	if (!Spawn)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutateCharacterToMother: SpawnSubsystem 未注入, 拒绝变异. "
			     "【v90 修复】检查 URoomMotherMutationSubsystem::InitializeSubsystem 是否被 ARoomGameMode::InjectSubsystemConfigs 调用."));
		return false;
	}

	// ===== Step 1: 委派 Pawn 重建 (SpawnSubsystem 单一入口) =====
	AController* TargetController = Target->GetController();
	if (!TargetController)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutateCharacterToMother: Target='%s' 的 Controller 为空, 拒绝变异. "
			     "【v90 修复】检查 Target 是否被 Possess."),
			*Target->GetName());
		return false;
	}

	// ==========================================
	// 【v90 大厂架构】真理源 — RowName 走 GM 配置, 不硬编码
	// ==========================================
	//
	// 旧 (v89) 反模式: 硬编码 const FString MotherCharRowName = TEXT("MT001");
	//   - 散落业务层, 策划改不了
	//   - RowName 跟 DT_CharacterInfo 配置错位 → 永远查不到
	//
	// 新 (v90) 真理源: ARoomGameMode::MotherCharacterRowName (UPROPERTY EditDefaultsOnly)
	//   - 业务层只读 GM 字段
	//   - 策划在 BP_GM_RoomGameMode ClassDefaults → MetalSlug|Match → MotherCharacterRowName 配
	//   - 零兜底: RowName 空 → Log Error + 拒绝变异
	// 注: World 沿用本函数防御层(line 320) 已获取的引用, 不重新定义
	// 注: 局部变量名不叫 GameMode (类成员已叫 GameMode) — 改名 RoomGM
	ARoomGameMode* RoomGM = World ? World->GetAuthGameMode<ARoomGameMode>() : nullptr;
	if (!RoomGM)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutateCharacterToMother: GameMode 为空, 拒绝变异. "
			     "【v90 修复】检查 ARoomGameMode::InitLifecycleSubsystem 链路."));
		return false;
	}
	const FString MotherCharRowName = RoomGM->MotherCharacterRowName;
	if (MotherCharRowName.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutateCharacterToMother: GM.MotherCharacterRowName 为空, 拒绝变异. "
			     "【v90 零兜底】必须在 BP_GM_RoomGameMode.uasset → ClassDefaults → MetalSlug|Match → MotherCharacterRowName 配 (如 'MT001'). "
			     "DT_CharacterInfo 中对应 RowName 的 CharacterBlueprint 必须指向 BP_MuTi 蓝图类."));
		return false;
	}

	// 委派 SpawnSubsystem 真重建 Pawn (原地变 — 业务核心)
	const bool bSpawnSuccess = Spawn->MutatePawnToMother(TargetController, MotherCharRowName);
	if (!bSpawnSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutateCharacterToMother: Target='%s' 委派 Pawn 重建失败 (RowName='%s'). "
			     "可能根因: 1) DT_CharacterInfo 中 Row='%s' 配错; "
			     "2) CharacterBlueprint 未指向 BP_MuTi 蓝图类; "
			     "3) BP_MuTi 蓝图类 LoadSynchronous 失败. "
			     "【v90 零兜底】拒绝变异, 本次变异失败不标记 MotherMutationHasFired."),
			*Target->GetName(),
			*MotherCharRowName,
			*MotherCharRowName);
		return false;
	}

	// ===== Step 2: 拿到新母体 Pawn =====
	ABaseCharacter* NewMotherPawn = Cast<ABaseCharacter>(TargetController->GetPawn());
	if (!NewMotherPawn)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutateCharacterToMother: 委派后 Controller Pawn 与 ABaseCharacter 派生不一致. "
			     "【v90 零兜底】MutatePawnToMother 内部应已验证, 这里兜底拒绝."));
		return false;
	}

	// ===== Step 2.5: 【v99.1 大厂架构 — 状态/RPC 已下沉到 MutatePawnToMother】=====
	//
	// 历史 (v90-v99):
	//   - 旧版业务层 Step 3 + Step 3.7 写 Pawn.bIsMother / bIsHuman / PS->bIsMother
	//   - 旧版 Step 4 触发 Multicast_PlayMutationFX RPC
	//   - 与 MutatePawnToMother 各管一段 → 复活链直接调 Spawn 函数会漏写状态与漏发 RPC → Bug
	//
	// 新 (v99.1) 单一真理源 — 母体 Pawn 创建入口 = MutatePawnToMother:
	//   - 母体 Pawn 业务字段 (bIsMother/bIsHuman) → MutatePawnToMother Step 5.7
	//   - 复活链真理源 PS->bIsMother → MutatePawnToMother Step 6
	//   - 视觉特效 Multicast_PlayMutationFX → MutatePawnToMother Step 7
	//   - 业务层只负责: 选目标、委派 Pawn 重建、记账 (MotherCharacters 业务账本)
	//
	// 业务层只追加验证链 (Step 3.5 / 3.6), 不写状态不触发 RPC

	// ===== Step 3.5: 【v93.3 大厂架构 — 阵营验证】业务层强制验证 FactionTag = Faction.Offense =====
	//
	// 根因 (用户 2026.07.25 Session1.log line 765):
	//   - MutatePawnToMother 在 Step 5.5 已切到 Offense, 但这是 SpawnSubsystem 的职责
	//   - 业务层必须在末尾验证, 不允许 SpawnSubsystem 出错被业务层静默放过
	//   - 如果这里 FactionTag != Offense, 说明 SpawnSubsystem Step 5.5 没跑成功, 是配置错 / 流程错
	//     必须 Log Error, 让策划/程序修复链路
	//
	// 大厂原则 — 零兜底:
	//   - FactionTag 不对 → Log Error, 但**不**中断流程 (Pawn 已创建, 业务态已设)
	//   - 理由: 静默 return false 会让 Pawn 创建出来但没入账本 + 没发 RPC → 半截变异, 玩家看到"角色卡住"
	//   - 而 Log Error + 继续走 → 玩家至少看到母体视觉, 但攻击链路 (CanDamage 同阵营守卫) 拒判
	//   - 强制报错让根因**立刻可见**, 而不是用户再花一次 chat session 排查
	//
	// 业务核心: 母体攻击 → 人类变母体, 必须经过 FFactionTags::CanDamage 守卫
	//          而守卫要求 Attacker.FactionTag == Offense, Victim.FactionTag == Defense
	//          如果新母体 FactionTag = Defense, 攻击下一个 Defense 人类时被守卫拒判 → 永远不变母体
	if (!FFactionTags::IsOffense(NewMotherPawn->FactionTag))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutateCharacterToMother: NewMotherPawn='%s' FactionTag='%s' (不是 Faction.Offense). "
			     "【v93.3 零兜底】母体必须为 Faction.Offense 阵营, 否则母体攻击永远不变母体 (同阵营守卫拒判). "
			     "可能根因: SpawnSubsystem::MutatePawnToMother Step 5.5 阵营切换失败. "
			     "请检查 RoomSpawnSubsystem.cpp MutatePawnToMother 末尾."),
			*NewMotherPawn->GetName(),
			*NewMotherPawn->FactionTag.ToString());
		// 不 return false — 业务态已设, RPC 照常发, 让玩家看到母体视觉; 但攻击链路会用错阵营
		// 零兜底 = 强制报错, 不允许静默继续
	}

	// ===== Step 3.6: 【v133.4 大厂架构 — 血量验证】业务层强制验证 MaxHealth = MotherMaxHealth =====
	//
	// 业务规则 (用户 2026.08.02 明确):
	//   - 母体血量要变成 MotherMaxHealth (业务可调, 来自 PlayerConfigAsset.MotherMaxHealth)
	//
	// 大厂原则 — 零兜底:
	//   - MaxHealth 不对 → Log Error + 不中断流程 (与 Step 3.5 同理: 静默 return 会让半截变异)
	//   - 业务层验证 = 强制让根因立刻可见, 策划/程序可立即修复
	//
	// 【v133.4 真理源迁移】验证期望值 = PlayerConfigAsset.MotherMaxHealth (不再是 GM.MotherMaxHealth)
	//
	// 【v133.4.1 真理源唯一入口】通过 Spawn->PlayerConfigAsset 读, 不通过 GM->PlayerConfigAsset
	//   - GM.PlayerConfigAsset 是 TSoftObjectPtr (只配引用, 直指针在 SpawnSubsystem)
	//   - Spawn->PlayerConfigAsset 才是直指针 (GM 通过 SetPlayerConfigAsset 注入)
	//   - 大厂原则 — 单一真理源: SpawnSubsystem 持有 PlayerConfigAsset 业务实例
	{
		const UHealthComponent* MotherHC = NewMotherPawn->ResolveHealthComponent();

		// 【v133.4.1 真理源唯一入口】从 Spawn->PlayerConfigAsset 读 (TObjectPtr 直指针)
		float ExpectedMotherHealth = -1.0f;
		if (Spawn && Spawn->GetPlayerConfigAsset())
		{
			ExpectedMotherHealth = Spawn->GetPlayerConfigAsset()->MotherMaxHealth;
		}

		if (!MotherHC)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[MotherMutation] MutateCharacterToMother: NewMotherPawn='%s' 找不到 HealthComponent. "
				     "【v93.4 零兜底】母体血量未验证, 实际值未知. "
				     "修复: 检查 BP_MuTi 蓝图是否挂了 HealthComponent."),
				*NewMotherPawn->GetName());
		}
		else if (ExpectedMotherHealth <= 0.0f)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[MotherMutation] MutateCharacterToMother: PlayerConfigAsset.MotherMaxHealth=%.1f (≤0, 配置非法). "
				     "【v133.4 零兜底】实际母体血量=%.1f, 与预期不一致. "
				     "修复: DA_PlayerConfig.uasset → Config|Health → Mother Max Health (默认 200)."),
				ExpectedMotherHealth, MotherHC->GetMax());
		}
		else if (!FMath::IsNearlyEqual(MotherHC->GetMax(), ExpectedMotherHealth))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[MotherMutation] MutateCharacterToMother: NewMotherPawn='%s' HealthComponent->MaxHealth=%.1f (期望=%.1f). "
				     "【v133.4 零兜底】母体血量与配置不一致. "
				     "可能根因: SpawnSubsystem::MutatePawnToMother Step 5.6 失败. "
				     "【废弃警告】GM.MotherMaxHealth 不再是真理源, 请迁移到 PlayerConfigAsset.MotherMaxHealth."),
				*NewMotherPawn->GetName(), MotherHC->GetMax(), ExpectedMotherHealth);
			// 不 return false — 业务态已设, RPC 照常发; 但血量会错
		}
	}

	// 【v128 P0 大厂架构 — 集中调度】母体账本注册走 RegisterMotherPawn 公开接口
	//   - 不直接 MotherCharacters.AddUnique (违反 SSOT,业务层漏写后任意路径都能逃过)
	//   - RegisterMotherPawn 内部统一日志 + 以后扩展(如统计、事件)都集中
	RegisterMotherPawn(NewMotherPawn);

	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] MutateCharacterToMother: '%s' 已变异为母体 (母体数=%d, Class=%s, Location=%s). "
		     "【v99.1 大厂架构】状态写入 + Multicast RPC 触发已在 MutatePawnToMother 末尾统一执行,本函数不重复触发."),
		*NewMotherPawn->GetName(),
		MotherCharacters.Num(),
		*NewMotherPawn->GetClass()->GetName(),
		*NewMotherPawn->GetActorLocation().ToString());

	return true;
}


// ==========================================
// 【v128 2026.08.02 大厂架构 — 账本集中调度入口】
// ==========================================
//
// 为什么需要:
void URoomMotherMutationSubsystem::RegisterMotherPawn(ABaseCharacter* MotherPawn)
{
	// 零兜底 — 入参校验
	if (!MotherPawn)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MotherMutation] RegisterMotherPawn: 入参 MotherPawn=nullptr, 拒绝注册."));
		return;
	}

	if (!IsValid(MotherPawn))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MotherMutation] RegisterMotherPawn: 入参 MotherPawn '%s' 已失效, 拒绝注册."),
			*MotherPawn->GetName());
		return;
	}

	const int32 OldCount = MotherCharacters.Num();
	MotherCharacters.AddUnique(MotherPawn);
	const int32 NewCount = MotherCharacters.Num();

	if (OldCount != NewCount)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[MotherMutation] 【v128 账本同步】RegisterMotherPawn: 新增 '%s' (%d → %d). "
			     "账本是 BTService_UpdateZombieTargets / GetAliveMotherCount 的唯一真理源."),
			*MotherPawn->GetName(), OldCount, NewCount);
	}
	// OldCount == NewCount: 幂等命中,Verb
}

void URoomMotherMutationSubsystem::UnregisterMotherPawn(ABaseCharacter* MotherPawn)
{
	if (!MotherPawn)
	{
		return; // 安全 no-op
	}

	const int32 OldCount = MotherCharacters.Num();
	MotherCharacters.RemoveAll([MotherPawn](const TWeakObjectPtr<ABaseCharacter>& Weak)
	{
		ABaseCharacter* C = Weak.Get();
		return C == nullptr || C == MotherPawn;
	});
	const int32 NewCount = MotherCharacters.Num();

	const int32 Removed = OldCount - NewCount;
	if (Removed > 0)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[MotherMutation] 【v128 账本清理】UnregisterMotherPawn: '%s' 共移除 %d 条 "
			     "(含失效 TWeakObjectPtr, %d → %d). 防止账本长期膨胀."),
			*MotherPawn->GetName(), Removed, OldCount, NewCount);
	}
}


// ==========================================
// 【业务查询 — 母体变异计数】
// ==========================================

int32 URoomMotherMutationSubsystem::GetMotherMutationCount() const
{
	// 大厂原则 — SSOT 转发壳: GameState.MotherMutationCount 是唯一真理源
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}
	if (const ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>())
	{
		return RoomGS->MotherMutationCount;
	}
	return 0;
}


// ==========================================
// 【v107 2026.07.28 生化模式 BT】存活母体数
// ==========================================
//
// 严格定义 (用户 2026.07.28 明确):
//   - AliveMotherCount == 1 才算"只剩一个母体" — 人类追杀分支触发条件
//   - 死掉的母体不计入 (Pawn Destroy 后自动失效)
//   - 复活中的母体不计入 (Pawn 还没生成)
//   - 等待变异的"候选人类"不计入 (还没变异)
//
// 大厂原则 — 单一真理源:
//   - 不 GetAllActorsOfClass 散查
//   - MotherCharacters 是业务账本 (TArray<TWeakObjectPtr<ABaseCharacter>>)
//   - TWeakObjectPtr 自动失效: 死亡 Pawn 自然被跳过
//   - 遍历时验证 IsValid + !IsDead + bIsMother

int32 URoomMotherMutationSubsystem::GetAliveMotherCount() const
{
	int32 AliveCount = 0;

	for (const TWeakObjectPtr<ABaseCharacter>& WeakMother : MotherCharacters)
	{
		const ABaseCharacter* Mother = WeakMother.Get();
		if (!IsValid(Mother))           { continue; } // Pawn 已销毁
		if (Mother->IsDead())           { continue; } // 死亡 (复活不算存活)
		if (!Mother->bIsMother)         { continue; } // 数据不一致保护
		++AliveCount;
	}

	return AliveCount;
}


// ==========================================
// 【v107 2026.07.28 生化模式 BT】存活人类数
// ==========================================
//
// 严格定义:
//   - 玩家 Pawn + AI Pawn, 全对局内所有 ABaseCharacter
//   - !IsDead() && !bIsMother (排除死亡 + 已变异母体)
//
// 大厂原则 — 单一真理源:
//   - 走 URoomSpawnSubsystem::GetAllBattleCharacters() (业务层账本)
//   - 不 GetAllActorsOfClass (散查反模式)
//
// 注: 母体账本中的母体如果"被感染再次变异" (业务不允许), 这里会重复计入 — 防御层靠 !bIsMother 排除
// 注: 此函数每 ZombieTargetRefreshIntervalSeconds 调用一次, N<=20 性能可接受

int32 URoomMotherMutationSubsystem::GetAliveHumanCount() const
{
	int32 AliveCount = 0;

	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}

	if (!Spawn)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] GetAliveHumanCount: SpawnSubsystem 未注入, 返回 0. "
			     "【修复】检查 URoomMotherMutationSubsystem::InitializeSubsystem 是否被 ARoomGameMode::InjectSubsystemConfigs 调用."));
		return 0;
	}

	const TArray<ABaseCharacter*> AllBattleChars = Spawn->GetAllBattleCharacters();

	// 【v108 大厂可观测性】记录扫描结果
	if (AllBattleChars.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MotherMutation] GetAliveHumanCount: GetAllBattleCharacters 返回空! "
			     "【根因】场景中无 ABaseCharacter Pawn (玩家/AI 均未生成). "
			     "检查: 1) 战斗是否已开始 (PerformGameStart 是否调用); "
			     "2) 玩家/AI 是否成功 Spawn; "
			     "3) ABaseCharacter::bPendingDestruction 残留导致遍历被过滤."));
	}

	for (ABaseCharacter* Char : AllBattleChars)
	{
		if (!IsValid(Char))      { continue; }
		if (Char->IsDead())     { continue; } // 死亡排除
		if (Char->bIsMother)   { continue; } // 已变异母体排除
		++AliveCount;
	}

	// 【v108 大厂可观测性】记录结果
	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] GetAliveHumanCount: AllChars=%d → 有效人类=%d"),
		AllBattleChars.Num(), AliveCount);

	return AliveCount;
}


// ==========================================
// 【v108 大厂架构】新回合开始时重置母体账本
// ==========================================

void URoomMotherMutationSubsystem::ResetForNewRound()
{
	const int32 OldCount = MotherCharacters.Num();
	MotherCharacters.Empty();
	bMotherMutationFired_Local = false;

	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] ResetForNewRound: 清空 MotherCharacters (%d→0), 重置 bMotherMutationFired_Local."),
		OldCount);
}