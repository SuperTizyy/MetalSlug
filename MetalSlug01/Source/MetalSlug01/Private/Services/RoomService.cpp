// 版权声明：在项目设置的描述页面填写您的版权信息。

#include "Services/RoomService.h"
#include "Systems/RoomPlayerController.h"
#include "Systems/RoomGameMode.h"
#include "Systems/RoomGameState.h"
#include "Systems/Core/RoomPlayerState.h"
#include "Systems/Account/AccountSubsystem.h"
#include "Services/UIViewService.h"
#include "Systems/Session/SessionManagerSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
// 【2026.07.11 v28】FFactionTags::Offense / Defense (房间阵营常量)
#include "Data/Faction/FactionTags.h"
#include "Systems/Spawn/RoomSpawnSubsystem.h"
// 【v49 大厂架构】DT_CharacterInfo 反查 (CharacterName → RowName)
#include "Data/Tables/CharacterTableRow.h"          // 【v54.2】FCharacterInfo / ConfigSoftRef
#include "Data/AI/AIBehaviorConfigSO.h"             // 【v54.2】UAIBehaviorConfigSO
// 【v51 大厂架构】DT_WeaponInfo 反查 (WeaponName → RowName + 反查 PawnClass)
#include "Data/Tables/WeaponTableRow.h"
// 【v51 大厂架构】TSubclassOf<ABaseCharacter> 完整定义 (FAISpawnRequest.AIPawnClass)
#include "Characters/BaseCharacter.h"
#include "Engine/DataTable.h"
// 【v217 大厂架构】RequestLeaveRoom 单一入口 — 销毁 Session + 切回主大厅 UI 状态
#include "Systems/GameFlowSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Enums/CoreEnums.h" // EUIPanel, EMatchState

// ==========================================
// 静态访问器
// ==========================================

URoomService* URoomService::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = WorldContextObject->GetWorld();
	if (!World) return nullptr;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return nullptr;
	return GI->GetSubsystem<URoomService>();
}

// ==========================================
// 内部路由
// ==========================================

APlayerController* URoomService::GetEffectivePC() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	return World->GetFirstPlayerController();
}

ARoomGameMode* URoomService::GetRoomGameMode() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	return Cast<ARoomGameMode>(World->GetAuthGameMode());
}

/**
 * 【v49 大厂架构 — 单一真理源】反查 CharacterName → DT_CharacterInfo RowName
 *
 * 业务场景:
 *   UI ComboBox_AICharacter 显示 "斯沃特AI" (FCharacterInfo.CharacterName)
 *   但 SpawnAIInternal 需要 DT_CharacterInfo 的 RowName 才能查 CharacterBlueprint
 *   → 必须反查
 *
 * 大厂原则 (零兜底):
 *   - 找不到精确匹配 → Log Error + return NAME_None
 *   - 调用方必须显式拒绝入队, 强制修复 DT 配置
 *   - 不允许 fallback 到 "CharacterName 当 RowName" — 那是大厂反模式
 */
// 【v51 大厂重构 — 已删除】ResolveCharacterInfoRowName 函数已被完全删除
//
// 删除原因 (真理源整合):
//   - 旧 (v49): UI 选 CharacterName → 调本函数反查 RowName → SpawnAIInternal 又反查 PawnClass
//   - 两端反查, 反查路径不清晰
//   - 新 (v51): RequestAddAI 一次性反查全部字段 (RowName + AIPawnClass + WeaponID)
//   - 反查路径只有一条, 完全消除反查分散

FString URoomService::GetCurrentAccountName() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			return AccountSub->GetCurrentLoggedInUser();
		}
	}
	return TEXT("");
}

// ==========================================
// 公共 API（标准联机模式优先 RPC，独立进程模式走 GameMode）
// ==========================================

void URoomService::RequestChangeTeam(bool bToAttackTeam)
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomService] RequestChangeTeam(%s) called"),
		bToAttackTeam ? TEXT("Attack=Offense") : TEXT("Defense"));

	APlayerController* EffPC = GetEffectivePC();
	const FString PCClassName = EffPC ? EffPC->GetClass()->GetName() : TEXT("nullptr");
	UE_LOG(LogTemp, Log,
		TEXT("[RoomService] RequestChangeTeam: EffectivePC=%s (Class=%s)"),
		EffPC ? *EffPC->GetName() : TEXT("nullptr"),
		*PCClassName);

	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(EffPC))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RoomService] RequestChangeTeam: 走 RPC 路径 Server_RequestChangeTeam"));
		PC->Server_RequestChangeTeam(bToAttackTeam);
		return;
	}
	// 独立进程模式：直接调 GameMode
	UE_LOG(LogTemp, Warning,
		TEXT("[RoomService] RequestChangeTeam: PC 不是 ARoomPlayerController, 走独立进程 fallback"));
	if (ARoomGameMode* GM = GetRoomGameMode())
	{
		GM->ChangePlayerTeam(EffPC, bToAttackTeam);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomService] RequestChangeTeam: GameMode 也拿不到, 切队失败, 玩家不会换阵营!"));
	}
}

void URoomService::RequestReady(bool bIsReady)
{
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetEffectivePC()))
	{
		PC->Server_ToggleReady(bIsReady);
		return;
	}
	if (ARoomGameMode* GM = GetRoomGameMode())
	{
		GM->UpdatePlayerReadyState(GetEffectivePC(), bIsReady);
	}
}

void URoomService::RequestSendChatMessage(const FString& Message)
{
	if (Message.IsEmpty()) return;

	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetEffectivePC()))
	{
		PC->Server_SendChatMessage(Message);
		return;
	}
	// 独立进程模式：直接调 GameMode 广播
	if (ARoomGameMode* GM = GetRoomGameMode())
	{
		GM->BroadcastChatMessage(GetCurrentAccountName(), Message);
	}
}

void URoomService::RequestAddAI(bool bToAttackTeam, const FString& CharacterName, const FString& WeaponName, int32 Count)
{
	if (Count <= 0)
	{
		return;
	}

	// 【v51 大厂架构重构 — 单一真理源 + 零兜底】
	//
	// 真理源链路 (您设计的):
	//   UI ComboBox_AICharacter 选 CharacterName (FCharacterInfo.CharacterName)
	//      ↓ 反查 DT_CharacterInfo[CharacterName]
	//   拿 RowName + CharacterBlueprint (TSoftClassPtr<ABaseCharacter>)
	//      ↓ LoadSynchronous → AIPawnClass (BP 强类型)
	//   一并写入 Request
	//
	//   UI ComboBox_AIWeapon 选 WeaponName (FWeaponInfo.WeaponName)
	//      ↓ 反查 DT_WeaponInfo[WeaponName]
	//   拿 WeaponID (RowName)
	//   写入 Request.WeaponID
	//
	// 旧 (v49) 反模式:
	//   - UI 传 CharacterName → 反查 RowName → SpawnAIInternal 又反查 PawnClass
	//   - UI 传 WeaponName (字段名错叫 WeaponID) → SpawnAIInternal 反查 WeaponID
	//   - 两端反查, 反查路径不清晰
	//
	// 新 (v51):
	//   - RoomService 一次反查全做完 (RowName + PawnClass + WeaponID)
	//   - SpawnAIInternal 不再反查 (单一真理源)
	//   - 字段命名修正: 第二个参数实质是 WeaponName (UI 显示名)

	ARoomGameMode* GM = GetRoomGameMode();
	if (!GM)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomService::RequestAddAI] GM 为空, 拒绝入队. "
			     "【v51 零兜底】RoomService 必须在 GameMode 存在时才能入队 AI."));
		return;
	}

	// 1. 反查 DT_CharacterInfo → 拿 RowName + CharacterBlueprint + ConfigSoftRef (真理源完整传递)
	//
	// 【v54.2 大厂架构重构】
	//   - 旧 (v54.1): 反查只拿 RowName + CharacterBlueprint, ConfigSO 走 SpawnAIInternal fallback
	//     后果: ConfigSO 在 Possess 之后才注入, SpawnInvincibilitySeconds 等真理分散在多处
	//   - 新 (v54.2): UI 阶段一次性拿全 (CharacterInfoRowName + AIPawnClass + ConfigSO)
	//     好处: SpawnAIInternal 入口就拿到 Config, 所有派生计算真理源统一
	//
	FName CharacterInfoRowName = NAME_None;
	TSubclassOf<ABaseCharacter> AIPawnClass = nullptr;
	TObjectPtr<UAIBehaviorConfigSO> ResolvedConfig = nullptr;

	if (CharacterName.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomService::RequestAddAI] CharacterName 为空, 拒绝入队. "
			     "【v51 零兜底】UI ComboBox_AICharacter 必须选角色."));
		return;
	}

	if (!GM->CharacterDataTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomService::RequestAddAI] CharacterDataTable 未配置. "
			     "【修复路径】GM_RoomGameMode Class Defaults → CharacterDataTable 必须配 DT_CharacterInfo."));
		return;
	}

	static const FString Ctx(TEXT("RoomService.RequestAddAI.Character"));
	{
		TArray<FName> RowNames = GM->CharacterDataTable->GetRowNames();
		for (const FName& RowName : RowNames)
		{
			if (FCharacterInfo* Row = GM->CharacterDataTable->FindRow<FCharacterInfo>(RowName, Ctx))
			{
				if (Row->CharacterName.ToString() == CharacterName)
				{
					CharacterInfoRowName = RowName;
					// 【v51 关键】同步拿 PawnClass, 不让 SpawnAIInternal 二次反查
					if (!Row->CharacterBlueprint.IsNull())
					{
						AIPawnClass = Row->CharacterBlueprint.LoadSynchronous();
					}
					// 【v54.2 关键】同步拿 ConfigSO (真理源完整传递)
					//   - 用户决策 2026.07.16: "SpawnInvincibilitySeconds 是所有 AI 的字段"
					//   - 必须 UI 阶段拿到 Config, SpawnAIInternal 才有真理源
					if (!Row->ConfigSoftRef.IsNull())
					{
						ResolvedConfig = Row->ConfigSoftRef.LoadSynchronous();
					}
					break;
				}
			}
		}
	}

	if (CharacterInfoRowName.IsNone() || !AIPawnClass || !ResolvedConfig)
	{
		// 【v54.2 大厂原则 — 零兜底】三个字段 (RowName / AIPawnClass / ConfigSO) 任一为空都拒绝入队
		const FString AIPawnClassName = AIPawnClass ? AIPawnClass->GetName() : FString(TEXT("<null>"));
		const FString ConfigName = ResolvedConfig ? ResolvedConfig->GetName() : FString(TEXT("<null>"));
		UE_LOG(LogTemp, Error,
			TEXT("[RoomService::RequestAddAI] DT_CharacterInfo 行校验失败 (CharacterName='%s'). "
			     "RowName='%s', AIPawnClass='%s', ConfigSO='%s'. "
			     "【v54.2 零兜底】任一字段为空都拒绝入队. "
			     "【修复路径1】DT_CharacterInfo 行 CharacterBlueprint 字段必须配 BP_*. "
			     "【修复路径2】DT_CharacterInfo 行 ConfigSoftRef 字段必须配 DA_AIBehaviorConfig_*.uasset. "
			     "【修复路径3】检查 UI ComboBox_AICharacter 选项源与 DT_CharacterInfo 是否一一对应."),
			*CharacterName,
			*CharacterInfoRowName.ToString(),
			*AIPawnClassName,
			*ConfigName);
		return;
	}
	// 2. 反查 DT_WeaponInfo → 拿 WeaponID (RowName)
	//    WeaponID 是字符串 (与 DT_WeaponInfo RowName 一致), 不需要 Class 强类型
	//    Class 在武器挂载时由 WeaponAttachmentComponent 内部 DT_WeaponInfo → WeaponBlueprint 反查
	FString WeaponID;
	if (WeaponName.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomService::RequestAddAI] WeaponName 为空, 拒绝入队. "
			     "【v51 零兜底】UI ComboBox_AIWeapon 必须选武器."));
		return;
	}

	if (!GM->WeaponDataTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomService::RequestAddAI] WeaponDataTable 未配置. "
			     "【修复路径】GM_RoomGameMode Class Defaults → WeaponDataTable 必须配 DT_WeaponInfo."));
		return;
	}

	static const FString WCtx(TEXT("RoomService.RequestAddAI.Weapon"));
	{
		TArray<FName> RowNames = GM->WeaponDataTable->GetRowNames();
		for (const FName& RowName : RowNames)
		{
			if (FWeaponInfo* Row = GM->WeaponDataTable->FindRow<FWeaponInfo>(RowName, WCtx))
			{
				if (Row->WeaponName.ToString() == WeaponName)
				{
					WeaponID = RowName.ToString();
					break;
				}
			}
		}
	}

	if (WeaponID.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomService::RequestAddAI] DT_WeaponInfo 找不到 WeaponName='%s' 的有效行. "
			     "【v51 零兜底】拒绝入队. "
			     "【修复路径1】打开 DT_WeaponInfo — 添加 WeaponName='%s' 的行. "
			     "【修复路径2】检查 UI ComboBox_AIWeapon 选项源与 DT_WeaponInfo 是否一一对应."),
			*WeaponName, *WeaponName);
		return;
	}

	// 3. 构造 FAISpawnRequest — 真理源完整传递 (v54.2 Config 字段已加)
	FAISpawnRequest Request;
	// 【v93 大厂 P0 修复】Request.Mode 改为读 GameState->CurrentMatchMode (单一真理源)
	//   旧 (硬编码 Melee): 生化模式房间入队的 AI 走 Melee 链路 → 阵营 / 模式不匹配
	//   新 (读 GS): GS->CurrentMatchMode 由 ARoomGameMode::InitGame 从 URL ?Mode= 解析写入 → 房间模式真理源
	//   零兜底: GS 为 null 或 CurrentMatchMode == None → Log Error + 拒绝入队 (不允许默认分配)
	if (UWorld* World = GetWorld())
	{
		if (ARoomGameState* GS = World->GetGameState<ARoomGameState>())
		{
			if (GS->CurrentMatchMode == ERoomMatchMode::None)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomService::RequestAddAI] GS->CurrentMatchMode == ERoomMatchMode::None, 拒绝入队 AI. "
					     "【修复】检查 ARoomGameMode::InitGame 是否正确解析 URL ?Mode= 参数. "
					     "【业务根因】LANRoomPage::OnCreateSessionComplete 必须先调 FlowSub->SetTargetRoomMode()."));
				return;
			}
			Request.Mode = GS->CurrentMatchMode;
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomService::RequestAddAI] World->GetGameState<ARoomGameState>() 返回 null, 拒绝入队. "
				     "【修复】检查 World Settings → Default GameMode."));
			return;
		}
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomService::RequestAddAI] World 为 null, 拒绝入队. RoomService 必须在 World 就绪时才能入队 AI."));
		return;
	}
	Request.FactionTag           = bToAttackTeam ? FFactionTags::Offense() : FFactionTags::Defense();
	Request.CharacterInfoRowName = CharacterInfoRowName;  // UI 反查结果 (真理源)
	Request.AIPawnClass          = AIPawnClass;            // 【v51 新增】BP 强类型, 直接 Spawn
	Request.Config               = ResolvedConfig;         // 【v54.2 新增】ConfigSO 真理源, 通过 DT 反查拿到
	Request.WeaponID             = WeaponID;               // UI 反查结果 (真理源, RowName)
	Request.Count                = Count;
	// 【v54 大厂架构重构】ProfileTag 已从 FAISpawnRequest 删除 (字段不存在)
	Request.bUseTeamSpawnPoint   = true;

	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetEffectivePC()))
	{
		PC->Server_QueueAIForBattleSpawn(Request);
		return;
	}
	if (ARoomGameMode* GM2 = GetRoomGameMode())
	{
		GM2->QueueAIForBattleSpawn(Request);
	}
}

void URoomService::RequestSelectLoadout(const FString& CharacterRowName, const FString& WeaponPrimaryRowName, const FString& WeaponSecondaryRowName, const FString& WeaponMeleeRowName)
{
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetEffectivePC()))
	{
		PC->Server_SelectLoadout(CharacterRowName, WeaponPrimaryRowName, WeaponSecondaryRowName, WeaponMeleeRowName);
		return;
	}
	// 独立进程模式：直接写 PlayerState
	if (APlayerController* PC = GetEffectivePC())
	{
		if (ARoomPlayerState* PS = PC->GetPlayerState<ARoomPlayerState>())
		{
			PS->SetPlayerLoadout(CharacterRowName, WeaponPrimaryRowName, WeaponSecondaryRowName, WeaponMeleeRowName);

			// v31.4 P0: 同步到 URoomSpawnSubsystem 缓存 (复活路径的真理源)
			// 【v52 P0】缓存 3 把武器 (主+副+近战) 用于复活时恢复 Loadout
			if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
			{
				SpawnSys->SetPlayerSpawnData(
					PC->GetUniqueID(),
					CharacterRowName,
					WeaponPrimaryRowName,
					WeaponSecondaryRowName,
					WeaponMeleeRowName
				);
			}
		}
	}
}

void URoomService::RequestStartGame()
{
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetEffectivePC()))
	{
		PC->Server_RequestStartGame();
		return;
	}
	// 独立进程模式：直接调 GameMode
	if (APlayerController* PC = GetEffectivePC())
	{
		if (PC->HasAuthority())
		{
			if (ARoomGameMode* GM = GetRoomGameMode())
			{
				GM->RequestStartGame(PC);
			}
		}
	}
}

/**
 * RequestLeaveRoom
 *
 * 【v217 大厂架构 - 单一入口】玩家离开房间的统一入口
 *
 * 设计目标 — 大厂架构原则:
 *   - 单一职责: "玩家想离开房间" → 销毁 Session + 切回主大厅 UI
 *   - 单一入口: 战斗菜单 Esc / 结算页 ReturnToLobby / 房间被踢 / 网络失败 全部走这里
 *   - 状态正确: 已在 L_Login 上 → 不 OpenLevel, 直接 OnInterrupted + TransitToState
 *             在战斗地图上 → OpenLevel(L_Login) + RequestStateOnNextLoad
 *
 * 之前 (v216.x) 的死代码问题:
 *   - 旧 RequestLeaveRoom 只调 SessionManager->DestroyRoom, 完全不切 UI 状态
 *   - 注释说"状态切换交给 LANRoomPresenter 监听 OnDestroyRoomComplete" — LANRoomPresenter 根本没订阅
 *   - 死代码: 0 调用方
 *
 * 旧路径失效导致的具体 bug:
 *   - 结算页 WBP_ScoreboardWidget::OnReturnToLobbyClicked 调 TransitToState(MainLobby)
 *     → 切 UI ✓, 但 Session 没销毁 ✗
 *     → 下次进房 OSS 拒绝 "Session already exists, can't join twice"
 *
 * 新路径 (v217) 统一性:
 *   - UScoreboardWidget / WBP_GameHUDWidget::Button_ReturnToLobby / 其他 ESC 菜单全部调本 API
 *   - 无论从哪个 UI 进入, 都保证 Session 销毁 + UI 切回 LANRoom
 *   - 在 L_Login 上时, 不会再 OpenLevel 同一张图(防止循环切图)
 */
void URoomService::RequestLeaveRoom()
{
	UE_LOG(LogTemp, Display,
		TEXT("[RoomService] 【v217】RequestLeaveRoom: 玩家请求离开房间 (单一入口)."));

	// 0 兜底 - GameInstance 必须存在
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomService] 【v217 零兜底】RequestLeaveRoom: GetGameInstance() 失败. "
			     "修复: 检查 GameInstance 生命周期."));
		return;
	}

	// 步骤 1: 销毁 Session (异步, 不阻塞 UI 切换)
	// 大厂原则 — Service 委托 SessionManager, 不直接调 OnlineSubsystem
	USessionManagerSubsystem* SessionManager = GI->GetSubsystem<USessionManagerSubsystem>();
	if (!SessionManager)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomService] 【v217 零兜底】RequestLeaveRoom: SessionManagerSubsystem 不可用. "
			     "修复: 检查 GameInstanceSubsystem 注册."));
		return;
	}

	// 仅在当前还有 Session 时销毁(测试房主模式 bIsHost=true 但没真 Session)
	if (SessionManager->IsInSession() || SessionManager->IsHosting())
	{
		SessionManager->DestroyRoom(FOnDestroyRoomComplete());
		UE_LOG(LogTemp, Display,
			TEXT("[RoomService] 【v217】RequestLeaveRoom: 已触发 SessionManager->DestroyRoom."));
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RoomService] 【v217】RequestLeaveRoom: 当前不在 Session 中(测试房主模式?), 跳过 DestroyRoom."));
	}

	// 步骤 2: 切回主大厅 UI 状态
	UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>();
	if (!FlowSubsystem)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomService] 【v217 零兜底】RequestLeaveRoom: GameFlowSubsystem 不可用. "
			     "修复: 检查 GameInstanceSubsystem 注册."));
		return;
	}

	// 步骤 2a: 检测当前地图 — 在 L_Login 上时不要再 OpenLevel(防止循环切图)
	UWorld* World = GetWorld();
	const FString CurrentLevel = World ? World->GetMapName() : TEXT("");
	const bool bIsInLobby = CurrentLevel.Contains(TEXT("L_Login"));

	if (bIsInLobby)
	{
		// 场景: 已在 L_Login 上, 主动点 ReturnToLobby(结算页/房间页)
		// 路径: TransitToState + OnInterrupted, 不走 OpenLevel(已经在 L_Login 上, 无需切图)
		UE_LOG(LogTemp, Display,
			TEXT("[RoomService] 【v217】RequestLeaveRoom: 已在 L_Login 上, 走 TransitToState+OnInterrupted (无 OpenLevel)."));

		FlowSubsystem->TransitToState(EMatchState::MainLobby);
		FlowSubsystem->OnInterrupted.Broadcast(EUIPanel::LANRoom);
	}
	else
	{
		// 场景: 在战斗地图上(Esc 菜单退出房间)
		// 路径: OpenLevel(L_Login) + RequestStateOnNextLoad(MainLobby)
		// PostLoadMapWithWorld 会消费 RequestStateOnNextLoad → TransitToState(MainLobby) → UI 切到 LANRoom
		UE_LOG(LogTemp, Display,
			TEXT("[RoomService] 【v217】RequestLeaveRoom: 在战斗地图上 (%s), 走 OpenLevel(L_Login)+RequestStateOnNextLoad."),
			*CurrentLevel);

		FlowSubsystem->RequestStateOnNextLoad(EMatchState::MainLobby);
		if (World)
		{
			UGameplayStatics::OpenLevel(World, FName(TEXT("L_Login")), true, TEXT("?offline"));
		}
	}
}

// ==========================================
// 【P0 架构升级】身份同步 + 事件广播
// ==========================================

void URoomService::NotifyBecameHost()
{
	if (!bIsHost)
	{
		bIsHost = true;
		// 【P0】身份变化主动广播, 替代 View 定时器轮询
		OnHostChanged.Broadcast(true);
	}
}

void URoomService::NotifyBecameClient()
{
	if (bIsHost)
	{
		bIsHost = false;
		OnHostChanged.Broadcast(false);
	}
}

// ==========================================
// 【大厂 P0 修复 2026.07.03】测试房主模式
// ==========================================

/**
 * URoomService::EnterSkipToHostMode
 *
 * 显式 API: 同步把本机标记为"独立进程房主", 用于"勾选跳过登录"测试场景
 *
 * 行为:
 *   1. 幂等: 若已是 Host 直接返回
 *   2. 设 bIsHost = true + 广播 OnHostChanged(true)
 *   3. 广播 OnPlayerJoined(LocalAccountName) — 触发本机玩家标签显示
 *   4. 服务器 (Authority) 同步 GameState->HostPlayerName = 本机账号
 *      → 客户端 OnRep_HostPlayerName 自动触发 (在 LAN Room 模式)
 *
 * 设计动机:
 *   旧架构"勾选跳过登录"只调 MockLoginForTesting + TransitToState(MainLobby),
 *   但 URoomService::bIsHost 永远为 false (没人调 NotifyBecameHost),
 *   导致 RoomInsidePage 永远把本机当成普通玩家, 房主按钮全 Collapsed。
 *   新架构用显式 API 标 Host, 复用了所有下游 UI 的房主识别路径,
 *   业务流自洽, 不再依赖隐式副作用。
 */
void URoomService::EnterSkipToHostMode()
{
	// 幂等保护: 已是 Host 不重复广播
	if (bIsHost)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RoomService] EnterSkipToHostMode: 已是 Host, 跳过重复标定 (LocalAccount=%s)"),
			*GetCurrentAccountName());
		return;
	}

	const FString LocalAccountName = GetCurrentAccountName();
	if (LocalAccountName.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomService] EnterSkipToHostMode: LocalAccountName 为空! 请确保先调 MockLoginForTesting"));
		// 即便为空, 仍标 Host, 后续 RefreshRoomUI 会兜底
	}

	// 1. 标 Host + 广播
	bIsHost = true;
	OnHostChanged.Broadcast(true);
	UE_LOG(LogTemp, Log,
		TEXT("[RoomService] EnterSkipToHostMode: 本机已成为测试房主 (LocalAccount=%s)"),
		*LocalAccountName);

	// 2. 广播本机加入 — 触发本机玩家标签立即显示 (走订阅者, 不依赖 PlayerState 同步时延)
	OnPlayerJoined.Broadcast(LocalAccountName);

	// 3. 服务器权威: 同步 GameState->HostPlayerName, 让 LAN Room 模式 OnRep 链路也能工作
	if (UWorld* World = GetWorld())
	{
		if (ARoomGameState* GS = World->GetGameState<ARoomGameState>())
		{
			// 仅当 PC 有 Authority (PIE ListenServer 或独立进程) 才写 HostPlayerName
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (PC->HasAuthority() && !LocalAccountName.IsEmpty())
				{
					GS->HostPlayerName = LocalAccountName;
					UE_LOG(LogTemp, Log,
						TEXT("[RoomService] EnterSkipToHostMode: 已同步 GameState->HostPlayerName=%s"),
						*LocalAccountName);

					// ==========================================
					// 【2026.07.11 v29 P0 架构修复】测试模式走完整登录流程
					// 历史: EnterSkipToHostMode 只 set HostPlayerName, 完全绕过 PostLogin
					//   → PlayerState 没生成 → PlayerArray 空 → UI Box 不显示 widget
					//   → AI 不识别玩家阵营 → AI 永远不攻击玩家
					// 新: 显式调用 GM->AddPlayerToRoom(PC, LocalAccountName), 走完整登录流程
					//   - AddPlayerToRoom 内部: SpawnPlayerState (测试模式补回) + AddUnique PlayerArray
					//   - 智能分配攻/守方 + OnRep_FactionTag + BroadcastSystemMessage + OnPlayerJoined
					//   - 生产模式 PostLogin 已 add, AddUnique 幂等保证不重复
					// 大厂原则 - 单一真理源: HostPlayerName 和 PlayerArray 都由 GM 权威, 不再 RoomService 半截状态
					// ==========================================
					if (ARoomGameMode* GM = World->GetAuthGameMode<ARoomGameMode>())
					{
						GM->AddPlayerToRoom(PC, LocalAccountName);
					}
					else
					{
						UE_LOG(LogTemp, Error,
							TEXT("[RoomService] EnterSkipToHostMode: AuthGameMode 不是 ARoomGameMode (测试模式 GM 没生成?) — PlayerArray 不会 add, UI 不显示玩家"),
							*LocalAccountName);
					}
				}
			}
		}
	}
}

void URoomService::BroadcastHostChanged(const UObject* WorldContextObject, bool bIsHostNow)
{
	if (URoomService* Service = Get(WorldContextObject))
	{
		if (Service->bIsHost != bIsHostNow)
		{
			Service->bIsHost = bIsHostNow;
			Service->OnHostChanged.Broadcast(bIsHostNow);
		}
	}
}

void URoomService::BroadcastPlayerJoined(const UObject* WorldContextObject, const FString& PlayerName)
{
	if (URoomService* Service = Get(WorldContextObject))
	{
		Service->OnPlayerJoined.Broadcast(PlayerName);
	}
}

void URoomService::BroadcastPlayerLeft(const UObject* WorldContextObject, const FString& PlayerName)
{
	if (URoomService* Service = Get(WorldContextObject))
	{
		Service->OnPlayerLeft.Broadcast(PlayerName);
	}
}
