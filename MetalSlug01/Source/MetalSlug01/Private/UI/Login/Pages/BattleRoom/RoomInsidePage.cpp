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
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Components/Overlay.h"
#include "Components/UniformGridPanel.h"
#include "Components/Image.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Login/Pages/BattleRoom/PlayerLabelWidget.h"
#include "Systems/RoomPlayerController.h"
#include "Systems/Account/AccountSubsystem.h"
#include "Components/ComboBoxString.h"
#include "Engine/DataTable.h"
#include "Data/Tables/CharacterTableRow.h"
#include "Data/Enums/RoomEnums.h"
#include "Components/UniformGridSlot.h"
#include "UI/Login/Pages/BattleRoom/WeaponIconWidget.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Systems/GameFlowSubsystem.h"
#include "Systems/RoomGameState.h"
#include "Systems/Core/RoomPlayerState.h"


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
	if (Btn_JoinAttackTeam) Btn_JoinAttackTeam->OnClicked.AddDynamic(this, &URoomInsidePage::OnJoinAttackTeamClicked);
	// 绑定加入守方按钮点击事件
	if (Btn_JoinDefenseTeam) Btn_JoinDefenseTeam->OnClicked.AddDynamic(this, &URoomInsidePage::OnJoinDefenseTeamClicked);
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

	// 绑定打开武器选择弹窗按钮点击事件
	if (Btn_ChangeWeapon){Btn_ChangeWeapon->OnClicked.AddDynamic(this, &URoomInsidePage::OnChangeWeaponClicked);}

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

	// 启动定时器，每 0.5 秒检查一次玩家变化
	GetWorld()->GetTimerManager().SetTimer(PlayerCheckTimerHandle, this, &URoomInsidePage::CheckForNewPlayers, 0.5f, true);
	// 立即执行一次 UI 刷新
	RefreshRoomUI();

	// 判断当前玩家是否为房主
	bool bIsHost = GetOwningPlayer()->HasAuthority();

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
			UAccountSubsystem* AccountSub = nullptr;
			if (UGameInstance* GI = GetGameInstance())
			{
				AccountSub = GI->GetSubsystem<UAccountSubsystem>();
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

			// 初始化武器配置，确保每个背包槽位都有默认武器
			if (WeaponDataTable && AccountSub)
			{
				TArray<FName> WeaponRows = WeaponDataTable->GetRowNames();
				FString DefaultWeapon = TEXT("");
				if (WeaponRows.Num() > 0) DefaultWeapon = WeaponRows[0].ToString();

				// 为两个背包槽位设置默认武器
				for (int32 WSlot = 1; WSlot <= 2; WSlot++)
				{
					FString SavedWeapon = AccountSub->GetLastSelectedWeapon(WSlot);
					if (SavedWeapon.IsEmpty())
					{
						// 如果该槽位没有保存的武器，使用默认武器
						AccountSub->SaveLastSelectedWeapon(WSlot, DefaultWeapon);
						UE_LOG(LogTemp, Warning, TEXT("[Room] Init weapon slot %d -> '%s'"), WSlot, *DefaultWeapon);
					}
				}
			}

			// 向服务器同步当前的装备配置
			if (ARoomPlayerController* PC2 = Cast<ARoomPlayerController>(GetOwningPlayer()))
			{
				FString W1 = AccountSub ? AccountSub->GetLastSelectedWeapon(1) : TEXT("");
				FString W2 = AccountSub ? AccountSub->GetLastSelectedWeapon(2) : TEXT("");
				PC2->Server_SelectLoadout(CharIDToSync, W1, W2);
			}

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
	UpdateWeaponDisplayImage(ActiveBackpackSlot);

	// 更新背包高亮指示器
	UpdateInventoryHighlightUI(ActiveBackpackSlot);

	// 初始化时隐藏 AI 配置面板
	if (Overlay_AddAI)
	{
		Overlay_AddAI->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 填充 AI 配置面板的数据
	PopulateAIPanelData();

	// 只有房主才能看到打开 AI 面板的按钮
	if (Btn_OpenAIPanel)
	{
		Btn_OpenAIPanel->SetVisibility(bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 从会话设置中读取房间名称和游戏模式，并显示在 UI 上
	if (Text_RoomName)
	{
		FString DisplayRoomName = TEXT("未命名房间");

		FString DisplayGameMode = TEXT("默认模式");

		IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
		if (OnlineSub)
		{
			IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
			if (Sessions.IsValid())
			{
				FNamedOnlineSession* Session = Sessions->GetNamedSession(NAME_GameSession);
				if (Session)
				{
					// 从会话设置中获取房间名称
					Session->SessionSettings.Get(FName("ROOM_NAME"), DisplayRoomName);
					// 从会话设置中获取游戏模式
					Session->SessionSettings.Get(FName("GAME_MODE"), DisplayGameMode);
				}
			}
		}
		// 组合房间名称和游戏模式显示文本
		FString FinalDisplayText = FString::Printf(TEXT("%s-%s"), *DisplayRoomName, *DisplayGameMode);
		Text_RoomName->SetText(FText::FromString(FinalDisplayText));
	}

	// 非房主显示准备按钮，房主隐藏该按钮
	if (Btn_ToggleReady) Btn_ToggleReady->SetVisibility(bIsHost ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);

	// 只有房主才能看到开始游戏按钮
	if (Btn_StartGame)
	{
		if (GetOwningPlayer() && GetOwningPlayer()->HasAuthority())
		{
			Btn_StartGame->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			Btn_StartGame->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

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
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		PC->Server_RequestStartGame();
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
	// 检查攻方人数是否已达到上限（5 人）
	if (Box_AttackTeam && Box_AttackTeam->GetChildrenCount() >= 5)
	{
		AddSystemMessageToChat(TEXT("系统提示: 攻方人数已满，不可更换队伍!"));
		return;
	}

	// 向服务器请求加入攻方 (true 表示攻方)
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		PC->Server_RequestChangeTeam(true);
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
	// 检查守方人数是否已达到上限（5 人）
	if (Box_DefenseTeam && Box_DefenseTeam->GetChildrenCount() >= 5)
	{
		AddSystemMessageToChat(TEXT("系统提示: 守方人数已满，不可更换队伍!"));
		return;
	}

	// 向服务器请求加入守方 (false 表示守方)
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		PC->Server_RequestChangeTeam(false);
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
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		PC->Server_ToggleReady(bIsReady);
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
		// 向服务器发送聊天消息
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
		{
			PC->Server_SendChatMessage(Text.ToString());
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
		if (UAccountSubsystem* AccSub = GI->GetSubsystem<UAccountSubsystem>())
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
 *
 * 确认更换武器
 * 1. 校验 TempSelectedWeaponRow
 * 2. 保存到 AccountSubsystem
 * 3. 调用 UpdateWeaponDisplayImage 刷新主界面图标
 * 4. 调用 SyncLoadoutToServer 同步服务器
 * 5. 关闭弹窗
 */
void URoomInsidePage::OnConfirmWeaponChangeClicked()
{
	if (!TempSelectedWeaponRow.IsNone())
	{
		// 保存选择的武器到 AccountSubsystem
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
			{
				AccountSub->SaveLastSelectedWeapon(ActiveBackpackSlot, TempSelectedWeaponRow.ToString());
			}
		}
		// 更新主界面武器显示
		UpdateWeaponDisplayImage(ActiveBackpackSlot);

		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green, FString::Printf(TEXT("背包 %d 已装备武器: %s"), ActiveBackpackSlot, *TempSelectedWeaponRow.ToString()));

		// 同步装备配置到服务器
		SyncLoadoutToServer();
	}

	// 关闭武器选择弹窗
	if (Overlay_WeaponSelect)
	{
		Overlay_WeaponSelect->SetVisibility(ESlateVisibility::Collapsed);
	}
}


/**
 * OnChangeWeaponClicked
 *
 * 打开武器选择弹窗
 * 1. 从 AccountSubsystem 读取当前背包槽位的武器
 * 2. 设置武器预览图
 * 3. 显示弹窗
 * 4. 调用 PopulateWeaponGrid 生成网格
 */
void URoomInsidePage::OnChangeWeaponClicked()
{
	// 从 AccountSubsystem 中读取当前背包槽位的武器
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			FString SavedWeapon = AccountSub->GetLastSelectedWeapon(ActiveBackpackSlot);
			TempSelectedWeaponRow = FName(*SavedWeapon);
		}
	}

	// 如果没有临时选择的武器，使用第一个武器作为默认值
	if (TempSelectedWeaponRow.IsNone() && WeaponDataTable)
	{
		TArray<FName> RowNames = WeaponDataTable->GetRowNames();
		if (RowNames.Num() > 0) TempSelectedWeaponRow = RowNames[0];
	}

	// 设置武器预览图
	if (!TempSelectedWeaponRow.IsNone() && WeaponDataTable && Image_WeaponPreview)
	{
		FWeaponInfo* WeaponData = WeaponDataTable->FindRow<FWeaponInfo>(TempSelectedWeaponRow, TEXT("InitPreview"));
		if (WeaponData && WeaponData->WeaponIcon)
		{
			Image_WeaponPreview->SetBrushFromTexture(WeaponData->WeaponIcon);
		}
	}

	// 显示武器选择弹窗并生成武器网格
	if (Overlay_WeaponSelect) Overlay_WeaponSelect->SetVisibility(ESlateVisibility::Visible);
	PopulateWeaponGrid();
}


// ==========================================
// 11. 背包切换
// ==========================================

/**
 * OnInventory1Clicked
 *
 * 切换到背包 1
 * 1. 设置 ActiveBackpackSlot = 1
 * 2. 更新武器显示
 * 3. 更新高亮指示器
 */
void URoomInsidePage::OnInventory1Clicked()
{
	ActiveBackpackSlot = 1;
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan, TEXT("当前切换至: 背包 1"));
	UpdateWeaponDisplayImage(ActiveBackpackSlot);
	UpdateInventoryHighlightUI(ActiveBackpackSlot);
}


/**
 * OnInventory2Clicked
 *
 * 切换到背包 2
 */
void URoomInsidePage::OnInventory2Clicked()
{
	ActiveBackpackSlot = 2;
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan, TEXT("当前切换至: 背包 2"));

	UpdateWeaponDisplayImage(ActiveBackpackSlot);
	UpdateInventoryHighlightUI(ActiveBackpackSlot);
}


// ==========================================
// 12. 武器网格生成
// ==========================================

/**
 * PopulateWeaponGrid
 *
 * 根据 WeaponDataTable 动态生成武器选择网格
 * 1. 清空现有网格
 * 2. 遍历所有武器，创建 UWeaponIconWidget
 * 3. 调用 SetupWeaponItem 设置信息
 * 4. 设置高亮（已选中的）
 * 5. 计算网格位置并 AddChildToUniformGrid
 */
void URoomInsidePage::PopulateWeaponGrid()
{
	if (!WeaponDataTable || !WeaponItemClass || !Grid_WeaponItems) return;

	// 清空现有网格内容
	Grid_WeaponItems->ClearChildren();

	static const FString ContextString(TEXT("Weapon Context"));
	TArray<FName> RowNames = WeaponDataTable->GetRowNames();

	int32 MaxColumns = 4; // 每行最多 4 个武器
	int32 CurrentIndex = 0;

	// 遍历所有武器，创建对应的图标 Widget
	for (const FName& RowName : RowNames)
	{
		FWeaponInfo* WeaponData = WeaponDataTable->FindRow<FWeaponInfo>(RowName, ContextString);
		if (WeaponData)
		{
			UWeaponIconWidget* NewItem = CreateWidget<UWeaponIconWidget>(this, WeaponItemClass);
			if (NewItem)
			{
				// 设置武器信息并绑定点击事件
				NewItem->SetupWeaponItem(RowName, *WeaponData, this);

				// 检查是否为当前选中的武器，如果是则高亮显示
				bool bIsEquippedWeapon = (RowName == TempSelectedWeaponRow);
				NewItem->SetHighlightFrameVisibility(bIsEquippedWeapon);

				// 计算网格位置
				int32 Row = CurrentIndex / MaxColumns;
				int32 Col = CurrentIndex % MaxColumns;

				UUniformGridSlot* GridSlot = Grid_WeaponItems->AddChildToUniformGrid(NewItem, Row, Col);

				CurrentIndex++;
			}
		}
	}
}


/**
 * OnWeaponItemSelectedInGrid
 *
 * 武器网格中被选中时调用
 * 1. 更新 TempSelectedWeaponRow
 * 2. 更新武器预览图
 * 3. 刷新所有武器图标的高亮状态
 */
void URoomInsidePage::OnWeaponItemSelectedInGrid(FName WeaponRowName)
{
	// 更新临时选择的武器
	TempSelectedWeaponRow = WeaponRowName;

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
				bool bShouldHighlight = (IconWidget->GetWeaponRowName() == TempSelectedWeaponRow);
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
 * 根据指定背包槽位的武器配置更新显示的武器图标
 * 1. 从 AccountSubsystem 读取武器
 * 2. 从 WeaponDataTable 查找 FWeaponInfo
 * 3. 设置 Image_WeaponDisplay 的画刷
 */
void URoomInsidePage::UpdateWeaponDisplayImage(int32 BackpackSlot)
{
	if (!Image_WeaponDisplay || !WeaponDataTable) return;

	FString SavedWeaponRow = TEXT("");

	// 从 AccountSubsystem 中读取保存的武器
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			SavedWeaponRow = AccountSub->GetLastSelectedWeapon(BackpackSlot);
		}
	}

	// 如果没有保存的武器，使用第一个武器作为默认值
	if (SavedWeaponRow.IsEmpty())
	{
		TArray<FName> RowNames = WeaponDataTable->GetRowNames();
		if (RowNames.Num() > 0)
		{
			SavedWeaponRow = RowNames[0].ToString();
		}
	}

	// 查找武器数据并更新显示
	if (!SavedWeaponRow.IsEmpty())
	{
		FWeaponInfo* WeaponData = WeaponDataTable->FindRow<FWeaponInfo>(FName(*SavedWeaponRow), TEXT("UpdateWeaponDisplay"));
		if (WeaponData && WeaponData->WeaponIcon)
		{
			Image_WeaponDisplay->SetBrushFromTexture(WeaponData->WeaponIcon);
		}
	}
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
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
		{
			bool bIsAttackTeam = SelectedTeam.Contains(TEXT("攻方"));
			PC->Server_AddAI(bIsAttackTeam, SelectedChar, ActualAddCount);
		}

		if (GEngine)
		{
			FString DebugMsg = FString::Printf(TEXT("已向服务器请求往%s添加 %d 名AI"), *SelectedTeam, ActualAddCount);
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
 * 0.5 秒一次的玩家变化检查
 * 1. 扫描 GameState->PlayerArray
 * 2. 新玩家 -> 加入 KnownPlayerStates + 订阅 OnStateChanged
 * 3. 已离开 -> 从 KnownPlayerStates 移除
 * 4. 期望人数 != UI 实际人数 -> 触发 RefreshRoomUI
 */
void URoomInsidePage::CheckForNewPlayers()
{
	ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>();
	if (!GS) return;

	bool bNeedsRefresh = false;

	// 遍历 GameState 中的所有玩家状态
	for (APlayerState* GenericPS : GS->PlayerArray)
	{
		if (ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(GenericPS))
		{
			// 检查是否为新玩家
			if (!KnownPlayerStates.Contains(RoomPS))
			{
				// 添加到已知玩家列表
				KnownPlayerStates.Add(RoomPS);
				// 订阅该玩家的状态变化事件
				RoomPS->OnStateChanged.AddDynamic(this, &URoomInsidePage::RefreshRoomUI);
				bNeedsRefresh = true;
			}
		}
	}

	// 检查是否有玩家离开
	for (int32 i = KnownPlayerStates.Num() - 1; i >= 0; --i)
	{
		ARoomPlayerState* TrackedPS = KnownPlayerStates[i];
		if (!IsValid(TrackedPS) || !GS->PlayerArray.Contains(TrackedPS))
		{
			// 从已知玩家列表中移除
			KnownPlayerStates.RemoveAt(i);
			bNeedsRefresh = true;
		}
	}

	// 计算期望的攻守方人数
	int32 ExpectedAttackCount = 0;
	int32 ExpectedDefenseCount = 0;

	for (ARoomPlayerState* PS : KnownPlayerStates)
	{
		if (IsValid(PS))
		{
			if (PS->CurrentTeam == ERoomTeam::Attack) ExpectedAttackCount++;
			else if (PS->CurrentTeam == ERoomTeam::Defense) ExpectedDefenseCount++;
		}
	}

	// 获取 UI 中实际显示的人数
	int32 UIAttackCount = Box_AttackTeam ? Box_AttackTeam->GetChildrenCount() : 0;
	int32 UIDefenseCount = Box_DefenseTeam ? Box_DefenseTeam->GetChildrenCount() : 0;
	int32 RenderedTotalCount = UIAttackCount + UIDefenseCount;

	// 检查是否需要刷新 UI（人数不匹配或队伍分布不一致）
	if (RenderedTotalCount != KnownPlayerStates.Num() ||
		ExpectedAttackCount != UIAttackCount ||
		ExpectedDefenseCount != UIDefenseCount)
	{
		bNeedsRefresh = true;
	}

	// 如果需要刷新，执行 UI 重绘
	if (bNeedsRefresh)
	{
		RefreshRoomUI();
	}
}


/**
 * RefreshRoomUI
 *
 * 重新绘制房间 UI，更新所有玩家的显示信息
 * 1. 清空攻守方列表
 * 2. 校验 PlayerLabelClass 是否已配置
 * 3. 获取房主名
 * 4. 遍历 KnownPlayerStates 创建 PlayerLabelWidget
 * 5. 设置 AI/房主/玩家标签样式
 * 6. 设置移除按钮可见性（房主有，且非房主自己）
 */
void URoomInsidePage::RefreshRoomUI()
{
	// 清空攻守方列表
	if (Box_AttackTeam) Box_AttackTeam->ClearChildren();
	if (Box_DefenseTeam) Box_DefenseTeam->ClearChildren();

	// 检查 PlayerLabelClass 是否已配置
	if (!PlayerLabelClass)
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, TEXT("[严重错误] PlayerLabelClass 未配置!"));
		return;
	}

	// 获取房主名称
	FString CurrentHostName = TEXT("");
	if (ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>())
	{
		CurrentHostName = GS->HostPlayerName;
	}

	// 遍历所有已知玩家，创建对应的 UI 标签
	for (ARoomPlayerState* PS : KnownPlayerStates)
	{
		if (!IsValid(PS)) continue;

		// 确定目标队伍容器
		UVerticalBox* TargetBox = nullptr;
		if (PS->CurrentTeam == ERoomTeam::Attack) TargetBox = Box_AttackTeam;
		else if (PS->CurrentTeam == ERoomTeam::Defense) TargetBox = Box_DefenseTeam;

		if (TargetBox)
		{
			// 创建玩家标签 Widget
			UPlayerLabelWidget* PlayerLabel = CreateWidget<UPlayerLabelWidget>(GetWorld(), PlayerLabelClass);
			if (PlayerLabel)
			{
				FString PName = PS->GetPlayerName();

				// 设置玩家名称和准备状态
				PlayerLabel->SetPlayerName(PName);
				PlayerLabel->SetReadyState(PS->bIsReady);

				bool bIsAI = PName.StartsWith(TEXT("[AI]")); // 是否为 AI 玩家
				bool bIsThisLabelTheHost = (PName == CurrentHostName); // 是否为房主

				if (bIsAI)
				{
					// 标记为 AI 玩家
					PlayerLabel->SetAsAI();
				}
				else if (bIsThisLabelTheHost)
				{
					// 标记为房主
					PlayerLabel->SetAsHost(true);
				}

				// 只有房主才能看到移除按钮（且不能移除自己）
				bool bAmIHost = GetOwningPlayer()->HasAuthority();
				PlayerLabel->SetRemoveButtonVisibility(bAmIHost && !bIsThisLabelTheHost);

				// 添加到对应的队伍容器中
				TargetBox->AddChild(PlayerLabel);
			}
		}
	}
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
		if (UAccountSubsystem* AccountSub = GetGameInstance()->GetSubsystem<UAccountSubsystem>())
		{
			int32 SelectedIndex = ComboBox_CharacterSelect->GetSelectedIndex();

			// 获取当前选择的角色 ID
			FString CurrentCharID = (SelectedIndex != INDEX_NONE) ? CachedCharacterIDs[SelectedIndex].ToString() : TEXT("Default");

			// 获取两个背包槽位的武器
			FString CurrentWeapon1 = AccountSub->GetLastSelectedWeapon(1);
			FString CurrentWeapon2 = AccountSub->GetLastSelectedWeapon(2);

			// 向服务器发送装备配置
			PC->Server_SelectLoadout(CurrentCharID, CurrentWeapon1, CurrentWeapon2);
		}
	}
}
