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
#include "Components/UniformGridSlot.h" // 棋盘格子槽位
#include "UI/Login/Pages/BattleRoom/WeaponIconWidget.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Systems/GameFlowSubsystem.h"
#include "Systems/RoomGameState.h"
#include "UI/Login/Core/RoomPlayerState.h"

bool URoomInsidePage::Initialize()
{
	if (!Super::Initialize()) return false;

	// 绑定按钮事件
	if (Btn_LeaveRoom) Btn_LeaveRoom->OnClicked.AddDynamic(this, &URoomInsidePage::OnLeaveRoomClicked);
	if (Btn_StartGame) Btn_StartGame->OnClicked.AddDynamic(this, &URoomInsidePage::OnStartGameClicked);
	if (Btn_JoinRedTeam) Btn_JoinRedTeam->OnClicked.AddDynamic(this, &URoomInsidePage::OnJoinRedTeamClicked);
	if (Btn_JoinBlueTeam) Btn_JoinBlueTeam->OnClicked.AddDynamic(this, &URoomInsidePage::OnJoinBlueTeamClicked);
	if (Btn_ToggleReady) Btn_ToggleReady->OnClicked.AddDynamic(this, &URoomInsidePage::OnToggleReadyClicked);

	bIsReady = false;
	if (Text_ReadyStatus) Text_ReadyStatus->SetText(FText::FromString(TEXT("准备")));

	// 【新增】绑定聊天输入框的回车事件
	if (Input_Chat){Input_Chat->OnTextCommitted.AddDynamic(this, &URoomInsidePage::OnChatTextCommitted);}
	
	// ==========================================
	// 绑定武器弹窗的按钮事件
	// ==========================================
	if (Btn_HideWeaponOverlay){Btn_HideWeaponOverlay->OnClicked.AddDynamic(this, &URoomInsidePage::OnHideWeaponOverlayClicked);}
	
	if (Btn_ConfirmWeaponChange){Btn_ConfirmWeaponChange->OnClicked.AddDynamic(this, &URoomInsidePage::OnConfirmWeaponChangeClicked);}
	
	// ==========================================
	// 绑定呼出武器弹窗的按钮
	// ==========================================
	if (Btn_ChangeWeapon){Btn_ChangeWeapon->OnClicked.AddDynamic(this, &URoomInsidePage::OnChangeWeaponClicked);}
	
	if (Btn_Inventory1) Btn_Inventory1->OnClicked.AddDynamic(this, &URoomInsidePage::OnInventory1Clicked);
	if (Btn_Inventory2) Btn_Inventory2->OnClicked.AddDynamic(this, &URoomInsidePage::OnInventory2Clicked);
	
	// 绑定 AI 面板的两个按钮
	if (Btn_HideAddAI){Btn_HideAddAI->OnClicked.AddDynamic(this, &URoomInsidePage::OnHideAddAIClicked);}
	if (Btn_ConfirmAddAI){Btn_ConfirmAddAI->OnClicked.AddDynamic(this, &URoomInsidePage::OnConfirmAddAIClicked);}
	
	// 打开 AI 面板的按钮
	if (Btn_OpenAIPanel) Btn_OpenAIPanel->OnClicked.AddDynamic(this, &URoomInsidePage::OnOpenAIPanelClicked);
	
	return true;
	
}

void URoomInsidePage::NativeConstruct()
{	
	Super::NativeConstruct();
	
	// 启动探头定时器
	GetWorld()->GetTimerManager().SetTimer(PlayerCheckTimerHandle, this, &URoomInsidePage::CheckForNewPlayers, 0.5f, true);
	RefreshRoomUI();
	
	bool bIsHost = GetOwningPlayer()->HasAuthority();

	// ==========================================
	// 工业级规范：数据与 UI 的强健绑定校验
	// ==========================================
	if (!CharacterDataTable)
	{
		// 规范 1：必须留下底层 Log 记录，便于自动化测试和崩溃定位
		UE_LOG(LogTemp, Error, TEXT("[URoomInsidePage] 严重错误：未绑定 CharacterDataTable！请检查 WBP_RoomInsidePage 的细节面板。"));
		
		if (GEngine) {GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, TEXT("【致命错误】：CharacterDataTable 缺失！"));}

		// 规范 2：容错处理。数据缺失时，不要让玩家点击空列表，锁死 UI 控件
		if (ComboBox_CharacterSelect)
		{
			ComboBox_CharacterSelect->ClearOptions();
			ComboBox_CharacterSelect->AddOption(TEXT("数据丢失"));
			ComboBox_CharacterSelect->SetSelectedIndex(0);
			ComboBox_CharacterSelect->SetIsEnabled(false); 
		}
		return;
	}

	// ==========================================
	// 核心修复：干净利落的单次填充与精准映射
	// ==========================================
	if (ComboBox_CharacterSelect)
	{
		ComboBox_CharacterSelect->ClearOptions();
		CachedCharacterIDs.Empty(); // 清空缓存，准备建立全新的 1:1 映射
		
		static const FString ContextString(TEXT("RoomUI_CharacterInit"));
		TArray<FName> RowNames = CharacterDataTable->GetRowNames();
		
		if (RowNames.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("[URoomInsidePage] 数据表为空，无法初始化角色列表！"));
			ComboBox_CharacterSelect->AddOption(TEXT("无可用角色"));
			ComboBox_CharacterSelect->SetIsEnabled(false);
		}
		else
		{
			// 1. 唯一遍历：同时填充 UI 显示名 和 内存 ID 缓存
			for (const FName& RowName : RowNames)
			{
				FCharacterInfo* Info = CharacterDataTable->FindRow<FCharacterInfo>(RowName, ContextString);
				// 规范：不仅检查指针，还要检查名字是否为空白
				if (Info && !Info->CharacterName.IsEmpty())
				{
					// UI 给玩家看的是：马可
					ComboBox_CharacterSelect->AddOption(Info->CharacterName.ToString());
					// 内存里记的是：Char_01
					CachedCharacterIDs.Add(RowName); 
				}
			}

			// 2. 智能选中逻辑：用 ID 找 Index
			FString SavedCharacterID = TEXT("");
			UAccountSubsystem* AccountSub = nullptr;
			if (UGameInstance* GI = GetGameInstance())
			{
				AccountSub = GI->GetSubsystem<UAccountSubsystem>();
				if (AccountSub)
				{
					// 获取存盘的 ID (如 "Char_01")
					SavedCharacterID = AccountSub->GetLastSelectedCharacter();
				}
			}

				// 【重磅修复】：不要去 ComboBox 里找文字，去 CachedCharacterIDs 里找真实的 ID 索引！
				int32 FoundIndex = CachedCharacterIDs.IndexOfByKey(FName(*SavedCharacterID));
				UE_LOG(LogTemp, Warning, TEXT("[Room] NativeConstruct: SavedCharID='%s', FoundIndex=%d, CachedCount=%d"),
					*SavedCharacterID, FoundIndex, CachedCharacterIDs.Num());

				FString CharIDToSync = TEXT("");
				if (FoundIndex != INDEX_NONE)
				{
					ComboBox_CharacterSelect->SetSelectedIndex(FoundIndex);
					// 更新图片（由于 UpdateCharacterDisplayImage 原本接受的是文字，这里从 UI 取回文字传给它）
					UpdateCharacterDisplayImage(ComboBox_CharacterSelect->GetOptionAtIndex(FoundIndex));
					CharIDToSync = CachedCharacterIDs[FoundIndex].ToString();
				}
				else
				{
					// 找不到（或是第一次进游戏），默认选第一个
					ComboBox_CharacterSelect->SetSelectedIndex(0);
					UpdateCharacterDisplayImage(ComboBox_CharacterSelect->GetOptionAtIndex(0));
					CharIDToSync = CachedCharacterIDs.Num() > 0 ? CachedCharacterIDs[0].ToString() : TEXT("");
				}

				// 【核心修复】：程序化 SetSelectedIndex 不会触发 OnSelectionChanged，必须手动同步
				UE_LOG(LogTemp, Warning, TEXT("[Room] Syncing char='%s' to server (FoundIndex=%d)"), *CharIDToSync, FoundIndex);
				if (AccountSub)
				{
					AccountSub->SaveLastSelectedCharacter(CharIDToSync);
				}

				// 【新增】：初始化武器（如果玩家从未选择过武器，默认给第一把）
				if (WeaponDataTable && AccountSub)
				{
					TArray<FName> WeaponRows = WeaponDataTable->GetRowNames();
					FString DefaultWeapon = TEXT("");
					if (WeaponRows.Num() > 0) DefaultWeapon = WeaponRows[0].ToString();

					for (int32 WSlot = 1; WSlot <= 2; WSlot++)
					{
						FString SavedWeapon = AccountSub->GetLastSelectedWeapon(WSlot);
						if (SavedWeapon.IsEmpty())
						{
							AccountSub->SaveLastSelectedWeapon(WSlot, DefaultWeapon);
							UE_LOG(LogTemp, Warning, TEXT("[Room] Init weapon slot %d -> '%s'"), WSlot, *DefaultWeapon);
						}
					}
				}

				if (ARoomPlayerController* PC2 = Cast<ARoomPlayerController>(GetOwningPlayer()))
				{
					FString W1 = AccountSub ? AccountSub->GetLastSelectedWeapon(1) : TEXT("");
					FString W2 = AccountSub ? AccountSub->GetLastSelectedWeapon(2) : TEXT("");
					PC2->Server_SelectLoadout(CharIDToSync, W1, W2);
				}
			
			// 3. 一切就绪后，再绑定委托（防止初始化期间误触发）
			ComboBox_CharacterSelect->OnSelectionChanged.AddDynamic(this, &URoomInsidePage::OnCharacterSelectionChanged);
		}
	}
	
	// 刚进入房间时，强制隐藏武器选择面板 (Collapsed 意味着隐藏且不占用布局空间)
	if (Overlay_WeaponSelect)
	{
		Overlay_WeaponSelect->SetVisibility(ESlateVisibility::Collapsed);
	}
	
	// ==========================================
	// 刚进入房间时，默认激活背包 1，并刷新大厅的武器图标！
	// ==========================================
	ActiveBackpackSlot = 1;
	UpdateWeaponDisplayImage(ActiveBackpackSlot);
	
	// ==========================================
	// 【新增】：刚进大厅时，默认高亮停留在背包 1
	// ==========================================
	UpdateInventoryHighlightUI(ActiveBackpackSlot);
	
	// 默认隐藏 AI 配置面板
	if (Overlay_AddAI)
	{
		Overlay_AddAI->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 填充 AI 下拉框的数据！
	PopulateAIPanelData();
	
	// ==========================================
	// 【新增 1】：权限控制！只有房主才能看到“添加 AI”按钮
	// ==========================================
	if (Btn_OpenAIPanel)
	{
		// 如果是房主，显示按钮；如果是普通加入的玩家，直接折叠隐藏！
		Btn_OpenAIPanel->SetVisibility(bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// ==========================================
	// 【新增 2】：从底层 Session 中挖出“房间名称”并显示
	// ==========================================
	if (Text_RoomName)
	{
		// 默认给个保底名字，万一底层没读到也不至于空着
		FString DisplayRoomName = TEXT("未命名房间");

		// 【修复】：完美拼接"房间名称-游戏模式"格式
		FString DisplayGameMode = TEXT("默认模式");

		// 呼叫在线子系统，获取当前所在的房间 (Session)
		IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
		if (OnlineSub)
		{
			IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
			if (Sessions.IsValid())
			{
				FNamedOnlineSession* Session = Sessions->GetNamedSession(NAME_GameSession);
				if (Session)
				{
					// 【核心魔法 1】：提取房间名称
					Session->SessionSettings.Get(FName("ROOM_NAME"), DisplayRoomName);
					
					// 【核心魔法 2】：提取游戏模式
					Session->SessionSettings.Get(FName("GAME_MODE"), DisplayGameMode);
				}
			}
		}
		// 【修复】：完美拼接"房间名称-游戏模式"格式
		FString FinalDisplayText = FString::Printf(TEXT("%s-%s"), *DisplayRoomName, *DisplayGameMode);
		// 把提取到的名字刷到 UI 文本上
		Text_RoomName->SetText(FText::FromString(FinalDisplayText));
	}
	
	// 【核心】：房主不需要准备按钮，直接隐藏！只有非房主才能看到。
	if (Btn_ToggleReady) Btn_ToggleReady->SetVisibility(bIsHost ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	
	// ==========================================
	// 【核心权限鉴定】：谁能看到开始游戏按钮？
	// ==========================================
	if (Btn_StartGame)
	{
		// 问当前控制这个 UI 的玩家：“你是服务器（房主）吗？”
		if (GetOwningPlayer() && GetOwningPlayer()->HasAuthority())
		{
			// 是房主：大权在握，显示按钮！
			Btn_StartGame->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			// 是普通玩家：乖乖等房主开局，把按钮彻底折叠隐藏！
			// 注意：用 Collapsed 而不是 Hidden，这样按钮连排版空间都不会占
			Btn_StartGame->SetVisibility(ESlateVisibility::Collapsed); 
		}
	}
	
	// ==========================================
	//向全局管家订阅状态改变的“报纸”
	// ==========================================
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			// 绑定委托，当状态机发生变化时，通知本 UI
			FlowSubsystem->OnStateChanged.AddDynamic(this, &URoomInsidePage::OnGameFlowStateChanged);
		}
	}
}                                                           

void URoomInsidePage::NativeDestruct()
{
	// 【内存安全】：UI 被销毁时，必须拔掉探头定时器，防止崩溃！
	GetWorld()->GetTimerManager().ClearTimer(PlayerCheckTimerHandle);

	// ==========================================
	// UI 销毁前必须解绑委托，否则会引起野指针崩溃
	// ==========================================
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
// 按钮点击响应 (目前先写个壳子，之后连上对讲机发 RPC)
// ==========================================

void URoomInsidePage::OnLeaveRoomClicked()
{
	// ==========================================
	// 【新增】：防呆设计 - 准备状态下锁死退出功能！
	// ==========================================
	if (bIsReady)
	{
		// 呼叫聊天框发出系统警告！
		AddChatMessage(TEXT(""), false, TEXT("取消准备才能退出房间。"), true);
		return; // 直接拦截，绝不执行后续的退房代码！
	}
	
	// 获取自己的对讲机，下达撤退指令！
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		PC->LeaveRoom();
	}
}

void URoomInsidePage::OnStartGameClicked()
{
	// 只有房主能点到这个按钮，所以我们直接呼叫对讲机，让服务器去执行严格的“查房”逻辑
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		PC->Server_RequestStartGame();
	}
}

void URoomInsidePage::OnJoinRedTeamClicked()
{
	
	// 【核心拦截 1】：检查红队当前人数是否已达到或超过 5 人
	if (Box_RedTeam && Box_RedTeam->GetChildrenCount() >= 5)
	{
		AddSystemMessageToChat(TEXT("系统提示：红队人数已满，不可更换队伍！"));
		return; // 拦截！不往下执行切换队伍的代码
	}
	
	// 获取自己的对讲机 (本地拥有者)
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		// 呼叫对讲机：我要去红队 (true)
		PC->Server_RequestChangeTeam(true);
	}
}

void URoomInsidePage::OnJoinBlueTeamClicked()
{
	// 【核心拦截 2】：检查蓝队当前人数是否已达到或超过 5 人
	if (Box_BlueTeam && Box_BlueTeam->GetChildrenCount() >= 5)
	{
		AddSystemMessageToChat(TEXT("系统提示：蓝队人数已满，不可更换队伍！"));
		return; // 拦截！不往下执行切换队伍的代码
	}
	
	// 获取自己的对讲机 (本地拥有者)
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		// 呼叫对讲机：我要去蓝队 (false)
		PC->Server_RequestChangeTeam(false);
	}
}

void URoomInsidePage::OnToggleReadyClicked()
{
	bIsReady = !bIsReady;
	if (Text_ReadyStatus)
	{
		Text_ReadyStatus->SetText(FText::FromString(bIsReady ? TEXT("取消准备") : TEXT("准备")));
	}
	
	// 【新增】：立刻呼叫对讲机，让服务器把我的准备状态广播给全网！
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		PC->Server_ToggleReady(bIsReady);
	}
}

void URoomInsidePage::OnChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	// 只有当玩家按下回车键 (OnEnter)，且输入框不是空的时候才发送
	if (CommitMethod == ETextCommit::OnEnter && !Text.IsEmpty())
	{
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
		{
			// 呼叫对讲机，把文字发给服务器
			PC->Server_SendChatMessage(Text.ToString());
		}
		
		// 发送完后，清空输入框，方便下次输入
		Input_Chat->SetText(FText::GetEmpty());
	}
}

void URoomInsidePage::AddChatMessage(const FString& SenderName, bool bIsHost, const FString& Message, bool bIsSystemMsg)
{
	if (!ScrollBox_ChatList) return;

	// 获取当前日期的 YYYY-MM-DD 格式
	FString DateStr = FDateTime::Now().ToString(TEXT("%H:%M:%S"));
	
	FString FinalString;
	FColor TextColor;

	// 严格按照你要求的格式进行组装！
	if (bIsSystemMsg)
	{
		FinalString = FString::Printf(TEXT("%s系统提示：%s"), *DateStr, *Message);
		TextColor = FColor::Green; // 系统提示为绿色
	}
	else if (bIsHost)
	{
		FinalString = FString::Printf(TEXT("%s（%s）-房主：%s"), *DateStr, *SenderName, *Message);
		TextColor = FColor::Yellow; // 房主发言给个醒目的黄色
	}
	else
	{
		FinalString = FString::Printf(TEXT("%s（%s）：%s"), *DateStr, *SenderName, *Message);
		TextColor = FColor::White; // 普通玩家为白色
	}

	// 在内存中动态生成一个普通的 TextBlock 文字控件
	UTextBlock* NewMsgBlock = NewObject<UTextBlock>(this);
	NewMsgBlock->SetText(FText::FromString(FinalString));
	NewMsgBlock->SetColorAndOpacity(FSlateColor(TextColor));

	// 把文字塞进滚动框里
	ScrollBox_ChatList->AddChild(NewMsgBlock);
	
	// 每次来新消息，自动滚动到最底部！
	ScrollBox_ChatList->ScrollToEnd();
}

void URoomInsidePage::OnCharacterSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// 当玩家切换角色时触发
	if (GEngine) 
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan, FString::Printf(TEXT("本地玩家切换了战备角色：%s"), *SelectedItem));
	}
	
	// ==========================================
	// 玩家每次点击下拉菜单，立刻更新头像！
	// ==========================================
	UpdateCharacterDisplayImage(SelectedItem);
	
	// 从 AccountSubsystem 中读取并保存的是 RowName ID（如 "Char_01"），不是显示名！
	// 【修复】：必须用 CachedCharacterIDs 数组中对应的 RowName，而不是 SelectedItem（显示名）
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			int32 SelectedIdx = ComboBox_CharacterSelect->GetSelectedIndex();
			FString CurrentCharID = (SelectedIdx != INDEX_NONE) ? CachedCharacterIDs[SelectedIdx].ToString() : TEXT("Default");
			AccSub->SaveLastSelectedCharacter(CurrentCharID);
		}
	}

	//立刻通知服务器！
	SyncLoadoutToServer();
	
}

// ==========================================
// 根据名字查表，并更换 Image 贴图
// ==========================================
void URoomInsidePage::UpdateCharacterDisplayImage(const FString& SelectedCharacterName)
{
	// 安全校验：表和图片控件必须都在
	if (!CharacterDataTable || !Image_CharacterDisplay) return;

	static const FString ContextString(TEXT("Character Context"));
	TArray<FCharacterInfo*> AllCharacters;
	CharacterDataTable->GetAllRows<FCharacterInfo>(ContextString, AllCharacters);

	// 遍历数据表，寻找名字匹配的那一行
	for (FCharacterInfo* CharInfo : AllCharacters)
	{
		if (CharInfo && CharInfo->CharacterName.ToString() == SelectedCharacterName)
		{
			// 找到了！看看美术有没有给它配头像
			if (CharInfo->AvatarIcon)
			{
				// 神奇的 API：直接把 2D 贴图刷到 UI 图片上！
				Image_CharacterDisplay->SetBrushFromTexture(CharInfo->AvatarIcon);
			}
			break; // 找到了就不用再往下找了，直接跳出循环
		}
	}
}

void URoomInsidePage::OnHideWeaponOverlayClicked()
{
	// 玩家点了“取消”或“关闭”，把整个覆盖面板隐藏掉
	if (Overlay_WeaponSelect)
	{
		Overlay_WeaponSelect->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void URoomInsidePage::OnConfirmWeaponChangeClicked()
{
	if (!TempSelectedWeaponRow.IsNone())
	{
		// 向管家存盘！
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
			{
				AccountSub->SaveLastSelectedWeapon(ActiveBackpackSlot, TempSelectedWeaponRow.ToString());
			}
		}
		// 2. 【新增】：武器换成功了，立刻让大厅常驻的那个图片跟着变！
		UpdateWeaponDisplayImage(ActiveBackpackSlot);

		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green, FString::Printf(TEXT("背包 %d 已装备武器：%s"), ActiveBackpackSlot, *TempSelectedWeaponRow.ToString()));
	
		//立刻通知服务器！
		SyncLoadoutToServer();
	}

	// 确认完毕后，同样把面板关掉
	if (Overlay_WeaponSelect)
	{
		Overlay_WeaponSelect->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void URoomInsidePage::OnChangeWeaponClicked()
{
	// 1. 在打开弹窗前，先向管家打听当前背包装备的是哪把武器
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			FString SavedWeapon = AccountSub->GetLastSelectedWeapon(ActiveBackpackSlot);
			TempSelectedWeaponRow = FName(*SavedWeapon);
		}
	}

	// 2. 兜底逻辑：如果没装备过武器，默认选中数据表里的第一个
	if (TempSelectedWeaponRow.IsNone() && WeaponDataTable)
	{
		TArray<FName> RowNames = WeaponDataTable->GetRowNames();
		if (RowNames.Num() > 0) TempSelectedWeaponRow = RowNames[0];
	}
	
	// ==========================================
	// 【新增】：3. 查表！让右侧的大预览图立刻显示这把武器！
	// ==========================================
	if (!TempSelectedWeaponRow.IsNone() && WeaponDataTable && Image_WeaponPreview)
	{
		FWeaponInfo* WeaponData = WeaponDataTable->FindRow<FWeaponInfo>(TempSelectedWeaponRow, TEXT("InitPreview"));
		if (WeaponData && WeaponData->WeaponIcon)
		{
			Image_WeaponPreview->SetBrushFromTexture(WeaponData->WeaponIcon);
		}
	}

	// 3. 呼出弹窗并生成棋盘格！
	if (Overlay_WeaponSelect) Overlay_WeaponSelect->SetVisibility(ESlateVisibility::Visible);
	PopulateWeaponGrid();
	
}

void URoomInsidePage::OnInventory1Clicked()
{
	ActiveBackpackSlot = 1;
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan, TEXT("当前切换至：背包 1"));
	// 【选做】你可以让背包1的按钮变亮，背包2变暗，给玩家视觉反馈
	// 切换背包后，立刻刷新主页面的武器图标！
	UpdateWeaponDisplayImage(ActiveBackpackSlot);
	
	// 【新增】：高亮框移回背包 1
	UpdateInventoryHighlightUI(ActiveBackpackSlot);
}

void URoomInsidePage::OnInventory2Clicked()
{
	ActiveBackpackSlot = 2;
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Cyan, TEXT("当前切换至：背包 2"));

	// 切换背包后，立刻刷新主页面的武器图标！
    	UpdateWeaponDisplayImage(ActiveBackpackSlot);
	
	// 【新增】：高亮框移到背包 2
	UpdateInventoryHighlightUI(ActiveBackpackSlot);
    	
}

void URoomInsidePage::PopulateWeaponGrid()
{
	if (!WeaponDataTable || !WeaponItemClass || !Grid_WeaponItems) return;

	Grid_WeaponItems->ClearChildren(); // 清空旧格子

	static const FString ContextString(TEXT("Weapon Context"));
	TArray<FName> RowNames = WeaponDataTable->GetRowNames(); // 获取所有武器 ID

	int32 MaxColumns = 4; // 棋盘格每行放 4 个武器
	int32 CurrentIndex = 0;

	for (const FName& RowName : RowNames)
	{
		FWeaponInfo* WeaponData = WeaponDataTable->FindRow<FWeaponInfo>(RowName, ContextString);
		if (WeaponData)
		{
			// 动态生成细胞控件
			UWeaponIconWidget* NewItem = CreateWidget<UWeaponIconWidget>(this, WeaponItemClass);
			if (NewItem)
			{
				// 把数据和大厅指针塞给它
				NewItem->SetupWeaponItem(RowName, *WeaponData, this);

				// ==========================================
				// 【新增】：如果这个格子正好是当前背包装备的武器，默认高亮！
				// ==========================================
				bool bIsEquippedWeapon = (RowName == TempSelectedWeaponRow);
				NewItem->SetHighlightFrameVisibility(bIsEquippedWeapon);
				
				// 算出它在棋盘格里的行和列
				int32 Row = CurrentIndex / MaxColumns;
				int32 Col = CurrentIndex % MaxColumns;

				// 添加到棋盘格
				UUniformGridSlot* GridSlot = Grid_WeaponItems->AddChildToUniformGrid(NewItem, Row, Col);
				
				CurrentIndex++;
			}
		}
	}
}

// 接收来自小格子的报告
void URoomInsidePage::OnWeaponItemSelectedInGrid(FName WeaponRowName)
{
	TempSelectedWeaponRow = WeaponRowName;

	// 查表换大预览图
	if (WeaponDataTable)
	{
		FWeaponInfo* WeaponData = WeaponDataTable->FindRow<FWeaponInfo>(WeaponRowName, TEXT(""));
		if (WeaponData && WeaponData->WeaponIcon && Image_WeaponPreview)
		{
			Image_WeaponPreview->SetBrushFromTexture(WeaponData->WeaponIcon);
		}
	}

	// ==========================================
	// 【新增核心魔法】：遍历棋盘格，刷新所有格子的高亮状态！
	// ==========================================
	if (Grid_WeaponItems)
	{
		// 拿到棋盘格里所有的子控件
		TArray<UWidget*> AllGridItems = Grid_WeaponItems->GetAllChildren();
		
		for (UWidget* ChildWidget : AllGridItems)
		{
			// 将通用控件强制转换为我们的细胞控件 (WeaponIconWidget)
			if (UWeaponIconWidget* IconWidget = Cast<UWeaponIconWidget>(ChildWidget))
			{
				// 如果这个格子的 ID 等于玩家刚才点击的 ID，就亮起，否则熄灭
				bool bShouldHighlight = (IconWidget->GetWeaponRowName() == TempSelectedWeaponRow);
				IconWidget->SetHighlightFrameVisibility(bShouldHighlight);
			}
		}
	}
}

void URoomInsidePage::UpdateWeaponDisplayImage(int32 BackpackSlot)
{
	if (!Image_WeaponDisplay || !WeaponDataTable) return;

	FString SavedWeaponRow = TEXT("");
	
	// 1. 找管家要这个背包的存档数据
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			SavedWeaponRow = AccountSub->GetLastSelectedWeapon(BackpackSlot);
		}
	}

	// 2. 智能兜底：如果他从来没选过武器（返回的是空字符串），默认拿表里的第 0 把武器！
	if (SavedWeaponRow.IsEmpty())
	{
		TArray<FName> RowNames = WeaponDataTable->GetRowNames();
		if (RowNames.Num() > 0)
		{
			SavedWeaponRow = RowNames[0].ToString();
		}
	}

	// 3. 去武器表里查这把枪的资料
	if (!SavedWeaponRow.IsEmpty())
	{
		FWeaponInfo* WeaponData = WeaponDataTable->FindRow<FWeaponInfo>(FName(*SavedWeaponRow), TEXT("UpdateWeaponDisplay"));
		if (WeaponData && WeaponData->WeaponIcon)
		{
			// 查到了！直接把图片贴到 UI 上
			Image_WeaponDisplay->SetBrushFromTexture(WeaponData->WeaponIcon);
		}
	}
}

void URoomInsidePage::UpdateInventoryHighlightUI(int32 BackpackSlot)
{
	if (!Image_HighlightBP1 || !Image_HighlightBP2) return;

	// 根据选中的背包，控制可见性
	Image_HighlightBP1->SetVisibility(BackpackSlot == 1 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
	Image_HighlightBP2->SetVisibility(BackpackSlot == 2 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
}

// ==========================================
// 填充 AI 面板的基础数据
// ==========================================
void URoomInsidePage::PopulateAIPanelData()
{
	// ==========================================
	// 1. 填充 AI 角色 (读取 CharacterName 字段)
	// ==========================================
	if (ComboBox_AICharacter && CharacterDataTable)
	{
		ComboBox_AICharacter->ClearOptions();
		
		// 获取表里所有的结构体数据
		TArray<FCharacterInfo*> AllCharacters;
		CharacterDataTable->GetAllRows<FCharacterInfo>(TEXT("AIPanelContext"), AllCharacters);
		
		for (FCharacterInfo* CharInfo : AllCharacters)
		{
			if (CharInfo)
			{
				// 提取真正的展示名字！(假设你的 CharacterName 是 FName 或 FText，这里转成 String)
				ComboBox_AICharacter->AddOption(CharInfo->CharacterName.ToString());
			}
		}

		// 智能选中逻辑：如果以前点过确认，就尝试还原；否则默认选第 0 个
		if (!LastConfirmedAICharacter.IsEmpty() && ComboBox_AICharacter->FindOptionIndex(LastConfirmedAICharacter) != -1)
		{
			ComboBox_AICharacter->SetSelectedOption(LastConfirmedAICharacter);
		}
		else if (ComboBox_AICharacter->GetOptionCount() > 0)
		{
			ComboBox_AICharacter->SetSelectedIndex(0);
		}
	}

	// ==========================================
	// 2. 填充 AI 武器 (读取 WeaponName 字段)
	// ==========================================
	if (ComboBox_AIWeapon && WeaponDataTable)
	{
		ComboBox_AIWeapon->ClearOptions();
		
		// 获取表里所有的武器结构体数据
		TArray<FWeaponInfo*> AllWeapons;
		WeaponDataTable->GetAllRows<FWeaponInfo>(TEXT("AIPanelContext"), AllWeapons);

		for (FWeaponInfo* WeaponData : AllWeapons)
		{
			if (WeaponData)
			{
				// 提取真正的武器展示名字！(因为你的 WeaponName 是 FText，需要 ToString)
				ComboBox_AIWeapon->AddOption(WeaponData->WeaponName.ToString());
			}
		}

		// 智能选中逻辑：尝试还原记忆
		if (!LastConfirmedAIWeapon.IsEmpty() && ComboBox_AIWeapon->FindOptionIndex(LastConfirmedAIWeapon) != -1)
		{
			ComboBox_AIWeapon->SetSelectedOption(LastConfirmedAIWeapon);
		}
		else if (ComboBox_AIWeapon->GetOptionCount() > 0)
		{
			ComboBox_AIWeapon->SetSelectedIndex(0);
		}
	}

	// ==========================================
	// 3. 填充队伍选择 (保持不变)
	// ==========================================
	if (ComboBox_AITeam)
	{
		// ... 这里保留你之前的红蓝队添加代码
		ComboBox_AITeam->ClearOptions();
		ComboBox_AITeam->AddOption(TEXT("红队"));
		ComboBox_AITeam->AddOption(TEXT("蓝队"));
		// 智能选中逻辑：尝试还原上次的记忆
		if (!LastConfirmedAITeam.IsEmpty() && ComboBox_AITeam->FindOptionIndex(LastConfirmedAITeam) != -1)
		{
			ComboBox_AITeam->SetSelectedOption(LastConfirmedAITeam);
		}
		else
		{
			// 如果没记忆（第一次打开），默认选第 0 个（红队）
			ComboBox_AITeam->SetSelectedIndex(0);
		}
	}

	// 4. 设置默认添加人数为 1
	if (Input_AICount)
	{
		Input_AICount->SetText(FText::FromString(TEXT("1")));
	}
}

// ==========================================
// 呼出 AI 配置面板
// ==========================================
void URoomInsidePage::OnOpenAIPanelClicked()
{
	// 每次打开面板前，重新运行一次填充函数，这样它就会自动去读 LastConfirmed 变量并选中！
	PopulateAIPanelData();
	
	// 每次打开面板，把上一次的报错提示隐藏掉
	if (Text_AddAIHint)
	{
		Text_AddAIHint->SetVisibility(ESlateVisibility::Hidden);
	}

	if (Overlay_AddAI) Overlay_AddAI->SetVisibility(ESlateVisibility::Visible);
}

// ==========================================
// 隐藏 AI 配置面板
// ==========================================
void URoomInsidePage::OnHideAddAIClicked()
{
	if (Overlay_AddAI)
	{
		Overlay_AddAI->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// ==========================================
// 提取数据：确认添加 AI
// ==========================================
void URoomInsidePage::OnConfirmAddAIClicked()
{
	// 1. 安全校验所有控件都在
	if (!ComboBox_AICharacter || !ComboBox_AIWeapon || !ComboBox_AITeam || !Input_AICount) return;

	// 2. 提取下拉框里选中的字符串
	FString SelectedChar = ComboBox_AICharacter->GetSelectedOption();
	FString SelectedWeapon = ComboBox_AIWeapon->GetSelectedOption();
	FString SelectedTeam = ComboBox_AITeam->GetSelectedOption();
	
	// 3. 提取输入框的人数，并把字符串转成整数 (FCString::Atoi)
	FString CountStr = Input_AICount->GetText().ToString();
	int32 RequestedAICount = FCString::Atoi(*CountStr);

	// 防呆设计：如果玩家乱填字母或者填了负数，强制纠正为 1
	if (RequestedAICount <= 0) RequestedAICount = 1; 

	// ==========================================
	// 【记忆功能】：把这次的选择死死记在脑子里！
	// ==========================================
	LastConfirmedAICharacter = SelectedChar;
	LastConfirmedAIWeapon = SelectedWeapon;
	LastConfirmedAITeam = SelectedTeam; 

	// ==========================================
	// 【核心算法】：容量校验与超额钳制 (Clamp)
	// ==========================================
	
	// 确定目标队伍的 UI 容器
	UVerticalBox* TargetTeamBox = nullptr;
	if (SelectedTeam.Contains(TEXT("红队"))) TargetTeamBox = Box_RedTeam;
	else TargetTeamBox = Box_BlueTeam;

	if (TargetTeamBox && PlayerLabelClass)
	{
		// 计算单队最大容纳人数 (假设红蓝队平分总人数，10 / 2 = 5人)
		int32 MaxPlayersPerTeam = MaxNumPublicConnections / 2;

		// 统计该队目前已有多少条目 (真人标签 + 之前加的 AI 标签)
		int32 CurrentTeamMembers = TargetTeamBox->GetChildrenCount();

		// 计算还剩几个空位
		int32 RemainingSlots = MaxPlayersPerTeam - CurrentTeamMembers;

		// 如果连 1 个空位都没了，直接弹警告并拦截！
		if (RemainingSlots <= 0)
		{
			if (Text_AddAIHint)
			{
				Text_AddAIHint->SetText(FText::FromString(TEXT("添加失败：该队伍已满员！")));
				Text_AddAIHint->SetVisibility(ESlateVisibility::Visible);
			}
			return; // 拦截！不往下执行
		}

		// 智能钳制：玩家想加的数量 和 剩余空位，取最小值！
		// (比如剩 2 个空位，玩家填了 99，那就只给他加 2 个)
		int32 ActualAddCount = FMath::Min(RequestedAICount, RemainingSlots);

		// ==========================================
		// 【终极联机解法】：不要在这里自己偷偷生成 UI 了！
		// 呼叫你的对讲机，让服务器大脑把 AI 加进真实名单里！
		// ==========================================
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
		{
			// 假设红队是 true，蓝队是 false
			bool bIsRedTeam = SelectedTeam.Contains(TEXT("红队"));
			
			// 把你要加的 AI 角色名字和数量发给服务器
			// (你需要去 RoomPlayerController 里加上这个 Server_AddAI 函数！)
			PC->Server_AddAI(bIsRedTeam, SelectedChar, ActualAddCount);
		}

		// 打印结果反馈
		if (GEngine)
		{
			FString DebugMsg = FString::Printf(TEXT("已向服务器请求往%s添加 %d 个 AI！"), *SelectedTeam, ActualAddCount);
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, DebugMsg);
		}

		// 【修改】：如果生成数量比玩家填的少（被钳制了），给出提示！
		if (ActualAddCount < RequestedAICount)
		{
			if (Text_AddAIHint)
			{
				FString HintMsg = FString::Printf(TEXT("队伍空间不足，仅成功添加了 %d 个 AI！"), ActualAddCount);
				Text_AddAIHint->SetText(FText::FromString(HintMsg));
				Text_AddAIHint->SetVisibility(ESlateVisibility::Visible);
			}
			// 注意：这里不要关闭面板，让玩家看一眼提示
		}
		else
		{
			// 如果完全成功，直接关闭面板
			OnHideAddAIClicked();
		}
	}
}

void URoomInsidePage::AddSystemMessageToChat(const FString& Message)
{
	if (!ScrollBox_ChatList) return;

	// 动态创建一个文本块控件
	UTextBlock* SystemMsgText = NewObject<UTextBlock>(this);
	if (SystemMsgText)
	{
		SystemMsgText->SetText(FText::FromString(Message));
		// 系统提示设为醒目的黄色
		SystemMsgText->SetColorAndOpacity(FSlateColor(FLinearColor::Yellow)); 
		
		// 稍微调整一下字体大小（可选）
		FSlateFontInfo FontInfo = SystemMsgText->GetFont();
		FontInfo.Size = 14;
		SystemMsgText->SetFont(FontInfo);

		// 塞进聊天列表，并让滚动条自动滚到最底部
		ScrollBox_ChatList->AddChild(SystemMsgText);
		ScrollBox_ChatList->ScrollToEnd();
	}
}

// ==========================================
// 监控探头：自动发现人员进出，并挂载监听器
// ==========================================
void URoomInsidePage::CheckForNewPlayers()
{
	ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>();
	if (!GS) return;

	bool bNeedsRefresh = false;

	// 1. 扫描：探测是否有新玩家进入房间
	for (APlayerState* GenericPS : GS->PlayerArray)
	{
		if (ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(GenericPS))
		{
			if (!KnownPlayerStates.Contains(RoomPS))
			{
				KnownPlayerStates.Add(RoomPS);
				// 监听玩家状态变化（注意：此机制对服务器房主无效，仅对客户端生效）
				RoomPS->OnStateChanged.AddDynamic(this, &URoomInsidePage::RefreshRoomUI);
				bNeedsRefresh = true; 
			}
		}
	}

	// 2. 清理：探测是否有人悄悄退房
	for (int32 i = KnownPlayerStates.Num() - 1; i >= 0; --i)
	{
		ARoomPlayerState* TrackedPS = KnownPlayerStates[i];
		if (!IsValid(TrackedPS) || !GS->PlayerArray.Contains(TrackedPS))
		{
			KnownPlayerStates.RemoveAt(i);
			bNeedsRefresh = true; 
		}
	}

	// ==========================================
	// 3. 【核心架构修复：跨越 OnRep 缺陷的深度脏校验】
	// 专门解决房主（服务器）修改队伍属性时不会触发 UI 更新的问题。
	// 我们不再只查“总人数”，而是精准核对“红蓝队兵力分布”。
	// ==========================================
	
	int32 ExpectedRedCount = 0;
	int32 ExpectedBlueCount = 0;

	// A. 统计底层 PlayerState 数据中，真正属于红蓝队的人数
	for (ARoomPlayerState* PS : KnownPlayerStates)
	{
		if (IsValid(PS))
		{
			if (PS->CurrentTeam == ERoomTeam::Red) ExpectedRedCount++;
			else if (PS->CurrentTeam == ERoomTeam::Blue) ExpectedBlueCount++;
		}
	}

	// B. 统计当前 UI 界面上，各个框里实际塞了多少个标签
	int32 UIRedCount = Box_RedTeam ? Box_RedTeam->GetChildrenCount() : 0;
	int32 UIBlueCount = Box_BlueTeam ? Box_BlueTeam->GetChildrenCount() : 0;
	int32 RenderedTotalCount = UIRedCount + UIBlueCount;

	// C. 触发重绘的三大绝对条件（短路运算，性能极高）：
	// 条件 1: UI 渲染总数 和 名册总数 不等（解决刚进房 2 秒内的渲染缺失）
	if (RenderedTotalCount != KnownPlayerStates.Num() || 
	// 条件 2: 理论红队人数 和 UI 红队人数 不等（解决某人切到了红队，或者离开了红队）
		ExpectedRedCount != UIRedCount || 
	// 条件 3: 理论蓝队人数 和 UI 蓝队人数 不等
		ExpectedBlueCount != UIBlueCount)
	{
		bNeedsRefresh = true;
	}

	// 4. 只有确信数据不一致时，才执行耗时的重绘操作
	if (bNeedsRefresh)
	{
		RefreshRoomUI();
	}
}

// ==========================================
// 核心重绘逻辑（仅在人员变动或状态变动时触发）
// ==========================================
void URoomInsidePage::RefreshRoomUI()
{
	// 1. 清理现有队伍列表中的旧UI，准备重新生成
	if (Box_RedTeam) Box_RedTeam->ClearChildren();
	if (Box_BlueTeam) Box_BlueTeam->ClearChildren();
	
	// 【防御性编程】：蓝图类判空
	if (!PlayerLabelClass)
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, TEXT("[严重错误] PlayerLabelClass 未配置！"));
		return;
	}

	// 2. 获取当前的全局房主名字
	FString CurrentHostName = TEXT("");
	if (ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>())
	{
		CurrentHostName = GS->HostPlayerName;
	}

	// 3. 遍历当前已知的（UI已主动订阅的）所有玩家状态
	for (ARoomPlayerState* PS : KnownPlayerStates)
	{
		if (!IsValid(PS)) continue;

		// 4. 判断该玩家属于哪个队伍
		UVerticalBox* TargetBox = nullptr;
		if (PS->CurrentTeam == ERoomTeam::Red) TargetBox = Box_RedTeam;
		else if (PS->CurrentTeam == ERoomTeam::Blue) TargetBox = Box_BlueTeam;

		// 如果分配了队伍，开始生成 UI
		if (TargetBox)
		{
			UPlayerLabelWidget* PlayerLabel = CreateWidget<UPlayerLabelWidget>(GetWorld(), PlayerLabelClass);
			if (PlayerLabel)
			{
				// 获取我们在 Controller 中刚修复好的真实玩家名
				FString PName = PS->GetPlayerName();
				
				// 赋予基础属性
				PlayerLabel->SetPlayerName(PName);
				PlayerLabel->SetReadyState(PS->bIsReady);

				// ==========================================
				// 【核心重构：身份判定三板斧】
				// ==========================================
				bool bIsAI = PName.StartsWith(TEXT("[AI]"));
				
				// 严谨比对：这个正在被渲染的标签，是不是房主本人？
				bool bIsThisLabelTheHost = (PName == CurrentHostName);

				// 身份 A：AI 玩家
				if (bIsAI)
				{
					PlayerLabel->SetAsAI();
				}
				// 身份 B：房主本人
				else if (bIsThisLabelTheHost)
				{
					// 执行房主专属的 UI 清理逻辑
					PlayerLabel->SetAsHost(true);
				}

				// ==========================================
				// 逻辑闭环：谁能看到“移除玩家（踢人）”按钮？
				// 条件 1: 我(当前看屏幕的玩家)必须是这局游戏的最高权限者 (HasAuthority)。
				// 条件 2: 正在被渲染的这个标签，不能是我自己 ( !bIsThisLabelTheHost )。
				// ==========================================
				bool bAmIHost = GetOwningPlayer()->HasAuthority();
				PlayerLabel->SetRemoveButtonVisibility(bAmIHost && !bIsThisLabelTheHost);

				// 挂载到对应的队伍 UI 容器下
				TargetBox->AddChild(PlayerLabel);
			}
		}
	}
}

// ==========================================
// 状态机响应：UI 的自我救赎
// ==========================================
void URoomInsidePage::OnGameFlowStateChanged(EMatchState NewState)
{
	// 一旦发现服务器下令进入了战斗状态 (Battleing)
	if (NewState == EMatchState::Battleing)
	{
		UE_LOG(LogTemp, Log, TEXT("[RoomInsidePage] 收到战斗开始指令，大厅UI开始自我销毁！"));
		
		// 核心魔法：将自己从屏幕上彻底移除！
		this->RemoveFromParent();
	}
}

// 1. 实现助手函数
void URoomInsidePage::SyncLoadoutToServer()
{
	// 获取玩家拥有的对讲机 (Controller)
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetOwningPlayer()))
	{
		// 从本地大管家那里获取目前最新的完整装配信息
		if (UAccountSubsystem* AccountSub = GetGameInstance()->GetSubsystem<UAccountSubsystem>())
		{
			// 获取当前选中的索引
			int32 SelectedIndex = ComboBox_CharacterSelect->GetSelectedIndex();
			
			// 从缓存数组中取出真实的 RowName ID
			FString CurrentCharID = (SelectedIndex != INDEX_NONE) ? CachedCharacterIDs[SelectedIndex].ToString() : TEXT("Default");
            
			FString CurrentWeapon1 = AccountSub->GetLastSelectedWeapon(1);
			FString CurrentWeapon2 = AccountSub->GetLastSelectedWeapon(2);
			
			// 发送给服务器的是 RowName ID 了！
			PC->Server_SelectLoadout(CurrentCharID, CurrentWeapon1, CurrentWeapon2);
		}
	}
}