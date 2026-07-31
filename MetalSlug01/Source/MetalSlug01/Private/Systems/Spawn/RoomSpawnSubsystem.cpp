// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/Spawn/RoomSpawnSubsystem.h"
#include "Systems/RoomGameMode.h"
#include "Systems/RoomGameState.h"
#include "Systems/Core/RoomPlayerState.h"
#include "Systems/BaseAIController.h"
#include "Systems/RoomPlayerController.h"
#include "Systems/Mother/RoomMotherMutationSubsystem.h" // 【v128 新增】URoomMotherMutationSubsystem (RegisterMotherPawn / UnregisterMotherPawn)
#include "Data/Enums/RoomEnums.h"
#include "Data/Faction/FactionTags.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Data/Tables/CharacterTableRow.h"
#include "Data/Tables/SpawnTableRow.h"
#include "Data/Tables/WeaponTableRow.h"      // 【v54.3 新增】FWeaponInfo (DT_WeaponInfo 行)
#include "Weapons/BaseWeapon.h"              // 【v54.3 新增】ABaseWeapon (ResolveWeaponClassFromID 返回类型)
#include "Combat/WeaponAttachmentComponent.h" // 【v52 P0 新增】Server_SpawnAllWeapons 调用
#include "Combat/CharacterIconComponent.h"    // 【v105 新增】MutatePawnToMother 末尾调用刷新头像
#include "Data/Config/PlayerConfigAsset.h"
#include "Characters/BaseCharacter.h"
#include "Components/HealthComponent.h"
#include "Components/EnergyComponent.h"
#include "Components/HealthRegenComponent.h"
#include "AIController.h"  // 【v54 修复】AAIController 完整类型 - SpawnActor<AAIController> 需要
#include "Engine/World.h"
#include "EngineUtils.h" // TActorIterator
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

// ==========================================
// UWorldSubsystem 基础
// ==========================================

URoomSpawnSubsystem* URoomSpawnSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		return World->GetSubsystem<URoomSpawnSubsystem>();
	}
	return nullptr;
}

bool URoomSpawnSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Server-only
	if (UWorld* World = Cast<UWorld>(Outer))
	{
		return World->GetNetMode() != NM_Client;
	}
	return false;
}

// ==========================================
// 【v41 大厂架构】角色战斗参数初始化
// ==========================================

/**
 * ApplyCharacterConfigToCharacter — v41 大厂架构
 *
 * 从 DT_CharacterConfig 读取战斗参数并应用到角色组件
 *
 * 参数来源 (按优先级):
 *   1. DT_CharacterConfig 表中有配置 → 使用配置
 *   2. 表未配置或查不到行 → 使用组件默认值 (HealthComponent::MaxHealth=100 等)
 *
 * 应用目标:
 *   - HealthComponent: MaxHealth → InitializeHealth(Max)
 *   - EnergyComponent: MaxEnergy → InitializeEnergy(max, max)
 *   - HealthRegenComponent: HealthRegenRate / EnergyRegenRate / RegenerationDelay
 *   - BaseCharacter: RespawnDelaySeconds / WeaponDestroyDelaySeconds / DefaultSpawnInvincibilitySeconds
 *                   / HealthRewardPerKill / EnergyRewardPerKill / AssistTimeWindow
 *
 * 大厂原则 - 零兜底:
 *   - 配置表未配置时使用默认值, 不 Log Error
 *   - 这是数据驱动的设计: 表是权威, 组件默认值是 fallback
 */
void URoomSpawnSubsystem::ApplyCharacterConfigToCharacter(ABaseCharacter* Character)
{
	if (!Character)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomSpawnSubsystem] ApplyCharacterConfigToCharacter: Character 为空!"));
		return;
	}

	// v41 大厂架构: 从 PlayerConfigAsset 读取参数
	if (!PlayerConfigAsset)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomSpawnSubsystem] ApplyCharacterConfigToCharacter: PlayerConfigAsset 未配置!"
				" 请在 GM_RoomGameMode 蓝图中配置 PlayerConfigAsset = DA_PlayerConfig。"));
		return;
	}

	// ==========================================
	// 1. HealthComponent 初始化
	// ==========================================
	//
	// 【v133.4 2026.08.02 大厂架构】按 Pawn.bIsMother 分流 — 单一真理源
	//   - bIsMother=true  → MotherMaxHealth (母体玩家, 变异增强)
	//   - bIsMother=false → MaxHealth      (人类玩家, 默认)
	if (UHealthComponent* HC = Character->ResolveHealthComponent())
	{
		const float EffectiveMaxHealth = Character->bIsMother
			? PlayerConfigAsset->MotherMaxHealth
			: PlayerConfigAsset->MaxHealth;

		// 【零兜底】配错 <= 0 → 显式 Error, 不静默
		if (EffectiveMaxHealth <= 0.f)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomSpawnSubsystem] ApplyCharacterConfig: %sMaxHealth=%.1f (<=0, 配错). "
				     "Pawn=%s (bIsMother=%d) 无法初始化血量. "
				     "【修复】DA_PlayerConfig → Config|Health → %s 设置 > 0."),
				Character->bIsMother ? TEXT("Mother") : TEXT(""),
				EffectiveMaxHealth,
				*Character->GetName(),
				Character->bIsMother ? 1 : 0,
				Character->bIsMother ? TEXT("MotherMaxHealth") : TEXT("MaxHealth"));
			return;
		}

		HC->InitializeHealth(EffectiveMaxHealth);
		UE_LOG(LogTemp, Log,
			TEXT("[RoomSpawnSubsystem] ApplyCharacterConfig: bIsMother=%d, MaxHealth=%.1f (Pawn=%s, 真理源=%s)"),
			Character->bIsMother ? 1 : 0,
			EffectiveMaxHealth,
			*Character->GetName(),
			Character->bIsMother ? TEXT("MotherMaxHealth") : TEXT("MaxHealth"));
	}

	// ==========================================
	// 2. EnergyComponent 初始化
	// ==========================================
	if (UEnergyComponent* EC = Character->ResolveEnergyComponent())
	{
		EC->InitializeEnergy(PlayerConfigAsset->MaxEnergy, PlayerConfigAsset->MaxEnergy);
		UE_LOG(LogTemp, Log,
			TEXT("[RoomSpawnSubsystem] ApplyCharacterConfig: MaxEnergy=%.1f (Pawn=%s)"),
			PlayerConfigAsset->MaxEnergy, *Character->GetName());
	}

	// ==========================================
	// 3. HealthRegenComponent 初始化
	// ==========================================
	if (UHealthRegenComponent* HRC = Character->ResolveHealthRegenComponent())
	{
		HRC->HealthRegenRate = PlayerConfigAsset->HealthRegenRate;
		HRC->EnergyRegenRate = PlayerConfigAsset->EnergyRegenRate;
		HRC->RegenerationDelay = PlayerConfigAsset->RegenerationDelay;
		UE_LOG(LogTemp, Log,
			TEXT("[RoomSpawnSubsystem] ApplyCharacterConfig: RegenRates H=%.2f E=%.2f Delay=%.1f (Pawn=%s)"),
			HRC->HealthRegenRate, HRC->EnergyRegenRate, HRC->RegenerationDelay, *Character->GetName());
	}

	// ==========================================
	// 4. BaseCharacter 字段 (使用 public setter 方法 — 大厂架构)
	// ==========================================
	Character->SetRespawnDelaySeconds(PlayerConfigAsset->RespawnDelaySeconds);
	Character->SetWeaponDestroyDelaySeconds(PlayerConfigAsset->WeaponDestroyDelaySeconds);
	Character->SetDefaultSpawnInvincibilitySeconds(PlayerConfigAsset->SpawnInvincibilitySeconds);
	Character->SetHealthRewardPerKill(PlayerConfigAsset->HealthRewardPerKill);
	Character->SetEnergyRewardPerKill(PlayerConfigAsset->EnergyRewardPerKill);
	Character->SetAssistTimeWindow(PlayerConfigAsset->AssistTimeWindow);

	// ==========================================
	// 5. 移动速度配置 (CharacterMovementComponent)
	// ==========================================
	if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
	{
		// 【v133.3 2026.08.02 大厂架构】按 Pawn.bIsMother 分流 — 单一真理源
		//   - bIsMother=true  → MotherMaxWalkSpeed (母体玩家, 变异加速感)
		//   - bIsMother=false → MaxWalkSpeed      (人类玩家, 默认)
		//
		// 注意: MutateCharacterToMother 完成后会再次调用本函数, 此时 Pawn 已被重建为母体 BP,
		//       bIsMother=true → 立即切到 MotherMaxWalkSpeed (用户明确指示)
		const float EffectiveWalkSpeed = Character->bIsMother
			? PlayerConfigAsset->MotherMaxWalkSpeed
			: PlayerConfigAsset->MaxWalkSpeed;

		// 【零兜底】配错 <= 0 → 显式 Error, 不静默
		if (EffectiveWalkSpeed <= 0.f)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomSpawnSubsystem] ApplyCharacterConfig: %sMaxWalkSpeed=%.1f (<=0, 配错). "
				     "Pawn=%s (bIsMother=%d) 无法移动. 【修复】DA_PlayerConfig → Config|Movement → %s 设置 > 0."),
				Character->bIsMother ? TEXT("Mother") : TEXT(""),
				EffectiveWalkSpeed,
				*Character->GetName(),
				Character->bIsMother ? 1 : 0,
				Character->bIsMother ? TEXT("MotherMaxWalkSpeed") : TEXT("MaxWalkSpeed"));
			return;
		}

		MoveComp->MaxWalkSpeed = EffectiveWalkSpeed;
		// 下蹲速度默认是正常速度的一半
		MoveComp->MaxWalkSpeedCrouched = EffectiveWalkSpeed * 0.5f;

		UE_LOG(LogTemp, Log,
			TEXT("[RoomSpawnSubsystem] ApplyCharacterConfig: bIsMother=%d, MaxWalkSpeed=%.1f CrouchedSpeed=%.1f (Pawn=%s, 真理源=%s)"),
			Character->bIsMother ? 1 : 0,
			EffectiveWalkSpeed,
			EffectiveWalkSpeed * 0.5f,
			*Character->GetName(),
			Character->bIsMother ? TEXT("MotherMaxWalkSpeed") : TEXT("MaxWalkSpeed"));
	}

	UE_LOG(LogTemp, Log,
		TEXT("[RoomSpawnSubsystem] ApplyCharacterConfigToCharacter 完成: Pawn=%s"),
		*Character->GetName());
}


// ==========================================
// 【v133.4 2026.08.02 大厂架构】AI ConfigSO 配置应用 (真理源分离)
// ==========================================
//
// 真理源分离 (大厂原则 — 单一真理源 + 职责对等):
//   - 玩家路径 = PlayerConfigAsset (ApplyCharacterConfigToCharacter)
//   - AI 路径   = ConfigSO.AIBehaviorConfig (ApplyAICharacterConfigToCharacter)
//
// 之前 v41 的反模式:
//   - AI 路径也调 ApplyCharacterConfigToCharacter → AI 血量被 PlayerConfigAsset.MaxHealth=100 覆盖
//   - 真理源混淆 → AI 配置失效 (ConfigSO 的血量永远不会被应用)
//   - ConfigSO 加血量字段毫无意义 (被 PlayerConfigAsset 覆盖)
//
// 新架构 (v133.4):
//   - AI 路径只调 ApplyAICharacterConfigToCharacter → 读 ConfigSO.Health
//   - 玩家路径调 ApplyCharacterConfigToCharacter → 读 PlayerConfigAsset
//   - 真理源严格分离, ConfigSO 字段真正生效
//
// 调用方:
//   - URoomSpawnSubsystem::SpawnAIInternal (大厅 AI Spawn 成功后)
//   - AMeleeAIController::SetupMeleeAI 末尾 (关卡预放 AI)
//   - URoomSpawnSubsystem::MutatePawnToMother 末尾 (母体 AI 复活)
// ==========================================
void URoomSpawnSubsystem::ApplyAICharacterConfigToCharacter(ABaseCharacter* Character)
{
	if (!Character)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomSpawnSubsystem] ApplyAICharacterConfigToCharacter: Character 为空!"));
		return;
	}

	// ==========================================
	// 1. 拿 ConfigSO — 真理源 (运行时)
	// ==========================================
	// 大厂原则 — 单一真理源:
	//   - ConfigSO 走 BaseAIController->GetConfig() 取得 (运行时, 单一入口)
	//   - 不直接读 Config->SpawnInvincibilitySeconds (避免与 BaseAIController 重复实现)
	UAIBehaviorConfigSO* Config = nullptr;

	if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(Character->GetController()))
	{
		// 【v133.4.1 大厂架构】GetConfig() 返回 const, 但本函数只读字段 (MotherMaxHealth/MaxHealth)
		// const_cast 用于消除编译错误, 实际不修改 Config 字段 — 真理源仍是只读
		Config = const_cast<UAIBehaviorConfigSO*>(BaseAIC->GetConfig());
	}

	if (!Config)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomSpawnSubsystem] ApplyAICharacterConfigToCharacter: AI=%s 的 BaseAIController->GetConfig() 为空. "
			     "【v133.4 零兜底】拒绝应用 ConfigSO, AI 配置失败. "
			     "【修复】检查 ConfigSO 是否在 AIController ClassDefaults 配. "
			     "如果该 Pawn 是 Player Controller 管辖, 请用 ApplyCharacterConfigToCharacter 而非本函数."),
			*Character->GetName());
		return;
	}

	// ==========================================
	// 2. HealthComponent 初始化 (按 bIsMother 分流)
	// ==========================================
	//
	// 【v133.4 大厂架构】按 Pawn.bIsMother 分流 — 单一真理源
	//   - bIsMother=true  → ConfigSO.MotherMaxHealth (母体 AI)
	//   - bIsMother=false → ConfigSO.MaxHealth      (人类 AI)
	if (UHealthComponent* HC = Character->ResolveHealthComponent())
	{
		const float EffectiveMaxHealth = Character->bIsMother
			? Config->MotherMaxHealth
			: Config->MaxHealth;

		// 【零兜底】配错 <= 0 → 显式 Error, 不静默
		if (EffectiveMaxHealth <= 0.f)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomSpawnSubsystem] ApplyAICharacterConfig: %s=%.1f (<=0, 配错). "
				     "AI=%s (bIsMother=%d) 无法初始化血量. "
				     "【修复】DA_AIBehaviorConfig_XXX → Health → %s 设置 > 0."),
				Character->bIsMother ? TEXT("MotherMaxHealth") : TEXT("MaxHealth"),
				EffectiveMaxHealth,
				*Character->GetName(),
				Character->bIsMother ? 1 : 0,
				Character->bIsMother ? TEXT("MotherMaxHealth") : TEXT("MaxHealth"));
			return;
		}

		HC->InitializeHealth(EffectiveMaxHealth);
		UE_LOG(LogTemp, Log,
			TEXT("[RoomSpawnSubsystem] ApplyAICharacterConfig: bIsMother=%d, MaxHealth=%.1f (AI=%s, 真理源=%s)"),
			Character->bIsMother ? 1 : 0,
			EffectiveMaxHealth,
			*Character->GetName(),
			Character->bIsMother ? TEXT("ConfigSO.MotherMaxHealth") : TEXT("ConfigSO.MaxHealth"));
	}
}

// ==========================================
// 出生点扫描与分配
// ==========================================

/**
 * ScanAndCachePlayerStarts — 扫描所有 PlayerStart 并按 PlayerStartTag 分类
 *
 * Tag 规则:
 *   - TAG_Faction_Offense → 攻方 (AttackSpawnPoints)
 *   - TAG_Faction_Defense → 守方 (DefenseSpawnPoints)
 *   - 旧 TAG_Faction_Attack → 兼容接收 + Log Warning
 *   - 无 tag / 不匹配 → Log Error + 跳过 (零兜底)
 */
void URoomSpawnSubsystem::ScanAndCachePlayerStarts(bool bReScan)
{
	if (bSpawnPointsScanned && !bReScan)
	{
		return;
	}

	// 重新扫描时清空
	AttackSpawnPoints.Reset();
	DefenseSpawnPoints.Reset();
	MotherSpawnPoints.Reset(); // 【v104 新增】清空母体复活点

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 初始化 TAG 常量 (如果未注入)
	if (TAG_Faction_Offense.IsNone())
	{
		TAG_Faction_Offense = FName(TEXT("Faction_Offense"));
	}
	if (TAG_Faction_Defense.IsNone())
	{
		TAG_Faction_Defense = FName(TEXT("Faction_Defense"));
	}
	// 【v104 新增】母体复活点 TAG
	if (TAG_Faction_Mother.IsNone())
	{
		TAG_Faction_Mother = FName(TEXT("Faction_Mother"));
	}

	// 兼容旧 TAG
	const FName LegacyTag_Attack = FName(TEXT("Faction_Attack"));

	int32 TotalFound = 0;
	int32 MatchedFaction = 0;
	int32 LegacyMatched = 0;
	int32 MotherMatched = 0; // 【v104 新增】
	int32 ErrorSkipped = 0;

	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		APlayerStart* PS = *It;
		if (!PS)
		{
			continue;
		}

		++TotalFound;

		const FName PlayerStartTag = PS->PlayerStartTag;
		const FString StartName = PS->GetName();

		// 【v104 新增】母体复活点分支 (优先级最高，因为它有特殊用途)
		if (PlayerStartTag == TAG_Faction_Mother)
		{
			MotherSpawnPoints.Add(PS);
			++MotherMatched;
			continue; // 母体点不归入攻守方阵营
		}

		if (PlayerStartTag == TAG_Faction_Offense)
		{
			AttackSpawnPoints.Add(PS);
			++MatchedFaction;
		}
		else if (PlayerStartTag == TAG_Faction_Defense)
		{
			DefenseSpawnPoints.Add(PS);
			++MatchedFaction;
		}
		else if (PlayerStartTag == LegacyTag_Attack)
		{
			// 兼容旧 tag — 接收到 AttackSpawnPoints, 但 Log Warning 推动用户迁移
			AttackSpawnPoints.Add(PS);
			++LegacyMatched;
			UE_LOG(LogTemp, Warning,
				TEXT("[Spawn] PlayerStart '%s' 使用旧 Tag '%s' (推荐改 '%s'). "
				     "已接收归入 Offense 阵营, 但请迁移到新命名."),
				*StartName, *LegacyTag_Attack.ToString(), *TAG_Faction_Offense.ToString());
		}
		else
		{
			// 零兜底: 没 tag / 不匹配 → 显式 Log Error, 跳过
			++ErrorSkipped;
			UE_LOG(LogTemp, Error,
				TEXT("[Spawn] PlayerStart '%s' 没有阵营 Tag (期望 '%s' / '%s' / '%s'). "
				     "【拒绝默认归类 - 大厂原则】此点不会分配给任何阵营."
				     " 修复: 在编辑器 Details → Player Start Tag 配 Faction_Offense / Faction_Defense / Faction_Mother."),
				*StartName,
				*TAG_Faction_Offense.ToString(),
				*TAG_Faction_Defense.ToString(),
				*TAG_Faction_Mother.ToString());
		}
	}

	bSpawnPointsScanned = true;

	// 【v104 新增】日志输出母体复活点统计
	if (MotherSpawnPoints.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Spawn] ScanPlayerStarts 完成: 总 %d, 匹配阵营 %d, 旧 TAG %d, 母体点 %d, 错误跳过 %d. "
			     "AttackSpawnPoints=%d, DefenseSpawnPoints=%d, MotherSpawnPoints=0 【警告】母体点缺失!"),
			TotalFound, MatchedFaction, LegacyMatched, MotherMatched, ErrorSkipped,
			AttackSpawnPoints.Num(), DefenseSpawnPoints.Num());
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Spawn] ScanPlayerStarts 完成: 总 %d, 匹配阵营 %d, 旧 TAG %d, 母体点 %d, 错误跳过 %d. "
			     "AttackSpawnPoints=%d, DefenseSpawnPoints=%d, MotherSpawnPoints=%d"),
			TotalFound, MatchedFaction, LegacyMatched, MotherMatched, ErrorSkipped,
			AttackSpawnPoints.Num(), DefenseSpawnPoints.Num(), MotherSpawnPoints.Num());
	}
}

AActor* URoomSpawnSubsystem::GetAvailableSpawnPointForFaction(FGameplayTag PlayerFactionTag, bool bRemoveOccupied, AController* OccupancyOwner)
{
	TArray<APlayerStart*>* FactionSpawns = nullptr;
	if (FFactionTags::IsOffense(PlayerFactionTag))
	{
		FactionSpawns = &AttackSpawnPoints;
	}
	else if (FFactionTags::IsDefense(PlayerFactionTag))
	{
		FactionSpawns = &DefenseSpawnPoints;
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Spawn] GetAvailableSpawnPointForFaction: 阵营 '%s' 非 Faction.Offense/Defense, 不分配出生点."
			     " 上游调用方必须立即停止 Spawn."),
			*PlayerFactionTag.ToString());
		return nullptr;
	}

	if (FactionSpawns->Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Spawn] 阵营 '%s' 无可用出生点 (地图布点缺失)"),
			*PlayerFactionTag.ToString());
		return nullptr;
	}

	for (APlayerStart* SpawnPoint : (*FactionSpawns))
	{
		if (SpawnPoint && !OccupiedSpawnPoints.Contains(SpawnPoint))
		{
			if (bRemoveOccupied)
			{
				OccupiedSpawnPoints.Add(SpawnPoint);
				// 【v39 P0 修复】记录 Controller → PlayerStart 映射, 供 ReleaseSpawnPointByController 精准释放
				// 根因: 旧版释放走 ResetAllSpawnPointOccupancy 会误清空其他玩家占用 (多玩家同帧死亡场景)
				// 大厂原则: 集中调度 + SSOT — 死亡时通过 Controller 反查释放
				if (OccupancyOwner)
				{
					OccupiedSpawnByController.Add(OccupancyOwner, SpawnPoint);
				}
			}
			return SpawnPoint;
		}
	}

	UE_LOG(LogTemp, Error,
		TEXT("[Spawn] GetAvailableSpawnPointForFaction: 阵营 '%s' 全部 %d 个出生点都被占用. "
		     "拒绝随机复用 (v30 零兜底). 排查上游 ReleaseSpawnPoint."),
		*PlayerFactionTag.ToString(),
		FactionSpawns->Num());
	return nullptr;
}

// ==========================================
// 【v56 新增】关卡预放 AI 阵营缓存
// ==========================================

/**
 * ScanAndCacheLevelPlacedAIFactions — 扫描并缓存关卡预放 AI 的阵营
 *
 * 设计决策 (用户 2026.07.16):
 *   - AIBehaviorConfigSO 不允许添加 DefaultFactionTag
 *   - 关卡预放 AI 的阵营必须从角色类 (Pawn BP) 的 FactionTag 属性获取
 *
 * 大厂原则:
 *   - 扫描时机: PerformGameStart 前 (BattleStarted 广播前)
 *   - 扫描范围: 所有 ABaseCharacter 子类 Pawn
 *   - 缓存: PawnClass.GetName() → Pawn.FactionTag
 *   - 调用入口: SpawnAllPlayersIntoBattle 开始时
 */
void URoomSpawnSubsystem::ScanAndCacheLevelPlacedAIFactions()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	LevelPlacedAIFactionByClassName.Reset();

	int32 TotalScanned = 0;
	int32 CachedCount = 0;

	for (TActorIterator<ABaseCharacter> It(World); It; ++It)
	{
		ABaseCharacter* AIChar = *It;
		if (!AIChar)
		{
			continue;
		}

		++TotalScanned;

		// 只处理 AI Pawn (有 Controller 的)
		AController* AIC = Cast<AController>(AIChar->GetOwner());
		if (!AIC || AIC->IsPlayerController())
		{
			continue;
		}

		// 获取角色类的名称作为 key
		if (UClass* PawnClass = AIChar->GetClass())
		{
			const FString ClassName = PawnClass->GetName();
			const FGameplayTag FactionTag = AIChar->GetFactionTag();

			// 缓存阵营 (如果已经有则不覆盖)
			if (!LevelPlacedAIFactionByClassName.Contains(ClassName))
			{
				LevelPlacedAIFactionByClassName.Add(ClassName, FactionTag);

				UE_LOG(LogTemp, Display,
					TEXT("[RoomSpawn][v56] 缓存关卡预放 AI 阵营: PawnClass='%s', FactionTag='%s'"),
					*ClassName, *FactionTag.ToString());
				++CachedCount;
			}
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("[RoomSpawn][v56] ScanAndCacheLevelPlacedAIFactions 完成: 扫描=%d, 缓存=%d"),
		TotalScanned, CachedCount);
}

/**
 * GetCachedLevelPlacedAIFaction — 查询关卡预放 AI 的阵营
 *
 * 优先级:
 *   1. LevelPlacedAIFactionByClassName 缓存 (SetupMeleeAI 调用前已扫描)
 *   2. Pawn->GetFactionTag() 直接读取 (兜底, 理论上应该已经缓存)
 *
 * @param AIPawnClass Pawn 类
 * @param AIPawn Pawn 实例
 * @return FactionTag (如果都获取不到则返回 Empty)
 */
FGameplayTag URoomSpawnSubsystem::GetCachedLevelPlacedAIFaction(
	TSubclassOf<AActor> AIPawnClass,
	ABaseCharacter* AIPawn) const
{
	// 优先从缓存获取
	if (AIPawnClass)
	{
		const FString ClassName = AIPawnClass->GetName();
		if (const FGameplayTag* CachedFaction = LevelPlacedAIFactionByClassName.Find(ClassName))
		{
			return *CachedFaction;
		}
	}

	// 兜底: 从 Pawn 实例直接获取
	if (AIPawn)
	{
		return AIPawn->GetFactionTag();
	}

	return FGameplayTag{};
}

/**
 * 【v39 P0 修复】按 Controller 精准释放出生点
 *
 * 大厂原则 - 集中调度:
 *   - 死亡链路唯一释放入口 (UCombatDeathComponent::ReleaseOccupiedSpawnPoint 调用)
 *   - 通过 OccupiedSpawnByController 反查 Controller 上次 Spawn 的 PlayerStart
 *   - 同时清 OccupiedSpawnPoints + OccupiedSpawnByController, 保持两表一致
 *
 * 与 ResetAllSpawnPointOccupancy 的区别:
 *   - ResetAllSpawnPointOccupancy: 清空所有出生点占用 (粗粒度, 多玩家同帧死亡会误清空)
 *   - ReleaseSpawnPointByController: 精准释放单个 Controller 的占用 (细粒度, 大厂首选)
 *
 * @param Controller 上次 Spawn 时的 PlayerController (通常是死亡 Pawn 的 Owner)
 */
void URoomSpawnSubsystem::ReleaseSpawnPointByController(AController* Controller)
{
	if (!Controller)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Spawn] ReleaseSpawnPointByController: Controller 为空. "
			     "【v39 零兜底】拒绝静默, 上游必须传有效 Controller."));
		return;
	}

	TWeakObjectPtr<AController> WeakController(Controller);
	TWeakObjectPtr<APlayerStart>* FoundSpawnPtr = OccupiedSpawnByController.Find(WeakController);
	if (!FoundSpawnPtr)
	{
		// 没记录 (没走过 Spawn) — 不是错误, 但 Log Verbose 便于诊断
		UE_LOG(LogTemp, Verbose,
			TEXT("[Spawn] ReleaseSpawnPointByController: Controller '%s' 无出生点占用记录 (没走过 Spawn?). "
			     "无需释放."),
			*Controller->GetName());
		return;
	}

	APlayerStart* PlayerStart = FoundSpawnPtr->Get();
	if (PlayerStart)
	{
		OccupiedSpawnPoints.Remove(PlayerStart);
		UE_LOG(LogTemp, Log,
			TEXT("[Spawn] ReleaseSpawnPointByController: 已释放 Controller '%s' 占用的出生点 '%s'. "
			     "【v39 P0】修复: 旧版 ReleaseSpawnPoint 0 调用 → 5 次后所有出生点被永久占用 → 玩家不复活."),
			*Controller->GetName(),
			*PlayerStart->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Spawn] ReleaseSpawnPointByController: Controller '%s' 记录的 PlayerStart 已失效 (被 GC 或 Destroy). "
			     "仅清理映射, 不动 OccupiedSpawnPoints."),
			*Controller->GetName());
	}

	OccupiedSpawnByController.Remove(WeakController);
}

void URoomSpawnSubsystem::ReleaseSpawnPoint(AActor* PlayerStart)
{
	if (!PlayerStart) return;
	if (APlayerStart* PS = Cast<APlayerStart>(PlayerStart))
	{
		if (OccupiedSpawnPoints.Contains(PS))
		{
			OccupiedSpawnPoints.Remove(PS);
		}
	}
}

void URoomSpawnSubsystem::ResetAllSpawnPointOccupancy()
{
	OccupiedSpawnPoints.Empty();
}


// ==========================================
// 【v54.3 大厂架构 — 武器 Class 翻译器】ResolveWeaponClassFromID
// ==========================================

/**
 * ResolveWeaponClassFromID — 把 WeaponID (FString) 翻译为 WeaponClass (TSubclassOf<ABaseWeapon>)
 *
 * 用户原话 2026.07.16:
 *   "DefaultWeaponRowName 属性为什么不直接引用 DT_WeaponInfo 表的 WeaponBlueprint 内容啊"
 *   → 大厅/玩家路径(UI 真理源是字符串)必须经此 helper 翻译, 不能再走 SpawnAndEquipWeapon 内部反查
 *
 * 数据流:
 *   WeaponID (来自 UI / SpawnRequest / CachedWeaponID)
 *   ↓
 *   DT_WeaponInfo[RowName=WeaponID].WeaponBlueprint (TSoftClassPtr<ABaseWeapon>)
 *   ↓
 *   LoadSynchronous() → WeaponClass (TSubclassOf<ABaseWeapon>)
 *
 * 大厂原则:
 *   - 单一反查入口: 全项目 WeaponID → WeaponClass 只此一处
 *   - DT 缺失 / Row 缺失 / Blueprint 缺失 → 三层全失败 Log Error + return nullptr
 *   - 不静默兜底 (不允许默认武器, 不允许 RowName 近似匹配)
 *   - 不允许缓存 (DT 是项目级真理源, 改后必须立即生效, 缓存会隐藏配置变更)
 *   - 允许返回 nullptr, 调用方负责处理"配置错" → Log Error + 拒绝 Spawn
 *
 * 调用方 (硬约束):
 *   - 必须检查返回值, nullptr 即放弃本次 Spawn
 *   - 不能 fallback 到任何"默认武器" (大厂零兜底)
 */
TSubclassOf<ABaseWeapon> URoomSpawnSubsystem::ResolveWeaponClassFromID(const FString& WeaponID) const
{
	// 零兜底: WeaponID 必须非空
	if (WeaponID.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomSpawn] ResolveWeaponClassFromID: WeaponID 为空. "
				 "【v54.3 零兜底】拒绝翻译. 调用方传空 = 配置错."));
		return nullptr;
	}

	// 零兜底: DT_WeaponInfo 必须配
	if (!WeaponDataTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomSpawn] ResolveWeaponClassFromID: WeaponDataTable 未设置. "
				 "【v54.3 零兜底】无法反查. "
				 "修复: UE 编辑器 → GM_RoomGameMode → ClassDefaults → WeaponDataTable 必须配 DT_WeaponInfo 资产."));
		return nullptr;
	}

	// 查 DT (UE 5.6: FindRow 与上下文)
	static const FString Context(TEXT("WeaponClassResolve"));
	FWeaponInfo* Row = WeaponDataTable->FindRow<FWeaponInfo>(FName(*WeaponID), Context);
	if (!Row)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomSpawn] ResolveWeaponClassFromID: DT_WeaponInfo 中找不到 RowName='%s'. "
				 "【v54.3 零兜底】字符串拼写错永远不可见, 现在强制显式. "
				 "修复: 打开 DT_WeaponInfo → 添加 Row 或修正字符串拼写."),
			*WeaponID);
		return nullptr;
	}

	// 零兜底: Row.WeaponBlueprint 必须配
	if (Row->WeaponBlueprint.IsNull())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomSpawn] ResolveWeaponClassFromID: DT_WeaponInfo.Row[%s].WeaponBlueprint 为空. "
				 "【v54.3 零兜底】DT 配置不完整. "
				 "修复: 打开 DT_WeaponInfo → Row[%s] → WeaponBlueprint 字段必须配 BP_Weapon_*.uasset."),
			*WeaponID, *WeaponID);
		return nullptr;
	}

	// LoadSynchronous: 强类型 Class
	TSubclassOf<ABaseWeapon> WeaponClass = Row->WeaponBlueprint.LoadSynchronous();
	if (!WeaponClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomSpawn] ResolveWeaponClassFromID: DT_WeaponInfo.Row[%s].WeaponBlueprint.LoadSynchronous() 失败. "
				 "【v54.3 零兜底】资产可能被删除或路径错. "
				 "修复: 重新配 DT_WeaponInfo.Row[%s].WeaponBlueprint."),
			*WeaponID, *WeaponID);
		return nullptr;
	}

	return WeaponClass;
}

// ==========================================
// AI 大厅入队 (v28)
// ==========================================

int32 URoomSpawnSubsystem::QueueAIForBattleSpawn(const FAISpawnRequest& Request)
{
	if (Request.Count <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[QueueAIForBattleSpawn] Count=%d 非法, 拒绝入队."), Request.Count);
		return 0;
	}

	if (Request.Mode == ERoomMatchMode::None)
	{
		UE_LOG(LogTemp, Error, TEXT("[QueueAIForBattleSpawn] Mode=None 非法, 拒绝入队."));
		return 0;
	}

	if (!FFactionTags::IsValidFaction(Request.FactionTag))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[QueueAIForBattleSpawn] FactionTag='%s' 非有效阵营, 拒绝入队."),
			*Request.FactionTag.ToString());
		return 0;
	}

	// 【v51 大厂架构 — 零兜底】AIPawnClass 校验
	//
	// 真理源 (大厂原则):
	//   - AIPawnClass 必须由 UI 阶段反查 DT_CharacterInfo 拿到 (Class 强类型)
	//   - 不允许 SpawnAIInternal 内部再反查 (那是 v50 之前的反模式)
	//   - 调用方传 nullptr → Log Error + 拒绝入队 (强制修复 RoomService::RequestAddAI)
	if (!Request.AIPawnClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[QueueAIForBattleSpawn] Request.AIPawnClass 为空, 拒绝入队. "
			     "【v51 零兜底】UI 必须反查 DT_CharacterInfo.CharacterBlueprint 拿到 PawnClass, 然后传入 Request.AIPawnClass. "
			     "【修复路径】检查 URoomService::RequestAddAI 链路, 必须填入 Request.AIPawnClass."));
		return 0;
	}

	for (int32 i = 0; i < Request.Count; ++i)
	{
		FPendingAIEntry Entry;
		// 【v49 大厂架构】DisplayName 格式: CharacterInfoRowName_WeaponID
		// 例: "MeleeGruntAI001_WQ001"
		Entry.DisplayName = FString::Printf(TEXT("%s_%s"),
			*Request.CharacterInfoRowName.ToString(),
			*Request.WeaponID);
		Entry.FactionTag = Request.FactionTag;
		// 【v54 大厂架构重构】ProfileTag 已删除, 不再写入 (UFUNCTION 不存在)
		Entry.CharacterInfoRowName = Request.CharacterInfoRowName;
		Entry.AIPawnClass = Request.AIPawnClass;  // 【v51 新增】真理源 (Class 强类型)
		Entry.WeaponID = Request.WeaponID;
		Entry.Mode = Request.Mode;
		Entry.SequenceID = AllocatePendingAISequenceID();
		// 【v55.1 大厂架构修复】Config 必须透传 (SpawnInvincibilitySeconds 等真理源)
		Entry.Config = Request.Config;
		PendingAIQueue.Add(Entry);

		// 【v46 大厂架构修复 P0】同步到 GameState (Replicated → 客户端可见)
		if (ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>())
		{
			GS->ReplicatedPendingAIQueue = PendingAIQueue;
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[QueueAIForBattleSpawn] 成功入队 %d 个 AI → Faction='%s' Mode=%d"),
		Request.Count, *Request.FactionTag.ToString(), (int32)Request.Mode);

	return Request.Count;
}

TArray<FPendingAIEntry> URoomSpawnSubsystem::GetPendingAIInFaction(FGameplayTag FactionTag) const
{
	if (!FFactionTags::IsValidFaction(FactionTag))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GetPendingAIInFaction] FactionTag='%s' 非有效阵营, 返回空数组"),
			*FactionTag.ToString());
		return TArray<FPendingAIEntry>();
	}

	return PendingAIQueue.FilterByPredicate([FactionTag](const FPendingAIEntry& Entry)
	{
		return Entry.FactionTag == FactionTag;
	});
}

bool URoomSpawnSubsystem::IsPendingAIByName(const FString& DisplayName) const
{
	if (DisplayName.IsEmpty()) return false;
	return PendingAIQueue.ContainsByPredicate([DisplayName](const FPendingAIEntry& Entry)
	{
		return Entry.DisplayName == DisplayName;
	});
}

bool URoomSpawnSubsystem::RemovePendingAIByName(const FString& DisplayName)
{
	if (DisplayName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[RemovePendingAIByName] DisplayName 为空, 拒绝删除"));
		return false;
	}

	// 【v56.4 大厂架构修复】只删除第一个匹配项（按入队顺序）
	// 
	// 根因 (Bug): QueueAIForBattleSpawn 入队时 DisplayName = CharacterRowName_WeaponID（不含 SequenceID）
	//              一次性添加 3 个相同的 AI，DisplayName 都是 "AI001_WQ002"
	//              RemoveAll 会删除所有匹配项 → 踢一个全踢
	//
	// 修复: 改为只删除第一个匹配项，与 UI "点击哪个踢哪个" 一一对应
	//        大厂原则 - 单一职责: UI 传 DisplayName → 后端按顺序删第一个匹配的
	//        这样即使 DisplayName 重复，也能正确删除用户点击的那个
	for (int32 i = 0; i < PendingAIQueue.Num(); ++i)
	{
		if (PendingAIQueue[i].DisplayName == DisplayName)
		{
			const int32 SequenceID = PendingAIQueue[i].SequenceID;
			PendingAIQueue.RemoveAt(i);
			UE_LOG(LogTemp, Log, TEXT("[RemovePendingAIByName] 成功移除 1 条: DisplayName='%s' SequenceID=%d"),
				*DisplayName, SequenceID);

			// 【v46 大厂架构修复 P0】同步到 GameState (Replicated → 客户端可见)
			if (ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>())
			{
				GS->ReplicatedPendingAIQueue = PendingAIQueue;
			}
			return true;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[RemovePendingAIByName] 未找到 DisplayName='%s' 的 PendingAI 条目"), *DisplayName);
	return false;
}

FAISpawnRequest URoomSpawnSubsystem::BuildSpawnRequestFromPending(const FPendingAIEntry& Entry) const
{
	FAISpawnRequest Request;
	Request.FactionTag = Entry.FactionTag;
	// 【v54 大厂架构重构】ProfileTag 已删除, 不再写入 (字段不存在)
	Request.CharacterInfoRowName = Entry.CharacterInfoRowName;  // 诊断字段 (RowName)
	Request.AIPawnClass = Entry.AIPawnClass;  // 【v51 真理源】Class 强类型, SpawnAIInternal 直接 Spawn
	Request.WeaponID = Entry.WeaponID;
	Request.Mode = Entry.Mode;
	Request.Count = 1;
	Request.bUseTeamSpawnPoint = true;
	// 【v55.1 大厂架构修复】Config 必须透传 (SpawnInvincibilitySeconds 等真理源)
	Request.Config = Entry.Config;
	return Request;
}

TArray<FAISpawnRequest> URoomSpawnSubsystem::ConsumePendingAIForBattleSpawn()
{
	TArray<FAISpawnRequest> Result;
	Result.Reserve(PendingAIQueue.Num());

	for (const FPendingAIEntry& Entry : PendingAIQueue)
	{
		Result.Add(BuildSpawnRequestFromPending(Entry));
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[ConsumePendingAIForBattleSpawn] 消费 %d 个 PendingAI, 即将 Spawn"),
		Result.Num());

	PendingAIQueue.Empty(); // 消费即清空

	// 【v46 大厂架构修复 P0】清空后同步到 GameState (Replicated → 客户端可见)
	if (ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>())
	{
		GS->ReplicatedPendingAIQueue = PendingAIQueue;
	}

	return Result;
}

// ==========================================
// 【v54 大厂架构重构 — 删除】Profile 解析
// ==========================================
//
// 历史 (v49-v53):
//   - ResolveProfileExact(Mode, ProfileTag) 按 Mode + Tag 精确查 Profile
//   - ResolveProfileByTag(Mode, ProfileTag) 是 ResolveProfileExact 的别名
//   - 内部走 ProfilesByMode[Mode].Profiles[ProfileTag].LoadSynchronous()
//   - 大厂原则: 单一真理源入口 (替代 v50 之前的 ResolveProfileFromPawnClass 反查)
//
// v54 重构 (用户决策 2026.07.16):
//   - UAIProfileAsset 整个类已删除, 这些函数整体不再有意义
//   - FAIProfileRegistry 已删除, ProfilesByMode 字段已删除
//   - 关卡预放 AI 直接从 ConfigSO (DA_AIBehaviorConfig_*.uasset) 读默认武器/AIController
//   - 大厅入队 AI 走 Request (UI 直接传武器/AIController), 不需要反查
//   - 这两个函数整个删除 — 大厂原则: 删除中间层 = 删除反查链
//
// 如果有旧代码调本函数, 必须改成调:
//   - 关卡预放 AI: ABaseAIController::GetConfig() (BaseAIController.h)
//   - 大厅入队 AI: RoomSpawnSubsystem::SpawnAIInternal(Request, Config) — Config 来自 AIC.GetConfig()
//
// 【v51 大厂重构 — 已删除】ResolveProfileFromPawnClass 函数已被完全删除
//
// 删除原因 (单一真理源 + 零重复):
//   - 旧路径: SpawnAIInternal 走本函数反查 (按 PawnClass 遍历 Profiles)
//   - 这是完全冗余的反查 — UI 已经知道选了哪个 Profile, 不应该再绕一圈反查
//   - 新路径: SpawnAIInternal 调 ResolveProfileByTag (按 Request.ProfileTag 直接查)
//
// 如果有旧代码调本函数, 必须改成调 ResolveProfileByTag.

	FString URoomSpawnSubsystem::DumpModeRulesKeys() const
	{
		FString Result;
		for (const auto& Pair : ModeRulesByMode)
		{
			if (!Result.IsEmpty()) Result += TEXT(", ");
			Result += FString::Printf(TEXT("Mode=%d"), (int32)Pair.Key);
		}
		return Result.IsEmpty() ? TEXT("(空)") : Result;
	}

bool URoomSpawnSubsystem::GetModeRules(ERoomMatchMode Mode, FAIModeRules& OutRules) const
{
	// 【v56 诊断日志】追踪 ModeRules 是否正确注入
	UE_LOG(LogTemp, Display,
		TEXT("[RoomSpawn][GetModeRules] 请求 Mode=%d, ModeRulesByMode.Num()=%d, Keys=[%s]"),
		(int32)Mode, ModeRulesByMode.Num(), *DumpModeRulesKeys());

	if (const FAIModeRules* Found = ModeRulesByMode.Find(Mode))
	{
		UE_LOG(LogTemp, Display,
			TEXT("[RoomSpawn][GetModeRules] 命中! Mode=%d, BehaviorTree=%s, AIControllerClass=%s"),
			(int32)Mode, *GetNameSafe(Found->BehaviorTree.Get()), *GetNameSafe(Found->AIControllerClass));
		OutRules = *Found;
		return true;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[RoomSpawn][GetModeRules] 未找到 Mode=%d 的配置. 已配置的 Mode: [%s]"),
		(int32)Mode, *DumpModeRulesKeys());
	return false;
}

// ==========================================
// 玩家生成数据缓存
// ==========================================

bool URoomSpawnSubsystem::GetPlayerSpawnData(uint32 ControllerUniqueID, FString& OutCharID, FString& OutWeaponID) const
{
	if (const FPlayerSpawnData* Cached = PlayerSpawnDataCache.Find(ControllerUniqueID))
	{
		OutCharID = Cached->CharID;
		// 【v52 P0】保持向后兼容: 老接口返回主武器 (Slot 1)
		OutWeaponID = Cached->WeaponPrimaryID;
		return true;
	}
	return false;
}

bool URoomSpawnSubsystem::GetPlayerSpawnDataAllWeapons(uint32 ControllerUniqueID, FString& OutCharID, FString& OutPrimaryID, FString& OutSecondaryID, FString& OutMeleeID) const
{
	if (const FPlayerSpawnData* Cached = PlayerSpawnDataCache.Find(ControllerUniqueID))
	{
		OutCharID = Cached->CharID;
		OutPrimaryID = Cached->WeaponPrimaryID;
		OutSecondaryID = Cached->WeaponSecondaryID;
		OutMeleeID = Cached->WeaponMeleeID;
		return true;
	}
	return false;
}

void URoomSpawnSubsystem::SetPlayerSpawnData(uint32 ControllerUniqueID, const FString& CharID, const FString& PrimaryWeaponID, const FString& SecondaryWeaponID, const FString& MeleeWeaponID)
{
	// 【v52 P0】3 把武器一起存, 主+副+近战
	PlayerSpawnDataCache.FindOrAdd(ControllerUniqueID) = FPlayerSpawnData{CharID, PrimaryWeaponID, SecondaryWeaponID, MeleeWeaponID};
}

/**
 * SpawnAIInternal - v55 大厂重构 (Controller 获取链路零兜底)
 *
 * 业务流 (单一真理源 + 零兜底):
 *   1. 校验 Request.AIPawnClass 非空 (v51: UI 反查时已拿 Class 强类型)
 *   2. 校验 Request.FactionTag (阵营)
 *   3. 校验 Config (预放 AI 必传, 大厅 AI 为空)
 *   4. 决定 ControllerClass (【v55 重构】预放: ConfigSO / 大厅: ModeRules — 完全分离)
 *   5. 循环 Spawn: 出生点 → SpawnController → SpawnPawn → SetSpawnLoadout → Possess
 *   6. SetGenericTeamId → InitializeFromConfig → RequestWeaponSpawn → ApplyCharacterConfig → ActivateSpawnInvincibility
 *
 * 【v55 大厂架构重构 — Controller 获取链路分离】
 *   - 预放 AI (Config != nullptr): ConfigSO.LevelPlacedAIControllerClass (唯一)
 *   - 大厅 AI (Config == nullptr): ModeRulesByMode[Mode].AIControllerClass (唯一)
 *
 * 【v55 删除的旧设计】(违反零兜底原则):
 *   - Request.AIControllerClass (UI 显式传入已废弃)
 *   - PawnCDO->AIControllerClass (UE 标准声明是偏好, 不是业务配置)
 *   - GM.DefaultControllerClass (全局默认值已删除)
 *
 * 【v54 大厂架构重构 — UAIProfileAsset 删除】
 *   - 删除所有 Profile 字段读写 (ProfileTag / PawnClass / FactionTag)
 *   - 删除 ResolveProfileByTag / ResolveProfileExact 整个链路
 *
 * 大厂原则:
 *   - 单一真理源: AI Pawn Class 来自 Request.AIPawnClass
 *   - 单一真理源: 阵营来自 Request.FactionTag (大厅 AI) / Pawn.FactionTag (预放 AI)
 *   - 零兜底: 任何字段为空 → Log Error + return 0, 拒绝静默
 *
 * @param Request AI Spawn 请求 (AIPawnClass / WeaponID / FactionTag / Mode 等)
 * @param Config 预放 AI 时传入 ConfigSO (BaseAIController.GetConfig()), 大厅 AI 为 nullptr
 * @param OptionalExistingController 复活场景下复用的 Controller (nullptr = 新建)
 * @return 实际生成数 (失败返回 0, 已 Log Error)
 */
int32 URoomSpawnSubsystem::SpawnAIInternal(const FAISpawnRequest& Request, UAIBehaviorConfigSO* Config, AAIController* OptionalExistingController)
{
	// ============================================================
	// 【v51 大厂架构重构 — 单一真理源】AI PawnClass 校验
	//
	// 真理源: Request.AIPawnClass (UI 反查时拿到, 不在 Spawn 链路重复反查)
	// ============================================================
	if (!Request.AIPawnClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomSpawn] SpawnAIInternal: Request.AIPawnClass 为空, 拒绝 Spawn. "
			     "【v51 修复】UI ComboBox_AICharacter 选中的角色必须反查 DT_CharacterInfo.CharacterBlueprint 拿到 PawnClass, 然后传入 Request.AIPawnClass. "
			     "【修复路径】检查 URoomService::RequestAddAI 的反查逻辑是否成功填入 Request.AIPawnClass."));
		return 0;
	}

	// ============================================================
	// 【v54 大厂架构重构 — 单一真理源】Config 校验
	//
	// 关卡预放 AI: 必须传 Config (BaseAIController.GetConfig())
	// 大厅入队 AI: Config 可以为空 (走 Request 直接传武器/AIController)
	//   - 这种情况下 Request.AIPawnClass 已经是真理源, 不需要 Config 派生
	//
	// 大厂原则 (零兜底):
	//   - 不允许"Profile 找不到 → 用 default Profile" 这种跨层级兜底
	//   - Config 为空是合法的 (大厅路径), 调用方必须明确传入空指针表示"走 Request 路径"
	// ============================================================
	if (!Config)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[RoomSpawn] SpawnAIInternal: Config 为空 (大厅入队 AI 路径). 直接走 Request.AIPawnClass + Request.WeaponID + Request.FactionTag. "
			     "【v54 大厂架构】UAIProfileAsset 已删除, 不需要 Config 派生. "
			     "【v51 真理源】Request 直接携带所有必要的 Spawn 参数."));
	}

	// 3. 阵营 (零兜底): Request.FactionTag 必须有效
	const FGameplayTag DesiredFaction = Request.FactionTag;
	if (!DesiredFaction.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomSpawn] SpawnAIInternal: Request.FactionTag 无效 (空). 拒绝 Spawn. "
			     "【修复】调用方需用 ModeRules[Mode].AttackTeamFaction/DefenseTeamFaction 填 Request.FactionTag."));
		return 0;
	}

	// 4. 循环 Spawn
	int32 SpawnedCount = 0;
	for (int32 i = 0; i < Request.Count; ++i)
	{
		const int32 CurrentID = AllocateAINextID();
		// 【v53 大厂架构 — Profile 字段瘦身】AIName 命名规则不再读 Profile.DisplayName (字段已删除)
		//   旧 (v50): "AI_%s_%d" 用 Profile.DisplayName (策划填的展示名, 例如 "近战肉搏怪")
		//   新 (v53): 用 Request.AIPawnClass 的类名 (BP_GruntAI_C → GruntAI) 作后缀, 简洁且真理源唯一
		//   大厂原则: 不依赖 Profile.DisplayName (策划字段, 可空, 不可作为命名真理源)
		const FString AIPawnClassName = Request.AIPawnClass
			? Request.AIPawnClass->GetName().Replace(TEXT("BP_"), TEXT("")).Replace(TEXT("_C"), TEXT(""))
			: FString(TEXT("Unknown"));
		const FString AIName = FString::Printf(
			TEXT("AI_%s_%d"),
			*AIPawnClassName,
			CurrentID);

		// 【v55.1 大厂架构修复】Controller 获取 — 修正分支逻辑
		//
		// 旧设计 (bug):
		//   - if (Config) → 用 Config->LevelPlacedAIControllerClass
		//   - 大厅 AI 现在 Config != nullptr → 错误地走了 Config->LevelPlacedAIControllerClass (空!)
		//
		// 新设计 (v55.1):
		//   - 优先用 ModeRules.AIControllerClass (大厅 AI 用这个)
		//   - 关卡预放 AI: ModeRules 查不到, 才用 Config->LevelPlacedAIControllerClass
		TSubclassOf<AAIController> ControllerClass = nullptr;
		FAIModeRules ModeRulesFound;
		const bool bGotModeRules = GetModeRules(Request.Mode, ModeRulesFound);

		// 【v56 诊断日志】打印 ModeRulesFound 的完整状态
		UE_LOG(LogTemp, Display,
			TEXT("[RoomSpawn][v56-Diag] ModeRulesFound 状态: Mode=%d, bGotModeRules=%d, "
			     "BehaviorTree.IsNull=%d, BehaviorTree.Get()=%s, "
			     "AIControllerClass=%s"),
			(int32)Request.Mode, bGotModeRules ? 1 : 0,
			ModeRulesFound.BehaviorTree.IsNull() ? 1 : 0,
			*GetNameSafe(ModeRulesFound.BehaviorTree.Get()),
			*GetNameSafe(ModeRulesFound.AIControllerClass));

		if (bGotModeRules && ModeRulesFound.AIControllerClass)
		{
			// 【大厅 AI 路径】用 ModeRules.AIControllerClass
			ControllerClass = ModeRulesFound.AIControllerClass;
		}
		else if (Config && Config->LevelPlacedAIControllerClass)
		{
			// 【关卡预放 AI 路径】用 ConfigSO.LevelPlacedAIControllerClass
			ControllerClass = Config->LevelPlacedAIControllerClass;
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomSpawn] SpawnAIInternal: 无法派生 AIControllerClass. "
					 "bGotModeRules=%d, ModeRulesFound.AIControllerClass=%s, Config=%s, Config->LevelPlacedAIControllerClass=%s. "
					 "【v55.1 零兜底】拒绝 Spawn. "
					 "修复: 大厅 AI → GM_ModeRulesByMode.Melee.AIControllerClass 配 BP_MeleeAIController_C; "
					 "关卡预放 AI → DA_AIBehaviorConfig_.LevelPlacedAIControllerClass 配."),
				bGotModeRules ? 1 : 0,
				*GetNameSafe(ModeRulesFound.AIControllerClass),
				*GetNameSafe(Config),
				*GetNameSafe(Config ? Config->LevelPlacedAIControllerClass : nullptr));
			return SpawnedCount;
		}

		// 4a. 出生点分配
		FVector SpawnLoc = FVector::ZeroVector;
		FRotator SpawnRot = FRotator::ZeroRotator;
		if (Request.bUseTeamSpawnPoint)
		{
			ScanAndCachePlayerStarts(false);
			// 【v43 修复】必须传 OccupancyOwner (AI Controller), 否则 ReleaseSpawnPointByController 找不到记录
			// 根因: AI 复活时占用出生点但没记录 OccupancyOwner → ReleaseOccupiedSpawnPoint 找不到记录
			//       → 出生点永远不释放 → 5 次后全部占用 → AI 无法复活
			// 注意: AIC 此时还未声明，用 OptionalExistingController（复活时非空，首次 Spawn 时为空让系统分配）
			AActor* SpawnPt = GetAvailableSpawnPointForFaction(DesiredFaction, true, OptionalExistingController);
			if (!SpawnPt)
			{
				// 【v43 零兜底】出生点分配失败，禁止在 ZeroVector 生成（会导致碰撞失败）
				UE_LOG(LogTemp, Error,
					TEXT("[RoomSpawn] GetAvailableSpawnPointForFaction 返回 nullptr，出生点全部被占用。"
						 " Faction=%s. 终止 Spawn。"),
					*DesiredFaction.ToString());
				return SpawnedCount; // 终止整个 Spawn 流程
			}
			SpawnLoc = SpawnPt->GetActorLocation();
			SpawnRot = SpawnPt->GetActorRotation();
		}

		// 4b. Spawn Controller (复用 OptionalExistingController if provided, 用于复活场景)
		AAIController* AIC = OptionalExistingController;
		if (!AIC)
		{
			FActorSpawnParameters SP;
			SP.Owner = GetWorld()->GetAuthGameMode();
			SP.Name = FName(*FString::Printf(TEXT("AIC_%s"), *AIName));

			AIC = GetWorld()->SpawnActor<AAIController>(
				ControllerClass, SpawnLoc, SpawnRot, SP);

			if (!AIC)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomSpawn] SpawnAIController 失败 (Class=%s)"), *GetNameSafe(ControllerClass));
				continue;
			}
		}

		// 4c. Spawn Pawn + Possess
		FActorSpawnParameters PawnSP;
		PawnSP.Owner = AIC;
		PawnSP.Instigator = nullptr;

		ABaseCharacter* AIPawn = GetWorld()->SpawnActor<ABaseCharacter>(
			Request.AIPawnClass, SpawnLoc, SpawnRot, PawnSP);

		if (!AIPawn)
		{
			UE_LOG(LogTemp, Error, TEXT("[RoomSpawn] Spawn AIPawn '%s' 失败"), *Request.AIPawnClass->GetName());
			if (!OptionalExistingController) // 不销毁复用 Controller
			{
				AIC->Destroy();
			}
			continue;
		}

		// ============================================================
		// 【v49 大厂架构重构 — 武器 ID 单一真理源】
		//
		// 旧 (v47): Profile.WeaponID 是"AI 类型默认武器", 既给关卡预放 AI 用, 又给大厅 AI 用
		//         → 两者语义不同, 但代码没区分
		//
		// 新 (v49): 严格按调用方分类
		//   - 大厅入队 AI (走 SpawnAIInternal): 武器 ID 由 Request.WeaponID 决定 (UI ComboBox 选择)
		//   - 关卡预放 AI (走 AMeleeAIController::SetupMeleeAI): 武器 ID 由 Profile.WeaponID 决定
		//   - 这是两条完全独立的链路, 不允许交叉兜底
		//
		// 大厂原则 (零兜底):
		//   - Request.WeaponID 必非空 (UI 强制选武器)
		//   - 空 → Log Error + 拒绝 Spawn (强制修复 UI 配置)
		//   - 不再 fallback 到 Profile.WeaponID (那是关卡预放 AI 的字段)
		// ============================================================
		FString DesiredCharID = Request.CharacterInfoRowName.ToString();  // 【v49】不再用 Profile.CharacterRowName
		FString DesiredWeaponID = Request.WeaponID;

		if (DesiredWeaponID.IsEmpty())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomSpawn] SpawnAIInternal: Request.WeaponID 为空. AI '%s' (Pawn=%s) 拒绝 Spawn. "
				     "【v49 修复】UI ComboBox_AIWeapon 必须选武器 — 强制选武器, 不再走 Profile.WeaponID 兜底 (那是关卡预放 AI 用的)."),
				*AIName, *AIPawn->GetName());
			AIPawn->Destroy();
			if (!OptionalExistingController) AIC->Destroy();
			continue;
		}

		AIPawn->SetSpawnLoadout(DesiredCharID, DesiredWeaponID);

		AIC->Possess(AIPawn);

		// 4d. Faction Tag
		//   v31.5 大厂重构: 走 FFactionTags::ToGenericTeamId 统一转换 (单一真理源)
		//   旧调用 ResolveGenericTeamIdFromTag 已被删除 (P0 2026.07.10 重构)
		AIPawn->SetGenericTeamId(FFactionTags::ToGenericTeamId(DesiredFaction));

		// 4e. InitializeFromConfig (感知 + BT + 阵营协议)
		//
		// 【v54.4 大厂架构重构】BT 来源分层:
		//   - 大厅 AI: Config 为空 → 从 ModeRules[Request.Mode].BehaviorTree 拿 BT
		//   - 关卡预放 AI: Config 非空 → BT 走 ConfigSO.LevelPlacedBehaviorTree (InitializeFromConfig 内部读)
		//
		// 【v55.1 大厂架构修复 P0】大厅 AI BehaviorTree 来源重构
		//
		// 旧设计 (v54.4 bug):
		//   - 大厅 AI: Config != null → BehaviorTreeOverride=nullptr → 读 ConfigSO.LevelPlacedBehaviorTree
		//   - 问题: ConfigSO.LevelPlacedBehaviorTree 是空的 (用户没配) → 拒绝 Spawn
		//
		// 新设计 (v55.1):
		//   - 大厅 AI (bGotModeRules=true): 从 ModeRules.BehaviorTree 拿 BT
		//   - 关卡预放 AI (bGotModeRules=false): LobbyBehaviorTree=null → InitializeFromConfig 内部读 ConfigSO.LevelPlacedBehaviorTree
		//
		// 真理源 (按来源分层):
		//   1. 大厅 AI: ModeRules.BehaviorTree (用户已配)
		//   2. 关卡预放 AI: ConfigSO.LevelPlacedBehaviorTree (关卡专属)
		UBehaviorTree* LobbyBehaviorTree = nullptr;
		if (bGotModeRules)
		{
			// 【大厅 AI 路径】从 ModeRules.BehaviorTree 拿 BT
			LobbyBehaviorTree = ModeRulesFound.BehaviorTree.Get();

			// 【v56 修复】TSoftObjectPtr.Get() 可能返回 None (软引用未解析)
			// Fallback 到 ConfigSO->LevelPlacedBehaviorTree
			if (!LobbyBehaviorTree && Config)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[RoomSpawn] ModeRules.BehaviorTree.Get()=None, Fallback 到 ConfigSO->LevelPlacedBehaviorTree. Config=%s"),
					*GetNameSafe(Config));
				LobbyBehaviorTree = Config->LevelPlacedBehaviorTree.LoadSynchronous();
			}

			if (!LobbyBehaviorTree)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomSpawn] SpawnAIInternal: 大厅 AI 路径, Mode=%d 的 ModeRules.BehaviorTree 为空且 ConfigSO->LevelPlacedBehaviorTree 也为空. "
						 "【v56 零兜底】拒绝 Spawn. "
						 "修复: 打开 GM_RoomGameMode → Class Defaults → ModeRulesByMode → Melee → BehaviorTree 拖入 BT_MeleeAI.uasset."),
					static_cast<int32>(Request.Mode));
				AIPawn->Destroy();
				if (!OptionalExistingController) AIC->Destroy();
				continue;
			}
		}
		// else: 关卡预放 AI 路径, LobbyBehaviorTree=nullptr → InitializeFromConfig 读 ConfigSO.LevelPlacedBehaviorTree

		// ============================================================
		// 【v55.2 大厂架构修复 P0】时序修正
		//
		// 旧设计 (bug): InitializeFromConfig 在 SetCachedFactionTag 之前调用
		//   → InitializeFromConfig 内读 CachedFactionTag 为空 → Error
		//   → SetCachedFactionTag 在 InitializeFromConfig 之后才写
		//
		// 新设计 (v55.2): 先写 CachedFactionTag, 再调 InitializeFromConfig
		//   → InitializeFromConfig 内读 CachedFactionTag 已有值 → 正确设置阵营
		// ============================================================

		// 4e.5. 【v53 大厂架构 — 运行时真理源缓存】先写入 CachedFactionTag
		if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
		{
			BaseAIC->SetCachedAIPawnClass(Request.AIPawnClass);
			BaseAIC->SetCachedWeaponID(Request.WeaponID);
			BaseAIC->SetCachedFactionTag(Request.FactionTag); // ← 先写, InitializeFromConfig 会读这个
			BaseAIC->SetCachedIsMother(false); // 【v109.1 大厂架构】新 Spawn 的 AI 初始为非母体
			BaseAIC->InitializeFromConfig(Config, LobbyBehaviorTree);

			// 【v54.3 大厂重构 — Class 强类型真理源】同步 CachedWeaponClass
			TSubclassOf<ABaseWeapon> ResolvedClass = ResolveWeaponClassFromID(Request.WeaponID);
			if (ResolvedClass)
			{
				BaseAIC->SetCachedWeaponClass(ResolvedClass);
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomSpawn] SpawnAIInternal: AI='%s' 的 Request.WeaponID='%s' 反查失败. "
					     "【v54.3】CachedWeaponClass 留空. 复活路径将无法生成武器. "
					     "修复: 检查 GM_RoomGameMode.ClassDefaults.WeaponDataTable 配置."),
					*AIPawn->GetName(), *Request.WeaponID);
			}
		}

		// 4f. 【v56.3 大厂架构修复】武器 Spawn — 大厅 AI 走这里，关卡预放 AI 走 AIController::SetupMeleeAI
		//
		// 根因 (Bug 2 v56.2 修复后仍失败): SetupMeleeAI 先调 SetMeleeConfig → 武器已生成 (WQ001)
		//              SpawnAIInternal 读 Request.WeaponID = WQ002 ✓
		//              但 RequestWeaponSpawn 里有幂等检查: 已有武器就跳过
		//              → WQ002 永远不生效
		//
		// 修复: 大厅 AI 路径在 RequestWeaponSpawn 之前先销毁旧武器
		if (bGotModeRules)
		{
			// 【v56.3 P0】先销毁旧武器，再生成新武器
			if (UWeaponAttachmentComponent* WeaponAttach = AIPawn->ResolveWeaponAttach())
			{
				ABaseWeapon* OldWeapon = WeaponAttach->GetCurrentWeapon();
				if (OldWeapon && OldWeapon->IsValidLowLevel())
				{
					UE_LOG(LogTemp, Log,
						TEXT("[RoomSpawn] SpawnAIInternal 销毁旧武器: %s (AI=%s, 即将换新武器)"),
						*OldWeapon->GetName(), *AIPawn->GetName());
					OldWeapon->Destroy();
				}
			}

			// 【v56.2】直接用 Request.WeaponID
			const FString UIWeaponID = Request.WeaponID;
			if (!UIWeaponID.IsEmpty())
			{
				TSubclassOf<ABaseWeapon> WeaponClass = ResolveWeaponClassFromID(UIWeaponID);
				if (WeaponClass)
				{
					UE_LOG(LogTemp, Log,
						TEXT("[RoomSpawn] SpawnAIInternal 触发武器 Spawn (UI 选择): WeaponID=%s → WeaponClass=%s (AI=%s, AIC=%s)"),
						*UIWeaponID, *WeaponClass->GetName(), *AIPawn->GetName(), *AIC->GetName());
					AIPawn->RequestWeaponSpawn(WeaponClass);
				}
				else
				{
					UE_LOG(LogTemp, Error,
						TEXT("[RoomSpawn] SpawnAIInternal: AI '%s' 的 WeaponID='%s' 无法反查为 WeaponClass. "
							 "【v56.3 零兜底】DT_WeaponInfo 缺失或 Row 配置错. "
							 "修复: 1) GM_RoomGameMode.ClassDefaults.WeaponDataTable 必须配 DT_WeaponInfo; "
							 "2) DT_WeaponInfo 里有 RowName='%s' 的行"),
						*AIPawn->GetName(), *UIWeaponID, *UIWeaponID);
				}
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomSpawn] SpawnAIInternal: AI '%s' 的 Request.WeaponID 为空. "
						 "【v56.3 零兜底】拒绝 Spawn 武器."),
					*AIPawn->GetName());
			}
		}
		// else: 关卡预放 AI 路径 → 走 AMeleeAIController::SetupMeleeAI (内部调 RequestWeaponSpawn)

		// 【v41 大厂架构】应用角色战斗参数配置
		// 【v133.4 真理源分离】AI 路径改调 ApplyAICharacterConfigToCharacter (读 ConfigSO)
		// 旧版调 ApplyCharacterConfigToCharacter 会用 PlayerConfigAsset, 真理源混淆 → 改 ConfigSO 加字段无意义
		ApplyAICharacterConfigToCharacter(AIPawn);

		// 【v45 大厂架构修复】AI 复活无敌期激活
		//
		// 根因: 旧版 SpawnAIInternal 完全没调 ActivateSpawnInvincibility
		//   → AI Pawn 生成后没有 bIsInvincible=true → HealthComponent 没激活无敌期
		//   → FlickerComponent 永远收不到 OnInvincibilityChanged(true) → 不闪烁
		//   → 玩家路径 (HandlePlayerRequestSpawn Step 7) 有这行, AI 路径漏了
		//
		// 修复: 在 AI Pawn Spawn 成功后立即调 ActivateSpawnInvincibility
		// - 与玩家路径对称: HandlePlayerRequestSpawn Step 7 同样调 ActivateSpawnInvincibility
		// - 时序: SpawnActor ✓ → Possess ✓ → InitializeFromProfile ✓ → RequestWeaponSpawn ✓ → ApplyCharacterConfig ✓ → ActivateInvincibility ✓
		// 【v54.2 大厂架构重构 — 统一真理源入口】AI 复活无敌期激活
		//
		// 用户原话 2026.07.16:
		//   "这个字段应该是所有ai的复活无敌期, 不管是否预放还是生成"
		//   → 所有 AI 路径 (关卡预放 / 大厅入队 / 复活) 走同一个真理源 ConfigSO.SpawnInvincibilitySeconds
		//
		// 真理源入口 (大厂原则 — 单一入口):
		//   - 通过 BaseAIC->GetSpawnInvincibilitySeconds() 读 ConfigSO.SpawnInvincibilitySeconds
		//   - 不直接读 Config->SpawnInvincibilitySeconds (避免与 BaseAIController 重复实现)
		//   - 修复 MeleeAIController 路径同步使用本入口 (统一真理源访问)
		//   - 命名: 重命名 BaseAIC 避开上面 line 754 的 AAIController* AIC (C++ 局部遮蔽检查)
		//
		// 行为约定 (零兜底):
		//   - > 0  : 激活无敌期
		//   - <= 0 : 跳过激活 (用户决策: 静默跳过, 不强制默认)
		//   - Config 为空 → GetSpawnInvincibilitySeconds() 内部 Log Error
		//
		// [v54.2 编译修复] C2371 重定义: 上面 line 754 已经声明 AAIController* AIC
		//   - 这里改名为 BaseAIC (语义: ABaseAIController* — 派生类型, 能调 GetConfig/GetSpawnInvincibilitySeconds)
		//   - 从 AIC 重新 Cast, 不是新声明
		//   - HasAuthority() 是 AIPawn 的方法, 这里 AIPawn 已存在, 直接调
		//   - const InvSeconds 不用三元初始化 (C++ 不允许 const 三元 partial init), 改用 if/else 显式赋值
		//
		if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
		{
			if (AIPawn->HasAuthority())
			{
				const float InvSeconds = BaseAIC->GetSpawnInvincibilitySeconds();
				if (InvSeconds > 0.f)
				{
					AIPawn->ActivateSpawnInvincibility(InvSeconds);
					UE_LOG(LogTemp, Log,
						TEXT("[RoomSpawn] SpawnAIInternal 激活 AI 无敌期: AI=%s, Duration=%.2fs (统一入口=BaseAIController.GetSpawnInvincibilitySeconds)"),
						*AIPawn->GetName(), InvSeconds);
				}
				else
				{
					UE_LOG(LogTemp, Log,
						TEXT("[RoomSpawn] SpawnAIInternal: AI=%s 的 ConfigSO.SpawnInvincibilitySeconds <= 0, 跳过无敌期激活 (用户决策: 静默跳过, 不强制默认)"),
						*AIPawn->GetName());
				}
			}
		}
		else
		{
			// AIC 不是 ABaseAIController 派生 (理论不会发生, 但零兜底要校验)
			UE_LOG(LogTemp, Error,
				TEXT("[RoomSpawn] SpawnAIInternal: AIC='%s' 不是 ABaseAIController 派生类型, 无法读 ConfigSO. "
				     "【v54.2 零兜底】跳过无敌期激活 — 这是项目架构错误, 必须修复."),
				*AIC->GetName());
		}

		UE_LOG(LogTemp, Log,
			TEXT("[RoomSpawn] AI 生成: %s, Mode=%d, Faction=%s, Class=%s, Weapon='%s'"),
			*AIName, (int32)Request.Mode,
			*DesiredFaction.ToString(), *GetNameSafe(ControllerClass),
			*AIPawn->GetSpawnWeaponID());

		++SpawnedCount;
	}

	if (SpawnedCount > 0 && GetWorld()->GetAuthGameMode())
	{
		// 通过 GameMode 广播系统消息
		if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
		{
			GM->BroadcastSystemMessage(FString::Printf(TEXT("已添加 %d 名 AI 参战"), SpawnedCount));
		}
	}

	return SpawnedCount;
}

// ==========================================
// 玩家 Spawn 主入口 — v31.1 完整实现
// ==========================================

// ==========================================
// 【v93.1 大厂架构新增】业务层角色账本查询
// ==========================================

/**
 * GetAllBattleCharacters — 对局内所有 ABaseCharacter 角色账本查询
 *
 * 大厂原则 — 单一入口:
 *   - 业务方应走本函数, 不直接 GetAllActorsOfClass
 *   - 本函数是未来账本缓存的预留接口 (若性能不够, 可内部加 TArray<TWeakObjectPtr<ABaseCharacter>>)
 *
 * 为什么不维护账本:
 *   - SpawnAIInternal / HandlePlayerRequestSpawn 各调一次 SpawnActor, 散落 N 处
 *   - 加账本需要每次 Spawn 后手动 AddUnique, 容易漏 (账本漂移)
 *   - 选母体是每局 N 次 (N=玩家数+AI数), 不需要账本 — 直接 GetAllActorsOfClass 即可
 *   - 镜像 v28: 大厅 AI 入队走 PendingAIQueue 账本, 但那是 UI 路径; 运行时战斗用 GetAllActorsOfClass
 *
 * 性能:
 *   - N <= 20 (玩家 6 + AI 14), GetAllActorsOfClass 扫一次 < 0.01ms
 *   - 每局选母体 N 次, 总开销 < 0.2ms, 完全可接受
 *   - 若未来 N > 50 触发性能问题, 再引入 TArray<TWeakObjectPtr<ABaseCharacter>> 账本
 */
TArray<ABaseCharacter*> URoomSpawnSubsystem::GetAllBattleCharacters() const
{
	TArray<ABaseCharacter*> Result;

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomSpawn] GetAllBattleCharacters: World 为空, 返回空账本."));
		return Result;
	}

	for (TActorIterator<ABaseCharacter> It(World, ABaseCharacter::StaticClass()); It; ++It)
	{
		if (ABaseCharacter* Char = *It)
		{
			Result.Add(Char);
		}
	}

	return Result;
}


void URoomSpawnSubsystem::SpawnAllPlayersIntoBattle()
{
	// 【v48 大厂架构修复 P0】真正的根因修复
	//
	// 历史 bug (v47 错误设计):
	//   - v47 守卫逻辑: CurrentRoomState == BattleInProgress && !bSpawnInProgress → 拒绝
	//   - 但 bSpawnInProgress 只在下面 line 936 才设为 true, 守卫检查时还是 false
	//   - PerformGameStart 先设 BattleInProgress 再启动 timer
	//   - timer 回调时 CurrentRoomState=BattleInProgress && bSpawnInProgress=false → 永远拒绝
	//   - → SpawnAllPlayersIntoBattle 永远不执行 → AI 永远不 Spawn
	//
	// 大厂原则 - 单一真理源 + 集中调度:
	//   - 移除状态守卫: SpawnAllPlayersIntoBattle 由 LifecycleSubsystem timer 唯一触发, 无需自检
	//   - 防御性: 仅检查 Subsystem 是否已初始化 (World/SpawnSubsystem 自身有效)
	//   - 禁止任何"自我判断是否该跑"的兜底逻辑

	if (!GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("[Spawn] SpawnAllPlayersIntoBattle: GetWorld() 为空, 拒绝执行"));
		return;
	}

	// 【v48 修复】bSpawnInProgress 必须在守卫前设为 true (timer 回调才能通过)
	// 旧 v47 是先检查再设, 永远过不了
	if (bSpawnInProgress)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Spawn] SpawnAllPlayersIntoBattle: bSpawnInProgress 已经是 true, 拒绝重入! "
			     "说明 SpawnAllPlayersIntoBattle 被同时调用了两次, 请检查调用栈."));
		return;
	}
	bSpawnInProgress = true;

	UE_LOG(LogTemp, Warning, TEXT("[Spawn] SpawnAllPlayersIntoBattle called!"));

	// 重新扫描出生点 (bReScan=true 强扫, 即使已扫过)
	ScanAndCachePlayerStarts(true);

	// 【v56 新增】扫描并缓存关卡预放 AI 的阵营
	//   调用时机: BattleStarted 广播前, OnPossess 已经触发
	//   扫描: 所有 ABaseCharacter Pawn, 按 PawnClass 缓存阵营
	//   用途: AIController::SetupMeleeAI 从这里获取关卡预放 AI 的阵营
	ScanAndCacheLevelPlacedAIFactions();

	if (UWorld* World = GetWorld())
	{
		// 【v56.6 大厂架构修复】使用 World 的权威 PlayerController 列表
		//
		// 根因: PS->GetOwner() 在某些时序下返回 nullptr (特别是测试模式/PIE 早期)
		//        → 玩家 Pawn 永远不被 Spawn → 玩家看到"飞翔视角"
		//
		// 修复: 使用 World->GetPlayerControllerIterator() (UE 引擎权威 API)
		//        这是 UE 引擎保证权威的列表，不依赖 PS->Owner 引用
		TArray<APlayerController*> PlayerControllers;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			PlayerControllers.Add(It->Get());
		}

		UE_LOG(LogTemp, Warning, TEXT("[Spawn] SpawnAllPlayersIntoBattle: 从 World 获取到 %d 个 PlayerController"),
			PlayerControllers.Num());

		for (APlayerController* PC : PlayerControllers)
		{
			if (!PC) continue;

			// 获取 PlayerState
			if (ARoomPlayerState* PS = Cast<ARoomPlayerState>(PC->PlayerState))
			{
				// 【v31.2 零兜底】拒绝 fallback — 必须有 Loadout
				// 【v52 P0】3 把武器一起读 (主+副+近战), 主武器必须非空 (Slot 1 必选), 副/近战可空
				const FString CharID = PS->GetSelectedCharacterID();
				const FString PrimaryID = PS->GetSelectedWeapon1ID();
				const FString SecondaryID = PS->GetSelectedWeapon2ID();
				const FString MeleeID = PS->GetSelectedWeapon3ID();

				if (CharID.IsEmpty() || PrimaryID.IsEmpty())
				{
					UE_LOG(LogTemp, Error,
						TEXT("[Spawn] SpawnAllPlayersIntoBattle: 玩家 '%s' Loadout 不完整 (CharID='%s', Primary='%s'). "
						     "【v31.2 零兜底】拒绝 Spawn."),
						*PS->GetPlayerName(), *CharID, *PrimaryID);
					continue;
				}

				HandlePlayerRequestSpawn(PC, CharID, PrimaryID, SecondaryID, MeleeID);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[Spawn] SpawnAllPlayersIntoBattle: PC->PlayerState 不是 ARoomPlayerState: %s"),
					*PC->GetName());
			}
		}
	}

	// ==========================================
	// 【v50 大厂架构重构 — 单一真理源】AI Spawn 逻辑
	//
	// 历史 (v47-v49 反模式):
	//   - SpawnAllPlayersIntoBattle 重复做 PawnClass 反查 + Profile 查找
	//   - SpawnAIInternal 入口"Profile 可空"允许裸 AI Spawn
	//   - 错误时用 continue 静默跳过, 整批 AI 失败时用户只看到一行错误
	//
	// 新架构 (v50 — 重复架构消除 + 零兜底):
	//   - PawnClass 反查只在 SpawnAIInternal 入口做一次 (单一真理源)
	//   - Profile 查找只在 SpawnAIInternal 入口做一次
	//   - SpawnAIInternal 总是要求 Profile 非空 (拒绝裸 AI)
	//   - 任何错误立即 abort 整批, 强制修复而非吞错
	//   - Count != 1 立即报错 (单条 Spawn 不应走这条批量路径)
	// ==========================================
	UE_LOG(LogTemp, Warning, TEXT("[Spawn] SpawnAllPlayersIntoBattle: 开始处理 %d 个 AI"), PendingAIQueue.Num());
	TArray<FAISpawnRequest> AIRequests = ConsumePendingAIForBattleSpawn();
	int32 AISpawnedCount = 0;
	for (const FAISpawnRequest& AIReq : AIRequests)
	{
		// 【v50 零兜底】Count 必须 == 1: 这条路径只处理单条 Spawn
		// 多条 Spawn 应在入队时 Count=N 分拆, 不应在这里循环里塞 N 个
		if (AIReq.Count != 1)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Spawn] SpawnAllPlayersIntoBattle: AIReq.Count=%d != 1, 拒绝 Spawn. "
				     "【v50 零兜底】批量 Spawn 应在 Queue 阶段按 Count 分拆成多条 Request. "
				     "【修复路径】检查 ConsumePendingAIForBattleSpawn 链路. "
				     "CharacterInfoRowName='%s', Mode=%d"),
				AIReq.Count, *AIReq.CharacterInfoRowName.ToString(), (int32)AIReq.Mode);
			return;  // abort 整批 — 防止半成品 Spawn
		}

		// 【v54.2 大厂架构】Config 已由 RoomService.RequestAddAI 通过 DT_CharacterInfo.ConfigSoftRef 反查填入
		// 不再传 nullptr (那是 v54.1 的 bug, Config=null 时 SpawnInvincibilitySeconds 等真理源断裂)
		const int32 Spawned = SpawnAIInternal(AIReq, AIReq.Config, /*ExistingController=*/ nullptr);
		if (Spawned > 0)
		{
			AISpawnedCount += Spawned;
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Spawn] SpawnAllPlayersIntoBattle: SpawnAIInternal 返回 0! "
				     "CharacterInfoRowName='%s', WeaponID='%s', Faction='%s', Mode=%d, AIPawnClass='%s'. "
				     "【v54 大厂架构】UAIProfileAsset 已删除, 不再需要 ProfileTag. "
				     "请检查上方 SpawnAIInternal 错误日志定位根因."),
				*AIReq.CharacterInfoRowName.ToString(), *AIReq.WeaponID,
				*AIReq.FactionTag.ToString(), (int32)AIReq.Mode,
				*GetNameSafe(AIReq.AIPawnClass));
		}
	}
	UE_LOG(LogTemp, Warning,
		TEXT("[Spawn] SpawnAllPlayersIntoBattle: AI Spawn 完成! 成功=%d, 总请求=%d"),
		AISpawnedCount, AIRequests.Num());

	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->BroadcastSystemMessage(TEXT("战斗开始!"));
	}

	// 【v47 大厂架构修复】清除 Spawn 标志
	bSpawnInProgress = false;
}

void URoomSpawnSubsystem::HandlePlayerRequestSpawn(AController* PlayerToSpawn, const FString& CharRowName, const FString& WeaponPrimaryRowName, const FString& WeaponSecondaryRowName, const FString& WeaponMeleeRowName)
{
	if (!PlayerToSpawn) return;

	UE_LOG(LogTemp, Warning, TEXT("[Spawn] HandlePlayerRequestSpawn called. Char='%s', Primary='%s', Secondary='%s', Melee='%s'"),
		*CharRowName, *WeaponPrimaryRowName, *WeaponSecondaryRowName, *WeaponMeleeRowName);

	// 【v44 大厂架构修复】战斗状态守卫 — 防止战斗期间意外移动玩家 Pawn
	// 
	// 根因: 如果 SpawnAllPlayersIntoBattle 在战斗期间被意外调用（状态机回环/重入），
	// 所有玩家的 Pawn 会被 SetActorLocationAndRotation 移动到出生点 → 玩家"瞬移"
	//
	// 修复: 检查房间状态，只在 InRoom 状态允许玩家 Spawn
	// - InRoom: 允许 (开局生成/大厅生成)
	// - BattleInProgress: 拒绝 (不允许在战斗中重新 Spawn 所有玩家)
	// - 其他状态: 允许 (如 PostBattle 结算后清理)
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		if (GM->CurrentRoomState == ERoomState::BattleInProgress)
		{
			// 检查是否真的是"战斗中的复活"（通过 Player 是否有死亡标记）
			// 复活场景: Controller 仍然存在，Pawn 可能在死亡中
			// SpawnAllPlayersIntoBattle 场景: 遍历 PlayerArray，对所有玩家调用（包括活着的）
			
			// 关键判断: 如果 Controller 的 Pawn 还活着且有效，说明这不是复活，而是意外调用
			if (APawn* ExistingPawn = PlayerToSpawn->GetPawn())
			{
				const bool bIsPawnAlive = !ExistingPawn->IsPendingKillPending() 
					&& ExistingPawn->GetLifeSpan() <= 0.0f;
				
				if (bIsPawnAlive && ExistingPawn->GetClass() == ABaseCharacter::StaticClass())
				{
					// Pawn 活着且有效，但正在 BattleInProgress 状态调 HandlePlayerRequestSpawn
					// 这说明是意外调用（不是复活），拒绝移动玩家位置
					UE_LOG(LogTemp, Error,
						TEXT("[Spawn] HandlePlayerRequestSpawn: 拒绝! 战斗期间意外调用导致玩家 Pawn '%s' 瞬移。"
						     "当前状态=BattleInProgress, Pawn 存活。这是 SpawnAllPlayersIntoBattle 在战斗期间被意外调用的根因。"
						     "【v44 大厂架构修复】请检查状态机链路，确保战斗期间不会重复触发 SpawnAllPlayersIntoBattle。"),
						*ExistingPawn->GetName());
					return;
				}
			}
			// 否则: Pawn 不存在或正在销毁，这是复活场景，允许继续
		}
	}

	// 【v36 零兜底改造】删除 Step 0 "合并缓存" 兜底
	//
	// 旧实现反模式:
	//   if (FinalCharID.IsEmpty()) FinalCharID = ExistingCache->CharID;
	//   if (FinalWeaponID.IsEmpty()) FinalWeaponID = ExistingCache->WeaponID;
	//   → "传空就让 GM 用上次缓存" = 隐藏兜底, 违反"调用方必须显式传 Loadout"
	//   → RoomPlayerController::Server_RequestSpawn_Implementation 故意传空字符串
	//   → 任何调用方都能"懒得查就传空", 配置错永远不可见
	//
	// 新架构 (v36 — 显式优于隐式):
	//   - 调用方必须显式传非空 CharID/WeaponID
	//   - 空字符串 → Log Error + 拒绝 Spawn (零兜底)
	//   - 缓存只用于跨帧/跨调用持久化 (RoomLifecycle 已写入), 不参与运行时兜底
	const FString FinalCharID = CharRowName;
	// 【v52 P0】主武器作为当前激活武器 (Slot 1) — 玩家开局默认挂主武器
	const FString FinalWeaponID = WeaponPrimaryRowName;
	// 【v52 P0】副武器 + 近战武器允许为空 (玩家可能没选), 由 WeaponAttachmentComponent 3 槽位架构决定实际生成几把
	const FString FinalSecondaryWeaponID = WeaponSecondaryRowName;
	const FString FinalMeleeWeaponID = WeaponMeleeRowName;

	// Step 1: 校验非空 (零兜底)
	if (FinalCharID.IsEmpty() || FinalCharID == TEXT("Default"))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Spawn] HandlePlayerRequestSpawn: CharID 为空 (Player=%s). 拒绝 Spawn."),
			*PlayerToSpawn->GetName());
		return;
	}
	if (FinalWeaponID.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Spawn] HandlePlayerRequestSpawn: 主武器 WeaponID 为空 (Player=%s). 拒绝 Spawn."),
			*PlayerToSpawn->GetName());
		return;
	}

	// Step 2: 写缓存 (供复活读) — 【v52 P0】3 把武器一起存
	FPlayerSpawnData SpawnData;
	SpawnData.CharID = FinalCharID;
	SpawnData.WeaponPrimaryID = FinalWeaponID;
	SpawnData.WeaponSecondaryID = FinalSecondaryWeaponID;
	SpawnData.WeaponMeleeID = FinalMeleeWeaponID;
	PlayerSpawnDataCache.Add(PlayerToSpawn->GetUniqueID(), SpawnData);

	// 同步 PlayerState — 【v52 P0】3 把武器一次写入
	if (ARoomPlayerState* PS = PlayerToSpawn->GetPlayerState<ARoomPlayerState>())
	{
		PS->SetPlayerLoadout(FinalCharID, FinalWeaponID, FinalSecondaryWeaponID, FinalMeleeWeaponID);
	}

	// Step 3: 查 DT_CharacterInfo
	TSubclassOf<ABaseCharacter> CharClassToSpawn = nullptr;
	if (CharacterDataTable)
	{
		static const FString CharCtx(TEXT("RoomSpawnSubsystem::HandlePlayerRequestSpawn"));
		if (FCharacterInfo* Info = CharacterDataTable->FindRow<FCharacterInfo>(FName(*FinalCharID), CharCtx))
		{
			if (!Info->CharacterBlueprint.IsNull())
			{
				CharClassToSpawn = Info->CharacterBlueprint.LoadSynchronous();
			}
		}
	}

	// 【v31.1 零兜底】拒绝 fallback 到 DefaultPawnClass
	if (!CharClassToSpawn)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Spawn] HandlePlayerRequestSpawn: CharID='%s' 在 DT_CharacterInfo 查不到行 (Player=%s). "
			     "【v31.1 零兜底】拒绝 Spawn, 拒绝 fallback 到 DefaultPawnClass."),
			*FinalCharID, *PlayerToSpawn->GetName());
		return;
	}

	// Step 4: 检查 Pawn 是否需要重新生成
	APawn* ExistingPawn = PlayerToSpawn->GetPawn();
	bool bNeedsRespawn = true;
	if (ExistingPawn)
	{
		// 检查 Pawn 是否有效（不在销毁中）
		// 根因: 如果 Pawn 被 SetLifeSpan(0.1f) 标记为即将销毁,
		// GetPawn() 仍会返回它，但此时移动它会导致玩家"瞬移"
		// 修复: 检查 RemainingLife 或 IsPendingKill
		const bool bIsDying = ExistingPawn->GetLifeSpan() > 0.0f || ExistingPawn->IsPendingKillPending();
		if (ExistingPawn->GetClass() == CharClassToSpawn && !bIsDying)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Spawn] HandlePlayerRequestSpawn: Pawn=%s 类已对且存活, 复用并移动到出生点."),
				*ExistingPawn->GetName());
			bNeedsRespawn = false;
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Spawn] HandlePlayerRequestSpawn: Pawn 类不匹配或正在销毁, 销毁旧 Pawn: %s (bIsDying=%d)"),
				*ExistingPawn->GetName(), bIsDying ? 1 : 0);
			if (!ExistingPawn->IsPendingKillPending())
			{
				ExistingPawn->Destroy();
			}
			bNeedsRespawn = true;
		}
	}

	// Step 5: 出生点分配
	ScanAndCachePlayerStarts(false);
	FVector SpawnLoc = FVector::ZeroVector;
	FRotator SpawnRot = FRotator::ZeroRotator;
	FGameplayTag PlayerFactionTag = FFactionTags::Offense();
	if (ARoomPlayerState* PS = PlayerToSpawn->GetPlayerState<ARoomPlayerState>())
	{
		PlayerFactionTag = PS->CurrentFactionTag;
	}

	AActor* AssignedSpawnPoint = GetAvailableSpawnPointForFaction(PlayerFactionTag, true, PlayerToSpawn);
	if (!AssignedSpawnPoint)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Spawn] HandlePlayerRequestSpawn: 阵营 '%s' 出生点分配失败. "
			     "【v31.1 零兜底】不调 FindPlayerStart 兜底."),
			*PlayerFactionTag.ToString());
		return;
	}

	SpawnLoc = AssignedSpawnPoint->GetActorLocation();
	SpawnRot = AssignedSpawnPoint->GetActorRotation();

	// Step 6: Spawn or Reuse
	ABaseCharacter* SpawnedChar = nullptr;
	if (bNeedsRespawn)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = PlayerToSpawn;
		SpawnParams.Instigator = PlayerToSpawn->GetPawn();

		SpawnedChar = GetWorld()->SpawnActor<ABaseCharacter>(
			CharClassToSpawn, SpawnLoc, SpawnRot, SpawnParams);

		if (SpawnedChar)
		{
			// 【2026.07.12 P0 修复】写入武器/角色 Loadout 到 Pawn 字段
			//
			// 根因: 旧版漏调 SetSpawnLoadout, 玩家 Pawn 的 WeaponAttachmentComponent 永远没数据
			//       AI 路径 (SpawnAIInternal line 547) 已调, 玩家路径漏掉
			//       → PossessedBy 从 PS 读武器 ID 兜底 (Host 端), 但 client 端 PS 复制时序问题
			//       → client 武器永远不在正确挂载位置
			//
			// 大厂原则 - 统一真理源:
			//   - AI 路径: Request.CharacterInfoRowName + Request.WeaponID (v49)
			//   - 玩家路径: PS.SelectedCharacterID + PS.SelectedWeapon1ID
			//   - 必须都走 SetSpawnLoadout, 跟 AI 路径对称
			SpawnedChar->SetSpawnLoadout(FinalCharID, FinalWeaponID);

			// 阵营设置
			SpawnedChar->SetGenericTeamId(FFactionTags::ToGenericTeamId(PlayerFactionTag));
			PlayerToSpawn->Possess(SpawnedChar);

			// 【v40.3 P0 修复】Possess 之后立即调 RequestWeaponSpawn
			//
			// 根因: 旧版依赖 BaseCharacter::PossessedBy 末尾触发武器 Spawn
			//   但 PossessedBy 在 v40.3 已删除武器 Spawn 链路 (避免重复入口)
			//   → 玩家 Pawn 在 Spawn 之后没有武器, 必须由这里显式触发
			//
			// 大厂原则:
			//   - 唯一入口: URoomSpawnSubsystem::HandlePlayerRequestSpawn (本函数)
			//   - 时序保证: SetSpawnLoadout ✓ → SetGenericTeamId ✓ → Possess ✓ → RequestWeaponSpawn ✓
			//   - 与 AI 路径对称: RoomSpawnSubsystem::SpawnAIInternal 同样在 Possess 后触发
			// 【v52 P0 扩展 3 槽位】玩家路径走 Server_SpawnAllWeapons (主+副+近战 一次 Spawn)
			//   - 旧版: 单独 Spawn 一把武器到 CurrentWeapon, 不支持多槽位
			//   - 新版: 反查 3 把武器的 Class, 一次性传给 WeaponAttachmentComponent
			TSubclassOf<ABaseWeapon> PrimaryClass = nullptr;
			TSubclassOf<ABaseWeapon> SecondaryClass = nullptr;
			TSubclassOf<ABaseWeapon> MeleeClass = nullptr;

			if (!FinalWeaponID.IsEmpty())
			{
				PrimaryClass = ResolveWeaponClassFromID(FinalWeaponID);
			}
			if (!FinalSecondaryWeaponID.IsEmpty())
			{
				SecondaryClass = ResolveWeaponClassFromID(FinalSecondaryWeaponID);
			}
			if (!FinalMeleeWeaponID.IsEmpty())
			{
				MeleeClass = ResolveWeaponClassFromID(FinalMeleeWeaponID);
			}

			// 零兜底 (v52): 主武器必须有, 副/近战可空
			if (!PrimaryClass)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Spawn] HandlePlayerRequestSpawn: 玩家 '%s' 的 FinalWeaponID='%s' 无法反查为 WeaponClass. "
					     "【v52 零兜底】主武器必须有, 拒绝 Spawn. "
					     "修复: 1) GM_RoomGameMode.ClassDefaults.WeaponDataTable 必须配 DT_WeaponInfo; "
					     "2) DT_WeaponInfo 里有 RowName='%s' 的行"),
					*PlayerToSpawn->GetName(), *FinalWeaponID, *FinalWeaponID);
				return;
			}

			UE_LOG(LogTemp, Log,
				TEXT("[Spawn] HandlePlayerRequestSpawn 触发 3 槽位 Spawn: Primary=%s Secondary=%s Melee=%s (Pawn=%s, Player=%s)"),
				*FinalWeaponID,
				*FinalSecondaryWeaponID,
				*FinalMeleeWeaponID,
				*SpawnedChar->GetName(), *PlayerToSpawn->GetName());

			// 走 3 槽位 Spawn (服务器权威, 与 AI 路径对称)
			if (UWeaponAttachmentComponent* WeaponAttachComp = SpawnedChar->FindComponentByClass<UWeaponAttachmentComponent>())
			{
				WeaponAttachComp->Server_SpawnAllWeapons(PrimaryClass, SecondaryClass, MeleeClass);
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Spawn] HandlePlayerRequestSpawn: Pawn '%s' 没有 UWeaponAttachmentComponent — 拒绝 Spawn. "
					     "【v52 零兜底】必须挂载 WeaponAttachment 组件."),
					*SpawnedChar->GetName());
				return;
			}

			// 【v41 大厂架构】应用角色战斗参数配置
			ApplyCharacterConfigToCharacter(SpawnedChar);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Spawn] HandlePlayerRequestSpawn: SpawnActor 失败 (Char='%s' Loc=%s)"),
				*FinalCharID, *SpawnLoc.ToString());
			return;
		}
	}
	else
	{
		// bNeedsRespawn=false: 复用 + 重定位
		ExistingPawn = PlayerToSpawn->GetPawn();
		if (ExistingPawn && Cast<ABaseCharacter>(ExistingPawn))
		{
			ExistingPawn->SetActorLocationAndRotation(SpawnLoc, SpawnRot, false, nullptr, ETeleportType::TeleportPhysics);
			PlayerToSpawn->Possess(ExistingPawn);
		}
		else
		{
			// 【v31.1 零兜底】拒绝静默吞错
			UE_LOG(LogTemp, Error,
				TEXT("[Spawn] HandlePlayerRequestSpawn: bNeedsRespawn=false 但 Pawn 不存在 (Player=%s). "
				     "拒绝静默兜底, 让上游 RestartPlayer 重新走完整 Spawn 流程."),
				*PlayerToSpawn->GetName());
			return;
		}
	}

	// Step 7: 激活无敌期
	if (ABaseCharacter* PlayerChar = Cast<ABaseCharacter>(PlayerToSpawn->GetPawn()))
	{
		PlayerChar->ActivateSpawnInvincibility();
	}
}

TSubclassOf<APawn> URoomSpawnSubsystem::GetDefaultPawnClassForController(AController* InController)
{
	if (!InController) return nullptr;

	// AI Controller: 返回 nullptr 让引擎跳过默认 Spawn (AI Pawn 由 SpawnAIInternal 控制)
	if (Cast<AAIController>(InController))
	{
		return nullptr;
	}

	// 玩家 Controller: 优先从缓存读 (绕过 PS 复制时序)
	FString CharID;
	if (FPlayerSpawnData* Cached = PlayerSpawnDataCache.Find(InController->GetUniqueID()))
	{
		if (!Cached->CharID.IsEmpty()) CharID = Cached->CharID;
	}
	if (CharID.IsEmpty())
	{
		if (ARoomPlayerState* PS = InController->GetPlayerState<ARoomPlayerState>())
		{
			CharID = PS->GetSelectedCharacterID();
		}
	}

	if (!CharID.IsEmpty() && CharID != TEXT("Default") && CharacterDataTable)
	{
		static const FString Ctx(TEXT("RoomSpawnSubsystem::GetDefaultPawnClass"));
		if (FCharacterInfo* Info = CharacterDataTable->FindRow<FCharacterInfo>(FName(*CharID), Ctx))
		{
			if (!Info->CharacterBlueprint.IsNull())
			{
				return Info->CharacterBlueprint.LoadSynchronous();
			}
		}
	}

	// 【v31.1 零兜底】拒绝 fallback 到 DefaultPawnClass
	UE_LOG(LogTemp, Error,
		TEXT("[Spawn] GetDefaultPawnClass: Controller=%s CharID='%s' 无有效 Blueprint. "
		     "【v31.1 零兜底】返回 nullptr, 让 HandlePlayerRequestSpawn 接管."),
		*InController->GetName(), *CharID);
	return nullptr;
}

void URoomSpawnSubsystem::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer) return;

	// 拦截 SpawnAllPlayersIntoBattle 期间的 RestartPlayer
	if (bSpawnInProgress)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Spawn] RestartPlayer 跳过: bSpawnInProgress=true"));
		return;
	}

	// 从缓存读 Char/Weapon
	// 【v52 P0】3 把武器一起读 (主+副+近战)
	if (FPlayerSpawnData* Cached = PlayerSpawnDataCache.Find(NewPlayer->GetUniqueID()))
	{
		HandlePlayerRequestSpawn(NewPlayer, Cached->CharID, Cached->WeaponPrimaryID, Cached->WeaponSecondaryID, Cached->WeaponMeleeID);
		return;
	}

	// 【v31.1 零兜底】拒绝 fallback 到 Super::RestartPlayer
	UE_LOG(LogTemp, Error,
		TEXT("[Spawn] RestartPlayer: Controller=%s 无缓存. "
		     "【v31.1 零兜底】拒绝调 Super::RestartPlayer (会随机挑 PlayerStart)."),
		*NewPlayer->GetName());
}

void URoomSpawnSubsystem::RequestRespawn(AController* DeadController, bool bImmediateRespawn)
{
	// 【v115.2 大厂架构诊断】记录 Controller 在 RequestRespawn 入口时的状态
	UE_LOG(LogTemp, Display,
		TEXT("[Respawn] 【v115.2-Diag】RequestRespawn ENTER: DeadController='%s' IsValid=%d bImmediate=%d"),
		DeadController ? *DeadController->GetName() : TEXT("NULL"),
		DeadController ? (DeadController->IsValidLowLevel() ? 1 : 0) : 0,
		bImmediateRespawn ? 1 : 0);

	if (!DeadController)
	{
		UE_LOG(LogTemp, Error, TEXT("[Respawn] RequestRespawn: DeadController is null!"));
		return;
	}

	// 【v115.2 大厂架构诊断】再次检查 Controller 有效性
	UE_LOG(LogTemp, Display,
		TEXT("[Respawn] 【v115.2-Diag】RequestRespawn 有效性检查后: DeadController='%s' IsValid=%d"),
		*DeadController->GetName(),
		DeadController->IsValidLowLevel() ? 1 : 0);

	// ===== 【v99 P0 大厂架构 — 母体复活链真理源】预先读 PS->bIsMother =====
	//
	// 业务核心 (用户 2026.07.26 明确):
	//   - 生化模式: 母体死后复活必须原地复活成母体
	//
	// 根因 (Session1.log line 1161-1218):
	//   - 旧版 RequestRespawn 走 HandlePlayerRequestSpawn, 用 PlayerSpawnDataCache.CharID (人类 CharID)
	//   - 即使死前是 BP_MuTi, 复活时一律走人类路径 → 复活成 BP_SWAT_C 人类 → 错
	//
	// 大厂原则 — 单一真理源 (SSOT):
	//   - 真理源: ARoomPlayerState::bIsMother (服务器设, Replicated 自动同步)
	//   - 写点: URoomMotherMutationSubsystem::MutateCharacterToMother Step 3.7
	//   - 读点: 本函数 (复活链决策点, 复活前读最新值)
	//
	// 时序关键 (大厂原则 — 零时序竞争):
	//   - Die() 同步链路: ApplyDamage → OnDeath → ExecuteDeathLocal → Die() → 派发 RequestRespawn
	//   - 在 RequestRespawn 入口读 PS->bIsMother 时, 死亡 Pawn 的 bIsMother=true 已生效
	//   - 3s 后实际复活时, PS->bIsMother 仍是 true (除非中途调过反变异, 但本项目无反变异 API)
	//   - 所以**同步读 + 3s 延迟**都正确, 无时序竞争
	//
	// AI 路径说明:
	//   - AI 没有 PlayerState, 单独走 Cast<AAIController> 分支
	//   - 母体 AI 复活 = 母体变母体 (永远变异, 因为 DA_AIProfile_*.SpawnCharacterRowName 是固定 CharRowName)
	//   - 真正的"AI 母体复活"问题不在本次范围, 玩家路径先修
	bool bDeadWasMother = false;
	if (const ARoomPlayerState* PS = DeadController->GetPlayerState<ARoomPlayerState>())
	{
		bDeadWasMother = PS->bIsMother;
	}

	// 【v115 大厂架构诊断】AI 分支的 bDeadWasMother 永远是 false (无 PlayerState)
	// 真正的母体判定在下面 AI 分支用 AIC->CachedIsMother
	bool bAICController = Cast<AAIController>(DeadController) != nullptr;
	bool bCachedIsMother = false;
	if (bAICController)
	{
		if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(DeadController))
		{
			bCachedIsMother = BaseAIC->GetCachedIsMother();
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[Respawn] RequestRespawn: Controller=%s, bImmediate=%d, bDeadWasMother(PS,player)=%d, bAIC=%d, CachedIsMother(AIC)=%d"),
		*DeadController->GetName(), bImmediateRespawn, bDeadWasMother ? 1 : 0, bAICController ? 1 : 0, bCachedIsMother ? 1 : 0);

	if (Cast<ARoomPlayerController>(DeadController))
	{
		// ===== 【v99 P0 大厂架构】母体复活分支: 走 MutatePawnToMother 而非 HandlePlayerRequestSpawn =====
		//
		// 业务规则 (用户 2026.07.26 明确):
		//   - 母体死后复活必须原地变成母体, 不能复活成人类
		//   - 复活路径 = URoomMotherMutationSubsystem::MutateCharacterToMother (单一入口)
		//   - 单一真理源 = GM.MotherCharacterRowName (BP_MuTi 的 CharRowName)
		//
		// 大厂原则 — 职责分层 + 集中调度:
		//   - 复活链决策 = RoomSpawnSubsystem (本函数)
		//   - 母体原地变 = RoomMotherMutationSubsystem::MutateCharacterToMother (业务层调 SpawnSubsystem::MutatePawnToMother)
		//   - 母体 CharRowName 真理源 = ARoomGameMode::MotherCharacterRowName
		//
		// 为什么不在 SpawnAI 路径处理 (避免重复架构):
		//   - AI 母体死后复活永远变母体 (Profile 写死), 不需要玩家路径的 bIsMother 判断
		//   - SpawnAIInternal 复活路径已读 AIC.CachedCharRowName, 母体 Profile 配的就是母体 CharRowName
		//   - 玩家路径独立处理: 用 PS->bIsMother 判断, 因为玩家大厅选的是人类 CharID
		if (bDeadWasMother)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[Respawn] 母体玩家复活分支 — Controller='%s' PS->bIsMother=true → 走 MutatePawnToMother 路径. "
				     "【v99 P0】不再读 PlayerSpawnDataCache.CharID (那是人类 CharID, 会错误复活成人类)."),
				*DeadController->GetName());

			// 模式校验: 只有生化模式 (Zombie) 才允许母体复活, 刀战模式 bIsMother 应永为 false
			ARoomGameMode* RoomGM = GetWorld() ? GetWorld()->GetAuthGameMode<ARoomGameMode>() : nullptr;
			if (!RoomGM)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Respawn] RoomGameMode 为空, 拒绝母体复活. Controller='%s'"),
					*DeadController->GetName());
				return;
			}
			if (RoomGM->MotherCharacterRowName.IsEmpty())
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Respawn] GM.MotherCharacterRowName 为空, 拒绝母体复活. "
					     "【v99 零兜底】配置错 — 必须配 BP_MuTi 的 CharRowName. "
					     "修复: BP_GM_RoomGameMode.uasset → ClassDefaults → MetalSlug|Match → Mother Character RowName."),
					*DeadController->GetName());
				return;
			}

			// 母体原地复活: 走 SpawnSubsystem::MutatePawnToMother 单一入口 (零兜底 — 不需要 Target Pawn)
			//
			// 大厂原则 — 职责单一:
			//   - MutateCharacterToMother(ABaseCharacter*) 需要活 Pawn, 复活链无 Pawn
			//   - MutatePawnToMother(Controller, RowName) 接受 Controller, 内部从 Controller->GetPawn() 拿
			//   - 复活链直接调 MutatePawnToMother (跳过 MutateCharacterToMother 的"业务层标记 bIsMother"层)
			//   - bIsMother 已经在 PS 上设过 (死亡前), MutatePawnToMother 内 Step 5.5 切阵营 + Step 5.6 写血
			//   - 业务层 Step 3.7 (PlayerState.bIsMother=true) 不会重复跑 (MutatePawnToMother 不调它)
			//   - **不重复**: 复活链直接调 SpawnSubsystem 入口, 跳过业务层 (避免 MutateCharacterToMother 重新写 bIsMother 触发 RPC)
			//
			// 不破坏刀战模式:
			//   - 刀战模式 PS->bIsMother=false → 不进本分支 → 永远走老路径
			const FString MotherCharRowName = RoomGM->MotherCharacterRowName;
			if (bImmediateRespawn)
			{
				// 立即调 (本项目目前未用 bImmediateRespawn=true 的玩家路径)
				MutatePawnToMother(DeadController, MotherCharRowName);
			}
			else
			{
				// 延迟 3s 后调 — 与人类复活路径一致, 给玩家"死亡 → 复活"反馈
				// 零兜底: 延迟后 Controller 可能失效 (TWeakObjectPtr 校验)
				TWeakObjectPtr<AController> WeakCtrl = DeadController;
				TWeakObjectPtr<URoomSpawnSubsystem> WeakSys = this;
				FTimerHandle LocalHandle;
				const FString CapturedMotherCharRowName = MotherCharRowName; // lambda 捕获需要 const 副本
				GetWorld()->GetTimerManager().SetTimer(LocalHandle,
					[WeakCtrl, WeakSys, CapturedMotherCharRowName]()
					{
						AController* C = WeakCtrl.Get();
						if (!C)
						{
							return; // Controller 已销毁, 跳过 (合法竞态, 不报错)
						}
						if (URoomSpawnSubsystem* Sys = WeakSys.Get())
						{
							Sys->MutatePawnToMother(C, CapturedMotherCharRowName);
						}
					},
					RespawnDelaySeconds, false);
			}
			return; // 母体复活分支已处理, 不走下面老路径
		}

		// ===== 人类复活路径 (刀战 + 生化模式都走这里) =====
		// 玩家复活 (复用 HandlePlayerRequestSpawn)
		FPlayerSpawnData* Cached = PlayerSpawnDataCache.Find(DeadController->GetUniqueID());
		if (!Cached)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Respawn] Player Controller '%s' 无 spawn 缓存, 拒绝复活."),
				*DeadController->GetName());
			return;
		}

		// 【v52 P0】3 把武器一起读 (主+副+近战), 完整恢复 Loadout
		const FString CharID = Cached->CharID;
		const FString PrimaryID = Cached->WeaponPrimaryID;
		const FString SecondaryID = Cached->WeaponSecondaryID;
		const FString MeleeID = Cached->WeaponMeleeID;

		if (bImmediateRespawn)
		{
			HandlePlayerRequestSpawn(DeadController, CharID, PrimaryID, SecondaryID, MeleeID);
		}
		else
		{
			TWeakObjectPtr<AController> WeakCtrl = DeadController;
			FTimerHandle LocalHandle;
			GetWorld()->GetTimerManager().SetTimer(LocalHandle,
				[WeakCtrl, CharID, PrimaryID, SecondaryID, MeleeID, this]()
				{
					if (AController* C = WeakCtrl.Get())
					{
						HandlePlayerRequestSpawn(C, CharID, PrimaryID, SecondaryID, MeleeID);
					}
				},
				RespawnDelaySeconds, false);
		}
	}
	else if (Cast<AAIController>(DeadController))
	{
		// AI 复活 (单一入口 SpawnAIInternal)
		ABaseAIController* BaseAIC = Cast<ABaseAIController>(DeadController);
		if (!BaseAIC)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Respawn] DeadController '%s' 不是 ABaseAIController 派生, 拒绝复活."),
				*DeadController->GetName());
			return;
		}

		// ============================================================
		// 【v109 大厂架构 — AI 母体复活链真理源】CachedIsMother 决策
		// ============================================================
		//
		// 业务核心 (用户 2026.07.31 明确):
		//   "生化模式：ai是母体。被打死后成了人类，这是错的，应该还是母体。
		//    请参照玩家的业务逻辑。"
		//
		// 镜像玩家路径 (RequestRespawn 玩家分支, line 2013):
		//   - 玩家: 读 PS->bIsMother=true → 走 MutatePawnToMother (母体原地变)
		//   - AI:   读 AIC->CachedIsMother=true → 走 MutatePawnToMother (母体原地变)
		//
		// 旧 (v99.1 之前) 错误行为:
		//   - AI 复活走 SpawnAIInternal (line 2307), 读 CachedAIPawnClass (人类 BP_GruntAI)
		//   - 生成人类 Pawn → Pawn.bIsMother=false → BT 切人类分支
		//   - 玩家看到 "AI 母体死后变人类" — 业务错误
		//
		// 新 (v109) 镜像玩家路径:
		//   - 读 AIC->CachedIsMother (Server 权威, 由 MutatePawnToMother Step 6 写入)
		//   - CachedIsMother=true → 走 MutatePawnToMother (母体原地变, 不走 SpawnAIInternal)
		//   - CachedIsMother=false → 走老路径 SpawnAIInternal (复活成原人类 Pawn)
		//
		// 不破坏刀战模式:
		//   - 刀战模式 AI CachedIsMother 永远是 false → 不进本分支 → 走老路径 → 0 影响
		//
		// 镜像玩家代码结构 (Step 1 ~ 7 全部复用):
		//   - PlayerController 分支 (line 1995-2080) 已有完整 MutatePawnToMother 调用 + 模式校验
		//   - AI 分支必须**逐行镜像** — 不能简化 (零重复架构 = 不写简化版)
		// ============================================================
		if (BaseAIC->GetCachedIsMother())
		{
			UE_LOG(LogTemp, Display,
				TEXT("[Respawn] 母体 AI 复活分支 — Controller='%s' AIC.CachedIsMother=true → 走 MutatePawnToMother 路径. "
				     "【v109 大厂架构】镜像玩家 PS->bIsMother 复活路径, 不再读 CachedAIPawnClass (那是人类 Pawn Class)."),
				*DeadController->GetName());

			// 模式校验: 只有生化模式 (Zombie) 才允许母体复活, 刀战模式 bIsMother 应永为 false
			ARoomGameMode* RoomGM = GetWorld() ? GetWorld()->GetAuthGameMode<ARoomGameMode>() : nullptr;
			if (!RoomGM)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Respawn] RoomGameMode 为空, 拒绝母体 AI 复活. Controller='%s'"),
					*DeadController->GetName());
				return;
			}
			if (RoomGM->MotherCharacterRowName.IsEmpty())
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Respawn] GM.MotherCharacterRowName 为空, 拒绝母体 AI 复活. "
					     "【v109 零兜底】配置错 — 必须配 BP_MuTi 的 CharRowName. "
					     "修复: BP_GM_RoomGameMode.uasset → ClassDefaults → MetalSlug|Match → Mother Character RowName."),
					*DeadController->GetName());
				return;
			}

			// 【v110 修复】AI 分支也需要定义 MotherCharRowName
			const FString MotherCharRowName = RoomGM->MotherCharacterRowName;

			// 母体原地复活: 走 SpawnSubsystem::MutatePawnToMother 单一入口 (镜像玩家路径)
			//
			// 大厂原则 — 复用而非重写:
			//   - 与玩家路径**完全相同的代码** (镜像), 不写 AI 专属简化版
			//   - MutatePawnToMother 内部 Step 6 已经镜像处理 AI 分支 (v109 新增)
			//   - 复活链直接调 MutatePawnToMother (跳过业务层 MutateCharacterToMother 的"业务层标记 bIsMother"层)
			//   - CachedIsMother 已在 AIC 上设过 (MutatePawnToMother Step 6), 复活链真理源已就位
			//
			// 时序 (与玩家路径完全镜像):
			//   - 立即调 (bImmediateRespawn=true) 或延迟 3s (bImmediateRespawn=false)
			//   - 延迟后 Controller 可能失效 (TWeakObjectPtr 校验)
			// 【v111 大厂架构修复】使用 AIController 上的 TimerHandle, 生命周期绑定
			// 旧: 存储在 Subsystem 局部变量, Controller 销毁时 Timer 回调找不到 Controller
			// 新: TimerHandle 存储在 AIController 上, 生命周期与 Controller 绑定
			// 注意: 不再需要 WeakPtr, 因为 TimerHandle 本身在 AIController 上
			if (bImmediateRespawn)
			{
				MutatePawnToMother(DeadController, MotherCharRowName);
			}
			else
			{
				// 【v115 大厂架构修复】使用 TWeakObjectPtr 捕获 AIController
				// 根因分析:
				//   - 旧代码: [this, BaseAIC, CapturedMotherCharRowName]() — BaseAIC 按值捕获
				//   - 当 Controller 在 Timer 触发前被销毁时，原始 BaseAIC 指针指向的对象被销毁
				//   - 但 lambda 闭包中的副本仍然指向相同的（现在是无效的）内存地址
				//   - Timer 触发时，lambda 中的 BaseAIC 是一个悬空指针！
				//   - IsValidLowLevel() 返回 false，但此时已经无法复活了
				//
				// 修复:
				//   - 捕获 TWeakObjectPtr<ABaseAIController> 而非原始指针
				//   - Timer 触发时检查 WeakPtr 是否仍然有效
				//   - 如果无效，跳过复活（Controller 已被销毁）
				//
				// 大厂原则 — 生命周期安全:
				//   - 所有跨 Timer 的 Actor 引用必须用 TWeakObjectPtr
				//   - 不能依赖"Controller 永远不会被销毁"的假设
				//   - 必须处理 Controller 被外部销毁的边界情况
				TWeakObjectPtr<ABaseAIController> WeakBaseAIC(BaseAIC);
				const FString CapturedMotherCharRowName = MotherCharRowName;
				GetWorld()->GetTimerManager().SetTimer(
					BaseAIC->MotherRespawnTimerHandle,
					[this, WeakBaseAIC, CapturedMotherCharRowName]()
					{
						// 【v115.6 大厂架构诊断】增强 Timer 回调日志
						if (!WeakBaseAIC.IsValid())
						{
							UE_LOG(LogTemp, Error,
								TEXT("[Respawn] 【v115.6-Warning】母体 AI 复活 Timer 回调触发: AIController 已销毁 (WeakBaseAIC.IsValid=false), 跳过复活. "
								     "可能原因: 战斗结束或地图切换."));
							return;
						}
						ABaseAIController* ValidBaseAIC = WeakBaseAIC.Get();
						const FString ControllerName = ValidBaseAIC->GetName();
						const UWorld* ControllerWorld = ValidBaseAIC->GetWorld();
						UE_LOG(LogTemp, Error,
							TEXT("[Respawn] 【v115.6-Diag】母体 AI 复活 Timer 回调触发: AIController=%s IsValid=%d World=%s."),
							*ControllerName,
							ValidBaseAIC->IsValidLowLevel() ? 1 : 0,
							ControllerWorld ? *ControllerWorld->GetName() : TEXT("NULL"));
						if (!ValidBaseAIC->IsValidLowLevel())
						{
							UE_LOG(LogTemp, Error,
								TEXT("[Respawn] 【v115.6-Error】母体 AI 复活失败: AIController IsValidLowLevel=false."));
							return;
						}
						UE_LOG(LogTemp, Error,
							TEXT("[Respawn] 【v115.6-Diag】母体 AI 复活: 即将调 MutatePawnToMother(AIController=%s, RowName=%s)."),
							*ControllerName, *CapturedMotherCharRowName);
						this->MutatePawnToMother(ValidBaseAIC, CapturedMotherCharRowName);
					},
					RespawnDelaySeconds, false);
				UE_LOG(LogTemp, Display,
					TEXT("[Respawn] 【v115-Diag】母体 AI 复活 Timer 已设置: AIController=%s Delay=%.1fs. 等待 %.1fs 后复活..."),
					*DeadController->GetName(), RespawnDelaySeconds, RespawnDelaySeconds);
			}
			return; // 母体 AI 复活分支已处理, 不走下面 SpawnAIInternal 老路径
		}

		// ===== 下面是 v54 的人类 AI 复活路径 (CachedIsMother=false 时才执行) =====
		// AI 复活 (单一入口 SpawnAIInternal)

		// ============================================================
		// 【v54 大厂架构 — 运行时真理源】复活路径优先级链 (全部从 Controller.CachedXXX 派生)
		// ============================================================
		//
		// 设计动机 (用户原话 2026.07.16):
		//   "房间页面添加的ai，应该是知道每个ai的武器，角色，阵营的，
		//    且应该存在内存中不是吗？所以应该不存在ai复活因为不知道武器，
		//    角色，阵营，而导致复活不了的情况发生。"
		//
		// v54 真理源优先级链 (AI 复活路径, 全部从 Controller 内存读):
		//   1. Controller.CachedFactionTag (运行时真理源, 大厅 AI 复活走这条)
		//   2. Controller.CachedAIPawnClass (运行时真理源)
		//   3. Controller.CachedWeaponID (运行时真理源, 关卡预放 AI 走 ConfigSO 兜底)
		//   4. Controller.CachedWeaponID 为空 → ConfigSO.DefaultWeaponRowName fallback
		//   5. 都不存在 → Log Error + return (零兜底, 强制修复)
		//
		// 这是对用户洞察的直接落地: 大厅 AI 的所有 Spawn 参数都在 Controller 内存里,
		// 不依赖 Profile 反查
		//
		// 【v54 大厂架构重构 — UAIProfileAsset 删除】
		//   - Profile.PawnClass / Profile.WeaponID / Profile.FactionTag / Profile.ProfileTag 全部已删除
		//   - 不依赖任何 Profile 反查路径
		//   - 关卡预放 AI 默认值走 ConfigSO.LevelPlacedAI_xxx (BaseAIController.GetConfig())
		// ============================================================

		// ----- 阵营派生 (v54: CachedFactionTag 唯一真理源 — 无 Profile 反查) -----
		FGameplayTag RespawnFactionTag = FGameplayTag::EmptyTag;
		if (BaseAIC->CachedFactionTag.IsValid())
		{
			// 大厅 AI 路径: 直接用运行时真理 (v26 链路, CachedFactionTag 在 Spawn 时已写入)
			RespawnFactionTag = BaseAIC->CachedFactionTag;
		}
		else
		{
			// 关卡预放 AI 路径: CachedFactionTag 为空 — 强制修复
			UE_LOG(LogTemp, Error,
				TEXT("[Respawn] AI 复活阵营派生失败: Controller.CachedFactionTag 为空. "
					 "【v54 大厂架构】不允许用任何兜底 — 关卡预放 AI 必须在 BP_GruntAI 细节面板的 Faction Tag 字段配 (例如 Faction.Offense). "
					 "大厅入队 AI 必须在 RoomService::RequestAddAI 时 FactionTag 有效."));
			return;
		}

		// ----- AIPawnClass 派生 (v54: Controller.CachedAIPawnClass 唯一真理源) -----
		//
		// v54 重构: UAIProfileAsset 已删除, 无任何 Profile 反查!
		//   - 大厅 AI 复活: 用 Controller.CachedAIPawnClass (Spawn 时已写入, 运行时真理)
		//   - 关卡预放 AI 复活: 关卡预放 AI 的 Pawn Class == 关卡里配置的 Pawn BP
		//                     → CachedAIPawnClass 在 SetupMeleeAI 时已经写入 (本节前面代码)
		//                     → 直接读 CachedAIPawnClass 即可
		//   - 都不存在 (CachedAIPawnClass 为空) → Log Error + return
		//
		// 大厂原则 (v54):
		//   - 真理源 = 实际 Spawn 出来的 Pawn 的 Class, 不是 Profile 反查的 Class
		//   - 关卡预放 AI 的 Pawn Class = 关卡里的 BP (关卡决定, ConfigSO 不重复持有)
		TSubclassOf<class ABaseCharacter> RespawnAIPawnClass = BaseAIC->GetCachedAIPawnClass();
		if (!RespawnAIPawnClass)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Respawn] AI 复活: Controller='%s' 的 CachedAIPawnClass 为空, 拒绝复活. "
					 "【v53 大厂架构 — 运行时真理源】Controller.CachedAIPawnClass 是复活真理源, 必须在 Spawn 时写入. "
					 "【根因排查】"
					 "1. 关卡预放 AI 路径: AMeleeAIController::SetupMeleeAI 末尾会写入 (检查是否走到这步). "
					 "2. 大厅入队 AI 路径: ARoomGameMode::SpawnAIInternal 末尾会写入 (检查 Spawn 时是否报错). "
					 "3. 如果 Pawn 被销毁但 Controller 复用, Cached 字段可能丢失 — 这种情况需要重启游戏, AI 永不复活."),
				*DeadController->GetName());
			return;
		}

		// ----- WeaponID 派生 (v54: Controller.CachedWeaponID > ConfigSO fallback > 错误) -----
		FString RespawnWeaponID = BaseAIC->GetCachedWeaponID();
		if (RespawnWeaponID.IsEmpty())
		{
			// 【v54.4】关卡预放 AI 路径: 走 ConfigSO.LevelPlacedWeaponClass fallback
			TSoftClassPtr<ABaseWeapon> ConfigLevelPlacedWeaponClass = BaseAIC->GetDefaultWeaponClass();
			if (!ConfigLevelPlacedWeaponClass.IsNull())
			{
				TSubclassOf<ABaseWeapon> WeaponClassCache = ConfigLevelPlacedWeaponClass.LoadSynchronous();
				if (WeaponClassCache)
				{
					BaseAIC->SetCachedWeaponClass(WeaponClassCache);
					RespawnWeaponID = WeaponClassCache->GetName();
					BaseAIC->SetCachedWeaponID(RespawnWeaponID);
					UE_LOG(LogTemp, Log,
						TEXT("[Respawn] AI '%s' 关卡预放路径, 走 ConfigSO.LevelPlacedWeaponClass fallback = '%s'"),
						*DeadController->GetName(), *RespawnWeaponID);
				}
				else
				{
					UE_LOG(LogTemp, Error,
						TEXT("[Respawn] AI '%s' 的 ConfigSO.LevelPlacedWeaponClass.LoadSynchronous() 失败. 复活后武器为空."),
						*DeadController->GetName());
				}
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Respawn] AI 复活: Controller='%s' 的 CachedWeaponID + ConfigSO.LevelPlacedWeaponClass 都为空. "
						 "【v54.4 零兜底】不允许用任何兜底. "
						 "【修复路径1】大厅入队 AI: 检查 UI ComboBox_AIWeapon 是否选了武器. "
						 "【修复路径2】关卡预放 AI: 打开 DA_AIBehaviorConfig_%s → LevelPlacedAI → LevelPlacedWeaponClass 配 (BP_Weapon_*.uasset)."),
					*DeadController->GetName(),
					*BaseAIC->GetConfig()->GetName());
				return;
			}
		}

		// ----- CharacterInfoRowName (诊断字段, 可选) -----
		// v54 不依赖任何 Profile 反查. 改为通过 RespawnAIPawnClass 查表.
		// 如果查不到, 不强制失败 (RowName 是诊断字段, 仅 SpawnAIInternal 用, 不影响 Spawn)
		FName RespawnCharInfoRowName = NAME_None;
		if (CharacterDataTable)
		{
			static const FString RespawnCtx(TEXT("Respawn.ResolveCharacterInfoRowName"));
			TArray<FName> RowNames = CharacterDataTable->GetRowNames();
			for (const FName& RowName : RowNames)
			{
				if (FCharacterInfo* Row = CharacterDataTable->FindRow<FCharacterInfo>(RowName, RespawnCtx))
				{
					if (!Row->CharacterBlueprint.IsNull()
						&& Row->CharacterBlueprint.LoadSynchronous() == RespawnAIPawnClass)
					{
						RespawnCharInfoRowName = RowName;
						break;
					}
				}
			}
		}

		// ----- MatchMode 派生 (GameState 真理源, 与 v50 一致) -----
		ERoomMatchMode MatchMode = ERoomMatchMode::None;
		if (ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>())
		{
			MatchMode = GS->CurrentMatchMode;
		}
		if (MatchMode == ERoomMatchMode::None)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomSpawn] RequestRespawn: GameState->CurrentMatchMode = None, 拒绝复活. "
					 "【v50 零兜底】不允许用硬编码 ERoomMatchMode::Melee 兜底 — 僵尸 AI 会被复活成刀战 AI. "
					 "【修复路径】检查 GameState 初始化时是否正确设置了 CurrentMatchMode."));
			return;
		}

		// ----- 构造 SpawnRequest (v54: 用 Controller.CachedXXX 真理源直接构造) -----
		FAISpawnRequest Request;
		Request.Mode = MatchMode;
		Request.FactionTag = RespawnFactionTag;
		Request.CharacterInfoRowName = RespawnCharInfoRowName;  // 诊断字段 (RowName, 可选)
		Request.AIPawnClass = RespawnAIPawnClass;                // 【v54 真理源】Controller.CachedAIPawnClass (Class 强类型)
		// 【v54 大厂架构重构】ProfileTag 已删除 (字段不存在)
		Request.WeaponID = RespawnWeaponID;                      // 【v54 真理源】Controller.CachedWeaponID (或 ConfigSO fallback)
		Request.Count = 1;
		Request.bUseTeamSpawnPoint = true;

		AAIController* ExistingAIC = Cast<AAIController>(DeadController);
		// 【v54.2 大厂架构】Config = BaseAIC->GetConfig() (运行时真理源, 复活路径唯一访问点)
		//
		// 真理源链 (v54.2 完整):
		//   第一次 Spawn: RoomService.RequestAddAI → DT_CharacterInfo.ConfigSoftRef → Request.Config → SpawnAIInternal
		//   → AIController.OnPossess → InitializeFromConfig(Config) → 注入到 BaseAIC.GetConfig() 内存
		//   复活 Spawn: RequestRespawn → 这里读 BaseAIC.GetConfig() (不需要查 DT 二次反查)
		//
		// 大厂原则:
		//   - 单一真理源: Config 不重复查表, 不走 SpawnAIInternal Config=null 路径
		//   - const_cast: SpawnAIInternal 内部只读不写, 解包零代价
		//   - 所有 AI 路径 (关卡预放/大厅入队/复活) 走同一个真理源 ConfigSO
		UAIBehaviorConfigSO* RespawnConfig = const_cast<UAIBehaviorConfigSO*>(BaseAIC->GetConfig());
		if (!RespawnConfig)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Respawn] AI='%s' 的 Config 为空. "
				     "【v54.2 零兜底】拒绝复活 — ConfigSO.SpawnInvincibilitySeconds 等真理源必须可访问. "
				     "【根因】BaseAIC.OnPossess 时 InitializeFromConfig 没被调, 或 Respawn 路径走错. "
				     "【修复】检查 SpawnAIInternal 链路是否给 BaseAIC 注入了 ConfigSO."),
				*DeadController->GetName());
			return;
		}
		const int32 Spawned = SpawnAIInternal(Request, RespawnConfig, ExistingAIC);
		if (Spawned <= 0)
		{
			// 【v52.2 大厂级可观测性增强】累计 AI 复活失败次数, 让用户/测试者第一时间知道
			// 根因: 之前每次 AI 复活失败都只打一行 Log Error, 但日志滚动太快看不出来
			//       当多个 AI 都因"配置错误"复活失败时, 场上 AI 会逐渐消失
			//       用户反馈"AI 杀着杀着全没了"就是这个根因, 但日志里只有一行一行的 Error
			// 修复: 累计计数器 + 醒目 Display 横幅, 立刻可观测
			static int32 RespawnFailureCount = 0;
			RespawnFailureCount++;
			UE_LOG(LogTemp, Display,
				TEXT("\n"
				     "================================================================\n"
				     "  【AI 复活失败累计 #%d】Controller=%s\n"
				     "  根因排查: 检查 Controller.CachedFactionTag / CachedAIPawnClass / CachedWeaponID\n"
				     "            是否在 Spawn 时已正确写入.\n"
				     "  【v54 大厂架构】UAIProfileAsset 已删除, ConfigSO 走 BaseAIC->GetConfig().\n"
				     "  修复路径: 打开 DA_AIBehaviorConfig_*.uasset → LevelPlacedAI 分类 → 全部字段已配\n"
				     "================================================================"),
				RespawnFailureCount,
				*DeadController->GetName());
		}
	}
}


// ==========================================
// 【v90 大厂架构重构】母体 Pawn 原地重建 (生化模式独有)
// ==========================================
//
// 设计动机 (Bug 2 — 2026.07.25 第 2 轮重构):
//   v89 母体变异会跳到出生点. 但业务规则 (用户 2026.07.25 明确):
//     "任何情况人类变成母体, 都是原地变成的, 不是回到出生点."
//   v89 同时还有 4 个违规:
//     1. 重复架构 — 释放 + 重新分配出生点 (母体不是重生, 不该走出生点账本)
//     2. 重复架构 — SetSpawnLoadout(MotherCharRowName, "") 写入 SpawnLoadout 账本
//        (母体不是新角色, 账本应该只由母亲系统维护 MotherCharacters)
//     3. 重复架构 — SetGenericTeamId (新 Pawn.Replicated.FactionTag 自动同阵营, 不需显式设)
//     4. 业务可调 — RowName 硬编码 TEXT("MT001") 散落业务层
//
// 大厂原则 — v90 业务分层 (与 HandlePlayerRequestSpawn 完全解耦):
//   - HandlePlayerRequestSpawn / SpawnAIInternal = 出生点调度 (Spawn 账本)
//   - MutatePawnToMother = 原地类变更 (不在 Spawn 账本 — 母体账本 = MotherCharacters)
//   - 禁止业务层自己 Destroy + SpawnActor (违反 SRP / 集中调度)
//
// v90 流程 (5 步, 极简):
//   1. 校验 Controller / RowName / OldPawn / World / DT_CharacterInfo (零兜底)
//   2. 查 DT_CharacterInfo 找 BP_MuTi 蓝图类 (零兜底)
//   3. 缓存 OldPawn.Location/Rotation (原地变位置)
//   4. UnPossess + Destroy + SpawnActor (位置 = 旧位置) + Possess
//   5. 验证 Possess 成功 (零兜底)
//
// v90 单一真理源 (Mirror v39 / v85 / v89 风格):
//   - BP_MuTi Pawn 蓝图类: DT_CharacterInfo.CharacterBlueprint (RowName 由 GM 配置)
//   - 位置:   OldPawn.GetActorLocation() (原地, 不动出生点)
//   - 旋转:   OldPawn.GetActorRotation() (原地)
//   - 阵营:   【v93.3 大厂架构修复】Step 5.5 显式切到 Offense (母体专属阵营), 不靠"自然继承"
//              旧 (v90) 自述"FactionTag 自然 Replicated 同旧 Pawn (零兜底)" 是错的!
//              实际流程: SpawnActor 出新 Pawn → 默认 FactionTag=EmptyTag → PossessedBy → SyncFactionTagFromController
//                       从 PS.CurrentFactionTag (=Defense, 旧值) 同步 → 新 Pawn.FactionTag = Defense
//                       → 母体攻击人类时, FFactionTags::CanDamage 同阵营守卫拒判 → 永远不变母体 (Session1.log line 765 bug)
//              新 (v93.3): Step 5.5 集中调度, 写 Pawn + PlayerState + AIController 三处真理源
//   - 武器:   母体不持武器, 不写 SpawnLoadout (改由 MotherCharacters 账本记身份)
//   - 视觉:   Multicast_PlayMutationFX (业务层调) + bIsMother OnRep (双保险)

bool URoomSpawnSubsystem::MutatePawnToMother(AController* Controller, const FString& MotherCharRowName)
{
	// 【v115 大厂架构诊断】增强 ENTER 日志，包含 Controller 有效性检查
	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] 【v115-Diag】MutatePawnToMother ENTER: Controller=%s(%p) MotherCharRowName=%s IsValid=%d HasPawn=%d."),
		Controller ? *Controller->GetName() : TEXT("NULL"), (void*)Controller,
		*MotherCharRowName,
		Controller ? Controller->IsValidLowLevel() : 0,
		Controller && Controller->GetPawn() ? 1 : 0);

	// 【v115 大厂架构诊断】如果 Controller 即将无效，记录栈信息
	if (Controller && !Controller->IsValidLowLevel())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] 【v115-严重】MutatePawnToMother: Controller 即将无效! 可能被其他代码销毁."));
	}

	// 母体复活时占用出生点记录
	// 【v104 新增】复活时需要记录 Controller → SpawnPoint 映射，供死亡时释放
	AController* OccupancyOwner = Controller;

	// ===== 防御层 1: 入参校验 (零兜底) =====
	if (!Controller)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutatePawnToMother: Controller 为空, 拒绝变异. "
			     "【v90 修复】检查 URoomMotherMutationSubsystem::MutateCharacterToMother 调用方."));
		return false;
	}
	if (MotherCharRowName.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutatePawnToMother: MotherCharRowName 为空, 拒绝变异. "
			     "【v90 零兜底】必须在 GM_RoomGameMode.ClassDefaults.MotherCharacterRowName 配 BP_MuTi RowName."));
		return false;
	}

	// ===== 【v103 大厂架构 — 幂等检查】防止多次调用生成多个母体 =====
	//
	// 根因 (Session2.log 2026.07.27):
	//   - 母体死亡后, Die() 派发 RequestRespawn → 设置 Timer 延迟复活
	//   - 3s 后 Timer 到期 → MutatePawnToMother 被调用 → 生成新母体 Pawn + Possess
	//   - 但 Controller 上可能还有其他 Timer (比如 UI 刷新、动画回调等) 也在触发 MutatePawnToMother
	//   - 结果: 多次调用 → 多次 SpawnActor → 生成多个母体 Pawn → 玩家同时看到多个母体
	//
	// 大厂原则 — 幂等性:
	//   - MutatePawnToMother 是"母体 Pawn 创建"的唯一入口
	//   - 第一次调用: Controller 还没有母体 Pawn → 创建新母体
	//   - 后续调用: Controller 已有母体 Pawn → 拒绝 (幂等 no-op)
	//   - 不抛异常, 不报错, 只是 Log Display 并返回 true (表示"已经完成")
	//
	// 校验逻辑:
	//   1. Controller->GetPawn() 非空 → Pawn 已存在
	//   2. Pawn->bIsMother == true → 已是母体 → 幂等返回
	//   3. Pawn->GetClass() 是母体类 → 已是母体 → 幂等返回
	{
		APawn* ExistingPawn = Controller->GetPawn();
		if (ExistingPawn && !ExistingPawn->IsPendingKillPending())
		{
			ABaseCharacter* ExistingChar = Cast<ABaseCharacter>(ExistingPawn);
			if (ExistingChar && ExistingChar->bIsMother)
			{
				UE_LOG(LogTemp, Display,
					TEXT("[MotherMutation] MutatePawnToMother: 幂等跳过 — Controller='%s' 已有母体 Pawn='%s' (bIsMother=true). "
					     "【v103 幂等修复】防止多次调用生成多个母体."),
					*Controller->GetName(),
					*ExistingPawn->GetName());
				return true;
			}
		}
	}

	// ===== 防御层 2: 拿原 Pawn =====
	//
	// 【v104 大厂架构重构】复活时从 MotherSpawnPoints 随机选取
	//
	// 旧 (v90-v103):
	//   - 复活链从 PS->LastDeathTransform 读取死亡位置
	//   - 用户需求 (2026.07.27): 母体复活应该随机在 MotherPoint 复活
	//
	// 新 (v104) 单一真理源:
	//   - 母体复活: 从 MotherSpawnPoints 随机选取一个点 (扫描时按 Faction_Mother TAG 分类)
	//   - 母体变异 (bWasAliveBeforeMutation=true): 用旧 Pawn 当前位置
	//
	// 大厂原则 — 业务分离:
	//   - 变异: 原位置 (被母体攻击时变母体)
	//   - 复活: MotherPoint (母体专属复活点)
	ABaseCharacter* OldPawn = Cast<ABaseCharacter>(Controller->GetPawn());
	FVector OldPawnLocation = FVector::ZeroVector;
	FRotator OldPawnRotation = FRotator::ZeroRotator;
	bool bWasAliveBeforeMutation = (OldPawn != nullptr);

	if (bWasAliveBeforeMutation)
	{
		// 变异核心: 用旧 Pawn 当前位置 (被母体攻击时变母体)
		OldPawnLocation = OldPawn->GetActorLocation();
		OldPawnRotation = OldPawn->GetActorRotation();
	}
	else
	{
		// 【v104 复活链路径】母体复活 — 从 MotherSpawnPoints 随机选取
		//
		// 大厂原则 — 单一真理源:
		//   - 随机选取一个未占用的 MotherSpawnPoints
		//   - 如果没有可用点 → Log Error + 拒绝复活 (零兜底)
		//   - 不允许用其他阵营的出生点 (MotherSpawnPoints 独立管理)
		ScanAndCachePlayerStarts(false); // 确保数组已扫描

		if (MotherSpawnPoints.Num() == 0)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[MotherMutation] MutatePawnToMother: 复活链 — MotherSpawnPoints 为空! "
				     "【v104 零兜底】母体复活必须有至少一个 Faction_Mother 复活点. "
				     "修复: 在地图中添加 PlayerStart, Player Start Tag = 'Faction_Mother'."),
				*Controller->GetName());
			return false;
		}

		// 过滤掉已被占用的点
		TArray<APlayerStart*> AvailableMotherSpawns;
		for (APlayerStart* SpawnPoint : MotherSpawnPoints)
		{
			if (SpawnPoint && !OccupiedSpawnPoints.Contains(SpawnPoint))
			{
				AvailableMotherSpawns.Add(SpawnPoint);
			}
		}

		if (AvailableMotherSpawns.Num() == 0)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[MotherMutation] MutatePawnToMother: 复活链 — 所有 %d 个母体复活点都被占用! "
				     "【v104 零兜底】拒绝在占用点上复活. 排查 OccupiedSpawnPoints 泄漏."),
				MotherSpawnPoints.Num());
			return false;
		}

		// 随机选取
		const int32 RandomIndex = FMath::RandRange(0, AvailableMotherSpawns.Num() - 1);
		APlayerStart* SelectedSpawn = AvailableMotherSpawns[RandomIndex];
		OldPawnLocation = SelectedSpawn->GetActorLocation();
		OldPawnRotation = SelectedSpawn->GetActorRotation();

		// 占用该点 (供后续释放)
		// 【v111.1 大厂架构修复】处理旧占用记录
		// 根因: 母体复活时调用 MutatePawnToMother, 新点 MotherSpawnPoint 被添加到 OccupiedSpawnPoints
		// 但旧的 PlayerStart 记录从未被释放，导致两个点都被占用
		// 修复: 如果 Controller 已有旧占用记录，先释放旧点，再添加新点
		if (OccupancyOwner)
		{
			if (TWeakObjectPtr<APlayerStart>* OldSpawnPtr = OccupiedSpawnByController.Find(OccupancyOwner))
			{
				if (APlayerStart* OldSpawn = OldSpawnPtr->Get())
				{
					OccupiedSpawnPoints.Remove(OldSpawn);
					UE_LOG(LogTemp, Display,
						TEXT("[MotherMutation] MutatePawnToMother: 释放旧出生点 '%s', 改为占用母体点 '%s'. Controller=%s."),
						*OldSpawn->GetName(), *SelectedSpawn->GetName(), *Controller->GetName());
				}
			}
			// 【v112 大厂架构修复】必须更新 OccupiedSpawnByController 映射！
			// 根因: 旧版只更新了 OccupiedSpawnPoints, 没有更新 OccupiedSpawnByController
			//        导致死亡时 ReleaseSpawnPointByController 找不到记录, MotherSpawnPoint 永远不被释放
			//        多次复活后所有 MotherSpawnPoint 都被占用 → ZeroVector 出生
			// 修复: 在 OccupiedSpawnPoints.Add(SelectedSpawn) 之后, 用新 key 覆盖旧映射
			// 注意: 
			//   1. OccupiedSpawnByController 的 key 是 TWeakObjectPtr<AController>, 需要转换
			//   2. 覆盖映射是正确的行为, 因为 Controller 重新占用了新点
			TWeakObjectPtr<AController> WeakOwner(OccupancyOwner);
			OccupiedSpawnByController.Add(WeakOwner, SelectedSpawn);
			UE_LOG(LogTemp, Display,
				TEXT("[MotherMutation] MutatePawnToMother: 【v112修复】更新 OccupiedSpawnByController: [%s] -> '%s'."),
				*OccupancyOwner->GetName(), *SelectedSpawn->GetName());
		}
		OccupiedSpawnPoints.Add(SelectedSpawn);

		UE_LOG(LogTemp, Display,
			TEXT("[MotherMutation] MutatePawnToMother: 复活链路径 — 从 MotherSpawnPoints 随机选取. Controller=%s Selected='%s' Loc=%s Rot=%s."),
			*Controller->GetName(),
			*SelectedSpawn->GetName(),
			*OldPawnLocation.ToString(),
			*OldPawnRotation.ToString());
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutatePawnToMother: World 为空, 拒绝变异."));
		return false;
	}

	// ===== 防御层 3: DT_CharacterInfo 校验 =====
	if (!CharacterDataTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutatePawnToMother: CharacterDataTable 为空, 拒绝变异. "
			     "【v90 修复】检查 GM_RoomGameMode::InjectSubsystemConfigs 是否调 SetCharacterDataTable."));
		return false;
	}

	// ===== Step 1: 查 DT_CharacterInfo 找 BP_MuTi 蓝图类 =====
	static const FString CharCtx(TEXT("RoomSpawnSubsystem::MutatePawnToMother"));
	FCharacterInfo* MotherInfo = CharacterDataTable->FindRow<FCharacterInfo>(FName(*MotherCharRowName), CharCtx);
	if (!MotherInfo)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutatePawnToMother: RowName='%s' 在 DT_CharacterInfo 查不到. "
			     "【v90 零兜底】必须配 BP_MuTi 蓝图类, 拒绝变异."),
			*MotherCharRowName);
		return false;
	}
	if (MotherInfo->CharacterBlueprint.IsNull())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutatePawnToMother: RowName='%s' 的 CharacterBlueprint 为空. "
			     "【v90 零兜底】必须配 BP_MuTi 蓝图类, 拒绝变异."),
			*MotherCharRowName);
		return false;
	}
	TSubclassOf<ABaseCharacter> MotherClass = MotherInfo->CharacterBlueprint.LoadSynchronous();
	if (!MotherClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutatePawnToMother: RowName='%s' LoadSynchronous 失败. "
			     "【v90 零兜底】蓝图类路径错误, 拒绝变异."),
			*MotherCharRowName);
		return false;
	}

	// ===== Step 2: 【v90 关键删除】原地变 — 不动出生点, 不动 SpawnLoadout 账本 =====
	//
	// 业务核心 (用户 2026.07.25 明确):
	//   "任何情况人类变成母体, 都是原地变成的, 不是回到出生点."
	//
	// 大厂原则 — 零兜底 + 拒绝重复架构:
	//   ❌ ReleaseSpawnPointByController(Controller)            — 原地变, 出生点占用关系不变
	//   ❌ GetAvailableSpawnPointForFaction(...)                 — 原地变, 不分配新出生点
	//   ❌ SetSpawnLoadout(MotherCharRowName, "")                — 母体账本 = MotherCharacters, 不写 SpawnLoadout
	//   ❌ SetGenericTeamId(...)                                 — Pawn.FactionTag 复制走继承, 不需显式设
	//   ❌ PlayerSpawnDataCache.Add(Controller, ...)             — 母体不算 "新 Spawn", 不动 SpawnData 账本
	//
	// 【v99 P0 修复】OldPawnLocation/OldPawnRotation 已在防御层 2 (line 2388) 设置, 不重复声明

	// 【v99 P0.1 修复】OldPawn 在复活链可能为 nullptr, 单独处理 Log
	if (bWasAliveBeforeMutation)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[MotherMutation] MutatePawnToMother: 原地变准备. OldPawn='%s' Location=%s Rotation=%s → NewClass=%s."),
			*OldPawn->GetName(),
			*OldPawnLocation.ToString(),
			*OldPawnRotation.ToString(),
			*MotherClass->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[MotherMutation] MutatePawnToMother: 复活链路径 — OldPawn 已销毁, NewClass=%s (新母体将生成在 ZeroVector)."),
			*MotherClass->GetName());
	}

	// ===== Step 3: 销毁旧 Pawn + Spawn 新母体 Pawn (原地) =====
	//
	// 【v99 P0 修复】复活链路径下 OldPawn 已销毁,跳过销毁逻辑
	//
	// 大厂原则 — 复活链兼容:
	//   - 老路径 (被母体攻击时变母体): OldPawn 存在,走完整销毁+Spawn
	//   - 复活链 (母体死后复活): OldPawn 不存在,只走 Spawn + Possess
	if (bWasAliveBeforeMutation)
	{
		// 大厂原则 — 销毁前 UnPossess, 避免 Engine 触发 PossessedBy(nullptr) 副作用
		Controller->UnPossess();

		// 【v128 P0 大厂架构修复 — 母体账本清理】销毁旧 Pawn 前先清账本
		//
		// 业务背景 (用户 2026.08.02 反馈):"NearestMotherTarget 母体被击杀复活后不更新值"
		//   - 根因 = 复活链漏 AddUnique (主因, v128 已修)
		//   - 副作用 = MotherCharacters 数组累积失效 TWeakObjectPtr (长期膨胀)
		//
		// 清理时机 (本函数内):
		//   - 在 OldPawn->Destroy() 之前调 → 业务账本先同步,再走 UE 销毁
		//   - 这样账本永远干净,GetMotherCharacters 读到的全是有效 Pawn
		//
		// 大厂原则 — 零冗余:
		//   - 即使后续 RegisterMotherPawn 再加入新 Pawn,账本容量可控
		//   - 防止 Round 跨局变 100+ 条账本 (v93.5 风险点)
		if (URoomMotherMutationSubsystem* MotherSys = URoomMotherMutationSubsystem::Get(this))
		{
			MotherSys->UnregisterMotherPawn(OldPawn);
		}

		// 【v93 大厂架构修复】显式销毁旧 Pawn 的武器 (母体不持武器 — 旧 v90 漏掉这步 = 武器残留 Bug)
		// 走 UWeaponAttachmentComponent::UnequipCurrentWeapon (单一销毁入口, v93 落地)
		//   - 旧 Pawn Destroy 会级联销毁武器, 但 UE 销毁延迟一帧
		//   - 期间武器 Mesh 仍可见 (挂在旧 Pawn 视觉坐标) — 客户端立刻收到 Destruction Bunch, 立即消失
		if (UWeaponAttachmentComponent* OldWeaponAttach = OldPawn->ResolveWeaponAttach())
		{
			OldWeaponAttach->UnequipCurrentWeapon();
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[MotherMutation] MutatePawnToMother: 旧 Pawn '%s' 找不到 WeaponAttachmentComponent, 跳过武器销毁. "
				     "【v93 警告】武器组件未挂载是配置错, 但不影响母体变异主流程."),
				*OldPawn->GetName());
		}

		OldPawn->Destroy();
	}
	else
	{
		// 【v99 复活链路径】OldPawn 已销毁,Controller 已 UnPossess (死亡链路自动),不需要重复 UnPossess
		UE_LOG(LogTemp, Display,
			TEXT("[MotherMutation] MutatePawnToMother: 复活链路径 - 跳过旧 Pawn 销毁 (OldPawn 已 SetLifeSpan 销毁)."),
			*Controller->GetName());
	}

	// SpawnCollisionHandlingOverride = AlwaysSpawn:
	//   旧 Pawn 销毁是延迟一帧, 原地可能短暂重叠 — 必须 AlwaysSpawn 跳过碰撞检查
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Controller;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ABaseCharacter* NewMotherPawn = World->SpawnActor<ABaseCharacter>(
		MotherClass, OldPawnLocation, OldPawnRotation, SpawnParams);
	if (!NewMotherPawn)
	{
		// 【v99 P0.1 修复】临时变量避免 UE_LOG 格式串模板推断失败 (UE 5.6 严格模式拒绝 *FString.ToString())
		const FString MotherClassName = MotherClass->GetName();
		const FString OldPawnLocationStr = OldPawnLocation.ToString();
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutatePawnToMother: SpawnActor<%s> 失败 (Controller=%s, Loc=%s). "
			     "【v90 零兜底】旧 Pawn 已 Destroy, 玩家暂时无 Pawn — 拒绝 Spawn 默认重来."),
			*MotherClassName,
			*Controller->GetName(),
			*OldPawnLocationStr);
		return false;
	}

	// ===== Step 4.5: 【v99.2 大厂架构 — 零等待视觉触发】立即广播 Multicast_PlayMutationFX =====
	//
	// 根因 (用户 2026.07.26 反馈):
	//   - 旧版 v99.1 RPC 放在 Step 7 (Step 5.5~6 全部写完之后) → 客户端等 4~5 帧才看到粒子
	//   - 用户期望: "角色一生成立马执行" = Pawn Replication 与粒子触发同一帧
	//
	// v99.2 大厂重构 — 提前到 SpawnActor 之后立即:
	//   - 客户端下一帧收到 RPC 时, Pawn 实体已同步到本地 (Pawn Replication 与 Multicast RPC 同一帧入队)
	//   - 粒子只校验 Owner / Mesh / 资产 (不依赖 bIsMother / FactionTag / 血量等 Replicated 字段)
	//   - 不会与 Step 5.5~6 写入冲突 — 粒子是纯视觉附加, 状态字段独立
	//   - 跳过运行期 GameState 模式校验 (v99.2 组件层已删) → 零等待
	//
	// 不破坏刀战模式 (大厂原则 — 零耦合):
	//   - 本函数只被生物化模式调用 (HandlePlayerRequestSpawn / SpawnAIInternal 走老路, 不进这里)
	//   - 母体变异 / 母体复活都走这一处 → 视觉与状态写入由 MutatePawnToMother 统一编排
	//
	// 不破坏 bIsMother / FactionTag 等真理源 (大厂原则 — 单一真理源):
	//   - Step 5.5 (FactionTag) / Step 5.7 (bIsMother) / Step 6 (PS->bIsMother) 仍在 Step 4.5 之后写
	//   - 粒子只是视觉, 与这些 Replicated 字段可独立并行
	//   - BP 蓝图订阅 OnMotherStatusChanged 仍由 Multicast_PlayMutationFX 触发 (Step 5.7 之后 OnRep 也会触发, 幂等)
	{
		const FString TargetName = NewMotherPawn->GetName();
		NewMotherPawn->Multicast_PlayMutationFX(TargetName);

		UE_LOG(LogTemp, Display,
			TEXT("[MotherMutation] MutatePawnToMother: 母体 RPC 已立即广播 (v99.2 零等待). Target=%s (SpawnActor 之后立即发, 客户端下一帧即收到)."),
			*TargetName);
	}

	// ===== Step 5: Possess + 验证 =====
	Controller->Possess(NewMotherPawn);
	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] 【v111-Diag】MutatePawnToMother: Step 5 Possess 完成. Controller='%s' GetPawn()='%s' NewMotherPawn='%s'. "),
		*Controller->GetName(),
		Controller->GetPawn() ? *Controller->GetPawn()->GetName() : TEXT("NULL"),
		*NewMotherPawn->GetName());
	if (Controller->GetPawn() != NewMotherPawn)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] MutatePawnToMother: Possess 后 Controller Pawn 与 NewMotherPawn 不一致. "
			     "【v90 零兜底】拒绝移交, NewPawn 销毁回滚."),
			*Controller->GetName());
		NewMotherPawn->Destroy();
		return false;
	}

	// ===== Step 5.1: 【v111.3 大厂架构修复】重新启动正确的 BT =====
	//
	// 根因 (用户反馈 2026.07.31):
	//   - 母体 AI 使用的是刀战模式行为树，而不是生化模式行为树
	//   - AIController 首次 Spawn 时正确持有 ModeRules.BehaviorTree (BT_ZombieModeAI)
	//   - 但 MutatePawnToMother 调用 InitializeFromConfig(Config, nullptr) 时:
	//     → BehaviorTreeOverride=nullptr 导致去读 ConfigSO.LevelPlacedBehaviorTree
	//     → ConfigSO 是关卡预放 AI 配置, LevelPlacedBehaviorTree=BT_MeleeAI (刀战!)
	//
	// 修复:
	//   - 不能传 nullptr, 必须传 ModeRules.BehaviorTree (生化模式 BT)
	//   - 需要从 GameMode 获取当前模式的 ModeRules, 再拿其 BehaviorTree
	//
	{
		if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(Controller))
		{
			// 【v111.3 P0】从 ModeRules 获取正确的 BT
			if (ARoomGameMode* RoomGM = GetWorld() ? GetWorld()->GetAuthGameMode<ARoomGameMode>() : nullptr)
			{
				ARoomGameState* RoomGS = GetWorld() ? GetWorld()->GetGameState<ARoomGameState>() : nullptr;
				if (RoomGS)
				{
					const ERoomMatchMode CurrentMode = RoomGS->CurrentMatchMode;
					FAIModeRules ModeRulesFound;
					if (RoomGM->GetModeRules(CurrentMode, ModeRulesFound))
					{
						UBehaviorTree* ZombieBT = ModeRulesFound.BehaviorTree.Get();
						if (ZombieBT)
						{
							UE_LOG(LogTemp, Display,
								TEXT("[MotherMutation] MutatePawnToMother: 【v111.3】重新启动 BT: Controller='%s' BT='%s' (来自 ModeRules[%d])."),
								*Controller->GetName(), *ZombieBT->GetName(), static_cast<int32>(CurrentMode));

							// 重新初始化 AI 配置，传入正确的 BT
							UAIBehaviorConfigSO* MutableConfig = const_cast<UAIBehaviorConfigSO*>(BaseAIC->GetConfig());
							if (MutableConfig)
							{
								BaseAIC->InitializeFromConfig(MutableConfig, ZombieBT);
							}
							else
							{
								UE_LOG(LogTemp, Error,
									TEXT("[MotherMutation] MutatePawnToMother: 【v111.3】Config 为空, 无法重新启动 BT. Controller='%s'."),
									*Controller->GetName());
							}
						}
						else
						{
							UE_LOG(LogTemp, Error,
								TEXT("[MotherMutation] MutatePawnToMother: 【v111.3】ModeRules.BehaviorTree 为空. CurrentMode=%d. "
								     "【零兜底】生化模式必须配置 BehaviorTree. 修复: BP_GM_RoomGameMode → ModeRules → Zombie → BehaviorTree 拖入 BT_ZombieModeAI."),
								static_cast<int32>(CurrentMode));
						}
					}
					else
					{
						UE_LOG(LogTemp, Error,
							TEXT("[MotherMutation] MutatePawnToMother: 【v111.3】GetModeRules 失败. CurrentMode=%d. "
							     "【零兜底】拒绝重新启动 BT."),
							static_cast<int32>(CurrentMode));
					}
				}
				else
				{
					UE_LOG(LogTemp, Error,
						TEXT("[MotherMutation] MutatePawnToMother: 【v111.3】RoomGameState 为空. Controller='%s'."),
						*Controller->GetName());
				}
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[MotherMutation] MutatePawnToMother: 【v111.3】RoomGameMode 为空. Controller='%s'."),
					*Controller->GetName());
			}
		}
	}

	// ===== Step 5.5: 【v93.3 大厂架构 — 母体阵营切换】集中调度 FactionTag 写入 =====
	//
	// 根因 (用户 2026.07.25 反馈 Session1.log line 765):
	//   - 母体 (BP_MuTi) 攻击人类 (BP_SWAT_C_1) 时, Server_ReportMotherAttackHit_Implementation
	//     调 FFactionTags::CanDamage(Attacker.FactionTag, Victim.FactionTag, ...)
	//   - CanDamage 守卫 3: 同阵营 → 拒绝扣血
	//   - 但 Attacker.FactionTag 仍是 Defense (从旧 Pawn 继承, PossessedBy 没切) → 永远同阵营
	//   - 结果: 母体攻击永远不变母体
	//
	// 旧 (v90-v93.2) 错误注释 (v90 自述):
	//   - 注释写 "FactionTag = 旧 Pawn 阵营 (Replicated 走继承)" — 这是错的!
	//   - 新 SpawnActor 的 Pawn 走默认构造函数 → FactionTag = EmptyTag
	//   - PossessedBy → SyncFactionTagFromController → 从 PlayerState.CurrentFactionTag (=Defense, 旧值) 同步
	//   - 所以新 Pawn.FactionTag = Defense, 而不是"自然复制旧 Pawn 阵营"
	//
	// 新 (v93.3) 大厂架构 — 集中调度 + 单一真理源:
	//   母体变异时, 业务核心 = 切换到母体专属阵营 (Faction.Offense)
	//   真真理源链 (3 个写点必须同步, 否则复活链会回滚):
	//     1. NewMotherPawn->FactionTag = Offense (Replicated 自动同步所有客户端)
	//     2. PlayerState.CurrentFactionTag = Offense (玩家路径真理源 — 不写则下次复活 SyncFactionTagFromController 又回 Defense)
	//     3. BaseAIController.CachedFactionTag = Offense (AI 路径真理源 — 不写则 RequestRespawn 又读 Defense)
	//   + SetGenericTeamId (UE AIPerception 协议层同步, 客户端 SightSense 立即生效)
	//
	// 单一真理源 — 3 处写点必须同步, 否则复活链会回滚:
	//   - Pawn.FactionTag       → AI 当前能看到母体是 Offense (本次变异后立刻攻击人类能成功)
	//   - PlayerState.CurrentFactionTag → 下次复活 SyncFactionTagFromController 不回滚
	//   - AIC.CachedFactionTag  → RequestRespawn 拿阵营时仍能拿到 Offense
	// 任意 1 处漏写 → 下一帧或下一次复活会回退到 Defense, 又被同阵营守卫拒判
	//
	// 不破坏刀战模式 (大厂原则 — 零耦合):
	//   - 本函数只被 MutateCharacterToMother 调用, 母体变异专属入口
	//   - 刀战模式不调本函数 → 永远不切 Offense → 刀战逻辑零影响
	{
		const FGameplayTag MotherFactionTag = FFactionTags::Offense();
		const FGenericTeamId MotherTeamID = FFactionTags::ToGenericTeamId(MotherFactionTag);

		// (1) 写 Pawn.FactionTag — Replicated 真理源 (单一入口)
		NewMotherPawn->FactionTag = MotherFactionTag;
		NewMotherPawn->SetGenericTeamId(MotherTeamID); // 同步 UE AIPerception 协议层缓存

		// (2) 写 PlayerState.CurrentFactionTag (玩家路径真理源) — 防止下次复活被 SyncFactionTagFromController 回滚
		if (ARoomPlayerState* PS = Controller->GetPlayerState<ARoomPlayerState>())
		{
			PS->CurrentFactionTag = MotherFactionTag; // ReplicatedUsing = OnRep_FactionTag, 客户端 OnRep 自动触发
		}

		// (3) 写 BaseAIController.CachedFactionTag (AI 路径真理源) — 防止 RequestRespawn 又读 Defense
		if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(Controller))
		{
			BaseAIC->SetCachedFactionTag(MotherFactionTag); // 单一公开 API, 不写 Controller 私有字段
		}

		UE_LOG(LogTemp, Display,
			TEXT("[MotherMutation] MutatePawnToMother: 母体阵营切换完成. NewMotherPawn='%s' FactionTag='%s' "
			     "(Pawn + PlayerState + AIController 三处真理源同步, 防止复活链回滚)."),
			*NewMotherPawn->GetName(), *MotherFactionTag.ToString());
	}

	// ===== Step 5.6: 【v133.4 大厂架构 — 母体血量初始化】真理源迁移到 PlayerConfigAsset.MotherMaxHealth (玩家) 或 ConfigSO.MotherMaxHealth (AI) =====
	//
	// 业务规则 (用户 2026.08.02 明确):
	//   - UPlayerConfigAsset 也要分人类最大血量和母体最大血量
	//   - UAIBehaviorConfigSO 也要分人类最大血量和母体最大血量
	//   - 母体血量真理源:
	//     - 玩家 = PlayerConfigAsset.MotherMaxHealth
	//     - AI   = ConfigSO.MotherMaxHealth (BaseAIController.GetConfig())
	//   - 不再读 GM.MotherMaxHealth (废弃, 但保留字段兼容老配置)
	//
	// 历史:
	//   - v93.4 (2026.07.25) 旧真理源 = GM.MotherMaxHealth = 200
	//   - v133.4 (2026.08.02) 新真理源 = PlayerConfigAsset.MotherMaxHealth (玩家) 或 ConfigSO.MotherMaxHealth (AI)
	//
	// 大厂原则 — 配置驱动 + 单一真理源:
	//   - 真理源按 Controller 类型分流 (玩家/AI 是两个独立真理源)
	//   - 策划可在 DA_PlayerConfig / DA_AIBehaviorConfig_XXX 各自配
	//   - 零硬编码: 不在 C++ 写 200.0f, 全部走配置
	//   - 调 HealthComponent->InitializeHealth(MotherMaxHealth) 自动:
	//     1) 写 MaxHealth = MotherMaxHealth (服务器本地)
	//     2) 写 CurrentHealth = MotherMaxHealth (满血)
	//     3) 清 bIsDead
	//     4) 服务器主动 Broadcast OnHealthChanged
	//     5) DOREPLIFETIME(MaxHealth) 自动同步客户端
	//
	// 不破坏刀战模式 (大厂原则 — 零耦合):
	//   - 本步只在母体变异时跑 (Step 5.5 已切 Offense, 这是生化专属)
	//   - 刀战模式不走这里
	{
		// 【v133.4 真理源分离】按 Controller 类型分流
		float MotherMaxHealthValue = 0.f;

		if (APlayerController* PC = Cast<APlayerController>(Controller))
		{
			// 玩家路径: 真理源 = PlayerConfigAsset.MotherMaxHealth
			if (PlayerConfigAsset && PlayerConfigAsset->MotherMaxHealth > 0.f)
			{
				MotherMaxHealthValue = PlayerConfigAsset->MotherMaxHealth;
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[MotherMutation] MutatePawnToMother: 玩家路径 — PlayerConfigAsset.MotherMaxHealth 无效 (Pawn=%s). "
					     "【v133.4 修复】请在 DA_PlayerConfig.uasset → Config|Health → Mother Max Health 配置有效值 (>0). "
					     "【废弃警告】GM.MotherMaxHealth 不再是真理源, 请迁移到 PlayerConfigAsset."),
					*NewMotherPawn->GetName());
			}
		}
		else if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(Controller))
		{
			// AI 路径: 真理源 = ConfigSO.MotherMaxHealth
			// 【v133.4.1 大厂架构】GetConfig() 返回 const, 只读字段, const_cast 消除编译错误
			if (UAIBehaviorConfigSO* Config = const_cast<UAIBehaviorConfigSO*>(BaseAIC->GetConfig()))
			{
				if (Config->MotherMaxHealth > 0.f)
				{
					MotherMaxHealthValue = Config->MotherMaxHealth;
				}
				else
				{
					UE_LOG(LogTemp, Error,
						TEXT("[MotherMutation] MutatePawnToMother: AI 路径 — ConfigSO.MotherMaxHealth=%.1f (≤0, 配错). "
						     "【v133.4 修复】请在 DA_AIBehaviorConfig_XXX → Health → Mother Max Health 配置有效值 (>0). "
						     "Pawn=%s"),
						Config->MotherMaxHealth, *NewMotherPawn->GetName());
				}
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[MotherMutation] MutatePawnToMother: AI 路径 — BaseAIController->GetConfig() 为空. "
					     "【v133.4 零兜底】拒绝写血量. "
					     "Pawn=%s. 检查 ConfigSO 是否在 AIController ClassDefaults 配."),
					*NewMotherPawn->GetName());
			}
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[MotherMutation] MutatePawnToMother: Controller 既不是 Player 也不是 ABaseAIController 派生 (Pawn=%s). "
				     "【v133.4 零兜底】拒绝写血量."),
				*NewMotherPawn->GetName());
		}

		if (MotherMaxHealthValue > 0.f)
		{
			if (UHealthComponent* MotherHC = NewMotherPawn->ResolveHealthComponent())
			{
				MotherHC->InitializeHealth(MotherMaxHealthValue); // 写 MaxHealth + CurrentHealth + 广播
				UE_LOG(LogTemp, Display,
					TEXT("[MotherMutation] MutatePawnToMother: 母体血量初始化完成. NewMotherPawn='%s' MaxHealth=%.1f "
					     "(真理源=%s, Controller=%s). "
					     "客户端会通过 DOREPLIFETIME(MaxHealth) 同步."),
					*NewMotherPawn->GetName(), MotherMaxHealthValue,
					Cast<APlayerController>(Controller) ? TEXT("PlayerConfigAsset.MotherMaxHealth") : TEXT("ConfigSO.MotherMaxHealth"),
					*Controller->GetName());
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[MotherMutation] MutatePawnToMother: NewMotherPawn='%s' 找不到 HealthComponent. "
					     "【v93.4 零兜底】拒绝写血量, 母体会保持默认 100 满血. "
					     "检查 BP_MuTi 蓝图是否挂了 HealthComponent."),
					*NewMotherPawn->GetName());
			}
		}
	}

	// ===== Step 5.7: 【v99.1 大厂架构 — 母体 Pawn 业务字段集中写入】Pawn.bIsMother + bIsHuman =====
	//
	// 历史 (v90-v99): 这两个字段写在 URoomMotherMutationSubsystem::MutateCharacterToMother Step 3,
	//                  与 MutatePawnToMother 各管一段 → 复活链直接调本函数会漏写 → bIsMother 永远 false → 错
	//
	// 新 (v99.1) 单一真理源 — 母体 Pawn 创建入口 = 本函数:
	//   - bIsMother / bIsHuman / PS->bIsMother / Multicast_PlayMutationFX 全部由本函数统一负责
	//   - 复活链 (RequestRespawn 直接调本函数) 与首次变异 (业务层调本函数) 走同一段写入 → 永不漏
	//   - 业务层 MutateCharacterToMother 中重复的 Step 3 + Step 3.7 全部删除 (后续 todo 处理)
	//
	// 互斥语义:
	//   - bIsMother = true, bIsHuman = false(注释约定,OnRep_bIsMother 校验)
	//   - 【v93.2 大厂架构】同时设两个字段, 顺序固定: 先 bIsHuman=false, 再 bIsMother=true
	//     — 这样客户端 OnRep_bIsMother 校验时 bIsHuman 已先于 bIsMother 到达 → 消除时序误报
	NewMotherPawn->bIsHuman = false;
	NewMotherPawn->bIsMother = true;

	// 【v93.2 大厂架构 — 服务器侧写入确认】写入后立即日志显示两字段值
	//   服务器侧是 100% 真相源, 这里看到的值是事实. 客户端 OnRep 看到的时序差异由客户端修复 (SetTimerForNextTick).
	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] MutatePawnToMother Step 5.7 写入确认: Pawn=%s bIsMother=%d bIsHuman=%d (互斥应一致). ")
		TEXT("【v93.2 大厂架构】服务器侧先 bIsHuman=false 再 bIsMother=true, 客户端 OnRep_bIsMother 延迟到下一帧校验, 消除时序误报."),
		*NewMotherPawn->GetName(),
		NewMotherPawn->bIsMother ? 1 : 0,
		NewMotherPawn->bIsHuman ? 1 : 0
	);

	// 【v99 P0.1 修复】临时变量避免 UE_LOG 格式串模板推断失败 (UE 5.6 严格模式)
	const FString ControllerName = Controller->GetName();
	const FString NewPawnName = NewMotherPawn->GetName();
	const FString MotherClassName = MotherClass->GetName();
	const FString OldPawnLocationStr = OldPawnLocation.ToString();
	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] MutatePawnToMother: 母体原地变异成功. Controller=%s → NewPawn=%s (Class=%s, Location=%s, RowName=%s). "
		     "【v90 业务核心】母体在原地, 未回出生点. 母体账本 + 视觉特效 + 业务标记由 MotherMutationSubsystem 接管."),
		*ControllerName,
		*NewPawnName,
		*MotherClassName,
		*OldPawnLocationStr,
		*MotherCharRowName);

	// ===== Step 6: 【v99 P0 大厂架构 — 复活链真理源】写 PlayerState.bIsMother = true =====
	//
	// 业务核心 (用户 2026.07.26 明确):
	//   - 生化模式: 母体死后复活, 必须原地复活成母体
	//
	// 根因 (Session1.log line 1161-1218):
	//   - 旧版复活链直接调 HandlePlayerRequestSpawn, 用 PlayerSpawnDataCache.CharID (人类 CharID)
	//   - 即使死前是 BP_MuTi, 复活时一律走人类路径 → 复活成 BP_SWAT_C 人类 → 错
	//
	// 大厂原则 — 单一真理源 (SSOT) — MutatePawnToMother 统一写 PS->bIsMother:
	//   - 老路径 (MutateCharacterToMother → MutatePawnToMother): 业务层 Step 3.7 已删除,这里**唯一**写点
	//   - 复活链 (RequestRespawn → 直接 MutatePawnToMother): **这里**写,保证复活链不漏
	//   - **统一真理源**: MutatePawnToMother 是"母体 Pawn 创建"的最终入口 → 它写 PS->bIsMother 才是真理源
	//
	// 不破坏刀战模式:
	//   - 刀战模式从不调 MutatePawnToMother → PS->bIsMother 永远是 false → RequestRespawn 走老路径
	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{
		if (ARoomPlayerState* PS = PC->GetPlayerState<ARoomPlayerState>())
		{
			PS->bIsMother = true; // 服务器本地写, Replicated 自动同步客户端
			UE_LOG(LogTemp, Display,
				TEXT("[MotherMutation] MutatePawnToMother: 已设 PlayerState.bIsMother=true (复活链真理源 — 单一入口). "
				     "下次 RequestRespawn 读到 PS->bIsMother=true → 走母体原地复活路径. "
				     "【v99 P0】兼容复活链 (直接调 MutatePawnToMother 跳过业务层)."));
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[MotherMutation] MutatePawnToMother: PlayerController='%s' 没有 ARoomPlayerState — 复活链真理源缺失. "
				     "【v99 零兜底】母体死后复活会错误地变成人类. "
				     "修复: 检查 ARoomGameMode::AddPlayerToRoom 是否正确 Spawn 了 ARoomPlayerState."),
				*PC->GetName());
		}
	}
	else if (ABaseAIController* AIC = Cast<ABaseAIController>(Controller))
	{
		// ============================================================
		// 【v109 大厂架构 — AI 母体复活链真理源】CachedIsMother = true
		// ============================================================
		//
		// 业务核心 (用户 2026.07.31 明确):
		//   "生化模式：ai是母体。被打死后成了人类，这是错的，应该还是母体。
		//    请参照玩家的业务逻辑。"
		//
		// 镜像对称 (玩家 vs AI):
		//   - 玩家: PS->bIsMother=true (Server 写, Replicated, 复活时读)
		//   - AI:   AIC->CachedIsMother=true (Server 写, 不 Replicate, 复活时读)
		//   - 两者都是"Controller 内存真理源", Pawn 销毁后字段仍存活
		//
		// 【v93.2 互斥语义镜像】AI 路径为什么不需要 AIC->CachedIsHuman:
		//   - bIsHuman 是 Pawn 层字段 (ABaseCharacter), 不属于 Controller
		//   - AIC 没有也不会有 CachedIsHuman (语义错位, AIC 不管"人类身份")
		//   - AI 路径的"互斥语义"由 Step 5.7 集中保证: Pawn.bIsMother=true + Pawn.bIsHuman=false
		//   - AIC.CachedIsMother 与 Pawn.bIsHuman 不冲突: 一个是"Controller 复活真理源", 一个是"Pawn 运行时身份"
		//
		// 不破坏刀战模式:
		//   - 刀战模式从不调 MutatePawnToMother → CachedIsMother 永远是 false → RequestRespawn 走老路径
		//   - 与 CachedFactionTag / CachedAIPawnClass / CachedWeaponID 同模式 (镜像)
		//
		// 不重复架构:
		//   - 复用现有 MutatePawnToMother Step 6 (玩家分支已是单一真理源入口)
		//   - AI 分支与之对称, 互不耦合
		// ============================================================
		AIC->SetCachedIsMother(true);
		UE_LOG(LogTemp, Display,
			TEXT("[MotherMutation] MutatePawnToMother: 已设 AIC.CachedIsMother=true (AI 复活链真理源 — 镜像玩家 PS->bIsMother). ")
			TEXT("下次 RequestRespawn 读到 CachedIsMother=true → 走母体原地复活路径, 不会再变成人类. ")
			TEXT("【v109 大厂架构】AI 没有 PS, 必须在 Controller 上持有等价的运行时真理源. ")
			TEXT("【v93.2】互斥语义由 Step 5.7 Pawn.bIsHuman=false 保证 (Pawn 层), AIC 不重复镜像.")
		);
	}
	// ===== Step 7: 【v99.2 已删除 — RPC 已在 Step 4.5 立即广播】=====
	//
	// 历史 (v99.1):
	//   - 旧逻辑: Multicast_PlayMutationFX 在 Step 7 触发
	//   - 客户端需要等 Step 5.5 (FactionTag=Offense) / Step 5.6 (血量) / Step 5.7 (bIsMother) / Step 6 (PS->bIsMother)
	//   - 客户端每帧等一组 Replicated 字段 → 4~5 帧后才看到粒子 = "延时"
	//
	// v99.2 大厂重构:
	//   - RPC 提前到 Step 4.5 (SpawnActor 之后立即)
	//   - 粒子只校验 Owner / Mesh / 资产, 不依赖后续 Replicated 字段
	//   - 状态写入 (Step 5.5 ~ 6) 仍走原顺序, 互不耦合
	//   - 整个项目 Multicast_PlayMutationFX 唯一触发点 = MutatePawnToMother Step 4.5

	// ===== Step 7: 【v105 大厂架构新增】刷新母体专属头像 UI =====
	//
	// 业务规则 (用户 2026.07.27 明确):
	//   - 变成母体后, 头像必须变成母体图片 (DT_CharacterInfo 的 MuTi 行 AvatarIcon)
	//   - 武器图标必须显示 MT001 (DT_WeaponInfo 的 MT001 行 WeaponIcon)
	//   - 武器弹药 UI 必须隐藏 (母体无武器)
	//
	// 大厂原则 - 集中调度:
	//   - 母体 UI 刷新入口 = MutatePawnToMother 末尾 (与粒子特效 Multicast_PlayMutationFX 同位置)
	//   - CharacterIconComponent::RefreshCharacterIcon 会根据 Owner->bIsMother 自动:
	//     1. 使用母体头像 ID ("MuTi") 查表显示母体头像
	//     2. 调用 RefreshWeaponIconOnHUD() 显示 MT001 武器图标
	//     3. 【v105.2】弹药 RPC BroadcastWeaponAmmoInfo (母体路径强制 1/1) - 弹药 UI 显示 "1/1"
	//
	// 不破坏刀战模式:
	//   - 刀战模式不调 MutatePawnToMother → 不走这段代码 → 头像/武器图标逻辑零影响
	//   - bIsMother 永远是 false → RefreshCharacterIcon 走正常人类路径
	if (ABaseCharacter* MotherChar = Cast<ABaseCharacter>(NewMotherPawn))
	{
		if (UCharacterIconComponent* IconComp = MotherChar->FindComponentByClass<UCharacterIconComponent>())
		{
			// 【v105 诊断日志】确认 bIsMother 状态
			UE_LOG(LogTemp, Display,
				TEXT("[MotherMutation] MutatePawnToMother: 已调用 RefreshCharacterIcon 刷新母体 UI. Pawn=%s, bIsMother=%d, bIsHuman=%d"),
				*NewMotherPawn->GetName(), MotherChar->bIsMother ? 1 : 0, MotherChar->bIsHuman ? 1 : 0);
			IconComp->RefreshCharacterIcon();

			// 【v105.2 大厂架构】母体弹药广播 (强制 1/1)
			// 业务规则 (用户 2026.07.27 反馈): 母体仍显示弹药 UI, 弹药数据固定 1/1
			// CharacterIconComponent::BroadcastWeaponAmmoInfo 已识别 bIsMother 路径 (强制 1/1)
			IconComp->BroadcastWeaponAmmoInfo(MotherChar);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[MotherMutation] MutatePawnToMother: 找不到 CharacterIconComponent (Pawn=%s). "
					 "【v105 零兜底】拒绝刷新母体 UI, 头像/武器图标不会更新."),
				*NewMotherPawn->GetName());
		}
	}

	// 【v128 P0 修复 2026.08.02】母体账本同步 — 单一真理源 + 集中调度
	//
	// 根因 (用户 2026.08.02 反馈):
	//   "NearestMotherTarget BB Key 在母体被击杀复活后不更新值. 被击杀后就一直是空"
	//
	// 触发链:
	//   1. 母体首次变异: URoomMotherMutationSubsystem::MutateCharacterToMother 调
	//      MotherCharacters.AddUnique(NewMotherPawn) (v128 前是直接的数组操作) → 账本有母体
	//   2. 母体被击杀: ABaseCharacter::Destroy → TWeakObjectPtr.Get() 失效
	//      但 TWeakObjectPtr 本身没从数组移除 (大厂原则 — 失效检测由读端 IsValid 处理)
	//   3. 母体复活 (3s 后): RequestRespawn → MutatePawnToMother 直接调 (绕过业务层)
	//      → 新 NewMotherPawn 生成 + Possess,但 **MotherCharacters 数组里挂的是旧 TWeakObjectPtr**
	//   4. 人类 BTService_UpdateZombieTargets 读 MotherSys->GetMotherCharacters():
	//      - 旧 TWeakObjectPtr.Get() = nullptr (Pawn 已 Destroy) → IsValid 守卫 → 跳过
	//      - **没有任何代码补回新 NewMotherPawn 到 MotherCharacters**
	//   5. → BTService 找不到活母体 → BB.NearestMotherTarget 永远是空
	//
	// 旧代码 (v99.1~v127) 反模式:
	//   - MotherCharacters 写入 = MutateCharacterToMother (业务层, 首次变异才走)
	//   - MotherCharacters 复活 = 漏写 (MutatePawnToMother 只管 Pawn 重建)
	//   - 数据源分裂: 首次变异走业务层, 复活走 SpawnSubsystem
	//
	// v128 修复 (大厂原则 - 单一真理源 + 集中调度):
	//   - MotherCharacters 写入 = 母体 Pawn 创建唯一入口 = MutatePawnToMother (本函数)
	//   - 业务层 MutateCharacterToMother 已有的 RegisterMotherPawn (v128 重构) → 保留作为业务账本不变量
	//     (首次变异时也调本函数, 所以双保险 — AddUnique 内部幂等)
	//   - 复活链直接调本函数 → 现在也走 RegisterMotherPawn → 不漏
	//
	// 抗 "重复 RegisterMotherPawn" 副作用:
	//   - AddUnique 内部用 == 判等, 同 Pawn 不重复
	//   - TWeakObjectPtr 比较的是对象身份, 不是指针地址, 安全
	//
	// 大厂原则 — 集中调度 (零兜底):
	//   - 任何 "添加母体到账本" 都走 RegisterMotherPawn 公开接口
	//   - 不允许业务层直接 MotherCharacters.AddUnique (违反 SSOT)
	//   - 未来扩展 (统计、事件、Replicate) 集中在这里, 调用方零改动
	if (URoomMotherMutationSubsystem* MotherSys = URoomMotherMutationSubsystem::Get(this))
	{
		MotherSys->RegisterMotherPawn(NewMotherPawn);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherMutation] 【v128 严重】MutatePawnToMother: 找不到 RoomMotherMutationSubsystem, "
			     "无法同步 MotherCharacters 账本. BTService_UpdateZombieTargets 读不到复活母体. "
			     "【修复路径】1) BP_GM_RoomGameMode.uasset ClassDefaults → MotherMutationSubsystemClass "
			     "应配 RoomMotherMutationSubsystem; 2) 检查 GM->InjectSubsystemConfigs 调用时机."),
			*NewMotherPawn->GetName());
	}

	// 【v111-Diag】确认 Controller 存活
	UE_LOG(LogTemp, Display,
		TEXT("[MotherMutation] 【v111-Diag】MutatePawnToMother 成功完成: Controller='%s' Pawn='%s' IsValid=%d."),
		*Controller->GetName(),
		Controller->GetPawn() ? *Controller->GetPawn()->GetName() : TEXT("NULL"),
		Controller->IsValidLowLevel() ? 1 : 0);

	// ============================================================
	// 【v133.3.1 2026.08.02 大厂架构 — 单一真理源修复】母体 Spawn 后仅切速度,不重置血量
	// ============================================================
	//
	// 根因 (v133.3 引入):
	//   - 调 ApplyCharacterConfigToCharacter → 内部调 HC->InitializeHealth(PlayerConfigAsset->MaxHealth=100)
	//   - 覆盖了 MutatePawnToMother line 3132 已设的 GM.MotherMaxHealth=200
	//   - 用户反馈: "变成母体生命值还是100所以闪红"
	//   - 母体血量被覆盖成 100,业务错了
	//
	// 大厂原则 — 零覆盖 + 单一真理源:
	//   - 母体血量真理源 = GM.MotherMaxHealth (已在 Step 5.6 写入)
	//   - 母体速度真理源 = PlayerConfigAsset.MotherMaxWalkSpeed (bIsMother 分流)
	//   - 不允许任何后续路径重写血量
	//
	// 修复方案:
	//   - 只调 SetMaxWalkSpeedByMotherStatus (新方法),不动血量/能量/无敌期等
	//   - 这是"最小干预"原则 — 只动需要变的字段
	// ============================================================
	if (NewMotherPawn)
	{
		if (UCharacterMovementComponent* MoveComp = NewMotherPawn->GetCharacterMovement())
		{
			if (PlayerConfigAsset && PlayerConfigAsset->MotherMaxWalkSpeed > 0.f)
			{
				MoveComp->MaxWalkSpeed = PlayerConfigAsset->MotherMaxWalkSpeed;
				MoveComp->MaxWalkSpeedCrouched = PlayerConfigAsset->MotherMaxWalkSpeed * 0.5f;

				UE_LOG(LogTemp, Display,
					TEXT("[MotherMutation] MutatePawnToMother: 【v133.3.1 修复】母体速度切到 MotherMaxWalkSpeed=%.1f "
					     "(仅切速度, 不动血量/能量/无敌期 — 血量真理源仍是 GM.MotherMaxHealth=%.1f). "
					     "Pawn=%s"),
					PlayerConfigAsset->MotherMaxWalkSpeed,
					NewMotherPawn->ResolveHealthComponent()
						? NewMotherPawn->ResolveHealthComponent()->GetMax()
						: 0.f,
					*NewMotherPawn->GetName());
			}
		}
	}

	return true;
}