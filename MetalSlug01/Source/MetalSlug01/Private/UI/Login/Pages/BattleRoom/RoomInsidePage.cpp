/**
 * @file RoomInsidePage.cpp
 * @brief 房间内部页面实现文件
 * @author 开发团队
 * @date 2026-04-20
 * 
 * 实现了房间内部UI的所有功能，包括：
 * - 玩家队伍管理（攻方/守方）
 * - 角色和武器选择
 * - 聊天系统
 * - AI玩家配置
 * - UI自动刷新机制
 */

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
#include "UI/Login/Core/AccountSubsystem.h"
#include "Components/ComboBoxString.h"
#include "Engine/DataTable.h"
#include "UI/Login/Data/StaticTable.h"
#include "Components/UniformGridSlot.h"
#include "UI/Login/Pages/BattleRoom/WeaponIconWidget.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Systems/GameFlowSubsystem.h"
#include "Systems/RoomGameState.h"
#include "UI/Login/Core/RoomPlayerState.h"

/**
 * @brief 初始化函数，设置所有UI控件的事件绑定
 * @return 初始化是否成功
 * @note 在此函数中完成所有按钮点击事件、文本输入事件的绑定
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

	// 初始化准备状态为false
	bIsReady = false;
	if (Text_ReadyStatus) Text_ReadyStatus->SetText(FText::FromString(TEXT("准备")));

	// 【诊断】检查聊天列表控件是否绑定成功，若失败说明蓝图控件名称与 C++ 属性名不匹配
	if (!ScrollBox_ChatList)
	{
		UE_LOG(LogTemp, Error, TEXT("[RoomInsidePage] 严重错误：ScrollBox_ChatList 未绑定！请确认 WBP_RoomInsidePage 蓝图中存在同名 ScrollBox 控件（区分大小写）。"));
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, TEXT("【致命错误】：ScrollBox_ChatList 未绑定！请检查蓝图控件命名。"));
	}

	// 绑定聊天输入框的回车事件
	if (Input_Chat){Input_Chat->OnTextCommitted.AddDynamic(this, &URoomInsidePage::OnChatTextCommitted);}

	// 绑定隐藏武器选择弹窗按钮点击事件
	if (Btn_HideWeaponOverlay){Btn_HideWeaponOverlay->OnClicked.AddDynamic(this, &URoomInsidePage::OnHideWeaponOverlayClicked);}

	// 绑定确认更换武器按钮点击事件
	if (Btn_ConfirmWeaponChange){Btn_ConfirmWeaponChange->OnClicked.AddDynamic(this, &URoomInsidePage::OnConfirmWeaponChangeClicked);}

	// 绑定打开武器选择弹窗按钮点击事件
	if (Btn_ChangeWeapon){Btn_ChangeWeapon->OnClicked.AddDynamic(this, &URoomInsidePage::OnChangeWeaponClicked);}

	// 绑定背包1切换按钮点击事件
	if (Btn_Inventory1) Btn_Inventory1->OnClicked.AddDynamic(this, &URoomInsidePage::OnInventory1Clicked);
	// 绑定背包2切换按钮点击事件
	if (Btn_Inventory2) Btn_Inventory2->OnClicked.AddDynamic(this, &URoomInsidePage::OnInventory2Clicked);

	// 绑定关闭AI面板按钮点击事件
	if (Btn_HideAddAI){Btn_HideAddAI->OnClicked.AddDynamic(this, &URoomInsidePage::OnHideAddAIClicked);}
	// 绑定确认添加AI按钮点击事件
	if (Btn_ConfirmAddAI){Btn_ConfirmAddAI->OnClicked.AddDynamic(this, &URoomInsidePage::OnConfirmAddAIClicked);}

	// 绑定打开AI面板按钮点击事件
	if (Btn_OpenAIPanel) Btn_OpenAIPanel->OnClicked.AddDynamic(this, &URoomInsidePage::OnOpenAIPanelClicked);

	return true;
}

/**
 * @brief UI构建完成后的初始化函数
 * @note 在此函数中完成数据表加载、UI元素初始化、事件监听器注册等操作
 */
void URoomInsidePage::NativeConstruct()
{
	Super::NativeConstruct();

	// 启动定时器，每0.5秒检查一次玩家变化
	GetWorld()->GetTimerManager().SetTimer(PlayerCheckTimerHandle, this, &URoomInsidePage::CheckForNewPlayers, 0.5f, true);
	// 立即执行一次UI刷新
	RefreshRoomUI();

	// 判断当前玩家是否为房主
	bool bIsHost = GetOwningPlayer()->HasAuthority();

	// 检查角色数据表是否已绑定
	if (!CharacterDataTable)
	{
		UE_LOG(LogTemp, Error, TEXT("[URoomInsidePage] 严重错误：未绑定 CharacterDataTable！请检查 WBP_RoomInsidePage 的细节面板！"));

		if (GEngine) {GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, TEXT("【致命错误】：CharacterDataTable 缺失！"));}

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
			UE_LOG(LogTemp, Warning, TEXT("[URoomInsidePage] 数据表为空，无法初始化角色列表！"));
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
					// 缓存角色ID，用于后续查找
					CachedCharacterIDs.Add(RowName);
				}
			}

			// 从AccountSubsystem中读取上次选择的角色
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
			// 保存当前选择的角色ID
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

	// 默认激活背包槽位1
	ActiveBackpackSlot = 1;
	UpdateWeaponDisplayImage(ActiveBackpackSlot);

	// 更新背包高亮指示器
	UpdateInventoryHighlightUI(ActiveBackpackSlot);

	// 初始化时隐藏AI配置面板
	if (Overlay_AddAI)
	{
		Overlay_AddAI->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 填充AI配置面板的数据
	PopulateAIPanelData();

	// 只有房主才能看到打开AI面板的按钮
	if (Btn_OpenAIPanel)
	{
		Btn_OpenAIPanel->SetVisibility(bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 从会话设置中读取房间名称和游戏模式，并显示在UI上
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

/**
 * @brief UI销毁时的清理工作
 * @note 清理事件监听器和定时器，防止内存泄漏
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

/**
 * @brief 退出房间按钮点击事件
 * @note 处理玩家离开房间的请求，如果已准备则不允许退出
 */
void URoomInsidePage::OnLeaveRoomClicked()
{
	// 如果已准备，提示先取消准备
	if (bIsReady)
	{
		AddSystemMessageToChat(TEXT("取消准备才能退出房间！"));
		return;
	}

	// 调用PlayerController的离开房间函数
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		PC->LeaveRoom();
	}
}

/**
 * @brief 开始游戏按钮点击事件
 * @note 房主点击后向服务器请求开始游戏
 */
void URoomInsidePage::OnStartGameClicked()
{
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		PC->Server_RequestStartGame();
	}
}

/**
 * @brief 加入攻方按钮点击事件
 * @note 检查攻方人数是否已满，未满则请求加入攻方
 */
void URoomInsidePage::OnJoinAttackTeamClicked()
{
	// 检查攻方人数是否已达到上限（5人）
	if (Box_AttackTeam && Box_AttackTeam->GetChildrenCount() >= 5)
	{
		AddSystemMessageToChat(TEXT("系统提示：攻方人数已满，不可更换队伍！"));
		return;
	}

	// 向服务器请求加入攻方（true表示攻方）
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		PC->Server_RequestChangeTeam(true);
	}
}

/**
 * @brief 加入守方按钮点击事件
 * @note 检查守方人数是否已满，未满则请求加入守方
 */
void URoomInsidePage::OnJoinDefenseTeamClicked()
{
	// 检查守方人数是否已达到上限（5人）
	if (Box_DefenseTeam && Box_DefenseTeam->GetChildrenCount() >= 5)
	{
		AddSystemMessageToChat(TEXT("系统提示：守方人数已满，不可更换队伍！"));
		return;
	}

	// 向服务器请求加入守方（false表示守方）
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		PC->Server_RequestChangeTeam(false);
	}
}

/**
 * @brief 切换准备状态按钮点击事件
 * @note 切换玩家的准备/取消准备状态，并同步到服务器
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

/**
 * @brief 监听玩家在输入框按下回车键发送消息
 * @param Text 输入的文本内容
 * @param CommitMethod 提交方式（如按回车键）
 * @note 当玩家在聊天输入框中按下回车时触发，向服务器发送聊天消息
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
 * @brief 向聊天框添加消息
 * @param SenderName 发送者名称
 * @param bIsHost 是否为房主
 * @param Message 消息内容
 * @param bIsSystemMsg 是否为系统消息
 * @note 该函数用于在房间内显示聊天信息，区分普通玩家消息、房主消息和系统消息
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
		// 系统消息：绿色显示
		FinalString = FString::Printf(TEXT("%s系统提示：%s"), *DateStr, *Message);
		TextColor = FColor::Green;
	}
	else if (bIsHost)
	{
		// 房主消息：黄色显示，带【房主】标记
		FinalString = FString::Printf(TEXT("%s【房主】%s：%s"), *DateStr, *SenderName, *Message);
		TextColor = FColor::Yellow;
	}
	else
	{
		// 普通玩家消息：白色显示
		FinalString = FString::Printf(TEXT("%s%s：%s"), *DateStr, *SenderName, *Message);
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

/**
 * @brief 监听角色选择下拉框变化事件
 * @param SelectedItem 选中的角色名称
 * @param SelectionType 选择类型
 * @note 当玩家在下拉框中选择不同角色时触发，更新头像并同步到服务器
 */
void URoomInsidePage::OnCharacterSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan, FString::Printf(TEXT("本地玩家切换了战备角色：%s"), *SelectedItem));
	}

	// 更新角色头像显示
	UpdateCharacterDisplayImage(SelectedItem);

	// 保存选择的角色ID到AccountSubsystem
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
 * @brief 根据选中的角色名称更新显示的头像图片
 * @param SelectedCharacterName 选中的角色名称
 * @note 从角色数据表中查找对应角色的头像并显示
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

/**
 * @brief 隐藏武器选择弹窗按钮点击事件
 * @note 关闭武器选择界面
 */
void URoomInsidePage::OnHideWeaponOverlayClicked()
{
	if (Overlay_WeaponSelect)
	{
		Overlay_WeaponSelect->SetVisibility(ESlateVisibility::Collapsed);
	}
}

/**
 * @brief 确认更换武器按钮点击事件
 * @note 确认选择的武器并应用到当前背包槽位，同步到服务器
 */
void URoomInsidePage::OnConfirmWeaponChangeClicked()
{
	if (!TempSelectedWeaponRow.IsNone())
	{
		// 保存选择的武器到AccountSubsystem
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
			{
				AccountSub->SaveLastSelectedWeapon(ActiveBackpackSlot, TempSelectedWeaponRow.ToString());
			}
		}
		// 更新主界面武器显示
		UpdateWeaponDisplayImage(ActiveBackpackSlot);

		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green, FString::Printf(TEXT("背包 %d 已装备武器：%s"), ActiveBackpackSlot, *TempSelectedWeaponRow.ToString()));

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
 * @brief 打开武器选择弹窗按钮点击事件
 * @note 初始化弹窗数据并显示武器选择界面
 */
void URoomInsidePage::OnChangeWeaponClicked()
{
	// 从AccountSubsystem中读取当前背包槽位的武器
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

/**
 * @brief 背包1按钮点击回调
 * @note 切换到第一个背包槽位，更新显示和高亮指示器
 */
void URoomInsidePage::OnInventory1Clicked()
{
	ActiveBackpackSlot = 1;
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan, TEXT("当前切换至：背包 1"));
	UpdateWeaponDisplayImage(ActiveBackpackSlot);
	UpdateInventoryHighlightUI(ActiveBackpackSlot);
}

/**
 * @brief 背包2按钮点击回调
 * @note 切换到第二个背包槽位，更新显示和高亮指示器
 */
void URoomInsidePage::OnInventory2Clicked()
{
	ActiveBackpackSlot = 2;
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan, TEXT("当前切换至：背包 2"));

	UpdateWeaponDisplayImage(ActiveBackpackSlot);
	UpdateInventoryHighlightUI(ActiveBackpackSlot);
}

/**
 * @brief 根据武器数据表动态生成武器选择网格
 * @note 遍历武器数据表，为每个武器创建图标Widget并添加到网格中
 */
void URoomInsidePage::PopulateWeaponGrid()
{
	if (!WeaponDataTable || !WeaponItemClass || !Grid_WeaponItems) return;

	// 清空现有网格内容
	Grid_WeaponItems->ClearChildren();

	static const FString ContextString(TEXT("Weapon Context"));
	TArray<FName> RowNames = WeaponDataTable->GetRowNames();

	int32 MaxColumns = 4; // 每行最多4个武器
	int32 CurrentIndex = 0;

	// 遍历所有武器，创建对应的图标Widget
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
 * @brief 当玩家在武器网格中选择某个武器时调用此函数
 * @param WeaponRowName 被选中的武器行名
 * @note 更新临时选择的武器，刷新预览图和高亮状态
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

/**
 * @brief 根据指定背包槽位的武器配置更新显示的武器图标
 * @param BackpackSlot 要更新的背包槽位 (1 或 2)
 * @note 从AccountSubsystem读取武器配置并显示对应图标
 */
void URoomInsidePage::UpdateWeaponDisplayImage(int32 BackpackSlot)
{
	if (!Image_WeaponDisplay || !WeaponDataTable) return;

	FString SavedWeaponRow = TEXT("");

	// 从AccountSubsystem中读取保存的武器
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
 * @brief 更新背包选择的高亮指示器位置
 * @param BackpackSlot 要高亮的背包槽位 (1 或 2)
 * @note 根据当前选中的背包槽位显示对应的高亮框
 */
void URoomInsidePage::UpdateInventoryHighlightUI(int32 BackpackSlot)
{
	if (!Image_HighlightBP1 || !Image_HighlightBP2) return;

	// 根据背包槽位显示/隐藏高亮指示器
	Image_HighlightBP1->SetVisibility(BackpackSlot == 1 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
	Image_HighlightBP2->SetVisibility(BackpackSlot == 2 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
}

/**
 * @brief 初始化填充 AI 面板的下拉框（角色、武器、队伍）
 * @note 从数据表中读取数据并填充AI配置界面的下拉选项，恢复上次的选择
 */
void URoomInsidePage::PopulateAIPanelData()
{
	// 填充AI角色选择下拉框
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

	// 填充AI武器选择下拉框
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

	// 填充AI队伍选择下拉框
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

	// 设置默认AI数量为1
	if (Input_AICount)
	{
		Input_AICount->SetText(FText::FromString(TEXT("1")));
	}
}

/**
 * @brief 打开AI配置面板按钮点击事件
 * @note 填充AI配置数据并显示AI配置界面
 */
void URoomInsidePage::OnOpenAIPanelClicked()
{
	PopulateAIPanelData();

	// 隐藏提示信息
	if (Text_AddAIHint)
	{
		Text_AddAIHint->SetVisibility(ESlateVisibility::Hidden);
	}

	// 显示AI配置面板
	if (Overlay_AddAI) Overlay_AddAI->SetVisibility(ESlateVisibility::Visible);
}

/**
 * @brief 关闭AI配置面板按钮点击事件
 * @note 隐藏AI配置界面
 */
void URoomInsidePage::OnHideAddAIClicked()
{
	if (Overlay_AddAI)
	{
		Overlay_AddAI->SetVisibility(ESlateVisibility::Collapsed);
	}
}

/**
 * @brief 确认添加AI按钮点击事件
 * @note 根据配置向房间中添加指定数量的AI玩家，检查队伍人数限制
 */
void URoomInsidePage::OnConfirmAddAIClicked()
{
	if (!ComboBox_AICharacter || !ComboBox_AIWeapon || !ComboBox_AITeam || !Input_AICount) return;

	// 获取AI配置
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
				Text_AddAIHint->SetText(FText::FromString(TEXT("添加失败：该队伍已满员！")));
				Text_AddAIHint->SetVisibility(ESlateVisibility::Visible);
			}
			return;
		}

		// 计算实际可添加的AI数量
		int32 ActualAddCount = FMath::Min(RequestedAICount, RemainingSlots);

		// 向服务器请求添加AI
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

/**
 * @brief 向聊天框发送系统提示消息
 * @param Message 要显示的系统消息内容
 * @note 用于显示系统级别的提示信息，如操作失败、状态变更等
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

void URoomInsidePage::ActivateChatInput()
{
	// 工业级规范：安全校验，防止空指针
	if (!Input_Chat) return;

	// 激活输入框：设置可见性 + 切 GameAndUI 模式 + 锁定鼠标
	Input_Chat->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Input_Chat->SetKeyboardFocus();

	// 配置输入模式：允许同时操作游戏和 UI，并将焦点锁定到聊天输入框
	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(Input_Chat->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		PC->bShowMouseCursor = true;
	}
}

/**
 * @brief 定时器回调函数：每0.5秒检查一次房间内的玩家变化
 * @note 扫描GameState中的玩家列表，检测新加入或离开的玩家，并订阅状态变化事件
 */
void URoomInsidePage::CheckForNewPlayers()
{
	ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>();
	if (!GS) return;

	bool bNeedsRefresh = false;

	// 遍历GameState中的所有玩家状态
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

	// 获取UI中实际显示的人数
	int32 UIAttackCount = Box_AttackTeam ? Box_AttackTeam->GetChildrenCount() : 0;
	int32 UIDefenseCount = Box_DefenseTeam ? Box_DefenseTeam->GetChildrenCount() : 0;
	int32 RenderedTotalCount = UIAttackCount + UIDefenseCount;

	// 检查是否需要刷新UI（人数不匹配或队伍分布不一致）
	if (RenderedTotalCount != KnownPlayerStates.Num() ||
		ExpectedAttackCount != UIAttackCount ||
		ExpectedDefenseCount != UIDefenseCount)
	{
		bNeedsRefresh = true;
	}

	// 如果需要刷新，执行UI重绘
	if (bNeedsRefresh)
	{
		RefreshRoomUI();
	}
}

/**
 * @brief 核心刷新函数：重新绘制房间UI，更新所有玩家的显示信息
 * @note 只有当人员变动，或者某个玩家触发OnStateChanged时，才执行重绘
 */
void URoomInsidePage::RefreshRoomUI()
{
	// 清空攻守方列表
	if (Box_AttackTeam) Box_AttackTeam->ClearChildren();
	if (Box_DefenseTeam) Box_DefenseTeam->ClearChildren();

	// 检查PlayerLabelClass是否已配置
	if (!PlayerLabelClass)
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, TEXT("[严重错误] PlayerLabelClass 未配置！"));
		return;
	}

	// 获取房主名称
	FString CurrentHostName = TEXT("");
	if (ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>())
	{
		CurrentHostName = GS->HostPlayerName;
	}

	// 遍历所有已知玩家，创建对应的UI标签
	for (ARoomPlayerState* PS : KnownPlayerStates)
	{
		if (!IsValid(PS)) continue;

		// 确定目标队伍容器
		UVerticalBox* TargetBox = nullptr;
		if (PS->CurrentTeam == ERoomTeam::Attack) TargetBox = Box_AttackTeam;
		else if (PS->CurrentTeam == ERoomTeam::Defense) TargetBox = Box_DefenseTeam;

		if (TargetBox)
		{
			// 创建玩家标签Widget
			UPlayerLabelWidget* PlayerLabel = CreateWidget<UPlayerLabelWidget>(GetWorld(), PlayerLabelClass);
			if (PlayerLabel)
			{
				FString PName = PS->GetPlayerName();

				// 设置玩家名称和准备状态
				PlayerLabel->SetPlayerName(PName);
				PlayerLabel->SetReadyState(PS->bIsReady);

				bool bIsAI = PName.StartsWith(TEXT("[AI]")); // 是否为AI玩家
				bool bIsThisLabelTheHost = (PName == CurrentHostName); // 是否为房主

				if (bIsAI)
				{
					// 标记为AI玩家
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

/**
 * @brief 监听游戏流程状态变化
 * @param NewState 新的游戏流程状态
 * @note 当游戏状态变为战斗状态时，销毁大厅UI
 */
void URoomInsidePage::OnGameFlowStateChanged(EMatchState NewState)
{
	if (NewState == EMatchState::Battleing)
	{
		UE_LOG(LogTemp, Log, TEXT("[RoomInsidePage] 收到战斗开始指令，大厅UI开始自我销毁！"));

		this->RemoveFromParent();
	}
}

/**
 * @brief 同步玩家的装备配置到服务器
 * @note 将当前选择的角色和武器配置发送到服务器，确保其他玩家能看到正确的装备
 */
void URoomInsidePage::SyncLoadoutToServer()
{
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		if (UAccountSubsystem* AccountSub = GetGameInstance()->GetSubsystem<UAccountSubsystem>())
		{
			int32 SelectedIndex = ComboBox_CharacterSelect->GetSelectedIndex();

			// 获取当前选择的角色ID
			FString CurrentCharID = (SelectedIndex != INDEX_NONE) ? CachedCharacterIDs[SelectedIndex].ToString() : TEXT("Default");

			// 获取两个背包槽位的武器
			FString CurrentWeapon1 = AccountSub->GetLastSelectedWeapon(1);
			FString CurrentWeapon2 = AccountSub->GetLastSelectedWeapon(2);

			// 向服务器发送装备配置
			PC->Server_SelectLoadout(CurrentCharID, CurrentWeapon1, CurrentWeapon2);
		}
	}
}
