// 版权声明：在项目设置的描述页面填写您的版权信息。

/**
 * @file RoomInsidePage.cpp
 * @brief 房间内部页面实现文件
 * @author 开发团队
 * @date 2026-04-20
 *
 * 实现了房间内部 UI 的所有功能，包括:
 * - 玩家队伍管理（攻方/守方）
 * - 角色和武器选择
 * - 聊天系统
 * - AI 玩家配置
 * - UI 自动刷新机制
 */

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Login/Pages/BattleRoom/RoomInsidePage.h"
#include "Components/VerticalBox.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h" // 【v93 修复】CreatePlayerLabelInBox 入参基类 (UPanelWidget)
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Components/CanvasPanel.h"  // 【v93 新增】刀战/生化模式容器
#include "Components/WrapBox.h"      // 【v93 新增】生化模式专用 WrapBox 容器
// 【2026.07.11 v29】FPendingAIEntry (路径 B: AI 占位数据) + ARoomGameMode (显式 GM->GetPendingAIInFaction)
#include "Systems/AI/AIBehaviorTypes.h"
#include "Systems/RoomGameMode.h"
#include "Components/Overlay.h"
#include "Components/UniformGridPanel.h"
#include "Components/Image.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Login/Pages/BattleRoom/PlayerLabelWidget.h"
#include "Systems/RoomPlayerController.h"
// 【架构修正】UI 层不直接调 Subsystem, 改走 Service 门面
// 【修复 C1083】项目内的 RoomService 在 Public/Systems/ 而非 Public/Services/
#include "Services/AccountService.h"
#include "Services/RoomService.h"
#include "Services/RoomStateService.h"
#include "Components/ComboBoxString.h"
#include "Engine/DataTable.h"
#include "Systems/Spawn/RoomLoadoutDefaults.h" // 【v212】业务默认 RowName 集中管理 (JZ001 等)
#include "Data/Tables/CharacterTableRow.h"
#include "Data/Enums/RoomEnums.h"
#include "Components/UniformGridSlot.h"
#include "UI/Login/Pages/BattleRoom/WeaponIconWidget.h"
// 【架构修正】已不再直读 OnlineSubsystem, SessionManager 已封装会话数据查询
#include "Systems/Session/SessionManagerSubsystem.h"
// 【Bug2 终极修复】RoomInsidePage 直接调 IOnlineSession::UpdateSession 时需要 EOnlineDataAdvertisementType
#include "OnlineSessionSettings.h"
// OnlineSubsystem 直引保留（兼容旧逻辑）; RoomGameState/RoomPlayerState 后续按 Service 化重构
#include "Systems/GameFlowSubsystem.h"
#include "Systems/RoomGameState.h"
#include "Systems/Core/RoomPlayerState.h"
#include "Data/Faction/FactionTags.h" // 【2026.07.10 P0 重构】阵营集中定义


// ==========================================
// 1. 初始化
// ==========================================

/**
 * URoomInsidePage::Initialize
 *
 * 初始化函数，设置所有 UI 控件的事件绑定
 * 1. 绑定退出/开始/加入攻守/准备/AI/武器按钮
 * 2. 诊断 ScrollBox_ChatList 是否绑定
 * 3. 绑定聊天输入框 OnTextCommitted
 * 4. 绑定武器弹窗、确认、呼出按钮
 * 5. 绑定背包 1/2 按钮
 * 6. 绑定 AI 面板开关/确认按钮
 */
bool URoomInsidePage::Initialize()
{
	if (!Super::Initialize()) return false;

	// 绑定退出房间按钮点击事件
	if (Btn_LeaveRoom) Btn_LeaveRoom->OnClicked.AddDynamic(this, &URoomInsidePage::OnLeaveRoomClicked);
	// 绑定开始游戏按钮点击事件
	if (Btn_StartGame) Btn_StartGame->OnClicked.AddDynamic(this, &URoomInsidePage::OnStartGameClicked);
	// 绑定加入攻方按钮点击事件
	// 【2026.07.11 v29.6 诊断】Btn_JoinAttackTeam 为空时显式报错 (BindWidget 没绑上)
	if (Btn_JoinAttackTeam)
	{
		Btn_JoinAttackTeam->OnClicked.AddDynamic(this, &URoomInsidePage::OnJoinAttackTeamClicked);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomInsidePage] Initialize: Btn_JoinAttackTeam 为空! "
			     "WBP_RoomInsidePage 蓝图里必须有同名 Button 才能 BindWidget 成功. "
			     "玩家点了 UI 也不会触发切队 → 出生队伍错."));
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red,
			TEXT("[致命错误] Btn_JoinAttackTeam 未绑定! 请在 WBP_RoomInsidePage 加同名 Button"));
	}
	// 绑定加入守方按钮点击事件
	// 【2026.07.11 v29.6 诊断】同上
	if (Btn_JoinDefenseTeam)
	{
		Btn_JoinDefenseTeam->OnClicked.AddDynamic(this, &URoomInsidePage::OnJoinDefenseTeamClicked);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomInsidePage] Initialize: Btn_JoinDefenseTeam 为空! "
			     "WBP_RoomInsidePage 蓝图里必须有同名 Button 才能 BindWidget 成功. "
			     "玩家点了 UI 也不会触发切队 → 出生队伍错."));
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red,
			TEXT("[致命错误] Btn_JoinDefenseTeam 未绑定! 请在 WBP_RoomInsidePage 加同名 Button"));
	}
	// 绑定切换准备状态按钮点击事件
	if (Btn_ToggleReady) Btn_ToggleReady->OnClicked.AddDynamic(this, &URoomInsidePage::OnToggleReadyClicked);

	// 初始化准备状态为 false
	bIsReady = false;
	if (Text_ReadyStatus) Text_ReadyStatus->SetText(FText::FromString(TEXT("准备")));

	// 【诊断】检查聊天列表控件是否绑定成功，若失败说明蓝图控件名称与 C++ 属性名不匹配
	if (!ScrollBox_ChatList)
	{
		UE_LOG(LogTemp, Error, TEXT("[RoomInsidePage] 严重错误: ScrollBox_ChatList 未绑定! 请确认 WBP_RoomInsidePage 蓝图中存在同名 ScrollBox 控件（区分大小写）。"));
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, TEXT("【致命错误】: ScrollBox_ChatList 未绑定! 请检查蓝图控件命名。"));
	}

	// 绑定聊天输入框的回车事件
	if (Input_Chat){Input_Chat->OnTextCommitted.AddDynamic(this, &URoomInsidePage::OnChatTextCommitted);}

	// 绑定隐藏武器选择弹窗按钮点击事件
	if (Btn_HideWeaponOverlay){Btn_HideWeaponOverlay->OnClicked.AddDynamic(this, &URoomInsidePage::OnHideWeaponOverlayClicked);}

	// 绑定确认更换武器按钮点击事件
	if (Btn_ConfirmWeaponChange){Btn_ConfirmWeaponChange->OnClicked.AddDynamic(this, &URoomInsidePage::OnConfirmWeaponChangeClicked);}

	// 【v52 P0 拆 3 个回调】绑定 3 个"换枪按钮" — 主/副/近战
	if (Btn_ChangePrimaryWeapon){Btn_ChangePrimaryWeapon->OnClicked.AddDynamic(this, &URoomInsidePage::OnChangePrimaryWeaponClicked);}
	if (Btn_ChangeSecondaryWeapon){Btn_ChangeSecondaryWeapon->OnClicked.AddDynamic(this, &URoomInsidePage::OnChangeSecondaryWeaponClicked);}
	if (Btn_ChangeMeleeWeapon){Btn_ChangeMeleeWeapon->OnClicked.AddDynamic(this, &URoomInsidePage::OnChangeMeleeWeaponClicked);}

	// 绑定背包 1 切换按钮点击事件
	if (Btn_Inventory1) Btn_Inventory1->OnClicked.AddDynamic(this, &URoomInsidePage::OnInventory1Clicked);
	// 绑定背包 2 切换按钮点击事件
	if (Btn_Inventory2) Btn_Inventory2->OnClicked.AddDynamic(this, &URoomInsidePage::OnInventory2Clicked);

	// 绑定关闭 AI 面板按钮点击事件
	if (Btn_HideAddAI){Btn_HideAddAI->OnClicked.AddDynamic(this, &URoomInsidePage::OnHideAddAIClicked);}
	// 绑定确认添加 AI 按钮点击事件
	if (Btn_ConfirmAddAI){Btn_ConfirmAddAI->OnClicked.AddDynamic(this, &URoomInsidePage::OnConfirmAddAIClicked);}

	// 绑定打开 AI 面板按钮点击事件
	if (Btn_OpenAIPanel) Btn_OpenAIPanel->OnClicked.AddDynamic(this, &URoomInsidePage::OnOpenAIPanelClicked);

	return true;
}


// ==========================================
// 2. UI 构建完成后的初始化
// ==========================================

/**
 * URoomInsidePage::NativeConstruct
 *
 * UI 构建完成后执行
 * 1. 启动 0.5 秒一次的玩家变化检查定时器
 * 2. 立即执行一次 UI 刷新
 * 3. 判断当前玩家是否为房主
 * 4. 校验 CharacterDataTable 是否绑定
 * 5. 初始化角色下拉框（从 DataTable 读 + 记忆上次选择）
 * 6. 初始化武器下拉框（两个背包槽位兜底默认）
 * 7. 通过 Server_SelectLoadout 同步装备到服务器
 * 8. 隐藏武器选择弹窗、AI 面板
 * 9. 默认激活背包槽 1
 * 10. 从 SessionSettings 读取房间名/游戏模式
 * 11. 根据房主身份控制按钮可见性
 * 12. 订阅 GameFlowSubsystem 状态变化
 */
void URoomInsidePage::NativeConstruct()
{
	Super::NativeConstruct();

	// 【P0 架构升级】订阅 URoomService 事件总线, 替代 0.5s 定时器轮询
	if (URoomService* RoomService = URoomService::Get(this))
	{
		RoomService->OnHostChanged.AddDynamic(this, &URoomInsidePage::OnRoomServiceHostChanged);
		RoomService->OnPlayerJoined.AddDynamic(this, &URoomInsidePage::OnRoomServicePlayerJoined);
		RoomService->OnPlayerLeft.AddDynamic(this, &URoomInsidePage::OnRoomServicePlayerLeft);
	}

	// ==========================================================
	// 【v93 新增】订阅 GameState 房间模式变化（互斥 Canvas 显示）
	// ==========================================================
	if (UWorld* World = GetWorld())
	{
		if (ARoomGameState* GS = World->GetGameState<ARoomGameState>())
		{
			GS->OnMatchModeChanged.AddDynamic(this, &URoomInsidePage::OnGameStateMatchModeChanged);

			// 立即应用一次可见性 (用当前 GameState 模式)
			ApplyVisibilityByMode(GS->CurrentMatchMode);

			UE_LOG(LogTemp, Log,
				TEXT("[RoomInsidePage] 订阅 GameState.OnMatchModeChanged, 当前模式=%d"),
				static_cast<int32>(GS->CurrentMatchMode));
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[RoomInsidePage] NativeConstruct: GameState 为空, 无法订阅 OnMatchModeChanged. "
				     "延迟到 CheckForNewPlayers 补订阅."));
		}
	}

	// ==========================================
	// 【2026-06-29 P0 修复】在 NativeConstruct 中启动 0.5s 兜底定时器
	// 根因 (1): OnViewShown() 永远不被调用 (UIViewService 没有调用 IView 生命周期的代码)
	//            → 上次修改放 OnViewShown 里的 0.5s 定时器启动完全无效
	// 根因 (2): 玩家 REP 同步需要时间 (DelayedSendPlayerInfo 延迟 2 秒),
	//            首次 RefreshRoomUI 时 KnownPlayerStates 为空
	// 根因 (3): URoomService 是 GameInstanceSubsystem, 服务器 Broadcast 事件
	//            根本到不了客户端, 必须客户端主动扫描
	// 修复: 直接在 NativeConstruct 启动 0.5s CheckForNewPlayers 循环定时器
	// ==========================================
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlayerCheckTimerHandle);
		World->GetTimerManager().SetTimer(
			PlayerCheckTimerHandle,
			this,
			&URoomInsidePage::CheckForNewPlayers,
			0.5f,
			true);  // 循环, 直到 ClearTimer
	}

	// 立即执行一次 UI 刷新
	RefreshRoomUI();

	// 【2026-06-29 P0 修复】删掉此处 bIsHost 局部变量计算, 改在 UpdateHostVisibility() 内部统一计算
	// 旧代码: NativeConstruct 这里算一次 bIsHost, 只用于按钮可见性
	//       → 按钮可见性已在 UpdateHostVisibility() 集中处理, 此处计算是浪费
	//       → 保留会触发 "unused variable" 编译警告

	// 检查角色数据表是否已绑定
	if (!CharacterDataTable)
	{
		UE_LOG(LogTemp, Error, TEXT("[URoomInsidePage] 严重错误: 未绑定 CharacterDataTable! 请检查 WBP_RoomInsidePage 的细节面板!"));

		if (GEngine) {GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, TEXT("【致命错误】: CharacterDataTable 缺失!"));}

		// 如果数据表缺失，禁用角色选择下拉框并显示错误提示
		if (ComboBox_CharacterSelect)
		{
			ComboBox_CharacterSelect->ClearOptions();
			ComboBox_CharacterSelect->AddOption(TEXT("数据丢失"));
			ComboBox_CharacterSelect->SetSelectedIndex(0);
			ComboBox_CharacterSelect->SetIsEnabled(false);
		}
		return;
	}

	// 初始化角色选择下拉框
	if (ComboBox_CharacterSelect)
	{
		// 清空现有选项
		ComboBox_CharacterSelect->ClearOptions();
		CachedCharacterIDs.Empty();

		static const FString ContextString(TEXT("RoomUI_CharacterInit"));
		// 获取数据表中的所有行名
		TArray<FName> RowNames = CharacterDataTable->GetRowNames();

		if (RowNames.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("[URoomInsidePage] 数据表为空，无法初始化角色列表!"));
			ComboBox_CharacterSelect->AddOption(TEXT("无可用角色"));
			ComboBox_CharacterSelect->SetIsEnabled(false);
		}
		else
		{
			// 遍历数据表，填充下拉框选项
			for (const FName& RowName : RowNames)
			{
				FCharacterInfo* Info = CharacterDataTable->FindRow<FCharacterInfo>(RowName, ContextString);
				if (Info && !Info->CharacterName.IsEmpty())
				{
					// 添加角色名称到下拉框
					ComboBox_CharacterSelect->AddOption(Info->CharacterName.ToString());
					// 缓存角色 ID，用于后续查找
					CachedCharacterIDs.Add(RowName);
				}
			}

			// 从 AccountSubsystem 中读取上次选择的角色
			FString SavedCharacterID = TEXT("");
			UAccountService* AccountSub = nullptr;
			if (UGameInstance* GI = GetGameInstance())
			{
				AccountSub = UAccountService::Get(this);
				if (AccountSub)
				{
					SavedCharacterID = AccountSub->GetLastSelectedCharacter();
				}
			}

			// 查找上次选择的角色在列表中的索引
			int32 FoundIndex = CachedCharacterIDs.IndexOfByKey(FName(*SavedCharacterID));
			UE_LOG(LogTemp, Warning, TEXT("[Room] NativeConstruct: SavedCharID='%s', FoundIndex=%d, CachedCount=%d"),
				*SavedCharacterID, FoundIndex, CachedCharacterIDs.Num());

			FString CharIDToSync = TEXT("");
			if (FoundIndex != INDEX_NONE)
			{
				// 如果找到上次选择的角色，设置为选中状态
				ComboBox_CharacterSelect->SetSelectedIndex(FoundIndex);
				UpdateCharacterDisplayImage(ComboBox_CharacterSelect->GetOptionAtIndex(FoundIndex));
				CharIDToSync = CachedCharacterIDs[FoundIndex].ToString();
			}
			else
			{
				// 如果未找到，选择第一个角色作为默认值
				ComboBox_CharacterSelect->SetSelectedIndex(0);
				UpdateCharacterDisplayImage(ComboBox_CharacterSelect->GetOptionAtIndex(0));
				CharIDToSync = CachedCharacterIDs.Num() > 0 ? CachedCharacterIDs[0].ToString() : TEXT("");
			}

			UE_LOG(LogTemp, Warning, TEXT("[Room] Syncing char='%s' to server (FoundIndex=%d)"), *CharIDToSync, FoundIndex);
			// 保存当前选择的角色 ID
			if (AccountSub)
			{
				AccountSub->SaveLastSelectedCharacter(CharIDToSync);
			}

			// 初始化武器配置 — 【v52 P0】每个背包只需要保存 1 把主武器到存档
			//   副武器 / 近战武器存在 TempSelectedWeaponsByType 中, 不持久化 (Q8=C 决策)
			if (WeaponDataTable && AccountSub)
			{
				TArray<FName> WeaponRows = WeaponDataTable->GetRowNames();
				FString DefaultWeapon = TEXT("");
				if (WeaponRows.Num() > 0) DefaultWeapon = WeaponRows[0].ToString();

				// 为两个背包槽位设置默认主武器 (向后兼容旧存档结构: 2 个 BackpackSlot 各存 1 把)
				for (int32 WSlot = 1; WSlot <= 2; WSlot++)
				{
					FString SavedWeapon = AccountSub->GetLastSelectedWeapon(WSlot);
					if (SavedWeapon.IsEmpty())
					{
						AccountSub->SaveLastSelectedWeapon(WSlot, DefaultWeapon);
						UE_LOG(LogTemp, Warning, TEXT("[Room] Init BP%d 主武器 -> '%s'"), WSlot, *DefaultWeapon);
					}
				}

				// 【v212 大厂架构修复 — Init 阶段默认填充 Secondary/Melee 临时选择 (业务默认 = JZ001)】
				//
				// 根因 (用户 2026.08.09 反馈):
				//   "玩家如果没选近战武器, 那就默认使用 DT_WeaponInfo 的 RowName=JZ001 的武器,
				//    选了就把所选的近战武器带入游戏中"
				//
				// v211 历史: 用 "DT 第 1 个 Melee 类型" 作默认 → 依赖 DT 行内容 (策划改 DT Melee 行 → 业务默认悄悄变)
				// v212 修复: 业务默认值集中在 FRoomLoadoutDefaults (用户明确指定 JZ001)
				//   1. 按 RowName 精确匹配 (不依赖 MeshType 过滤)
				//   2. 客户端 UI 预填 + 服务器 Spawn 兜底共享同一真理源
				//   3. DT_WeaponInfo 找不到 JZ001 → Log Error + 留空 (零兜底, 不 fallback 到 DT 第 N 行)
				//
				// 数据流:
				//   Init 阶段 → InitializeTempSelectedWeaponsByDefault → 读 FRoomLoadoutDefaults::MeleeDefaultRowName
				//     → 写入 TempSelectedWeaponsByType[Melee] = JZ001
				//     → SyncLoadoutToServer 读 TMap → 发 W3=JZ001 → SetPlayerLoadout 防御性写入
				//     → PS.SelectedWeaponID3 = JZ001
				//     → Spawn 阶段读 PS.SelMeleeID = JZ001 → 玩家拿到 JZ001 武器 ✅
				//
				// 注意 (v212 零兜底):
				//   - DT_WeaponInfo 找不到 RowName=JZ001 → Log Error + 留空 (Spawn 阶段也会兜底失败, 玩家无近战)
				//   - 玩家已选过 → 不覆盖 (TMap.Contains() 守卫)
				//
				// 抽取公共 helper: InitializeTempSelectedWeaponsByDefault
				InitializeTempSelectedWeaponsByDefault(WeaponDataTable);
			}

			// 【v211 大厂架构修复 — Init 阶段走 SyncLoadoutToServer, 替代直接 RequestSelectLoadout】
			//
			// 旧版 (v210) 代码:
			//   RoomService->RequestSelectLoadout(CharIDToSync, W1, TEXT(""), TEXT(""));
			//
			// v210 缺陷:
			//   Init 阶段直接发空串 W2/W3 → 防御性写入保留空值 → Spawn 走 v209 兜底
			//   玩家从未点"换近战武器"按钮 → TempSelectedWeaponsByType[Melee] 始终空
			//   → 玩家看到 DT 第 2 行兜底武器, 以为"我选的武器不见了"
			//
			// v211 修复:
			//   1. Init 阶段先调 InitializeTempSelectedWeaponsByDefault() 预填 TempSelectedWeaponsByType[Secondary/Melee]
			//   2. Init 阶段调 SyncLoadoutToServer() 而非直接 RequestSelectLoadout
			//      → SyncLoadoutToServer 内部读 TempSelectedWeaponsByType 拿 Secondary/Melee 默认值
			//   3. 主武器仍然从存档 (AccountSub->GetLastSelectedWeapon(1)) 读
			//   4. 角色从 ComboBox_CharacterSelect 读
			//   5. 配合 SetPlayerLoadout 防御性写入, 玩家手动选过的武器永远不会被覆盖
			//
			// 重连流程 (T2 重连场景):
			//   PostLogin → DelayedSendPlayerInfo → RequestSelectLoadout(char, W1, W2, "")
			//     → SetPlayerLoadout 防御性写入 → PS.SelMeleeID 保留 "WX001" (v211 预填的) ✅
			//   进游戏 → Spawn 读 PS.SelMeleeID = "WX001" → 玩家拿到 DT 第 1 个 Melee ✅
			//
			// 大厂原则 - 单一写入入口 (Single Write Entry):
			//   所有 Loadout 写入 → Server_SelectLoadout_Implementation → SetPlayerLoadout (防御性写入)
			//   Init / DelayedSendPlayerInfo / SyncLoadoutToServer 三个调用方调用方式统一

			// 【v211】调 SyncLoadoutToServer() — 它内部会读 TempSelectedWeaponsByType (已预填)
			SyncLoadoutToServer();

			// 绑定角色选择变化事件
			ComboBox_CharacterSelect->OnSelectionChanged.AddDynamic(this, &URoomInsidePage::OnCharacterSelectionChanged);
		}
	}

	// 初始化时隐藏武器选择弹窗
	if (Overlay_WeaponSelect)
	{
		Overlay_WeaponSelect->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 默认激活背包槽位 1
	ActiveBackpackSlot = 1;
	// 【v52 P0】刷新所有 3 个武器图标 (主+副+近战)
	RefreshAllWeaponDisplayImages();

	// 更新背包高亮指示器
	UpdateInventoryHighlightUI(ActiveBackpackSlot);

	// 初始化时隐藏 AI 配置面板
	if (Overlay_AddAI)
	{
		Overlay_AddAI->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 填充 AI 配置面板的数据
	PopulateAIPanelData();

	// 从会话设置中读取房间名称和游戏模式，并显示在 UI 上
	// 【v54.5.1 修复】先显式清空 TextBlock 蓝图默认值 ("-" 或其他垃圾文本),
	//   再设正确值。如果 GetCurrentSessionDisplayInfo 失败(空 RoomName)，保持清空状态(不显示误导性内容)
	if (Text_RoomName)
	{
		// 强制清空蓝图 TextBinding / 默认值
		Text_RoomName->SetText(FText::GetEmpty());

		FString DisplayRoomName;
		FString DisplayGameMode;

		// 【架构修正】UI 不直读 OnlineSubsystem, 改为通过 SessionManager 获取
		if (UGameInstance* GI = GetGameInstance())
		{
			if (USessionManagerSubsystem* SessionMgr = GI->GetSubsystem<USessionManagerSubsystem>())
			{
				const bool bGotInfo = SessionMgr->GetCurrentSessionDisplayInfo(DisplayRoomName, DisplayGameMode);
				if (!bGotInfo || DisplayRoomName.IsEmpty())
				{
					// 【大厂 P0 诊断】skip-login 模式下 GetCurrentSessionDisplayInfo 应该能读到 SkipLoginRoomName
					//   如果 DisplayRoomName 仍为空，说明 SessionManager::GetCurrentSessionDisplayInfo 有 bug
					UE_LOG(LogTemp, Error,
						TEXT("[RoomInsidePage] GetCurrentSessionDisplayInfo 返回空 RoomName! "
						     "bGotInfo=%d, DisplayRoomName=[%s], DisplayGameMode=[%s]. "
						     "【大厂诊断】如果 bSkipLoginDirectToLobby=true，检查 SessionManager::GetCurrentSessionDisplayInfo "
						     "是否正确优先返回 SkipLoginRoomName."),
						bGotInfo, *DisplayRoomName, *DisplayGameMode);
				}
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomInsidePage] SessionManagerSubsystem 获取失败! "
					     "【大厂诊断】GameInstance 上找不到 SessionManagerSubsystem."));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomInsidePage] GameInstance 获取失败!"));
		}

		// 只有 RoomName 非空时才设置 UI（空就是空，不显示误导性默认值）
		if (!DisplayRoomName.IsEmpty())
		{
			FString FinalDisplayText = FString::Printf(TEXT("%s-%s"), *DisplayRoomName, *DisplayGameMode);
			Text_RoomName->SetText(FText::FromString(FinalDisplayText));
		}
	}

	// 【2026-06-29 P0 修复】统一房主按钮可见性
	UpdateHostVisibility();

	// 注册游戏流程状态变化的监听器
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSubsystem->OnStateChanged.AddDynamic(this, &URoomInsidePage::OnGameFlowStateChanged);
		}
	}
}


// ==========================================
// 3. UI 销毁时的清理
// ==========================================

/**
 * URoomInsidePage::NativeDestruct
 *
 * UI 销毁时执行
 * 1. 清除玩家检查定时器
 * 2. 退订 GameFlowSubsystem 状态变化监听
 */
void URoomInsidePage::NativeDestruct()
{
	// 清除玩家检查定时器
	GetWorld()->GetTimerManager().ClearTimer(PlayerCheckTimerHandle);

	// 【P0】解绑 URoomService 事件订阅
	if (URoomService* RoomService = URoomService::Get(this))
	{
		RoomService->OnHostChanged.RemoveDynamic(this, &URoomInsidePage::OnRoomServiceHostChanged);
		RoomService->OnPlayerJoined.RemoveDynamic(this, &URoomInsidePage::OnRoomServicePlayerJoined);
		RoomService->OnPlayerLeft.RemoveDynamic(this, &URoomInsidePage::OnRoomServicePlayerLeft);
	}

	// 移除游戏流程状态变化的监听器
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSubsystem->OnStateChanged.RemoveDynamic(this, &URoomInsidePage::OnGameFlowStateChanged);
		}
	}

	Super::NativeDestruct();
}


// ==========================================
// 4. 退出房间
// ==========================================

/**
 * OnLeaveRoomClicked
 *
 * 退出房间按钮点击事件
 * 1. 检查是否已准备（已准备不允许退出）
 * 2. 调用 PC->LeaveRoom()
 */
void URoomInsidePage::OnLeaveRoomClicked()
{
	// 如果已准备，提示先取消准备
	if (bIsReady)
	{
		AddSystemMessageToChat(TEXT("取消准备才能退出房间!"));
		return;
	}

	// 调用 PlayerController 的离开房间函数
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		PC->LeaveRoom();
	}
}


// ==========================================
// 5. 开始游戏
// ==========================================

/**
 * OnStartGameClicked
 *
 * 房主点击后通过 PC->Server_RequestStartGame() 请求服务器开始游戏
 */
void URoomInsidePage::OnStartGameClicked()
{
	// 【P0 架构迁移】走 RoomService 业务门面, View 不感知 PC/RPC
	if (URoomService* RoomService = URoomService::Get(this))
	{
		RoomService->RequestStartGame();
	}
}


// ==========================================
// 6. 加入攻方/守方
// ==========================================

/**
 * OnJoinAttackTeamClicked
 *
 * 加入攻方按钮
 * 1. 检查攻方人数是否已满（5 人）
 * 2. 调用 PC->Server_RequestChangeTeam(true)
 */
void URoomInsidePage::OnJoinAttackTeamClicked()
{
	// 【2026.07.11 v29.6 诊断日志】让用户切队 trace 可见
	UE_LOG(LogTemp, Log,
		TEXT("[RoomInsidePage] OnJoinAttackTeamClicked: 玩家触发加入攻方请求"));

	// 检查攻方人数是否已达到上限（5 人）
	if (Box_AttackTeam && Box_AttackTeam->GetChildrenCount() >= 5)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomInsidePage] OnJoinAttackTeamClicked: 攻方人数已满, 拒绝切队 (Box_AttackTeam->GetChildrenCount()=%d >= 5)"),
			Box_AttackTeam->GetChildrenCount());
		AddSystemMessageToChat(TEXT("系统提示: 攻方人数已满，不可更换队伍!"));
		return;
	}

	// 向服务器请求加入攻方 (true 表示攻方)
	if (URoomService* RoomService = URoomService::Get(this))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RoomInsidePage] OnJoinAttackTeamClicked: 调 RoomService::RequestChangeTeam(true)"));
		RoomService->RequestChangeTeam(true);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomInsidePage] OnJoinAttackTeamClicked: RoomService 为空! 切队链路断了"));
	}
}


/**
 * OnJoinDefenseTeamClicked
 *
 * 加入守方按钮
 * 1. 检查守方人数是否已满（5 人）
 * 2. 调用 PC->Server_RequestChangeTeam(false)
 */
void URoomInsidePage::OnJoinDefenseTeamClicked()
{
	// 【2026.07.11 v29.6 诊断日志】让用户切队 trace 可见
	UE_LOG(LogTemp, Log,
		TEXT("[RoomInsidePage] OnJoinDefenseTeamClicked: 玩家触发加入守方请求"));

	// 检查守方人数是否已达到上限（5 人）
	if (Box_DefenseTeam && Box_DefenseTeam->GetChildrenCount() >= 5)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomInsidePage] OnJoinDefenseTeamClicked: 守方人数已满, 拒绝切队 (Box_DefenseTeam->GetChildrenCount()=%d >= 5)"),
			Box_DefenseTeam->GetChildrenCount());
		AddSystemMessageToChat(TEXT("系统提示: 守方人数已满，不可更换队伍!"));
		return;
	}

	// 向服务器请求加入守方 (false 表示守方)
	if (URoomService* RoomService = URoomService::Get(this))
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RoomInsidePage] OnJoinDefenseTeamClicked: 调 RoomService::RequestChangeTeam(false)"));
		RoomService->RequestChangeTeam(false);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomInsidePage] OnJoinDefenseTeamClicked: RoomService 为空! 切队链路断了"));
	}
}


// ==========================================
// 7. 切换准备状态
// ==========================================

/**
 * OnToggleReadyClicked
 *
 * 切换玩家准备/取消准备状态，并同步到服务器
 */
void URoomInsidePage::OnToggleReadyClicked()
{
	// 切换准备状态
	bIsReady = !bIsReady;
	if (Text_ReadyStatus)
	{
		// 更新显示文本
		Text_ReadyStatus->SetText(FText::FromString(bIsReady ? TEXT("取消准备") : TEXT("准备")));
	}

	// 向服务器同步准备状态
	if (URoomService* RoomService = URoomService::Get(this))
	{
		RoomService->RequestReady(bIsReady);
	}
}


// ==========================================
// 8. 聊天消息
// ==========================================

/**
 * OnChatTextCommitted
 *
 * 聊天输入框回车事件
 * 1. 检查是否按回车键
 * 2. 调用 PC->Server_SendChatMessage
 * 3. 清空输入框
 */
void URoomInsidePage::OnChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	// 检查是否是按回车键且文本不为空
	if (CommitMethod == ETextCommit::OnEnter && !Text.IsEmpty())
	{
		// 【P0 架构迁移】走 RoomService 业务门面
		if (URoomService* RoomService = URoomService::Get(this))
		{
			RoomService->RequestSendChatMessage(Text.ToString());
		}

		// 清空输入框
		Input_Chat->SetText(FText::GetEmpty());
	}
}


/**
 * AddChatMessage
 *
 * 向聊天框添加一条消息
 * 1. 区分系统消息（绿色）、房主消息（黄色）、普通消息（白色）
 * 2. 加上时间戳和发送者名
 * 3. 创建 UTextBlock 添加到 ScrollBox
 * 4. 滚动到底部
 */
void URoomInsidePage::AddChatMessage(const FString& SenderName, bool bIsHost, const FString& Message, bool bIsSystemMsg)
{
	if (!ScrollBox_ChatList) return;

	// 获取当前时间戳
	FString DateStr = FDateTime::Now().ToString(TEXT("%H:%M:%S"));

	FString FinalString;
	FColor TextColor;

	if (bIsSystemMsg)
	{
		// 系统消息: 绿色显示
		FinalString = FString::Printf(TEXT("%s系统提示: %s"), *DateStr, *Message);
		TextColor = FColor::Green;
	}
	else if (bIsHost)
	{
		// 房主消息: 黄色显示，带【房主】标记
		FinalString = FString::Printf(TEXT("%s【房主】%s: %s"), *DateStr, *SenderName, *Message);
		TextColor = FColor::Yellow;
	}
	else
	{
		// 普通玩家消息: 白色显示
		FinalString = FString::Printf(TEXT("%s%s: %s"), *DateStr, *SenderName, *Message);
		TextColor = FColor::White;
	}

	// 创建新的文本块并添加到聊天列表
	UTextBlock* NewMsgBlock = NewObject<UTextBlock>(this);
	NewMsgBlock->SetText(FText::FromString(FinalString));
	NewMsgBlock->SetColorAndOpacity(FSlateColor(TextColor));

	ScrollBox_ChatList->AddChild(NewMsgBlock);
	// 自动滚动到最新消息
	ScrollBox_ChatList->ScrollToEnd();
}


// ==========================================
// 9. 角色选择变化
// ==========================================

/**
 * OnCharacterSelectionChanged
 *
 * 角色下拉框切换事件
 * 1. 更新头像
 * 2. 保存角色 ID 到 AccountSubsystem
 * 3. 通过 SyncLoadoutToServer 同步装备到服务器
 */
void URoomInsidePage::OnCharacterSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan, FString::Printf(TEXT("本地玩家切换了战备角色: %s"), *SelectedItem));
	}

	// 更新角色头像显示
	UpdateCharacterDisplayImage(SelectedItem);

	// 保存选择的角色 ID 到 AccountSubsystem
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountService* AccSub = UAccountService::Get(this))
		{
			int32 SelectedIdx = ComboBox_CharacterSelect->GetSelectedIndex();
			FString CurrentCharID = (SelectedIdx != INDEX_NONE) ? CachedCharacterIDs[SelectedIdx].ToString() : TEXT("Default");
			AccSub->SaveLastSelectedCharacter(CurrentCharID);
		}
	}

	// 同步装备配置到服务器
	SyncLoadoutToServer();
}


/**
 * UpdateCharacterDisplayImage
 *
 * 根据选中的角色名称更新显示的头像图片
 * 1. 从 CharacterDataTable 查找匹配角色
 * 2. 设置 Image_CharacterDisplay 的画刷为 AvatarIcon
 */
void URoomInsidePage::UpdateCharacterDisplayImage(const FString& SelectedCharacterName)
{
	if (!CharacterDataTable || !Image_CharacterDisplay) return;

	static const FString ContextString(TEXT("Character Context"));
	TArray<FCharacterInfo*> AllCharacters;
	CharacterDataTable->GetAllRows<FCharacterInfo>(ContextString, AllCharacters);

	// 遍历所有角色，找到匹配的角色并更新头像
	for (FCharacterInfo* CharInfo : AllCharacters)
	{
		if (CharInfo && CharInfo->CharacterName.ToString() == SelectedCharacterName)
		{
			if (CharInfo->AvatarIcon)
			{
				Image_CharacterDisplay->SetBrushFromTexture(CharInfo->AvatarIcon);
			}
			break;
		}
	}
}


// ==========================================
// 10. 武器选择弹窗
// ==========================================

/**
 * OnHideWeaponOverlayClicked
 *
 * 关闭武器选择弹窗
 */
void URoomInsidePage::OnHideWeaponOverlayClicked()
{
	if (Overlay_WeaponSelect)
	{
		Overlay_WeaponSelect->SetVisibility(ESlateVisibility::Collapsed);
	}
}


/**
 * OnConfirmWeaponChangeClicked
 * 确认玩家在弹窗里点选的武器
 *
 * 【v52 P0 改造 — 按武器类型分发】
 * 1. 从 TempSelectedWeaponsByType[ActiveWeaponType] 读临时选择
 * 2. 校验非空
 * 3. 主武器 → 写存档 (向后兼容 BackpackSlot × Primary)
 *    副武器 / 近战武器 → 仅写运行时 TMap, 不持久化 (Q8=C)
 * 4. 刷新对应 Image 控件
 * 5. 调用 SyncLoadoutToServer 同步服务器 (3 把武器一起发)
 * 6. 关闭弹窗
 */
void URoomInsidePage::OnConfirmWeaponChangeClicked()
{
	const FName* SelectedRowPtr = TempSelectedWeaponsByType.Find(ActiveWeaponType);
	if (!SelectedRowPtr || SelectedRowPtr->IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Room] OnConfirmWeaponChangeClicked: TempSelectedWeaponsByType[%d] 为空, 跳过"),
			static_cast<int32>(ActiveWeaponType));
	}
	else
	{
		const FName SelectedRow = *SelectedRowPtr;
		UGameInstance* GI = GetGameInstance();
		UAccountService* AccountSub = GI ? UAccountService::Get(this) : nullptr;

		// 【v52 P0】主武器 (Primary) 走存档路径 — 向后兼容旧 BackpackSlot × 1 武器结构
		// 副武器 (Secondary) / 近战 (Melee) 走运行期 TMap, 不写存档 (Q8=C 决策)
		if (ActiveWeaponType == EWeaponMeshType::Primary)
		{
			if (AccountSub)
			{
				AccountSub->SaveLastSelectedWeapon(ActiveBackpackSlot, SelectedRow.ToString());
			}
		}
		// 副武器 / 近战武器: 不写存档, 仅运行时态

		// 刷新对应 Image 控件
		UpdateWeaponDisplayImage(ActiveWeaponType);

		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
			FString::Printf(TEXT("背包 %d 已装备%s: %s"),
				ActiveBackpackSlot,
				*UEnum::GetValueAsString(ActiveWeaponType),
				*SelectedRow.ToString()));

		// 同步装备配置到服务器 (3 把武器一起发)
		SyncLoadoutToServer();
	}

	// 关闭武器选择弹窗
	if (Overlay_WeaponSelect)
	{
		Overlay_WeaponSelect->SetVisibility(ESlateVisibility::Collapsed);
	}
}


/**
 * 【v52 P0 改造】打开武器选择弹窗 — 提取公共逻辑
 *
 * 业务流程:
 *   1. 设置 ActiveWeaponType (调用方传参)
 *   2. 从对应数据源读取当前选择
 *      - Primary: AccountSubsystem.GetLastSelectedWeapon(ActiveBackpackSlot) (存档路径)
 *      - Secondary / Melee: TempSelectedWeaponsByType[T] (运行时路径)
 *   3. 如果没选, 用 DT_WeaponInfo 第一个 Primary 类型武器作默认 (避免空值)
 *   4. 设置武器预览图
 *   5. 显示弹窗 + PopulateWeaponGrid (按 ActiveWeaponType 过滤)
 */
void URoomInsidePage::OpenWeaponSelectDialog(EWeaponMeshType WeaponType)
{
	// 1. 标记当前操作类型
	ActiveWeaponType = WeaponType;

	// 2. 读当前选择
	FName LoadedRow;
	if (WeaponType == EWeaponMeshType::Primary)
	{
		// Primary 走存档
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAccountService* AccountSub = UAccountService::Get(this))
			{
				FString SavedWeapon = AccountSub->GetLastSelectedWeapon(ActiveBackpackSlot);
				LoadedRow = FName(*SavedWeapon);
			}
		}
	}
	else
	{
		// Secondary / Melee 走运行时 TMap
		if (const FName* Found = TempSelectedWeaponsByType.Find(WeaponType))
		{
			LoadedRow = *Found;
		}
	}

	// 3. 没选过 → 找 DT 里第一个匹配类型武器作默认 (零兜底: 弹窗不为空)
	if (LoadedRow.IsNone() && WeaponDataTable)
	{
		TArray<FName> RowNames = WeaponDataTable->GetRowNames();
		for (const FName& RowName : RowNames)
		{
			if (const FWeaponInfo* Row = WeaponDataTable->FindRow<FWeaponInfo>(RowName, TEXT("DefaultWeaponPicker")))
			{
				if (Row->MeshType == WeaponType)
				{
					LoadedRow = RowName;
					break;
				}
			}
		}
		// 真没有匹配类型 (策划没配) → 拿第一个凑数, 但要警告
		if (LoadedRow.IsNone() && RowNames.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Room] OpenWeaponSelectDialog: DT_WeaponInfo 中没有任何 MeshType=%d 的武器, 用第 1 个凑数. "
				"【零兜底】请在 DT_WeaponInfo 里给武器行标 MeshType 字段"),
				static_cast<int32>(WeaponType));
			LoadedRow = RowNames[0];
		}
	}

	// 写入 TMap 临时选择
	TempSelectedWeaponsByType.FindOrAdd(WeaponType) = LoadedRow;

	// 4. 设置预览图
	if (!LoadedRow.IsNone() && WeaponDataTable && Image_WeaponPreview)
	{
		if (FWeaponInfo* WeaponData = WeaponDataTable->FindRow<FWeaponInfo>(LoadedRow, TEXT("InitPreview")))
		{
			if (WeaponData->WeaponIcon)
			{
				Image_WeaponPreview->SetBrushFromTexture(WeaponData->WeaponIcon);
			}
		}
	}

	// 5. 显示弹窗 + 按类型过滤的武器网格
	if (Overlay_WeaponSelect) Overlay_WeaponSelect->SetVisibility(ESlateVisibility::Visible);
	PopulateWeaponGrid(WeaponType);
}


/**
 * OnChangePrimaryWeaponClicked — 主武器换枪按钮
 *
 * 大厂原则 (职责对等):
 *   - 玩家点 Btn_ChangePrimaryWeapon → 调本回调
 *   - 弹窗只列出 EWeaponMeshType::Primary 的武器 (PopulateWeaponGrid 内过滤)
 */
void URoomInsidePage::OnChangePrimaryWeaponClicked()
{
	OpenWeaponSelectDialog(EWeaponMeshType::Primary);
}


/**
 * OnChangeSecondaryWeaponClicked — 副武器换枪按钮
 *
 * 弹窗只列 Secondary 武器
 */
void URoomInsidePage::OnChangeSecondaryWeaponClicked()
{
	OpenWeaponSelectDialog(EWeaponMeshType::Secondary);
}


/**
 * OnChangeMeleeWeaponClicked — 近战武器换枪按钮 (原 OnChangeWeaponClicked 重命名)
 *
 * 弹窗只列 Melee 武器
 */
void URoomInsidePage::OnChangeMeleeWeaponClicked()
{
	OpenWeaponSelectDialog(EWeaponMeshType::Melee);
}


// ==========================================
// 11. 背包切换
// ==========================================

/**
 * OnInventory1Clicked
 *
 * 切换到背包 1
 * 1. 设置 ActiveBackpackSlot = 1
 * 2. 刷新所有 3 个武器图标 (主+副+近战) — 【v52 P0】从单图改为多图
 * 3. 更新高亮指示器
 */
void URoomInsidePage::OnInventory1Clicked()
{
	ActiveBackpackSlot = 1;
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan, TEXT("当前切换至: 背包 1"));
	RefreshAllWeaponDisplayImages();
	UpdateInventoryHighlightUI(ActiveBackpackSlot);
}


/**
 * OnInventory2Clicked
 *
 * 切换到背包 2
 * 【v52 P0】切背包时整个 Loadout (主+副+近战) 整套替换显示
 */
void URoomInsidePage::OnInventory2Clicked()
{
	ActiveBackpackSlot = 2;
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan, TEXT("当前切换至: 背包 2"));

	RefreshAllWeaponDisplayImages();
	UpdateInventoryHighlightUI(ActiveBackpackSlot);
}


// ==========================================
// 12. 武器网格生成
// ==========================================

/**
 * PopulateWeaponGrid
 *
 * 根据 WeaponDataTable 动态生成武器选择网格
 *
 * 【v52 P0 改造 — 按武器类型过滤】
 *   - FilterType = Primary / Secondary / Melee: 只列出该类型武器
 *   - FilterType = None (默认): 列出所有武器 (向后兼容)
 *
 * 1. 清空现有网格
 * 2. 遍历所有武器 (按 FilterType 过滤), 创建 UWeaponIconWidget
 * 3. 调用 SetupWeaponItem 设置信息
 * 4. 设置高亮 (已选中的)
 * 5. 计算网格位置并 AddChildToUniformGrid
 */
void URoomInsidePage::PopulateWeaponGrid(EWeaponMeshType FilterType)
{
	if (!WeaponDataTable || !WeaponItemClass || !Grid_WeaponItems) return;

	// 清空现有网格内容
	Grid_WeaponItems->ClearChildren();

	static const FString ContextString(TEXT("Weapon Context"));
	TArray<FName> RowNames = WeaponDataTable->GetRowNames();

	int32 MaxColumns = 4; // 每行最多 4 个武器
	int32 CurrentIndex = 0;

	// 当前选中的武器 (按类型从 TMap 读)
	const FName CurrentSelected = TempSelectedWeaponsByType.Contains(ActiveWeaponType)
		? TempSelectedWeaponsByType[ActiveWeaponType]
		: NAME_None;

	// 遍历所有武器, 按 FilterType 过滤
	for (const FName& RowName : RowNames)
	{
		FWeaponInfo* WeaponData = WeaponDataTable->FindRow<FWeaponInfo>(RowName, ContextString);
		if (!WeaponData)
		{
			continue;
		}

		// 【v52 P0】按武器类型过滤 (大厂原则 — 弹窗内只列匹配类型武器)
		if (FilterType != EWeaponMeshType::None && WeaponData->MeshType != FilterType)
		{
			continue; // 类型不匹配, 跳过
		}

		UWeaponIconWidget* NewItem = CreateWidget<UWeaponIconWidget>(this, WeaponItemClass);
		if (NewItem)
		{
			// 设置武器信息并绑定点击事件
			NewItem->SetupWeaponItem(RowName, *WeaponData, this);

			// 检查是否为当前选中的武器, 如果是则高亮显示
			bool bIsEquippedWeapon = (RowName == CurrentSelected);
			NewItem->SetHighlightFrameVisibility(bIsEquippedWeapon);

			// 计算网格位置
			int32 Row = CurrentIndex / MaxColumns;
			int32 Col = CurrentIndex % MaxColumns;

			UUniformGridSlot* GridSlot = Grid_WeaponItems->AddChildToUniformGrid(NewItem, Row, Col);

			CurrentIndex++;
		}
	}

	// 零兜底 (v52): 过滤后没有武器显示 → 提示策划
	if (CurrentIndex == 0 && FilterType != EWeaponMeshType::None)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Room] PopulateWeaponGrid: DT_WeaponInfo 中没有 MeshType=%d 的武器, 弹窗为空. "
			"【修复】在 DT_WeaponInfo 给武器行打 MeshType 标签"),
			static_cast<int32>(FilterType));
	}
}


/**
 * OnWeaponItemSelectedInGrid
 *
 * 武器网格中被选中时调用
 * 1. 更新 TempSelectedWeaponsByType[ActiveWeaponType]
 * 2. 更新武器预览图
 * 3. 刷新所有武器图标的高亮状态
 */
void URoomInsidePage::OnWeaponItemSelectedInGrid(FName WeaponRowName)
{
	// 【v52 P0】按类型写入临时选择 TMap (替代旧单变量 TempSelectedWeaponRow)
	TempSelectedWeaponsByType.FindOrAdd(ActiveWeaponType) = WeaponRowName;

	// 更新武器预览图
	if (WeaponDataTable)
	{
		FWeaponInfo* WeaponData = WeaponDataTable->FindRow<FWeaponInfo>(WeaponRowName, TEXT(""));
		if (WeaponData && WeaponData->WeaponIcon && Image_WeaponPreview)
		{
			Image_WeaponPreview->SetBrushFromTexture(WeaponData->WeaponIcon);
		}
	}

	// 更新所有武器图标的高亮状态
	if (Grid_WeaponItems)
	{
		TArray<UWidget*> AllGridItems = Grid_WeaponItems->GetAllChildren();

		for (UWidget* ChildWidget : AllGridItems)
		{
			if (UWeaponIconWidget* IconWidget = Cast<UWeaponIconWidget>(ChildWidget))
			{
				bool bShouldHighlight = (IconWidget->GetWeaponRowName() == WeaponRowName);
				IconWidget->SetHighlightFrameVisibility(bShouldHighlight);
			}
		}
	}
}


// ==========================================
// 13. 主界面武器图标刷新
// ==========================================

/**
 * UpdateWeaponDisplayImage
 *
/**
 * UpdateWeaponDisplayImage
 *
 * 【v52 P0 改造】根据指定武器类型刷新对应的 Image 控件
 *
 * 旧 v51 签名: UpdateWeaponDisplayImage(int32 BackpackSlot)
 *   - 单一 Image_WeaponDisplay 显示"当前背包槽的武器"
 *   - 不区分武器类型
 *
 * 新 v52 签名: UpdateWeaponDisplayImage(EWeaponMeshType WeaponType)
 *   - 3 个 Image (主/副/近战) 各管各的
 *   - 数据源: Primary → 存档, Secondary/Melee → TempSelectedWeaponsByType
 *
 * @param WeaponType 主/副/近战
 */
void URoomInsidePage::UpdateWeaponDisplayImage(EWeaponMeshType WeaponType)
{
	if (!WeaponDataTable) return;

	// 选对应的 Image 控件 (大厂原则 — 职责对等)
	UImage* TargetImage = nullptr;
	switch (WeaponType)
	{
	case EWeaponMeshType::Primary:   TargetImage = Image_PrimaryWeaponIcon;   break;
	case EWeaponMeshType::Secondary: TargetImage = Image_SecondaryWeaponIcon; break;
	case EWeaponMeshType::Melee:     TargetImage = Image_MeleeWeaponIcon;     break;
	default: return; // EWeaponMeshType::None — 跳过
	}
	if (!TargetImage) return;

	// 1. 读取对应武器 ID
	FString WeaponRowStr = TEXT("");
	if (WeaponType == EWeaponMeshType::Primary)
	{
		// Primary 走存档路径 (Q5/Q8 = 向后兼容旧 BackpackSlot 索引)
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAccountService* AccountSub = UAccountService::Get(this))
			{
				WeaponRowStr = AccountSub->GetLastSelectedWeapon(ActiveBackpackSlot);
			}
		}
	}
	else
	{
		// Secondary / Melee 走运行时 TMap
		if (const FName* Found = TempSelectedWeaponsByType.Find(WeaponType))
		{
			WeaponRowStr = Found->ToString();
		}
	}

	// 2. 没选 → 找 DT 里第一个匹配类型武器作默认 (零兜底: 图标不能空)
	if (WeaponRowStr.IsEmpty())
	{
		TArray<FName> RowNames = WeaponDataTable->GetRowNames();
		for (const FName& RowName : RowNames)
		{
			if (const FWeaponInfo* Row = WeaponDataTable->FindRow<FWeaponInfo>(RowName, TEXT("DefaultWeaponFallback")))
			{
				if (Row->MeshType == WeaponType)
				{
					WeaponRowStr = RowName.ToString();
					break;
				}
			}
		}
	}

	// 3. 查表 + 设置 Image 画刷
	if (!WeaponRowStr.IsEmpty())
	{
		if (const FWeaponInfo* WeaponData = WeaponDataTable->FindRow<FWeaponInfo>(FName(*WeaponRowStr), TEXT("UpdateWeaponDisplay")))
		{
			if (WeaponData->WeaponIcon)
			{
				TargetImage->SetBrushFromTexture(WeaponData->WeaponIcon);
				return;
			}
		}
		// 零兜底: DT 查不到 → Log Warning (策划没配武器行) — 不清空 Image, 保留上次状态
		UE_LOG(LogTemp, Warning, TEXT("[Room] UpdateWeaponDisplayImage: DT_WeaponInfo 找不到 WeaponRow='%s' (Type=%d). "
			"【零兜底】请检查 DT 配置"),
			*WeaponRowStr, static_cast<int32>(WeaponType));
	}
}


/**
 * RefreshAllWeaponDisplayImages
 *
 * 【v52 P0】一次刷新 3 个 Image 控件 (主+副+近战)
 * 用途: 切背包 / 初始化时调用, 避免 3 次单独调
 */
void URoomInsidePage::RefreshAllWeaponDisplayImages()
{
	UpdateWeaponDisplayImage(EWeaponMeshType::Primary);
	UpdateWeaponDisplayImage(EWeaponMeshType::Secondary);
	UpdateWeaponDisplayImage(EWeaponMeshType::Melee);
}


/**
 * UpdateInventoryHighlightUI
 *
 * 更新背包选择的高亮指示器位置
 * 1 = Image_HighlightBP1 可见
 * 2 = Image_HighlightBP2 可见
 */
void URoomInsidePage::UpdateInventoryHighlightUI(int32 BackpackSlot)
{
	if (!Image_HighlightBP1 || !Image_HighlightBP2) return;

	// 根据背包槽位显示/隐藏高亮指示器
	Image_HighlightBP1->SetVisibility(BackpackSlot == 1 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
	Image_HighlightBP2->SetVisibility(BackpackSlot == 2 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
}


// ==========================================
// 14. AI 配置面板
// ==========================================

/**
 * PopulateAIPanelData
 *
 * 初始化 AI 面板的下拉框
 * 1. 填充 AI 角色下拉框（从 CharacterDataTable）
 * 2. 填充 AI 武器下拉框（从 WeaponDataTable）
 * 3. 填充 AI 队伍下拉框（攻方/守方）
 * 4. 恢复上次的选择
 * 5. 设置默认 AI 数量为 1
 */
void URoomInsidePage::PopulateAIPanelData()
{
	// 填充 AI 角色选择下拉框
	if (ComboBox_AICharacter && CharacterDataTable)
	{
		ComboBox_AICharacter->ClearOptions();

		TArray<FCharacterInfo*> AllCharacters;
		CharacterDataTable->GetAllRows<FCharacterInfo>(TEXT("AIPanelContext"), AllCharacters);

		for (FCharacterInfo* CharInfo : AllCharacters)
		{
			if (CharInfo)
			{
				ComboBox_AICharacter->AddOption(CharInfo->CharacterName.ToString());
			}
		}

		// 恢复上次选择的角色
		if (!LastConfirmedAICharacter.IsEmpty() && ComboBox_AICharacter->FindOptionIndex(LastConfirmedAICharacter) != -1)
		{
			ComboBox_AICharacter->SetSelectedOption(LastConfirmedAICharacter);
		}
		else if (ComboBox_AICharacter->GetOptionCount() > 0)
		{
			ComboBox_AICharacter->SetSelectedIndex(0);
		}
	}

	// 填充 AI 武器选择下拉框
	if (ComboBox_AIWeapon && WeaponDataTable)
	{
		ComboBox_AIWeapon->ClearOptions();

		TArray<FWeaponInfo*> AllWeapons;
		WeaponDataTable->GetAllRows<FWeaponInfo>(TEXT("AIPanelContext"), AllWeapons);

		for (FWeaponInfo* WeaponData : AllWeapons)
		{
			if (WeaponData)
			{
				ComboBox_AIWeapon->AddOption(WeaponData->WeaponName.ToString());
			}
		}

		// 恢复上次选择的武器
		if (!LastConfirmedAIWeapon.IsEmpty() && ComboBox_AIWeapon->FindOptionIndex(LastConfirmedAIWeapon) != -1)
		{
			ComboBox_AIWeapon->SetSelectedOption(LastConfirmedAIWeapon);
		}
		else if (ComboBox_AIWeapon->GetOptionCount() > 0)
		{
			ComboBox_AIWeapon->SetSelectedIndex(0);
		}
	}

	// 填充 AI 队伍选择下拉框
	if (ComboBox_AITeam)
	{
		ComboBox_AITeam->ClearOptions();
		ComboBox_AITeam->AddOption(TEXT("攻方"));
		ComboBox_AITeam->AddOption(TEXT("守方"));
		// 恢复上次选择的队伍
		if (!LastConfirmedAITeam.IsEmpty() && ComboBox_AITeam->FindOptionIndex(LastConfirmedAITeam) != -1)
		{
			ComboBox_AITeam->SetSelectedOption(LastConfirmedAITeam);
		}
		else
		{
			ComboBox_AITeam->SetSelectedIndex(0);
		}
	}

	// 设置默认 AI 数量为 1
	if (Input_AICount)
	{
		Input_AICount->SetText(FText::FromString(TEXT("1")));
	}
}


/**
 * OnOpenAIPanelClicked
 *
 * 打开 AI 配置面板
 * 1. 调用 PopulateAIPanelData
 * 2. 隐藏提示信息
 * 3. 显示 AI 配置面板
 */
void URoomInsidePage::OnOpenAIPanelClicked()
{
	PopulateAIPanelData();

	// 隐藏提示信息
	if (Text_AddAIHint)
	{
		Text_AddAIHint->SetVisibility(ESlateVisibility::Hidden);
	}

	// 显示 AI 配置面板
	if (Overlay_AddAI) Overlay_AddAI->SetVisibility(ESlateVisibility::Visible);
}


/**
 * OnHideAddAIClicked
 *
 * 关闭 AI 配置面板
 */
void URoomInsidePage::OnHideAddAIClicked()
{
	if (Overlay_AddAI)
	{
		Overlay_AddAI->SetVisibility(ESlateVisibility::Collapsed);
	}
}


/**
 * OnConfirmAddAIClicked
 *
 * 确认添加 AI
 * 1. 读取选择: 角色、武器、队伍、数量
 * 2. 保存 LastConfirmedAI* 用于恢复
 * 3. 计算目标队伍容器、最大人数、剩余空位
 * 4. 实际可添加数量 = Min(请求数, 剩余空位)
 * 5. 调用 PC->Server_AddAI 添加
 * 6. 空间不足时显示提示
 * 7. 成功添加则关闭面板
 */
void URoomInsidePage::OnConfirmAddAIClicked()
{
	if (!ComboBox_AICharacter || !ComboBox_AIWeapon || !ComboBox_AITeam || !Input_AICount) return;

	// 获取 AI 配置
	FString SelectedChar = ComboBox_AICharacter->GetSelectedOption();
	FString SelectedWeapon = ComboBox_AIWeapon->GetSelectedOption();
	FString SelectedTeam = ComboBox_AITeam->GetSelectedOption();

	// 【v47 大厂原则 - 零兜底】武器选择不能为空
	if (SelectedWeapon.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomInsidePage] OnConfirmAddAIClicked: 武器选择为空! "
				"ComboBox_AIWeapon 必须选择一个武器. 拒绝入队."));
		if (Text_AddAIHint)
		{
			Text_AddAIHint->SetText(FText::FromString(TEXT("错误: 请选择武器")));
			Text_AddAIHint->SetVisibility(ESlateVisibility::Visible);
		}
		return;
	}

	FString CountStr = Input_AICount->GetText().ToString();
	int32 RequestedAICount = FCString::Atoi(*CountStr);

	if (RequestedAICount <= 0) RequestedAICount = 1;

	// 保存当前配置，下次打开时恢复
	LastConfirmedAICharacter = SelectedChar;
	LastConfirmedAIWeapon = SelectedWeapon;
	LastConfirmedAITeam = SelectedTeam;

	// 确定目标队伍容器
	UVerticalBox* TargetTeamBox = nullptr;
	if (SelectedTeam.Contains(TEXT("攻方"))) TargetTeamBox = Box_AttackTeam;
	else TargetTeamBox = Box_DefenseTeam;

	if (TargetTeamBox && PlayerLabelClass)
	{
		int32 MaxPlayersPerTeam = MaxNumPublicConnections / 2; // 每队最大人数

		int32 CurrentTeamMembers = TargetTeamBox->GetChildrenCount(); // 当前队伍人数

		int32 RemainingSlots = MaxPlayersPerTeam - CurrentTeamMembers; // 剩余空位

		// 检查队伍是否已满
		if (RemainingSlots <= 0)
		{
			if (Text_AddAIHint)
			{
				Text_AddAIHint->SetText(FText::FromString(TEXT("添加失败: 该队伍已满员!")));
				Text_AddAIHint->SetVisibility(ESlateVisibility::Visible);
			}
			return;
		}

		// 计算实际可添加的 AI 数量
		int32 ActualAddCount = FMath::Min(RequestedAICount, RemainingSlots);

		// 向服务器请求添加 AI
		bool bIsAttackTeam = SelectedTeam.Contains(TEXT("攻方"));
		if (URoomService* RoomService = URoomService::Get(this))
		{
			RoomService->RequestAddAI(bIsAttackTeam, SelectedChar, SelectedWeapon, ActualAddCount);
		}

		if (GEngine)
		{
			FString DebugMsg = FString::Printf(TEXT("已向服务器请求往%s添加 %d 名AI (武器: %s)"), *SelectedTeam, ActualAddCount, *SelectedWeapon);
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, DebugMsg);
		}

		// 如果实际添加数量少于请求数量，显示提示
		if (ActualAddCount < RequestedAICount)
		{
			if (Text_AddAIHint)
			{
				FString HintMsg = FString::Printf(TEXT("队伍空间不足，仅成功添加了%d 名AI"), ActualAddCount);
				Text_AddAIHint->SetText(FText::FromString(HintMsg));
				Text_AddAIHint->SetVisibility(ESlateVisibility::Visible);
			}
		}
		else
		{
			// 成功添加，关闭面板
			OnHideAddAIClicked();
		}
	}
}


// ==========================================
// 15. 系统消息
// ==========================================

/**
 * AddSystemMessageToChat
 *
 * 向聊天框发送系统提示消息
 * 1. 创建 UTextBlock
 * 2. 黄色显示
 * 3. 字体大小 14
 * 4. 添加到 ScrollBox 并滚动到底部
 */
void URoomInsidePage::AddSystemMessageToChat(const FString& Message)
{
	if (!ScrollBox_ChatList) return;

	// 创建系统消息文本块
	UTextBlock* SystemMsgText = NewObject<UTextBlock>(this);
	if (SystemMsgText)
	{
		SystemMsgText->SetText(FText::FromString(Message));
		SystemMsgText->SetColorAndOpacity(FSlateColor(FLinearColor::Yellow));

		// 设置字体大小
		FSlateFontInfo FontInfo = SystemMsgText->GetFont();
		FontInfo.Size = 14;
		SystemMsgText->SetFont(FontInfo);

		// 添加到聊天列表并滚动到底部
		ScrollBox_ChatList->AddChild(SystemMsgText);
		ScrollBox_ChatList->ScrollToEnd();
	}
}


/**
 * ActivateChatInput
 *
 * 激活聊天输入框
 * 1. 设置可见性 + 键盘焦点
 * 2. FInputModeGameAndUI 模式（同时操作游戏和 UI）
 * 3. 焦点锁定到 Input_Chat
 * 4. 显示鼠标
 */
void URoomInsidePage::ActivateChatInput()
{
	// 工业级规范: 安全校验，防止空指针
	if (!Input_Chat) return;

	// 激活输入框: 设置可见性 + 切 GameAndUI 模式 + 锁定鼠标
	Input_Chat->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Input_Chat->SetKeyboardFocus();

	// 配置输入模式: 允许同时操作游戏和 UI，并将焦点锁定到聊天输入框
	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(Input_Chat->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		PC->bShowMouseCursor = true;
	}
}


// ==========================================
// 16. 自动订阅 / 刷新引擎
// ==========================================

/**
 * CheckForNewPlayers
 *
 * 0.5 秒一次的玩家变化检查 (真人 + AI 占位)
 *
 * 【2026.07.11 v29 大厂架构重构】双数据流检查
 * 历史 (v28 错误做法):
 *   走 URoomStateService::GetAttackFactionSnapshots/GetDefenseFactionSnapshots 来填 KnownPlayerStates
 *     → 这两个函数读 PlayerArray, 但测试模式 MockLogin 不 add PlayerArray → 真人永远空
 *     → KnownPlayerStates 永远空 → CheckForNewPlayers 不做事
 *     → 但同时 RefreshRoomUI 内部用同一个空数据源 → Box 永远不显示
 *
 * 新 (v29) 原则:
 *   - 真人 vs AI 是两条独立数据流
 *   - 真人订阅链直接读 GS->PlayerArray (绕开快照层, 因为快照也是从 PlayerArray 读的, 只是中间多一层)
 *   - AI 数据根本不入 KnownPlayerStates (它没 PlayerState 可订阅)
 *   - RefreshRoomUI 自己负责 AI 占位的渲染
 *
 * 职责 (Single Responsibility):
 *   - CheckForNewPlayers: 只维护 KnownPlayerStates (真人订阅链) + 通知 RefreshRoomUI
 *   - RefreshRoomUI: 真人 + AI 双数据流渲染
 */
void URoomInsidePage::CheckForNewPlayers()
{
	ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>();
	ARoomGameMode*   GM = GetWorld()->GetAuthGameMode<ARoomGameMode>();

	bool bNeedsRefresh = false;

	// ==========================================
	// 路径 A: 真人 (维护 KnownPlayerStates 订阅链)
	// 【v29】直接读 GS->PlayerArray — 不走 URoomStateService 快照层 (它最终也是读 PlayerArray, 但中间多一层包装)
	// 大厂原则: 单一真理源 + 减熵 — 减少中间层, 数据流越直越可控
	// ==========================================
	if (GS)
	{
		// A1. 订阅链新增 (遍历 PlayerArray, 未订阅的 RoomPS 加入订阅)
		for (APlayerState* GenericPS : GS->PlayerArray)
		{
			ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(GenericPS);
			if (!RoomPS) continue;

			const bool bAlreadyKnown = KnownPlayerStates.ContainsByPredicate(
				[RoomPS](ARoomPlayerState* PS){ return PS == RoomPS; });

			if (!bAlreadyKnown)
			{
				KnownPlayerStates.Add(RoomPS);
				RoomPS->OnStateChanged.AddDynamic(this, &URoomInsidePage::RefreshRoomUI);
				bNeedsRefresh = true;
			}
		}

		// A2. 移除已离开的真人 (PC 失效 / PlayerArray 不再含此 RoomPS)
		for (int32 i = KnownPlayerStates.Num() - 1; i >= 0; --i)
		{
			ARoomPlayerState* TrackedPS = KnownPlayerStates[i];
			const bool bStillInRoom = TrackedPS && GS->PlayerArray.Contains(TrackedPS);

			if (!IsValid(TrackedPS) || !bStillInRoom)
			{
				KnownPlayerStates.RemoveAt(i);
				bNeedsRefresh = true;
			}
		}
	}

	// ==========================================
	// UI 刷新触发: 真人 + AI 占位, 与 RefreshRoomUI 渲染口径一致
	// 【v46 大厂架构修复】改用 GameState.ReplicatedPendingAIQueue (客户端可见)
	// 旧路径读 GM->GetAllPendingAI() — 但 ARoomGameMode.PendingAIQueue 不是 Replicated,
	// 客户端读永远为空, 导致 ExpectedTotalCount 不含 AI, RefreshRoomUI 路径 B 永远空
	// ==========================================
	const int32 RealPlayerCount = KnownPlayerStates.Num();

	// 【v46 修复】改读 GameState.ReplicatedPendingAIQueue (已同步到客户端)
	// 直接使用函数开头已声明的 GS 变量
	int32 AIPlaceholderCount = GS ? GS->ReplicatedPendingAIQueue.Num() : 0;
	if (!GS && GM)
	{
		AIPlaceholderCount = GM->GetAllPendingAI().Num();
		UE_LOG(LogTemp, Warning, TEXT("[RoomInsidePage] CheckForNewPlayers: GS 为空, fallback 到 GM.GetAllPendingAI (服务端 OK, 客户端永远空)"));
	}
	const int32 ExpectedTotalCount = RealPlayerCount + AIPlaceholderCount;

	int32 UIAttackCount = Box_AttackTeam ? Box_AttackTeam->GetChildrenCount() : 0;
	int32 UIDefenseCount = Box_DefenseTeam ? Box_DefenseTeam->GetChildrenCount() : 0;
	int32 RenderedTotalCount = UIAttackCount + UIDefenseCount;

	// 期望人数 != UI 实际 → 刷新
	if (RenderedTotalCount != ExpectedTotalCount)
	{
		bNeedsRefresh = true;
	}

	if (bNeedsRefresh)
	{
		RefreshRoomUI();
	}
}


/**
 * RefreshRoomUI
 *
 * 重新绘制房间 UI，更新所有玩家的显示信息
 *
 * 【2026.07.11 v29 大厂架构重构】双数据流渲染
 * 历史 (v28 错误做法):
 *   for (const FPlayerSnapshot& Snap : AllSnapshots) — 合并真人 + AI
 *     → 测试模式 MockLogin 不 add PlayerArray → 真人永远空 → Box 不显示 (用户反馈 bug)
 *
 * 新 (v29) 数据流:
 *   - 真人: KnownPlayerStates (ARoomPlayerState 事件订阅链, 已稳定 v18/v27)
 *   - AI 占位: GM->GetPendingAIInFaction (显式查询, 不混入真人)
 *   两者在 UI 层按 FactionTag 分类合并
 *
 * 大厂原则:
 *   - 显式意图: 真人 vs AI 是两条数据流, 各自走自己最可靠的源头
 *   - 单一真理源: 真人走 PlayerArray (KnownPlayerStates 维护), AI 走 PendingAIQueue
 *   - 零兜底: 任一数据源为空 → Box 渲染空 (这是合法的真空状态, 不报错)
 */
void URoomInsidePage::RefreshRoomUI()
{
	// 清空攻守方列表 + WrapBox (生化模式容器)
	if (Box_AttackTeam) Box_AttackTeam->ClearChildren();
	if (Box_DefenseTeam) Box_DefenseTeam->ClearChildren();
	if (WrapBox_ZombiePlayers) WrapBox_ZombiePlayers->ClearChildren();

	// 【v46 新增】获取 GameState 引用 (用于读 ReplicatedPendingAIQueue)
	ARoomGameState* GS = GetWorld() ? GetWorld()->GetGameState<ARoomGameState>() : nullptr;

	// 检查 PlayerLabelClass 是否已配置
	if (!PlayerLabelClass)
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, TEXT("[严重错误] PlayerLabelClass 未配置!"));
		return;
	}

	// 【P0】HostPlayerName 走 RoomStateService::GetMatchSnapshot, 不直接读 GameState
	FString CurrentHostName = TEXT("");
	if (URoomStateService* RoomStateForHost = URoomStateService::Get(this))
	{
		CurrentHostName = RoomStateForHost->GetMatchSnapshot().HostPlayerName;
	}

	// ==========================================
	// 【2026-06-30 P0 Bug1+Bug3 终极修复】本机房主身份判定 - 双路权威
	// --------------------------------------------------------------
	// 旧实现只走 URoomService::IsHost() 一条路, 跨图后行为未验证完全可靠
	// 新实现双路兜底:
	//   【路1 权威】 NetMode == NM_ListenServer → 本机必然是 listen server 上的房主
	//                → 直接基于 UE 网络层, 不依赖任何 Subsystem 状态
	//                → ListenServer 上 server 自己立即可见 (无 ON_REP 延迟)
	//   【路2 兜底】 URoomService::IsHost() — 老逻辑保留, 路1 拿不到时用
	//                → 路1 已确定 bAmILocalHost=true 时, 路2 只用于拿 LocalAccountName
	// 【P0 关键】: ListenServer 上的玩家 100% 是房主, 不需要等 HostPlayerName 同步
	//              这样房主 widget 在 RefreshRoomUI 第一帧就能被 SetAsHost(true)
	//              → Text_IsReady 永久 Collapsed, 不会显示"未准备"
	// ==========================================
	FString LocalAccountName = TEXT("");
	bool bAmILocalHost = false;

	if (UWorld* World = GetWorld())
	{
		// 路1: NetMode 权威判定 (ListenServer → 本机就是房主, 不依赖任何 GameState 同步)
		if (World->GetNetMode() == NM_ListenServer)
		{
			bAmILocalHost = true;

			// ==========================================
			// 【2026-06-30 P0 Bug1 终极修复】本机账号名: 优先走 UAccountService::GetCurrentAccountName
			// ----------------------------------------------------------------------
			// 旧实现直接读 LocalPC->LocalPS->GetPlayerName(), 在跨图后第一帧会拿到
			//   UE 默认填的"机器名" (如 YiYuanDesktop-7845BD) 而非登录玩家的真实账号 (如 111)。
			// 根因: RoomPS.SetPlayerName() 通过 Server_SendPlayerInfo (延迟 2 秒) 才会覆盖,
			//       而 UAccountSubsystem.CurrentLoggedInUser 是登录时就持久化的, 不会被覆盖。
			// 路1a (优先) 路1b (兜底) 双源拿 LocalAccountName, 保证第一帧就拿到对的名字
			//   - 路1a: UAccountService::GetCurrentAccountName() → 跨图持久, 准
			//   - 路1b: LocalPS->GetPlayerName() → 跨图后 2s 内可能为机器名, 用作兜底
			// ==========================================
			if (URoomService* RoomServiceForName = URoomService::Get(this))
			{
				LocalAccountName = RoomServiceForName->GetCurrentAccountName();
			}
			if (LocalAccountName.IsEmpty())
			{
				if (APlayerController* LocalPC = World->GetFirstPlayerController())
				{
					if (ARoomPlayerState* LocalPS = LocalPC->GetPlayerState<ARoomPlayerState>())
					{
						LocalAccountName = LocalPS->GetPlayerName();
					}
				}
			}
			// 【兜底】如果本地还没有名字 (例如 Standalone 测试模式),
			//         借 GameState.HostPlayerName 当作 LocalAccountName
			if (LocalAccountName.IsEmpty() && !CurrentHostName.IsEmpty())
			{
				LocalAccountName = CurrentHostName;
			}
		}

		// 路2: URoomService 兜底 (Standalone / 跨 GameInstance 等 NetMode 拿不到时)
		if (!bAmILocalHost)
		{
			if (URoomService* LocalRoomService = URoomService::Get(this))
			{
				bAmILocalHost = LocalRoomService->IsHost();
				if (bAmILocalHost)
				{
					LocalAccountName = LocalRoomService->GetCurrentAccountName();
				}
			}
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[RoomInsidePage] RefreshRoomUI: bAmILocalHost=%d, LocalAccount=[%s], CurrentHostName=[%s], KnownPlayers=%d"),
		bAmILocalHost ? 1 : 0, *LocalAccountName, *CurrentHostName, KnownPlayerStates.Num());

	// ==========================================
	// 【2026.07.11 v29 大厂架构重构】双数据流渲染 (真人 + AI 占位)
	//
	// 旧 (v28) 错误做法: 合并真人 + AI (AllSnapshots) — 但 AllSnapshots 用 PlayerArray 填真人
	//   → 测试模式 MockLogin 不 add PlayerArray → 真人永远空 → Box 不显示 ← 用户反馈 bug
	//
	// 新 (v29): 双数据流
	//   路径 A 真人: KnownPlayerStates (PS 事件订阅链, v18/v27 验证可靠)
	//   路径 B AI 占位: GM->GetPendingAIInFaction (大厅阶段独有, 战斗时已 Spawn 进 AIController)
	//
	// 【v93 大厂架构】模式分发 (刀战/生化分离容器):
	//   - Melee 模式: 路径 A/B 按 PS_FactionTag/Entry.FactionTag 分桶到 Box_AttackTeam/Box_DefenseTeam
	//   - Zombie 模式: 路径 A/B 不分阵营, 全部塞进 WrapBox_ZombiePlayers (横向自动换行)
	// ==========================================================
	const ERoomMatchMode CurrentMode = GS ? GS->CurrentMatchMode : ERoomMatchMode::None;
	const bool bIsZombieMode = (CurrentMode == ERoomMatchMode::Zombie);
	const bool bIsMeleeMode = (CurrentMode == ERoomMatchMode::Melee);

	// 生化模式: 准备 WrapBox_ZombiePlayers 容器
	if (bIsZombieMode && WrapBox_ZombiePlayers)
	{
		WrapBox_ZombiePlayers->ClearChildren();
	}

	// ==========================================
	// 路径 A: 真人玩家 (走 KnownPlayerStates)
	// 大厂原则 - 单一真理源: PS (PlayerState) 是阵营/准备状态的真理源, 直接读 PS 字段 (不再绕道 URoomStateService 快照层)
	// 旧 (v28) 错误: 用 GetAttackFactionSnapshots 查快照 → 又是绕一层, 还遇到 PlayerArray 空的问题
	// 【v93 重构】Zombie 模式不区分阵营, 全部进 WrapBox_ZombiePlayers
	// ==========================================
	for (ARoomPlayerState* PS : KnownPlayerStates)
	{
		if (!IsValid(PS)) continue;

		const FString PName = PS->GetPlayerName();

		// 【大厂原则 - 单一真理源】真人 PS 本身就是真相 — 直接读
		//   不再用快照层包装 (快照也是从 PS 抄的, 复制过程中可能不同步)
		FGameplayTag PS_FactionTag = PS->CurrentFactionTag;
		bool         PS_bIsReady = PS->bIsReady;

		// 【v93 模式分发】Zombie 模式: 直接用 WrapBox, 不分阵营
		if (bIsZombieMode)
		{
			if (WrapBox_ZombiePlayers)
			{
				CreatePlayerLabelInBox(
					WrapBox_ZombiePlayers,
					PName,
					/*bIsAI=*/ false,
					CurrentHostName,
					bAmILocalHost,
					LocalAccountName,
					/*bLabelReady=*/ PS_bIsReady);
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomInsidePage] 生化模式渲染真人时 WrapBox_ZombiePlayers 为空! 跳过 PName=%s"), *PName);
			}
			continue;
		}

		// 【Melee 模式】按阵营分桶到 Box_AttackTeam/Box_DefenseTeam
		UVerticalBox* TargetBox = nullptr;
		if (PS_FactionTag == FFactionTags::Offense()) TargetBox = Box_AttackTeam;
		else if (PS_FactionTag == FFactionTags::Defense()) TargetBox = Box_DefenseTeam;
		else
		{
			// 【大厂原则 - 零兜底】无效阵营 → 显式报错 + 不放入任何 Box
			UE_LOG(LogTemp, Warning,
				TEXT("[RoomInsidePage] 真人 %s 阵营无效: '%s', 不放入任何容器"),
				*PName, *PS_FactionTag.ToString());
			continue;
		}

		if (TargetBox)
		{
			CreatePlayerLabelInBox(
				TargetBox,
				PName,
				/*bIsAI=*/ false,
				CurrentHostName,
				bAmILocalHost,
				LocalAccountName,
				/*bLabelReady=*/ PS_bIsReady);
		}
	}

	// ==========================================
	// 路径 B: AI 占位 (走 GameState.ReplicatedPendingAIQueue)
	// 【v46 大厂架构修复】改用 GameState.ReplicatedPendingAIQueue (客户端可见)
	// 旧路径读 GM->GetPendingAIInFaction — 但 ARoomGameMode.PendingAIQueue 不是 Replicated,
	// 客户端读永远为空, 导致 AI 占位不显示
	// 【v93 模式分发】Zombie 模式不分阵营, 全部进 WrapBox_ZombiePlayers
	// ==========================================
	if (GS)
	{
		const TArray<FPendingAIEntry> PendingAttackAI = GS->ReplicatedPendingAIQueue.FilterByPredicate(
			[](const FPendingAIEntry& Entry) {
				return Entry.FactionTag == FFactionTags::Offense();
			});
		for (const FPendingAIEntry& Entry : PendingAttackAI)
		{
			if (Entry.DisplayName.IsEmpty())
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomInsidePage] PendingAI 空 DisplayName, FactionTag='%s' — GM 数据损坏"),
					*Entry.FactionTag.ToString());
				continue;
			}

			// 【v93 模式分发】Zombie 模式: 直接用 WrapBox
			if (bIsZombieMode)
			{
				if (WrapBox_ZombiePlayers)
				{
					CreatePlayerLabelInBox(
						WrapBox_ZombiePlayers,
						Entry.DisplayName,
						/*bIsAI=*/ true,
						CurrentHostName,
						bAmILocalHost,
						LocalAccountName,
						/*bLabelReady=*/ false);
				}
				continue;
			}

			// 【Melee 模式】按阵营分桶
			if (Box_AttackTeam)
			{
				CreatePlayerLabelInBox(
					Box_AttackTeam,
					Entry.DisplayName,
					/*bIsAI=*/ true,
					CurrentHostName,
					bAmILocalHost,
					LocalAccountName,
					/*bLabelReady=*/ false);
			}
		}

		// 守方 AI 占位
		const TArray<FPendingAIEntry> PendingDefenseAI = GS->ReplicatedPendingAIQueue.FilterByPredicate(
			[](const FPendingAIEntry& Entry) {
				return Entry.FactionTag == FFactionTags::Defense();
			});
		for (const FPendingAIEntry& Entry : PendingDefenseAI)
		{
			if (Entry.DisplayName.IsEmpty())
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomInsidePage] PendingAI 空 DisplayName, FactionTag='%s' — GM 数据损坏"),
					*Entry.FactionTag.ToString());
				continue;
			}

			// 【v93 模式分发】Zombie 模式: 直接用 WrapBox (跳过守方桶)
			if (bIsZombieMode)
			{
				if (WrapBox_ZombiePlayers)
				{
					CreatePlayerLabelInBox(
						WrapBox_ZombiePlayers,
						Entry.DisplayName,
						/*bIsAI=*/ true,
						CurrentHostName,
						bAmILocalHost,
						LocalAccountName,
						/*bLabelReady=*/ false);
				}
				continue;
			}

			// 【Melee 模式】按阵营分桶
			if (Box_DefenseTeam)
			{
				CreatePlayerLabelInBox(
					Box_DefenseTeam,
					Entry.DisplayName,
					/*bIsAI=*/ true,
					CurrentHostName,
					bAmILocalHost,
					LocalAccountName,
					/*bLabelReady=*/ false);
			}
		}
	}

	// ==========================================
	// 【2026.07.11 v29 大厂架构重构】RefreshRoomUI 收尾段 (从原 v28 末尾搬回)
	// 历史: v28 重构时, 把这段收尾段错误地留在了 CreatePlayerLabelInBox 之后
	//       → 全局作用域, 编译失败 + UpdateHostVisibility / TOTAL_PLAYERS_WITH_AI 推送被静默丢失
	// v29 修复: 显式搬回 RefreshRoomUI 函数体末尾
	// ==========================================

	// 【2026-06-29 P0 修复】刷新完成后再次确认房主按钮可见性
	// 场景: RefreshRoomUI 可能由 OnRoomServiceHostChanged 触发 (此时房主身份刚变),
	//       必须在最后一次 RefreshRoomUI 末尾确保按钮可见性与新身份一致
	UpdateHostVisibility();

	// ==========================================
	// 【2026-06-30 P0 Bug2 终极修复】房主端: 推送当前总人数到 SessionSettings
	// ----------------------------------------------------------------------
	// 旧实现走 USessionManagerSubsystem::BroadcastRoomPlayerCountStatic, 内部 bIsHost 护栏静默忽略
	//   → 跨图后 USessionManagerSubsystem.bIsHost = false (GameInstance 重建)
	//   → 推送被静默忽略 → TOTAL_PLAYERS_WITH_AI 永远停在 BuildSessionSettings 的初始值 1
	// 新实现绕开 bIsHost 护栏, 直接拿 SessionInterface 写 SessionSettings + UpdateSession
	//   - 本机是不是房主用 NetMode == NM_ListenServer 权威判定
	//   - 直接走 IOnlineSession 接口, 不依赖任何 Subsystem 状态
	//   - 推送时机: RefreshRoomUI 末尾, 此时 VBox 节点数 = 当前 (真人 + AI) 总数
	// ==========================================
	if (bAmILocalHost)
	{
		int32 AttackCount = Box_AttackTeam ? Box_AttackTeam->GetChildrenCount() : 0;
		int32 DefenseCount = Box_DefenseTeam ? Box_DefenseTeam->GetChildrenCount() : 0;
		int32 ZombieWrapCount = WrapBox_ZombiePlayers ? WrapBox_ZombiePlayers->GetChildrenCount() : 0;

		// 【v93 大厂架构】模式分支计算总人数 (大厂原则 — 单一真理源, 互斥容器):
		//   - Melee 模式: 用 AttackCount + DefenseCount (Box_AttackTeam/Box_DefenseTeam 内有人)
		//   - Zombie 模式: 用 ZombieWrapCount (WrapBox_ZombiePlayers 内有人, Box_Attack/Defense 是空的)
		//   - 其他模式: 全 0 → TotalWithAI = 1 (向下兼容, 不报错)
		int32 TotalWithAI = 0;
		if (bIsZombieMode)
		{
			TotalWithAI = ZombieWrapCount;
		}
		else
		{
			TotalWithAI = AttackCount + DefenseCount;
		}
		if (TotalWithAI < 1) TotalWithAI = 1;

		// 直接拿 SessionInterface, 绕开 USessionManagerSubsystem 内部 bIsHost 护栏
		if (IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get())
		{
			if (IOnlineSessionPtr SessionPtr = OnlineSub->GetSessionInterface())
			{
				if (FNamedOnlineSession* NamedSession = SessionPtr->GetNamedSession(NAME_GameSession))
				{
					const int32 MaxPlayers = FMath::Max(1, NamedSession->SessionSettings.NumPublicConnections);
					const int32 SafeTotal = FMath::Clamp(TotalWithAI, 1, MaxPlayers);
					NamedSession->SessionSettings.Set(
						FName("TOTAL_PLAYERS_WITH_AI"),
						SafeTotal,
						EOnlineDataAdvertisementType::ViaOnlineService);
					SessionPtr->UpdateSession(NAME_GameSession, NamedSession->SessionSettings, true);
					UE_LOG(LogTemp, Log,
						TEXT("[RoomInsidePage] RefreshRoomUI: 推送 TOTAL_PLAYERS_WITH_AI=%d (Attack=%d, Defense=%d, ZombieWrap=%d, Max=%d, Mode=%s)"),
						SafeTotal, AttackCount, DefenseCount, ZombieWrapCount, MaxPlayers,
						bIsZombieMode ? TEXT("Zombie") : TEXT("Melee"));
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[RoomInsidePage] RefreshRoomUI: 推送人数失败, NamedSession 为空"));
				}
			}
		}
	}
}


/**
 * 【2026.07.11 v29 大厂架构重构】内部辅助: 在 Box 里创建单个 PlayerLabel widget
 *
 * 设计动机: 旧 (v28) 错误做法 — RefreshRoomUI 内联 70 行 widget 创建/属性设置, 真人 + AI 各一份
 *   → 重复代码, 易出 bug (用户反馈: AI 移出按钮 / AI 准备按钮显示 等都曾错)
 * 新 (v29): 抽出本函数, 真人 + AI 共用, **单一入口 + 单一真理**
 *
 * 大厂原则:
 *   - 单一入口: widget 创建 + 所有属性设置只在此一处
 *   - 显式意图: bIsAI 显式传 — 调用方说明"这条是 AI 还是真人"
 *   - 零兜底: 各步骤出错显式 Log Error + 不渲染 (而不是"自动选个别的角色")
 */
void URoomInsidePage::CreatePlayerLabelInBox(
	UPanelWidget* TargetBox,
	const FString& PName,
	bool bIsAI,
	const FString& CurrentHostName,
	bool bAmILocalHost,
	const FString& LocalAccountName,
	bool bLabelReady)
{
	// 【大厂原则 - 零兜底】入参保护: TargetBox 为 nullptr → 显式报错, 不渲染
	if (!TargetBox)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomInsidePage] CreatePlayerLabelInBox: TargetBox=nullptr, PName=%s, bIsAI=%d"),
			*PName, bIsAI ? 1 : 0);
		return;
	}

	// 【大厂原则 - 零兜底】入参保护: 空名字 = 数据损坏
	if (PName.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomInsidePage] CreatePlayerLabelInBox: PName 为空, bIsAI=%d"),
			bIsAI ? 1 : 0);
		return;
	}

	// 创建 widget
	UPlayerLabelWidget* PlayerLabel = CreateWidget<UPlayerLabelWidget>(GetWorld(), PlayerLabelClass);
	if (!PlayerLabel)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomInsidePage] CreatePlayerLabelInBox: CreateWidget 失败, PName=%s — PlayerLabelClass 未配置?"),
			*PName);
		return;
	}

	// ==========================================
	// 【Bug1 修复 - 调用顺序关键】
	// 必须先 SetPlayerName (写 CachedPlayerName), 再 SetAsHost/SetAsAI (读 CachedPlayerName 拼后缀)
	// 然后 SetReadyState 才会被 bIsHostEntry / bIsAIEntry 拦截, 不会把 Text_IsReady 显示出来
	// ==========================================
	PlayerLabel->SetPlayerName(PName);

	// 【大厂 v29 修复】bIsHost 判定走显式字段, 不依赖 PName.StartsWith("[AI]") 字符串约定
	bool bIsThisLabelTheHost = false;
	if (!bIsAI) // AI 永远不是房主
	{
		bIsThisLabelTheHost = (PName == CurrentHostName); // 服务器权威

		// 【Bug1 本地权威兜底】即使 GameState 还没下发 HostPlayerName, 只要本机被标记为 Host,
		//                 也立即把本地账号对应的 widget 标为房主
		if (!bIsThisLabelTheHost && bAmILocalHost && !LocalAccountName.IsEmpty() &&
			PName.Equals(LocalAccountName, ESearchCase::IgnoreCase))
		{
			bIsThisLabelTheHost = true;
		}
	}

	// 先标记身份 (SetAsHost/SetAsAI 会写 bIsHostEntry/bIsAIEntry 状态位)
	if (bIsAI)
	{
		PlayerLabel->SetAsAI();
	}
	else if (bIsThisLabelTheHost)
	{
		PlayerLabel->SetAsHost(true);
	}

	// 再设置准备状态 — 内部被 bIsHostEntry/bIsAIEntry 拦截, 不会反向污染 Text_IsReady
	PlayerLabel->SetReadyState(bLabelReady);

	// 只有房主才能看到移除按钮（且不能移除自己）
	// 【2026-06-30 P0 修复】双路权威: NetMode 优先, URoomService 兜底
	bool bAmIHost = false;
	if (UWorld* World = GetWorld())
	{
		if (World->GetNetMode() == NM_ListenServer)
		{
			bAmIHost = true;
		}
	}
	if (!bAmIHost)
	{
		if (URoomService* RoomService = URoomService::Get(this))
		{
			bAmIHost = RoomService->IsHost();
		}
	}
	// 【v47 大厂架构修复】AI 占位也显示踢出按钮 (用户明确需求)
	// 房主可踢 AI 占位 (从 PendingAIQueue 移除), 不会影响已开始战斗的 AI
	PlayerLabel->SetRemoveButtonVisibility(bAmIHost && !bIsThisLabelTheHost);

	// 添加到对应的队伍容器中
	TargetBox->AddChild(PlayerLabel);
}


// ==========================================
// 17. 监听游戏流程状态变化
// ==========================================

/**
 * OnGameFlowStateChanged
 *
 * 监听游戏流程状态变化
 * 时机: 状态变为 EMatchState::Battleing
 * 用途: 销毁大厅 UI
 */
void URoomInsidePage::OnGameFlowStateChanged(EMatchState NewState)
{
	if (NewState == EMatchState::Battleing)
	{
		UE_LOG(LogTemp, Log, TEXT("[RoomInsidePage] 收到战斗开始指令，大厅UI开始自我销毁!"));

		this->RemoveFromParent();
	}
}


// ==========================================
// 18. 同步装备到服务器
// ==========================================

/**
 * SyncLoadoutToServer
 *
 * 同步玩家的装备配置到服务器
 * 1. 获取当前选中的角色 ID
 * 2. 获取两个背包槽位的武器
 * 3. 调用 PC->Server_SelectLoadout 同步服务器
 */
void URoomInsidePage::SyncLoadoutToServer()
{
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		if (UAccountService* AccountSub = UAccountService::Get(this))
		{
			int32 SelectedIndex = ComboBox_CharacterSelect->GetSelectedIndex();

			// 获取当前选择的角色 ID
			FString CurrentCharID = (SelectedIndex != INDEX_NONE) ? CachedCharacterIDs[SelectedIndex].ToString() : TEXT("Default");

			// 【v52 P0】3 把武器:
			//   - W1 (主武器): 来自存档 BP1 的 LastSelectedWeapon[1] — 向后兼容旧存档
			//   - W2 (副武器): 来自运行时 TempSelectedWeaponsByType[Secondary]
			//   - W3 (近战武器): 来自运行时 TempSelectedWeaponsByType[Melee]
			FString CurrentWeapon1 = AccountSub->GetLastSelectedWeapon(1); // 主武器 (BP1 存档)
			FString CurrentWeapon2 = TEXT("");
			FString CurrentWeapon3 = TEXT("");
			if (const FName* SecondaryRow = TempSelectedWeaponsByType.Find(EWeaponMeshType::Secondary))
			{
				CurrentWeapon2 = SecondaryRow->ToString();
			}
			if (const FName* MeleeRow = TempSelectedWeaponsByType.Find(EWeaponMeshType::Melee))
			{
				CurrentWeapon3 = MeleeRow->ToString();
			}

			// 向服务器发送装备配置 (3 把武器一起发, 通过 RoomService 抽象)
			if (URoomService* RoomService = URoomService::Get(this))
			{
				RoomService->RequestSelectLoadout(CurrentCharID, CurrentWeapon1, CurrentWeapon2, CurrentWeapon3);
			}
			// 【架构升级】原 PC->Server_SelectLoadout(...) 调用改走 RoomService
			// PC->Server_SelectLoadout(CurrentCharID, CurrentWeapon1, CurrentWeapon2, CurrentWeapon3);
		}
	}
}

// ==========================================
// 【v211 大厂架构 — Init 阶段默认填充 Secondary/Melee 临时选择】
// ==========================================

/**
 * InitializeTempSelectedWeaponsByDefault
 *
 * 遍历 TempSelectedWeaponsByType, 给"未选过的"武器类型预填业务默认值
 *
 * 业务背景 (用户 2026.08.09 反馈):
 *   "玩家如果没选近战武器, 那就默认使用 DT_WeaponInfo 的 RowName=JZ001 的武器,
 *    选了就把所选的近战武器带入游戏中"
 *
 * 根因 (v211 修复之后暴露的下一个问题):
 *   v211 用 "DT 第 1 个 Melee 类型" 作默认 → 这是按 MeshType 过滤, 行为依赖 DT 行内容
 *   → 策划调整 DT_Melee 行 → 业务默认悄悄改变 → 玩家行为变了但代码看不出原因
 *   → 业务默认值隐式散落在 DT 内容里, 违反大厂架构 "业务规则显式化"
 *
 * v212 修复 (大厂架构 — 业务默认值单一真理源):
 *   1. 业务默认值集中在 FRoomLoadoutDefaults (用户指定 JZ001)
 *   2. 按 RowName 精确匹配 (不依赖 MeshType 过滤) — 零兜底: 找不到 RowName → Log Error + 留空
 *   3. 不允许 "DT 第 N 行" 这种隐式约定 (策划改 DT 行序不应该影响业务默认)
 *   4. 与服务端 Spawn 兜底共享同一真理源 (FRoomLoadoutDefaults::MeleeDefaultRowName)
 *
 * 零兜底保证:
 *   - DT_WeaponInfo 找不到 JZ001 → Log Error + 留空 (Spawn 阶段也会拒绝 Spawn)
 *   - 玩家已选过 TMap[T] → 不覆盖 (玩家选择优先)
 *   - 不允许 "DT 第 1 个 Melee 类型" 这种 MeshType 过滤兜底
 *
 * 业务默认与 v209 Spawn 兜底的关系:
 *   - v212 客户端预填 (业务默认 = JZ001, 用户明确指定)
 *   - v212 Spawn 兜底 (同样读 FRoomLoadoutDefaults::MeleeDefaultRowName → JZ001)
 *   - 客户端 + 服务器**单一真理源**, 不再依赖 DT 行序 / MeshType 过滤
 *   - v209 (DT 第 2 行) 兜底已弃用, 但 ResolveDefaultWeaponRowName(0) 主武器兜底保留
 */
void URoomInsidePage::InitializeTempSelectedWeaponsByDefault(UDataTable* InWeaponDataTable)
{
	if (!InWeaponDataTable)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Room] v212 InitializeTempSelectedWeaponsByDefault: WeaponDataTable 为空, 跳过. "
			     "【零兜底】玩家未选 + 没默认 → Spawn 阶段兜底会失败. "
			     "修复: GM_RoomGameMode → ClassDefaults → WeaponDataTable 必须配 DT_WeaponInfo 资产."));
		return;
	}

	static const FString ContextString(TEXT("URoomInsidePage::InitializeTempSelectedWeaponsByDefault"));

	// 【v212 大厂架构 — 业务默认值从 FRoomLoadoutDefaults 读 (单一真理源)】
	//
	// v211 旧版用 "DT 第 1 个 Melee 类型" 兜底 — 用户已明确指定 JZ001, 不再依赖 MeshType 过滤
	struct FTypeDefaultMapping
	{
		EWeaponMeshType Type;
		FString DefaultRowName; // 来自 FRoomLoadoutDefaults
	};
	const TArray<FTypeDefaultMapping> TypeDefaultMappings = {
		// Secondary 当前没有业务默认 (用户没指定 Secondary 默认武器) → 留空, 走 Spawn v209 兜底
		// v212 阶段仅强制约束 Melee 业务默认 = JZ001
		{ EWeaponMeshType::Melee, FRoomLoadoutDefaults::MeleeDefaultRowName }
	};

	for (const FTypeDefaultMapping& Mapping : TypeDefaultMappings)
	{
		// 零覆盖: 玩家已选过 → 跳过
		if (TempSelectedWeaponsByType.Contains(Mapping.Type))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Room] v212 InitializeTempSelectedWeaponsByDefault: 类型=%d 玩家已选过, 跳过默认填充. (现有=%s)"),
				static_cast<int32>(Mapping.Type), *TempSelectedWeaponsByType[Mapping.Type].ToString());
			continue;
		}

		// 【v212 零兜底】按 RowName 精确匹配 — 不允许 "DT 第 N 行" 或 "MeshType 过滤" 兜底
		const FName DefaultRow(*Mapping.DefaultRowName);
		FWeaponInfo* Row = InWeaponDataTable->FindRow<FWeaponInfo>(DefaultRow, ContextString);
		if (!Row)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Room] v212 InitializeTempSelectedWeaponsByDefault: DT_WeaponInfo 中找不到 RowName='%s' (业务默认). "
				     "【零兜底】不会 fallback 到 DT 第 N 行 — 玩家未选 + 配置缺失 → Spawn 阶段会拒绝 Spawn. "
				     "修复: 1) 在 DT_WeaponInfo 里添加 RowName='%s' 的行; "
				     "2) 或修改 FRoomLoadoutDefaults::%s 指向存在的 RowName. "
				     "配置位置: Source/MetalSlug01/Private/Systems/Spawn/RoomLoadoutDefaults.cpp"),
				*Mapping.DefaultRowName, *Mapping.DefaultRowName,
				Mapping.Type == EWeaponMeshType::Melee ? TEXT("MeleeDefaultRowName") : TEXT("PrimaryDefaultRowName"));
			continue;
		}

		TempSelectedWeaponsByType.FindOrAdd(Mapping.Type) = DefaultRow;
		UE_LOG(LogTemp, Warning,
			TEXT("[Room] v212 InitializeTempSelectedWeaponsByDefault: 类型=%d 业务默认='%s' (玩家未选, 来自 FRoomLoadoutDefaults)"),
			static_cast<int32>(Mapping.Type), *Mapping.DefaultRowName);
	}
}

// ==========================================
// 【架构升级】View 接口实现
// ==========================================

void URoomInsidePage::OnViewShown()
{
    // ==========================================
    // 【2026-06-29 P0 修复】说明: 定时器启动已搬到 NativeConstruct 中
    // 根因: UIViewService::CreateAndShowPanel() 不会调用 IView 的 OnViewShown 生命周期方法
    //       (UIViewService 只 BindViewModel, 不触发 View 的 Show 回调)
    //       → 之前 OnViewShown 中启动的 0.5s 定时器永远不执行 → Bug 永远修不好
    // 修复: 在 NativeConstruct 启动定时器, 这里保留幂等 ClearTimer + 重新 SetTimer,
    //       即使未来 UIViewService 修复了 Show 调用, 此处也能正常工作
    // ==========================================
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PlayerCheckTimerHandle);
        World->GetTimerManager().SetTimer(
            PlayerCheckTimerHandle,
            this,
            &URoomInsidePage::CheckForNewPlayers,
            0.5f,
            true);  // 循环定时器, 直到 ClearTimer
    }

    // 立即刷新一次 UI (玩家进入房间瞬间的首次绘制)
    RefreshRoomUI();

    // 立即刷新房主按钮可见性 (兜底)
    UpdateHostVisibility();
    // 根因: NativeConstruct 设置的可见性在 widget 已创建后失效
    UpdateHostVisibility();
}

void URoomInsidePage::OnViewHidden()
{
    if (UWorld* World = GetWorld())
    {
        // ClearTimer 即使 Handle 未绑定也安全 (no-op), 保留无害
        World->GetTimerManager().ClearTimer(PlayerCheckTimerHandle);
    }

    // 解绑所有订阅的 PlayerState OnStateChanged, 防止内存泄漏
    for (ARoomPlayerState* PS : KnownPlayerStates)
    {
        if (IsValid(PS))
        {
            PS->OnStateChanged.RemoveDynamic(this, &URoomInsidePage::RefreshRoomUI);
        }
    }
    KnownPlayerStates.Reset();
}


// ==========================================
// 【P0 架构升级】URoomService 事件总线回调（替代 0.5s 定时器轮询）
// ==========================================

/**
 * UpdateHostVisibility
 *
 * 【2026-06-29 P0 修复】统一刷新房主/玩家专属按钮可见性
 * 【Bug3 加强】: 若 URoomService 此时 IsHost() 仍为 false (Presenter::NotifyBecameHost
 *                还未触发, 例如 OpenLevel 跨图过程中) → 不再静默丢失, 而是订阅 OnHostChanged
 *                让 Presenter 后续调 BroadcastHostChanged 时再次刷新
 *
 * 业务规则:
 *   - Btn_OpenAIPanel : 仅房主可见 (添加 AI 是房主专属权限)
 *   - Btn_StartGame   : 仅房主可见 (开始游戏是房主专属权限)
 *   - Btn_ToggleReady : 仅非房主可见 (房主永远不需要"准备", 房主随时可以开始)
 *
 * 设计理由:
 *   - 旧代码在 NativeConstruct 里设置可见性, 但 widget 早已创建后不会重新执行
 *   - 玩家B 加入时 NativeConstruct 不再跑, 按钮可见性停留在初次进入的状态
 *   - 提取为函数, 在三处统一调用: NativeConstruct / OnViewShown / OnRoomServiceHostChanged
 *   - 确保无论何时身份变化, 按钮可见性都立即生效
 */
void URoomInsidePage::UpdateHostVisibility()
{
    // ==========================================
    // 【2026-06-30 P0 Bug3 终极修复】双路权威判定本机是否房主
    //   路1: NetMode == NM_ListenServer (UE 网络层权威, 不依赖任何 Subsystem)
    //   路2: URoomService::IsHost() (跨图前已持久, 兜底)
    // 这样无论跨图前后/GameInstance 是否被重建, 按钮可见性都是 100% 准确的
    // ==========================================
    bool bIsHost = false;

    // 路1: NetMode 权威判定
    if (UWorld* World = GetWorld())
    {
        if (World->GetNetMode() == NM_ListenServer)
        {
            bIsHost = true;
        }
    }

    // 路2: URoomService 兜底 (NetMode 不为 ListenServer 时, 比如纯 Standalone 也可能)
    if (!bIsHost)
    {
        URoomService* RoomService = URoomService::Get(this);
        bIsHost = RoomService && RoomService->IsHost();
    }

    // ==========================================
    // 【Bug3 P0 防御】URoomService 此刻还拿不到 Host 身份怎么办?
    // 场景: 房主刚 OpenLevel 跳转到 L_Room → GameInstance 持久保留 bIsHost=true,
    //       但 LANRoomPresenter::NotifyBecameHost() 在新地图 OpenLevel 异步延迟回调
    //       → 期间 NativeConstruct / OnViewShown 调用此函数读到 bIsHost=true (GameInstance 持久)
    // 实际根因: GameInstanceSubsystem bIsHost 在切图过程中是持久的,
    //          所以读取永远是准确的 → 我们不需要 defer
    // 但若未来 bIsHost 不持久, 下面的 defer 兜底机制会兜住
    // ==========================================
    if (!URoomService::Get(this))
    {
        UE_LOG(LogTemp, Warning, TEXT("[RoomInsidePage] UpdateHostVisibility: URoomService 获取失败!"));
    }

    const ESlateVisibility HostVisibility = bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
    const ESlateVisibility ClientVisibility = bIsHost ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;

    // 房主专属: 打开 AI 面板
    if (Btn_OpenAIPanel)
    {
        Btn_OpenAIPanel->SetVisibility(HostVisibility);
    }

    // 房主专属: 开始游戏
    if (Btn_StartGame)
    {
        Btn_StartGame->SetVisibility(HostVisibility);
    }

    // 非房主专属: 切换准备状态 (房主永远不需要准备)
    if (Btn_ToggleReady)
    {
        Btn_ToggleReady->SetVisibility(ClientVisibility);
    }

    UE_LOG(LogTemp, Log,
        TEXT("[RoomInsidePage] UpdateHostVisibility: bIsHost=%d (NetMode=%d), OpenAI=%d, StartGame=%d, ToggleReady=%d"),
        bIsHost ? 1 : 0,
        GetWorld() ? (int32)GetWorld()->GetNetMode() : -1,
        Btn_OpenAIPanel ? (int32)Btn_OpenAIPanel->GetVisibility() : -1,
        Btn_StartGame ? (int32)Btn_StartGame->GetVisibility() : -1,
        Btn_ToggleReady ? (int32)Btn_ToggleReady->GetVisibility() : -1);
}

/**
 * OnRoomServiceHostChanged
 *
 * 房主身份变化时触发（RoomService.OnHostChanged）
 * 用途: 重新计算"开始游戏/移除玩家/添加 AI"等房主专属按钮的可见性
 *
 * 【Bug1 P0 修复】除了按钮可见性, 还要**主动修正本地玩家自己的 PlayerLabel**:
 *   场景:
 *     1. NativeConstruct 时 RoomStateService.HostPlayerName 还没同步下来 (服务器 ON_REP 时延)
 *     2. RefreshRoomUI 创建本地玩家的 widget 时, 没法识别"我是不是房主"
 *     3. 因此 SetAsHost(true) 没被调用, Text_IsReady 被错误地显示为 "未准备"
 *     4. 一旦 OnRep_HostPlayerName 下发 → URoomService.BroadcastHostChanged → 这里被触发
 *     5. 这里遍历 Box_AttackTeam/Box_DefenseTeam 找本地玩家 widget, 调 ApplyIdentity(true, false)
 *        → 强制把"我这条"标为房主 → Text_IsReady 立即 Collapsed
 */
void URoomInsidePage::OnRoomServiceHostChanged(bool bIsHostNow)
{
	// 1. 房主身份变了, 立即刷新按钮可见性 + 玩家列表
	UpdateHostVisibility();
	RefreshRoomUI();

	// 2. 【Bug1 防御】如果变成房主, 强制把所有以"本地账号名"命名的 widget 标为房主
	//    这是对 RefreshRoomUI 漏判的兜底
	if (bIsHostNow)
	{
		ForceApplyHostIdentityToLocalWidget();
	}
}


// ==========================================================
// 【v93 新增】刀战/生化模式 Canvas 显示/隐藏
// ==========================================================
//
// 职责:
//   - 根据 GameState.CurrentMatchMode, 互斥显示 Canvas_MeleeContainer 或 Canvas_ZombieContainer
//   - 模式切换时清空不匹配的旧 Box (避免残留 widget)
//   - 触发 RefreshRoomUI 重新渲染当前模式的玩家标签
//
// 大厂原则:
//   - 单一入口: ApplyVisibilityByMode 是 Canvas 显示/隐藏的唯一入口
//   - 零兜底: Mode == None/未识别模式 → 全部 Collapsed + Log Error (强制修复)
//   - 职责对等: Melee 和 Zombie 各有独立 Canvas, 互不耦合
// ==========================================================

/**
 * OnGameStateMatchModeChanged
 *
 * GameState.OnMatchModeChanged 委托回调 (服务器写入触发或客户端 OnRep 触发)
 * 流程:
 *   1. 立即 ApplyVisibilityByMode(NewMode) 切换 Canvas 显隐
 *   2. RefreshRoomUI 重新渲染当前模式的玩家标签
 *
 * 为什么必须 RefreshRoomUI:
 *   - 模式切换后, 旧 Box (例如 Melee 的 VerticalBox) 可能残留旧 widget
 *   - 新模式需要在新的 Canvas 里渲染对应模式的玩家/AI 标签
 *   - RefreshRoomUI 会清空所有 Box 然后按当前模式渲染
 */
void URoomInsidePage::OnGameStateMatchModeChanged(ERoomMatchMode NewMode)
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomInsidePage] OnGameStateMatchModeChanged: 房间模式变化, NewMode=%d"),
		static_cast<int32>(NewMode));

	// 1. 应用新模式的 Canvas 显隐
	ApplyVisibilityByMode(NewMode);

	// 2. 重新刷新玩家标签列表（按新模式渲染到对应 Canvas 内的容器）
	RefreshRoomUI();
}


/**
 * ApplyVisibilityByMode
 *
 * 根据模式设置 Canvas_MeleeContainer / Canvas_ZombieContainer 的 Visible/Collapsed
 *
 * 大厂原则 — 单一入口 + 互斥显示:
 *   - Melee: Melee Visible, Zombie Collapsed, WrapBox Collapsed
 *   - Zombie: Melee Collapsed, Zombie Visible, WrapBox Visible
 *   - None/其他: 全部 Collapsed + Log Error (零兜底)
 *
 * @param Mode 当前游戏模式 (来自 GameState.CurrentMatchMode)
 */
void URoomInsidePage::ApplyVisibilityByMode(ERoomMatchMode Mode)
{
	if (Mode == ERoomMatchMode::Melee)
	{
		// 刀战模式: 刀战容器可见, 生化容器折叠
		if (Canvas_MeleeContainer)
		{
			Canvas_MeleeContainer->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomInsidePage] ApplyVisibilityByMode: Canvas_MeleeContainer 为空! "
				     "请检查 BP_WBP_RoomInsidePage 是否有名为 Canvas_MeleeContainer 的 CanvasPanel 控件."));
		}
		if (Canvas_ZombieContainer)
		{
			Canvas_ZombieContainer->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (WrapBox_ZombiePlayers)
		{
			WrapBox_ZombiePlayers->ClearChildren();
			WrapBox_ZombiePlayers->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	else if (Mode == ERoomMatchMode::Zombie)
	{
		// 生化模式: 生化容器可见, 刀战容器折叠
		if (Canvas_MeleeContainer)
		{
			Canvas_MeleeContainer->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (Canvas_ZombieContainer)
		{
			Canvas_ZombieContainer->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomInsidePage] ApplyVisibilityByMode: Canvas_ZombieContainer 为空! "
				     "请检查 BP_WBP_RoomInsidePage 是否有名为 Canvas_ZombieContainer 的 CanvasPanel 控件."));
		}
		if (WrapBox_ZombiePlayers)
		{
			WrapBox_ZombiePlayers->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomInsidePage] ApplyVisibilityByMode: WrapBox_ZombiePlayers 为空! "
				     "请检查 BP_WBP_RoomInsidePage 是否有名为 WrapBox_ZombiePlayers 的 WrapBox 控件."));
		}

		// 清空刀战容器残留 widget (防止模式切换时 Box_AttackTeam/Box_DefenseTeam 残留旧模式 widget)
		if (Box_AttackTeam) Box_AttackTeam->ClearChildren();
		if (Box_DefenseTeam) Box_DefenseTeam->ClearChildren();
	}
	else
	{
		// 零兜底: 未识别模式 → 全部折叠 + 报错
		UE_LOG(LogTemp, Error,
			TEXT("[RoomInsidePage] ApplyVisibilityByMode: 未识别模式=%d, 所有 Canvas 折叠. "
			     "【修复】检查 GameState.CurrentMatchMode 是否被合法赋值 (Melee/Zombie)."),
			static_cast<int32>(Mode));
		if (Canvas_MeleeContainer) Canvas_MeleeContainer->SetVisibility(ESlateVisibility::Collapsed);
		if (Canvas_ZombieContainer) Canvas_ZombieContainer->SetVisibility(ESlateVisibility::Collapsed);
		if (WrapBox_ZombiePlayers) WrapBox_ZombiePlayers->SetVisibility(ESlateVisibility::Collapsed);
	}
}


/**
 * ForceApplyHostIdentityToLocalWidget
 *
 * 【Bug1 大厂 P0 修复】兜底刷新: 遍历本机 VBox, 把本地账号名对应的 PlayerLabel 标为房主
 * 触发场景: 服务器 OnRep_HostPlayerName 时延 + RoomStateService.GetMatchSnapshot 数据不一致
 * 副作用: ApplyIdentity 内会写 bIsHostEntry, 后续 SetReadyState 不会再把 Text_IsReady 显出来
 */
void URoomInsidePage::ForceApplyHostIdentityToLocalWidget()
{
	// 仅房主身份才需要做这件事
	URoomService* RoomService = URoomService::Get(this);
	if (!RoomService || !RoomService->IsHost()) return;

	const FString LocalAccountName = RoomService->GetCurrentAccountName();
	if (LocalAccountName.IsEmpty()) return;

	// 遍历 Box_AttackTeam / Box_DefenseTeam / WrapBox_ZombiePlayers 三个容器, 找到本地玩家 widget
	// 【v93 大厂架构】模式分支: Zombie 模式下玩家都在 WrapBox_ZombiePlayers, 不分阵营
	auto ApplyToBox = [&](UPanelWidget* Box)
	{
		if (!Box) return;
		for (int32 i = 0; i < Box->GetChildrenCount(); ++i)
		{
			if (UPlayerLabelWidget* PlayerLabel = Cast<UPlayerLabelWidget>(Box->GetChildAt(i)))
			{
				const FString LabelName = PlayerLabel->GetPlayerName();
				if (!LabelName.IsEmpty() && LabelName.Equals(LocalAccountName, ESearchCase::IgnoreCase))
				{
					// 【P0】调 ApplyIdentity 而非 SetAsHost: ApplyIdentity 内有"已是房主则跳过"判断, 安全幂等
					PlayerLabel->ApplyIdentity(/*bIsHostEntry=*/true, /*bIsAIEntry=*/PlayerLabel->IsAIEntry());

					UE_LOG(LogTemp, Log,
						TEXT("[RoomInsidePage] ForceApplyHostIdentityToLocalWidget: 修正本地玩家 [%s] 为房主 (UI 索引=%d)"),
						*LocalAccountName, i);
				}
			}
		}
	};

	ApplyToBox(Box_AttackTeam);
	ApplyToBox(Box_DefenseTeam);
	ApplyToBox(WrapBox_ZombiePlayers);
}

/**
 * OnRoomServicePlayerJoined
 *
 * 有玩家加入时触发（RoomService.OnPlayerJoined）
 * 用途: 立即刷新房间标签列表（不等 5s 兜底定时器）
 */
void URoomInsidePage::OnRoomServicePlayerJoined(const FString& PlayerName)
{
	// 直接调用 CheckForNewPlayers 重新维护 KnownPlayerStates + 触发 RefreshRoomUI
	CheckForNewPlayers();
}

/**
 * OnRoomServicePlayerLeft
 *
 * 有玩家离开时触发（RoomService.OnPlayerLeft）
 * 用途: 立即刷新房间标签列表
 */
void URoomInsidePage::OnRoomServicePlayerLeft(const FString& PlayerName)
{
	CheckForNewPlayers();
}
