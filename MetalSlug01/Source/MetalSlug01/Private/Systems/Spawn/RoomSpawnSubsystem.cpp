// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/Spawn/RoomSpawnSubsystem.h"
#include "Systems/RoomGameMode.h"
#include "Systems/RoomGameState.h"
#include "Systems/Core/RoomPlayerState.h"
#include "Systems/BaseAIController.h"
#include "Systems/RoomPlayerController.h"
#include "Data/Enums/RoomEnums.h"
#include "Data/Faction/FactionTags.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Data/Tables/CharacterTableRow.h"
#include "Data/Tables/SpawnTableRow.h"
#include "Data/Tables/WeaponTableRow.h"      // 【v54.3 新增】FWeaponInfo (DT_WeaponInfo 行)
#include "Weapons/BaseWeapon.h"              // 【v54.3 新增】ABaseWeapon (ResolveWeaponClassFromID 返回类型)
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
	if (UHealthComponent* HC = Character->ResolveHealthComponent())
	{
		HC->InitializeHealth(PlayerConfigAsset->MaxHealth);
		UE_LOG(LogTemp, Log,
			TEXT("[RoomSpawnSubsystem] ApplyCharacterConfig: MaxHealth=%.1f (Pawn=%s)"),
			PlayerConfigAsset->MaxHealth, *Character->GetName());
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
		MoveComp->MaxWalkSpeed = PlayerConfigAsset->MaxWalkSpeed;
		// 下蹲速度默认是正常速度的一半
		MoveComp->MaxWalkSpeedCrouched = PlayerConfigAsset->MaxWalkSpeed * 0.5f;
		UE_LOG(LogTemp, Log,
			TEXT("[RoomSpawnSubsystem] ApplyCharacterConfig: MaxWalkSpeed=%.1f CrouchedSpeed=%.1f (Pawn=%s)"),
			PlayerConfigAsset->MaxWalkSpeed,
			PlayerConfigAsset->MaxWalkSpeed * 0.5f,
			*Character->GetName());
	}

	UE_LOG(LogTemp, Log,
		TEXT("[RoomSpawnSubsystem] ApplyCharacterConfigToCharacter 完成: Pawn=%s"),
		*Character->GetName());
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

	// 兼容旧 TAG
	const FName LegacyTag_Attack = FName(TEXT("Faction_Attack"));

	int32 TotalFound = 0;
	int32 MatchedFaction = 0;
	int32 LegacyMatched = 0;
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
				TEXT("[Spawn] PlayerStart '%s' 没有阵营 Tag (期望 '%s' / '%s'). "
				     "【拒绝默认归类 - 大厂原则】此点不会分配给任何阵营."
				     " 修复: 在编辑器 Details → Player Start Tag 配 Faction_Offense / Faction_Defense."),
				*StartName,
				*TAG_Faction_Offense.ToString(),
				*TAG_Faction_Defense.ToString());
		}
	}

	bSpawnPointsScanned = true;

	UE_LOG(LogTemp, Warning,
		TEXT("[Spawn] ScanPlayerStarts 完成: 总 %d, 匹配阵营 %d, 旧 TAG %d, 错误跳过 %d. "
		     "AttackSpawnPoints=%d, DefenseSpawnPoints=%d"),
		TotalFound, MatchedFaction, LegacyMatched, ErrorSkipped,
		AttackSpawnPoints.Num(), DefenseSpawnPoints.Num());
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
		OutWeaponID = Cached->WeaponID;
		return true;
	}
	return false;
}

void URoomSpawnSubsystem::SetPlayerSpawnData(uint32 ControllerUniqueID, const FString& CharID, const FString& WeaponID)
{
	PlayerSpawnDataCache.FindOrAdd(ControllerUniqueID) = FPlayerSpawnData{CharID, WeaponID};
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
		ApplyCharacterConfigToCharacter(AIPawn);

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
				const FString CharID = PS->GetSelectedCharacterID();
				const FString WeaponID = PS->GetSelectedWeapon1ID();

				if (CharID.IsEmpty() || WeaponID.IsEmpty())
				{
					UE_LOG(LogTemp, Error,
						TEXT("[Spawn] SpawnAllPlayersIntoBattle: 玩家 '%s' Loadout 不完整 (CharID='%s', WeaponID='%s'). "
						     "【v31.2 零兜底】拒绝 Spawn."),
						*PS->GetPlayerName(), *CharID, *WeaponID);
					continue;
				}

				HandlePlayerRequestSpawn(PC, CharID, WeaponID);
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

void URoomSpawnSubsystem::HandlePlayerRequestSpawn(AController* PlayerToSpawn, const FString& CharRowName, const FString& WeaponRowName)
{
	if (!PlayerToSpawn) return;

	UE_LOG(LogTemp, Warning, TEXT("[Spawn] HandlePlayerRequestSpawn called. Char='%s', Weapon='%s'"),
		*CharRowName, *WeaponRowName);

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
	const FString FinalWeaponID = WeaponRowName;

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
			TEXT("[Spawn] HandlePlayerRequestSpawn: WeaponID 为空 (Player=%s). 拒绝 Spawn."),
			*PlayerToSpawn->GetName());
		return;
	}

	// Step 2: 写缓存 (供复活读)
	FPlayerSpawnData SpawnData;
	SpawnData.CharID = FinalCharID;
	SpawnData.WeaponID = FinalWeaponID;
	PlayerSpawnDataCache.Add(PlayerToSpawn->GetUniqueID(), SpawnData);

	// 同步 PlayerState
	if (ARoomPlayerState* PS = PlayerToSpawn->GetPlayerState<ARoomPlayerState>())
	{
		PS->SetPlayerLoadout(FinalCharID, FinalWeaponID, PS->GetSelectedWeapon2ID());
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
			if (!FinalWeaponID.IsEmpty())
			{
				TSubclassOf<ABaseWeapon> WeaponClass = ResolveWeaponClassFromID(FinalWeaponID);
				if (WeaponClass)
				{
					UE_LOG(LogTemp, Log,
						TEXT("[Spawn] HandlePlayerRequestSpawn 触发武器 Spawn: WeaponID=%s → WeaponClass=%s (Pawn=%s, Player=%s)"),
						*FinalWeaponID, *WeaponClass->GetName(), *SpawnedChar->GetName(), *PlayerToSpawn->GetName());
					SpawnedChar->RequestWeaponSpawn(WeaponClass);
				}
				else
				{
					UE_LOG(LogTemp, Error,
						TEXT("[Spawn] HandlePlayerRequestSpawn: 玩家 '%s' 的 WeaponID='%s' 无法反查为 WeaponClass. "
						     "【v54.3 零兜底】DT_WeaponInfo 缺失或 Row 配置错. "
						     "修复: 1) GM_RoomGameMode.ClassDefaults.WeaponDataTable 必须配 DT_WeaponInfo; "
						     "2) DT_WeaponInfo 里有 RowName='%s' 的行"),
						*PlayerToSpawn->GetName(), *FinalWeaponID, *FinalWeaponID);
				}
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Spawn] HandlePlayerRequestSpawn: 玩家 '%s' 的 FinalWeaponID 为空. "
					     "根因: PS.SelectedWeapon1ID 在 Lifecycle 阶段没写入, 或 PC.Server_RequestSpawn_Implementation 传空. "
					     "【v36 零兜底】拒绝 Spawn, 不允许没有武器的玩家进战斗."),
					*PlayerToSpawn->GetName());
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
	if (FPlayerSpawnData* Cached = PlayerSpawnDataCache.Find(NewPlayer->GetUniqueID()))
	{
		HandlePlayerRequestSpawn(NewPlayer, Cached->CharID, Cached->WeaponID);
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
	if (!DeadController)
	{
		UE_LOG(LogTemp, Error, TEXT("[Respawn] RequestRespawn: DeadController is null!"));
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[Respawn] RequestRespawn: Controller=%s, bImmediate=%d"),
		*DeadController->GetName(), bImmediateRespawn);

	if (Cast<ARoomPlayerController>(DeadController))
	{
		// 玩家复活 (复用 HandlePlayerRequestSpawn)
		FPlayerSpawnData* Cached = PlayerSpawnDataCache.Find(DeadController->GetUniqueID());
		if (!Cached)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Respawn] Player Controller '%s' 无 spawn 缓存, 拒绝复活."),
				*DeadController->GetName());
			return;
		}

		const FString CharID = Cached->CharID;
		const FString WeaponID = Cached->WeaponID;

		if (bImmediateRespawn)
		{
			HandlePlayerRequestSpawn(DeadController, CharID, WeaponID);
		}
		else
		{
			TWeakObjectPtr<AController> WeakCtrl = DeadController;
			FTimerHandle LocalHandle;
			GetWorld()->GetTimerManager().SetTimer(LocalHandle,
				[WeakCtrl, CharID, WeaponID, this]()
				{
					if (AController* C = WeakCtrl.Get())
					{
						HandlePlayerRequestSpawn(C, CharID, WeaponID);
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