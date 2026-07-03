// Copyright (c) 2026.
//
// 【Phase 2 模式化】刀战 AI 控制器
//
// 设计 (Phase 2 之后):
//   - 自身不写任何"配感知"代码 (已上收 ABaseAIController)
//   - 不写任何"选目标"代码 (走 RoomGameMode::RequestTargetForAI 多态)
//   - 仅保留 SetupMeleeAI 作为"刀战专属入口"的钩子 (BP 兼容性 + 未来刀战专属逻辑的扩展点)
//
// 与 Phase 1 区别:
//   - Phase 1: MeleeAIController 写死了 ConfigurePerceptionFromConfig + SightConfig 字段
//   - Phase 2: 全部上收 Base; MeleeAIController 仅留空类壳子 (BTTask 仍可 Cast 它调 BP)
//
// 未来: 若刀战加"怒气爆发"等专属机制, 在这里加; 否则可以删除直接用 ABaseAIController

// 引入本类的头文件，包含刀战 AI 控制器的类声明
#include "Systems/MeleeAIController.h"

// 引入 AI 运行时配置组件，用于存储难度缩放后的战斗/感知参数
#include "Systems/AI/AIRuntimeConfigComponent.h"
// 引入 AI 行为类型定义，包含难度等级枚举等
#include "Systems/AI/AIBehaviorTypes.h"
// 引入 AI 行为配置静态对象，包含行为树引用和战斗/感知原始参数
#include "Data/AI/AIBehaviorConfigSO.h"
// 引入 AI Profile 资产，包含阵营标签、行为配置和角色/武器信息
#include "Data/AI/AIProfileAsset.h"
// 引入基础角色类，用于设置角色的出生装备（武器/角色 ID）
#include "Characters/BaseCharacter.h"
// 【P0 修复 2026.07.06】引入 RoomPlayerState（用于武器生成兜底）
#include "Systems/Core/RoomPlayerState.h"
// 【P0 修复 2026.07.06】引入 RoomGameMode（用于武器生成兜底）
#include "Systems/RoomGameMode.h"

// 定义本文件的静态日志分类，所有 UE_LOG 使用此分类输出，方便在日志中过滤
DEFINE_LOG_CATEGORY_STATIC(LogMeleeAI, Log, All);

// 构造函数：初始化刀战 AI 控制器
AMeleeAIController::AMeleeAIController()
{
	// 构造函数空 — 所有配置由 Profile 注入
	// 不在构造函数中创建任何默认子对象或设置，所有配置通过 Profile 异步注入
}

// OnPossess：当控制器接管 Pawn 时调用，用于关卡预放 AI 的自举
void AMeleeAIController::OnPossess(APawn* InPawn)
{
	// 【P0 2026.07.08 调试用】Warning 级别 log, 强制显示 OnPossess 入口
	UE_LOG(LogMeleeAI, Warning, TEXT("[%s] >>> AMeleeAIController::OnPossess ENTERED (Pawn=%s, GetCurrentProfile=%s, DefaultMeleeProfile=%s)"),
		*GetName(), *GetNameSafe(InPawn),
		*GetNameSafe(GetCurrentProfile()),
		*GetNameSafe(DefaultMeleeProfile));

	// 【P0 大厂修复 2026.07.03 19:35】关卡预放 AI 自举
	//
	// 场景: BP_GruntAI 摆在地图上, 引擎自动 Possess 我们的 AIController.
	//       没有任何外部代码会调 SetupMeleeAI.
	//       → 当前 CurrentProfile 为空, 后续感知/BT/武器全失效.
	//
	// 修复: OnPossess 是 Controller 的"自举点" — 没 Profile 就用 DefaultMeleeProfile
	//       (设计师在 BP_MeleeAIController 蓝图里拖入 DA_AIProfile_MeleeGrunt 即可)

	// 调用父类 OnPossess，确保引擎基类的接管逻辑正常执行（如同步 RuntimeConfig 组件）
	Super::OnPossess(InPawn);

	// 检查条件：当前没有 Profile（关卡预放 AI 的典型场景）且默认刀战 Profile 已配置
	if (!GetCurrentProfile() && DefaultMeleeProfile)
	{
		// 输出日志，记录自举注入的默认 Profile 和接管的 Pawn 名称
		UE_LOG(LogMeleeAI, Warning, TEXT("[MeleeAI] OnPossess 自举注入 DefaultMeleeProfile=%s, Pawn=%s"),
			*DefaultMeleeProfile->GetName(), *GetNameSafe(InPawn));
		// 调用刀战专属设置函数，注入默认 Profile，完成 AI 初始化
		SetupMeleeAI(DefaultMeleeProfile);
	}
}

// SetupMeleeAI：刀战 AI 专属设置入口，配置武器/角色/阵营并启动行为树
void AMeleeAIController::SetupMeleeAI(UAIProfileAsset* MeleeProfile)
{
	// 【P0 2026.07.08 调试用】Warning 级别 log
	UE_LOG(LogMeleeAI, Warning, TEXT("[%s] >>> AMeleeAIController::SetupMeleeAI ENTERED (Profile=%s)"),
		*GetName(), *GetNameSafe(MeleeProfile));

	// 检查传入的 Profile 是否为空，为空则直接返回
	if (!MeleeProfile)
	{
		return;
	}

	// 1. 同步加载 Config -> Apply 到 RuntimeConfig (与 Phase 1 一致)
	// 同步加载行为配置资产（包含行为树、战斗/感知参数）
	UAIBehaviorConfigSO* Config = MeleeProfile->LoadBehaviorConfigSync();
	// 如果配置加载成功且运行时配置组件存在，将配置应用到组件上
	if (Config && RuntimeConfig)
	{
		// 将配置参数（战斗/感知）写入运行时配置组件，供后续感知/BT 使用
		RuntimeConfig->ApplyConfig(Config);
	}

	// 2. 设置 Faction (走 IGenericTeamAgentInterface)
	// 如果 Profile 的阵营标签有效，解析并设置 AI 的阵营 ID
	if (MeleeProfile->FactionTag.IsValid())
	{
		// 通过 ABaseCharacter 的工具函数将阵营标签转换为 GenericTeamId，并设置给 AI
		// 阵营 ID 会同步到 Pawn 上，供感知系统判定敌我
		SetGenericTeamId(ABaseCharacter::ResolveGenericTeamIdFromTag(MeleeProfile->FactionTag));
	}

	// 【P0 大厂架构重构 2026.07.06】改调 SetMeleeProfile
	//
	// 为什么改:
	//   - 旧: SetupMeleeAI 调 SetSpawnLoadout(CharID, WeaponID)
	//         但 SetSpawnLoadout 只在 !InXXXID.IsEmpty() 时写入
	//         如果 DA_AIProfile_MeleeGrunt.WeaponID="" (默认值 NAME_None)
	//         传入 FString() 空字符串 → 旧 SetSpawnLoadout 跳过写入
	//         → SpawnWeaponID 永远为空 → AI 没武器
	//
	//   - 新: 调 SetMeleeProfile(MeleeProfile)
	//         SetMeleeProfile 先存 MeleeProfile 引用
	//         再解析 Profile.WeaponID / Profile.CharacterRowName (FName → FString)
	//         最后调 SetSpawnLoadout (新实现: 无论是否为空都写入)
	//         即使 Profile.WeaponID="" 也会被写入
	//         PossessedBy 中的 SyncWeaponFromProfile 能从 MeleeProfile 兜底填充
	//
	// 调用栈:
	//   OnPossess → SetupMeleeAI → SetMeleeProfile → SetSpawnLoadout (同步写入)
	//   PossessedBy → SyncWeaponFromProfile → 兜底读取 MeleeProfile.WeaponID
	//   两层保障: 同步写入 + 异步兜底

	// 获取当前控制的 Pawn
	if (APawn* MyPawn = GetPawn())
	{
		// 尝试将 Pawn 转换为 ABaseCharacter 类型，以便设置出生装备
		if (ABaseCharacter* BaseChar = Cast<ABaseCharacter>(MyPawn))
		{
			// 【P0 修复 2026.07.06】改调 SetMeleeProfile
			// MeleeProfile 已在 OnPossess 开头通过 DefaultMeleeProfile 注入
			// SetMeleeProfile 会解析 WeaponID/CharacterRowName 并写入 Pawn 字段
			BaseChar->SetMeleeProfile(MeleeProfile);

			// 【P0 修复 2026.07.06 时序问题】手动触发武器生成
			//
			// 问题根因:
			//   PossessedBy 在 OnPossess 之前被引擎调用 (Possess 内部先调 Pawn->PossessedBy, 再调 Controller->OnPossess)
			//   所以 PossessedBy 第一次运行时 SpawnWeaponID 还是空的 (还没调 SetSpawnLoadout)
			//   即使 SetupMeleeAI 写入了 SpawnWeaponID, PossessedBy 也不会再被调用一次
			//
			// 修复方案:
			//   OnPossess 末尾手动读取 Pawn.SpawnWeaponID, 如果非空则调 SpawnAndEquipWeapon
			//   这模拟了 PossessedBy 读 SpawnWeaponID 的完整逻辑, 包括三路兜底读取
			//
			// 代码复制自 PossessedBy, 但直接用 SpawnWeaponID 而不再做三路读取 (因为 Profile 已写入)
			if (HasAuthority())
			{
				FString WeaponID = BaseChar->GetSpawnWeaponID();
				if (WeaponID.IsEmpty())
				{
					// 三路兜底 (与 PossessedBy 完全一致)
					if (ARoomPlayerState* PS = GetPlayerState<ARoomPlayerState>())
					{
						WeaponID = PS->GetSelectedWeapon1ID();
					}
					if (WeaponID.IsEmpty())
					{
						if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
						{
							FString CachedCharID;
							FString CachedWeaponID;
							if (GM->GetPlayerSpawnData(GetUniqueID(), CachedCharID, CachedWeaponID))
							{
								WeaponID = CachedWeaponID;
							}
						}
					}
				}

				if (!WeaponID.IsEmpty())
				{
					UE_LOG(LogMeleeAI, Log, TEXT("[MeleeAI] OnPossess 末尾触发武器生成: WeaponID=%s"), *WeaponID);
					// 【P0 修复 2026.07.06】SpawnAndEquipWeapon 是 protected, 通过 public RequestWeaponSpawn 调用
					BaseChar->RequestWeaponSpawn(WeaponID);
				}
				else
				{
					UE_LOG(LogMeleeAI, Warning, TEXT("[MeleeAI] OnPossess 末尾仍无 WeaponID, 跳过武器生成"));
				}
			}
		}
	}

	// 4. 调 Base 入口 — 感知配置 + BT 启动 (Phase 2 之后共用层统一)
	// 调用基类的 Profile 初始化函数，完成感知配置和行为树启动
	// Phase 2 之后，所有感知/阵营/BT 逻辑都上收到 ABaseAIController
	InitializeFromProfile(MeleeProfile);

	// 输出日志，提示刀战 AI 设置完成
	UE_LOG(LogMeleeAI, Log, TEXT("[MeleeAI] Setup 完成, Profile=%s"), *MeleeProfile->GetName());
}
