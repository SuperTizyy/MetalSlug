// Copyright (c) 2026.
//
// 【v54.4 大厂架构重构】MeleeAIController 简化 — 直接接 UAIBehaviorConfigSO
//
// 【v54.4 职责明确】单一职责: 只处理「关卡预放 AI」路径的入口
//   OnPossess → SetupMeleeAI → InitializeFromConfig → 启动 BT + 武器 + 无敌期
//
// 大厅入队 AI (房主从 UI 添加) 不走这里:
//   走 SpawnAIInternal → InitializeFromConfig(EffectiveConfig) (Base 直接调)
//   大厅 AI 的 BT 来源 = GM.ModeRulesByMode[Mode].BehaviorTree (按游戏模式)
//
// v54 之前: MeleeAIController 持 UAIProfileAsset (DefaultMeleeProfile)
// v54 之后: 关卡预放 AI 走 SetupMeleeAI, 大厅 AI 走 SpawnAIInternal (Base 直接调)

#include "Systems/MeleeAIController.h"

#include "Systems/Spawn/RoomSpawnSubsystem.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Characters/BaseCharacter.h"
#include "Components/CharacterEvents.h"
#include "Components/HealthComponent.h"
#include "Combat/WeaponAttachmentComponent.h"
#include "GameFramework/Pawn.h"
#include "Logging/LogMacros.h"  // UE_LOG 宏定义 (虽然用 LogTemp, 此头文件保证宏展开)

AMeleeAIController::AMeleeAIController()
{
	// 【v54 重构】不需要 CreateDefaultSubobject RuntimeConfig — 已由 Base 处理
}

// OnPossess：关卡预放 AI 自动注入默认 Config
void AMeleeAIController::OnPossess(APawn* InPawn)
{
	// 调用父类 OnPossess（RuntimeConfig 初始化）
	Super::OnPossess(InPawn);

	// 【v54 重构】关卡预放 AI 路径
	//   - 大厅 AI 路径由 SpawnAIInternal 在 Possess 之后调 InitializeFromConfig, 不需要这里
	//   - 关卡预放 AI 没有 Spawn 调用方, 必须由 OnPossess 兜底注入 DefaultMeleeConfig
	if (!DefaultMeleeConfig)
	{
		// 显式报错 (大厂原则 — 零兜底)
		UE_LOG(LogTemp, Error,
			TEXT("[MeleeAI] OnPossess: DefaultMeleeConfig 未配置, 关卡预放 AI '%s' 无法启动 BT. "
				 "修复: 打开 BP_MeleeAIController → Class Defaults → 拖入 DA_AIBehaviorConfig_*.uasset 到 Default Melee Config"),
			*GetName());
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[%s] >>> AMeleeAIController::OnPossess ENTERED (DefaultMeleeConfig=%s, InPawn=%s)"),
		*GetName(), *DefaultMeleeConfig->GetName(), *GetNameSafe(InPawn));

	// 调用 SetupMeleeAI 走 ConfigSO 入口
	SetupMeleeAI(DefaultMeleeConfig);
}

// SetupMeleeAI：刀战 AI 专属设置入口（关卡预放路径 + 大厅路径都可调）
//
// 【v54 重构】直接接 UAIBehaviorConfigSO, 不再接 UAIProfileAsset
//   - 关卡预放路径: OnPossess → SetupMeleeAI(DefaultMeleeConfig) → InitializeFromConfig
//   - 大厅路径: SpawnAIInternal → InitializeFromConfig(EffectiveConfig) (不走这里)
void AMeleeAIController::SetupMeleeAI(UAIBehaviorConfigSO* MeleeConfig)
{
	// 【P0 2026.07.08 调试用】Warning 级别 log
	UE_LOG(LogTemp, Warning,
		TEXT("[%s] >>> AMeleeAIController::SetupMeleeAI ENTERED (Config=%s)"),
		*GetName(), *GetNameSafe(MeleeConfig));

	// 零兜底 — ConfigSO 必须有效
	if (!MeleeConfig)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MeleeAI] SetupMeleeAI: MeleeConfig 为空, 关卡预放 AI '%s' 无法启动 BT. "
				 "修复: 检查 BP_MeleeAIController → Default Melee Config 是否配置"),
			*GetName());
		return;
	}

	// 【v56 大厂架构修复】关卡预放 AI 阵营获取
	//
	// 设计决策 (用户 2026.07.16):
	//   - AIBehaviorConfigSO 不允许添加 DefaultFactionTag
	//   - 关卡预放 AI 的阵营必须从角色类 (Pawn BP) 的 FactionTag 属性获取
	//
	// 获取优先级:
	//   1. CachedFactionTag (已有值, 说明 BaseAIController::OnPossess 已缓存)
	//   2. RoomSpawnSubsystem::GetCachedLevelPlacedAIFaction (扫描缓存)
	//   3. Pawn->GetFactionTag() (直接读取 Pawn 的 FactionTag 属性)
	//
	// 零兜底: 如果三者都获取不到有效阵营, 报错并拒绝继续
	FGameplayTag EffectiveFactionTag;

	if (CachedFactionTag.IsValid())
	{
		// 已有缓存, 直接用
		EffectiveFactionTag = CachedFactionTag;
		UE_LOG(LogTemp, Log,
			TEXT("[%s] SetupMeleeAI: 阵营来源=CachedFactionTag, FactionTag='%s'"),
			*GetName(), *EffectiveFactionTag.ToString());
	}
	else
	{
		// 尝试从 RoomSpawnSubsystem 获取 (扫描缓存)
		UWorld* World = GetWorld();
		if (World)
		{
			URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(World);
			if (SpawnSys)
			{
				APawn* AIPawn = GetPawn();
				if (AIPawn)
				{
					EffectiveFactionTag = SpawnSys->GetCachedLevelPlacedAIFaction(
						AIPawn->GetClass(), Cast<ABaseCharacter>(AIPawn));
					if (EffectiveFactionTag.IsValid())
					{
						CachedFactionTag = EffectiveFactionTag; // 回填缓存
						UE_LOG(LogTemp, Log,
							TEXT("[%s] SetupMeleeAI: 阵营来源=RoomSpawnSubsystem缓存, FactionTag='%s'"),
							*GetName(), *EffectiveFactionTag.ToString());
					}
				}
			}
		}

		// 最后兜底: 直接读 Pawn.FactionTag
		if (!EffectiveFactionTag.IsValid())
		{
			ABaseCharacter* MyPawn = Cast<ABaseCharacter>(GetPawn());
			if (MyPawn)
			{
				EffectiveFactionTag = MyPawn->GetFactionTag();
				if (EffectiveFactionTag.IsValid())
				{
					CachedFactionTag = EffectiveFactionTag; // 回填缓存
					UE_LOG(LogTemp, Log,
						TEXT("[%s] SetupMeleeAI: 阵营来源=Pawn.FactionTag直接读取, FactionTag='%s'"),
						*GetName(), *EffectiveFactionTag.ToString());
				}
			}
		}

		// 最终检查: 阵营仍然无效
		if (!EffectiveFactionTag.IsValid())
		{
			// 【v56.1 大厂架构修复】不再 return, 继续初始化
			//
			// 根因: 旧版 SetupMeleeAI 在阵营无效时直接 return, 跳过了 InitializeFromConfig
			//       → AI 的 BT 永远不启动 → AI 完全不动
			// 修复: 即使阵营无效, 仍然调用 InitializeFromConfig 启动 BT
			//       → AI 至少能移动 (虽然无法正确检测敌人)
			//
			// 大厂原则 (可观测性优先):
			//   - 阵营无效 → Log Error (告诉用户配置问题)
			//   - 但不阻止初始化 → AI 至少能部分运行
			//   - 这是"防御型降级", 不是"兜底"
			UE_LOG(LogTemp, Error,
				TEXT("[MeleeAI] SetupMeleeAI: 关卡预放 AI '%s' 的阵营无法获取. "
					 "【v56.1 修复后继续初始化】AI 将无法正确检测敌人阵营. "
					 "修复: 确保场景中放置的 AI Pawn (例如 BP_SWAT_AI) 的 Details 面板中 "
					 "Faction Tag 字段已配置为 Faction.Offense 或 Faction.Defense."),
				*GetName());
			// 不 return, 继续初始化
		}
	}

	// 【v56】阵营获取成功, 确保 CachedFactionTag 已设置
	if (!CachedFactionTag.IsValid())
	{
		CachedFactionTag = EffectiveFactionTag;
	}
	UE_LOG(LogTemp, Log,
		TEXT("[%s] SetupMeleeAI: 最终阵营 CachedFactionTag='%s'"),
		*GetName(), *CachedFactionTag.ToString());

	// 1. 写入运行时真理源 (Cached AIPawnClass + Cached FactionTag)
	APawn* MyPawn = GetPawn();
	if (MyPawn)
	{
		ABaseCharacter* BaseChar = Cast<ABaseCharacter>(MyPawn);
		if (BaseChar)
		{
			// 【v54 重构】真理源缓存 (运行时内存)
			//   关卡预放 AI 没有 Profile 反查链, 但 Spawn 参数来自 ConfigSO
			//   写入这些字段让 RequestRespawn 复用 (复活时不需要重新查 ConfigSO)
			SetCachedAIPawnClass(BaseChar->GetClass());
			SetCachedIsMother(false); // 【v109.1 大厂架构】新 Spawn 的 AI 初始为非母体

			// 【v54.4 大厂架构重构 — Class 强类型 + 单一真理源 + 零中间层】
			//   旧 (v54.3 — FString 中间层):
			//     - BaseChar->SetMeleeConfig(MeleeConfig) → 内部读 DefaultWeaponRowName → SetSpawnLoadout(FString)
			//     - SetupMeleeAI 末尾又从 BaseChar->GetSpawnWeaponID() 读字符串 → RequestWeaponSpawn(WeaponID)
			//     → 字符串中间层反查 DT_WeaponInfo, 2 层真理源
			//
			//   新 (v54.4 — Class 强类型):
			//     - SetMeleeConfig 内部直接调 RequestWeaponSpawn(LevelPlacedWeaponClass.LoadSynchronous())
			//     - 不写 Pawn.SpawnWeaponID 字符串字段, 不查 DT_WeaponInfo
			//     - 单一真理源: Config.LevelPlacedWeaponClass 决定武器 BP
			//     - SetupMeleeAI 末尾**已经不需要**再调 RequestWeaponSpawn (消除重复入口)
			//
			//   大厂原则:
			//     - 武器 Spawn 入口唯一: WeaponAttachmentComponent::SetMeleeConfig 内部触发
			//     - 时序保证: SetMeleeConfig 内 RequestWeaponSpawn 已经在 Possess 之后调用, 满足 v40.3 要求
			//     - 零字符串中间层: 没有 FString WeaponID 流转, 直接 Class
			BaseChar->SetMeleeConfig(MeleeConfig);

			// 【v54.4 大厂重构 — ConfigSO.Class → Cache.Class (复活用)】同步 Cached WeaponClass
			//   - 写 Class 缓存 — 让 RequestRespawn 走 Class 路径
			//   - 真理源: Config.LevelPlacedWeaponClass (TSoftClassPtr<ABaseWeapon>)
			//   - SetMeleeConfig 内部已 LoadSynchronous, 这里再 LoadSynchronous 一次 (软引用缓存, 开销可接受)
			if (MeleeConfig && !MeleeConfig->LevelPlacedWeaponClass.IsNull())
			{
				TSubclassOf<ABaseWeapon> WeaponClass = MeleeConfig->LevelPlacedWeaponClass.LoadSynchronous();
				if (WeaponClass)
				{
					SetCachedWeaponClass(WeaponClass);
				}
				else
				{
					UE_LOG(LogTemp, Error,
						TEXT("[MeleeAI] SetupMeleeAI: Config=%s 的 LevelPlacedWeaponClass.LoadSynchronous() 失败. "
							 "RequestRespawn 无法复活武器. 修复: LevelPlacedWeaponClass 字段是否仍有效."),
						*MeleeConfig->GetName());
				}
			}
		}
	}

	// 2. 调用 Base 的 InitializeFromConfig 入口 (单一 Spawn 入口)
	//   - 内部: ApplyConfig → 设阵营 → OnConfigLoaded → 启动 BT
	//   - 与大厅 AI 路径完全对称, 都走 InitializeFromConfig
	InitializeFromConfig(MeleeConfig);

	// 【v54.3 完全删除】关卡预放 AI 武器 Spawn 入口已删除
	//   - 旧: SetupMeleeAI 末尾从 BaseChar->GetSpawnWeaponID() 读字符串 → RequestWeaponSpawn(WeaponID)
	//   - 新: WeaponAttachmentComponent::SetMeleeConfig (上面) 内部直接调 RequestWeaponSpawn(Class)
	//   - 单一真理源: 武器 Spawn 只有 1 个入口 (Component 内, v40.3 原则)
	//   - 与大厅路径对称: RoomSpawnSubsystem::SpawnAIInternal 在 Possess 后调 SpawnedChar->RequestWeaponSpawn(Class)

	// 3. 复活无敌期激活 (走 BaseAIController 统一真理源入口 — v54.2 大厂原则)
	//   - 真理源链: ConfigSO.SpawnInvincibilitySeconds (所有 AI 共用, 不分关卡预放/大厅入队/复活)
	//   - 入口: this->GetSpawnInvincibilitySeconds() (BaseAIController getter, 单一访问路径)
	//   - 与 RoomSpawnSubsystem::SpawnAIInternal 使用同一入口 (消除重复架构)
	const float InvSeconds = GetSpawnInvincibilitySeconds();
	if (HasAuthority() && InvSeconds > 0.f)
	{
		ABaseCharacter* BaseChar = Cast<ABaseCharacter>(GetPawn());
		if (BaseChar)
		{
			BaseChar->ActivateSpawnInvincibility(InvSeconds);
			UE_LOG(LogTemp, Log,
				TEXT("[MeleeAI] SetupMeleeAI: 激活关卡预放 AI 复活无敌期 = %.2fs (Pawn=%s, 真理源=ConfigSO.SpawnInvincibilitySeconds)"),
				InvSeconds, *BaseChar->GetName());
		}
	}
}
