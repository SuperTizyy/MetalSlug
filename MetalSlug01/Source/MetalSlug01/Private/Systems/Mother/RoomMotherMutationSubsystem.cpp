// Copyright (c) 2026. All Rights Reserved.
// URoomMotherMutationSubsystem — 生化模式母体变异业务权威调度

#include "Systems/Mother/RoomMotherMutationSubsystem.h"

#include "Systems/RoomGameMode.h"
#include "Systems/RoomGameState.h"
#include "Systems/Lifecycle/RoomLifecycleSubsystem.h"
#include "Systems/Spawn/RoomSpawnSubsystem.h"
#include "Characters/BaseCharacter.h"
#include "Components/HealthComponent.h" // v93.4 — MutateCharacterToMother Step 3.6 血量验证 (GetMax)

#include "Data/Faction/FactionTags.h" // v93.3 — MutateCharacterToMother Step 3.5 阵营验证 (IsOffense)

#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
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

	// 2. 随机选母体
	ABaseCharacter* Selected = SelectRandomTarget(Candidates);
	if (!Selected)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] HandleCountdownExpired: 选母体失败 (Selected=nullptr), 拒绝触发变异."));
		return;
	}

	// 3. 标记 (防御层 1 + 2 写入)
	bMotherMutationFired_Local = true;
	RoomGS->MarkMotherMutationFired();

	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] HandleCountdownExpired: 选中母体 '%s' (%s), 即将变异"),
		*Selected->GetName(),
		*GetNameSafe(Selected->GetController()));

	// 4. 触发变异
	const bool bSuccess = MutateCharacterToMother(Selected);
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] HandleCountdownExpired: MutateCharacterToMother 失败, 但已标记 MotherMutationHasFired, "
			     "本局将不再触发. 排查 BP_MuTi 蓝图类加载 / SpawnSubsystem 配置."));
	}
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
// 【业务核心 — 母体变异】
// ==========================================

bool URoomMotherMutationSubsystem::MutateCharacterToMother(ABaseCharacter* Target)
{
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
	//   2. 业务层标记 bIsMother + bIsHuman (双字段同步, Replicated 自动复制)
	//   3. 账本 MotherCharacters.AddUnique
	//   4. RPC 广播视觉特效 (Multicast_PlayMutationFX)
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

	// ===== Step 3: 业务层标记 bIsMother (服务器本地 — Replicated 自动同步所有客户端) =====
	// 大厂原则 — 双字段同步: bIsMother=true 时 bIsHuman=false (互斥语义, 注释约定)
	//   - bIsMother ReplicatedUsing = OnRep_bIsMother → 客户端 OnRep → Broadcast OnMotherStatusChanged
	//   - bIsHuman Replicated (无 OnRep) → 客户端同步 (镜像 bIsMother)
	NewMotherPawn->bIsMother = true;
	NewMotherPawn->bIsHuman = false;

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

	// ===== Step 3.6: 【v93.4 大厂架构 — 血量验证】业务层强制验证 MaxHealth = MotherMaxHealth =====
	//
	// 业务规则 (用户 2026.07.25 明确):
	//   - 母体血量要变成 200 (业务可调, 来自 GM.MotherMaxHealth)
	//
	// 大厂原则 — 零兜底:
	//   - MaxHealth 不对 → Log Error + 不中断流程 (与 Step 3.5 同理: 静默 return 会让半截变异)
	//   - 业务层验证 = 强制让根因立刻可见, 策划/程序可立即修复
	{
		const UHealthComponent* MotherHC = NewMotherPawn->ResolveHealthComponent();
		const float ExpectedMotherHealth = (RoomGM != nullptr) ? RoomGM->MotherMaxHealth : -1.0f;
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
				TEXT("[MotherMutation] MutateCharacterToMother: GM.MotherMaxHealth=%.1f (≤0, 配置非法). "
				     "【v93.4 零兜底】实际母体血量=%.1f, 与预期不一致. "
				     "修复: BP_GM_RoomGameMode.uasset → ClassDefaults → MetalSlug|Match → Mother Max Health (默认 200)."),
				ExpectedMotherHealth, MotherHC->GetMax());
		}
		else if (!FMath::IsNearlyEqual(MotherHC->GetMax(), ExpectedMotherHealth))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[MotherMutation] MutateCharacterToMother: NewMotherPawn='%s' HealthComponent->MaxHealth=%.1f (期望=%.1f). "
				     "【v93.4 零兜底】母体血量与配置不一致. "
				     "可能根因: SpawnSubsystem::MutatePawnToMother Step 5.6 失败."),
				*NewMotherPawn->GetName(), MotherHC->GetMax(), ExpectedMotherHealth);
			// 不 return false — 业务态已设, RPC 照常发; 但血量会错
		}
	}

	// 记录到母体账本 (业务层唯一真理源, TWeakObjectPtr 天然失效检测)
	MotherCharacters.AddUnique(NewMotherPawn);

	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] MutateCharacterToMother: '%s' 已变异为母体 (母体数=%d, Class=%s, Location=%s)"),
		*NewMotherPawn->GetName(),
		MotherCharacters.Num(),
		*NewMotherPawn->GetClass()->GetName(),
		*NewMotherPawn->GetActorLocation().ToString());

	// ===== Step 4: 【RPC 边界 — 服务器广播】通知所有客户端播母体变异特效 =====
	// 纯数据 RPC (v31.6 大厂原则): 传 FString 而不是 Actor*
	// 跨 RPC 边界传 Actor* 在 UE 5.6 会因 NetGUID 失效导致 null 解引用 (v31.6 修复)
	//
	// 双保险链路:
	//   - 服务器 Multicast_PlayMutationFX(TargetName) → 所有客户端 Implementation → Broadcast OnMotherStatusChanged
	//   - 客户端 bIsMother OnRep_bIsMother → Broadcast OnMotherStatusChanged (重复触发, BP 幂等 OK)
	//
	// 注意: RowName 在 Multicast 客户端不需要, 只传 TargetName (视觉特效需要)
	const FString TargetName = NewMotherPawn->GetName();
	NewMotherPawn->Multicast_PlayMutationFX(TargetName);

	return true;
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