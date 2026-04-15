#include "UI/Login/Pages/MultiplayerMode/LANRoomPage.h"
#include "UI/Login/Pages/MultiplayerMode/RoomLabelWidget.h"
// 【新增】必须包含的在线子系统核心头文件
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Components/ScrollBox.h"
#include "Components/ListView.h"
#include "Components/EditableTextBox.h"
#include "Components/VerticalBox.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Login/Core/AccountSubsystem.h"
#include "UI/Login/Pages/BattleRoom/PlayerLabelWidget.h"
#include "UI/Login/Data/StaticTable.h"

bool ULANRoomPage::Initialize()
{
	if (!Super::Initialize()) return false;
	
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub)
	{
		IOnlineSessionPtr SessionInterface = OnlineSub->GetSessionInterface();
		if (SessionInterface.IsValid())
		{
			
			// ==========================================
			// 【修复 1】：删掉旧的两行绑定代码，换成这极简的一行！
			// 直接用 CreateUObject 绑定，防止委托变量被意外回收
			// ==========================================
			FindSessionsCompleteDelegateHandle = SessionInterface->AddOnFindSessionsCompleteDelegate_Handle(
				FOnFindSessionsCompleteDelegate::CreateUObject(this, &ULANRoomPage::OnFindSessionsComplete)
			);
		}
	}

	// ==========================================
	// 1. 绑定大厅层按钮
	// ==========================================
	if (Btn_ShowCreateRoom) Btn_ShowCreateRoom->OnClicked.AddDynamic(this, &ULANRoomPage::OnShowCreateRoomClicked);
	if (Btn_EnterRoom) Btn_EnterRoom->OnClicked.AddDynamic(this, &ULANRoomPage::OnEnterRoomClicked);
	if (Btn_BackToMenu) Btn_BackToMenu->OnClicked.AddDynamic(this, &ULANRoomPage::OnBackToMenuClicked);

	// ==========================================
	// 2. 绑定创房层按钮
	// ==========================================
	if (Btn_ConfirmCreateRoom) Btn_ConfirmCreateRoom->OnClicked.AddDynamic(this, &ULANRoomPage::OnConfirmCreateRoomClicked);
	if (Btn_HideCreateRoom) Btn_HideCreateRoom->OnClicked.AddDynamic(this, &ULANRoomPage::OnHideCreateRoomClicked);
	
	
	
	// ==========================================
	// 初始化 UI 面板状态
	// ==========================================
	// 刚进大厅时，隐藏创房面板和房间内面板
	if (Overlay_CreateRoom) Overlay_CreateRoom->SetVisibility(ESlateVisibility::Hidden);
	// 【新增】一开始隐藏创房提示框
	if (Text_CreateRoomHint) Text_CreateRoomHint->SetVisibility(ESlateVisibility::Hidden);
	
	// 初始为未准备状态
	bIsReady = false;

	// 【核心修复1】：在开启定时器前，先强杀可能残留的幽灵定时器！
	GetWorld()->GetTimerManager().ClearTimer(SearchTimerHandle);
	
	// 【修改】开启定时器，每 3 秒自动执行一次 FindLANRooms，并且一进来就立刻执行一次
	GetWorld()->GetTimerManager().SetTimer(SearchTimerHandle, this, &ULANRoomPage::FindLANRooms, 3.0f, true, 0.0f);
	
	// ==========================================
	// 3. 初始化游戏模式与地图选择下拉框
	// ==========================================
	if (ComboBox_GameMode)
	{
		// 添加游戏模式选项
		ComboBox_GameMode->AddOption(TEXT("刀战模式"));
		ComboBox_GameMode->AddOption(TEXT("生化模式"));
		// 默认选中第一个
		ComboBox_GameMode->SetSelectedIndex(0);
	}
	
	if (ComboBox_MapSelect && MapInfoDataTable)
	{
		// 清空旧选项
		ComboBox_MapSelect->ClearOptions();
		
		static const FString ContextString(TEXT("MapInfo Context"));
		TArray<FMapInfoRow*> AllMaps;
		MapInfoDataTable->GetAllRows<FMapInfoRow>(ContextString, AllMaps);
		
		// 遍历地图表，提取 DisplayName 填充下拉框
		for (FMapInfoRow* MapInfo : AllMaps)
		{
			if (MapInfo)
			{
				ComboBox_MapSelect->AddOption(MapInfo->DisplayName.ToString());
			}
		}
		
		// 如果表里有数据，默认选中第一个
		if (ComboBox_MapSelect->GetOptionCount() > 0)
		{
			ComboBox_MapSelect->SetSelectedIndex(0);
		}
	}
	
	return true;
}

// 2. 界面销毁时，必须关掉定时器！
void ULANRoomPage::NativeDestruct()
{
	// 关闭自动刷新定时器
	GetWorld()->GetTimerManager().ClearTimer(SearchTimerHandle);
	
	// // 2. 【核心修复】：拿着凭证去退订！做到人走茶凉！
	 IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	
	// ==========================================
	// 【核心修复 2】：只有当你既是房主，【并且】又不是因为传送而销毁 UI 时，才炸毁房间！
	// ==========================================
	if (bIsHost && !bIsTraveling)
	{
		
		if (OnlineSub && OnlineSub->GetSessionInterface().IsValid())
		{
			OnlineSub->GetSessionInterface()->DestroySession(NAME_GameSession);
			bIsHost = false; 
			
			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow, TEXT("检测到房主离开，后台房间已强制解散！"));
		}
	}
	
	Super::NativeDestruct();
}

// ==========================================
// 大厅层逻辑
// ==========================================

void ULANRoomPage::OnShowCreateRoomClicked()
{
	// 弹出创房面板
	if (Overlay_CreateRoom) Overlay_CreateRoom->SetVisibility(ESlateVisibility::Visible);
	
	// 【体验优化】：每次打开创房面板时，把上一次的错误提示隐藏，并清空输入框
	if (Text_CreateRoomHint) Text_CreateRoomHint->SetVisibility(ESlateVisibility::Hidden);
	if (Input_RoomName) Input_RoomName->SetText(FText::GetEmpty());
	if (Input_RoomPassword) Input_RoomPassword->SetText(FText::GetEmpty());
}

// ==========================================
// 处理房间选中与加入逻辑
// ==========================================

void ULANRoomPage::HandleRoomSelected(FString RoomName)
{
	// 记录当前选中的房间名
	CurrentSelectedRoomName = RoomName;
	
	// ==========================================
	// 【新增核心魔法】：遍历列表，刷新所有条目的高亮状态！
	// ==========================================
	if (List_Rooms)
	{
		TArray<UWidget*> AllRoomLabels = List_Rooms->GetAllChildren();
		for (UWidget* ChildWidget : AllRoomLabels)
		{
			if (URoomLabelWidget* RoomLabel = Cast<URoomLabelWidget>(ChildWidget))
			{
				// 如果这个条目的名字等于玩家刚才点击的名字，就高亮！否则熄灭！
				bool bShouldHighlight = (RoomLabel->GetRoomName() == CurrentSelectedRoomName);
				RoomLabel->SetHighlight(bShouldHighlight);
			}
		}
	}
}

void ULANRoomPage::OnEnterRoomClicked()
{
	// 1. 拦截：有没有选中房间？
	if (CurrentSelectedRoomName.IsEmpty())
	{
		return;
	}

	// 2. 从底层的搜索结果(SessionSearch->SearchResults)里，找到这个名字对应的真实房间数据
	FOnlineSessionSearchResult* TargetRoomResult = nullptr;
	if (SessionSearch.IsValid())
	{
		for (FOnlineSessionSearchResult& Result : SessionSearch->SearchResults)
		{
			FString FoundName;
			Result.Session.SessionSettings.Get(FName("ROOM_NAME"), FoundName);
			if (FoundName == CurrentSelectedRoomName)
			{
				TargetRoomResult = &Result;
				break;
			}
		}
	}

	// 3. 找到目标房间后，调用底层接口加入它！
	if (TargetRoomResult)
	{
		IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
		if (OnlineSub && OnlineSub->GetSessionInterface().IsValid())
		{
			
			// 绑定加入完成的回调
			JoinSessionCompleteDelegateHandle = OnlineSub->GetSessionInterface()->AddOnJoinSessionCompleteDelegate_Handle(
				FOnJoinSessionCompleteDelegate::CreateUObject(this, &ULANRoomPage::OnJoinSessionComplete));

			// 执行真正的加入指令
			OnlineSub->GetSessionInterface()->JoinSession(0, NAME_GameSession, *TargetRoomResult);
		}
	}
}

// ==========================================
// 成功加入房间后的“队伍自动平衡分配”算法
// ==========================================
void ULANRoomPage::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub && OnlineSub->GetSessionInterface().IsValid())
	{
		OnlineSub->GetSessionInterface()->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	}

	if (Result == EOnJoinSessionCompleteResult::Success)
	{
		bIsHost = false; // 我是加入者，不是房主

		// 【核心修改】：获取底层的连接字符串（其实就是房主的局域网 IP 地址）
		FString ConnectString;
		if (OnlineSub->GetSessionInterface()->GetResolvedConnectString(NAME_GameSession, ConnectString))
		{
			// 让当前玩家的控制器带着 IP 地址，瞬间飞进房主的世界！
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				ConnectString = TEXT("127.0.0.1:7777"); // 【终极本地测试写死大法】
				PC->ClientTravel(ConnectString, TRAVEL_Absolute);
			}
		}
	}
}

void ULANRoomPage::OnBackToMenuClicked()
{
	// ==========================================
	// 【核心跳转逻辑】：动态生成主菜单 UI 并销毁当前局域网大厅
	// ==========================================
	
	// 检查我们是否在蓝图里配置了主菜单的类
	if (GameMenuClass)
	{
		// 在内存中重新生成主菜单的实例
		UUserWidget* GameMenuWidget = CreateWidget<UUserWidget>(GetWorld(), GameMenuClass);
		
		// 确保生成成功
		if (GameMenuWidget)
		{
			// 将主菜单重新添加到玩家的屏幕上
			GameMenuWidget->AddToViewport();
			
			// 把自己（当前的局域网大厅）从屏幕上彻底销毁！
			this->RemoveFromParent();
		}
	}
}

// ==========================================
// 2. 搜索房间并显示到列表的逻辑！
// ==========================================
void ULANRoomPage::FindLANRooms()
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (!OnlineSub || !OnlineSub->GetSessionInterface().IsValid()) return;

	IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();

	// ==========================================
	// 【无敌逻辑 1】：使用引擎官方的状态锁，彻底抛弃我们自己写的 bIsSearching！
	// ==========================================
	if (SessionSearch.IsValid() && SessionSearch->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		return;
	}

	// ==========================================
	// 【无敌逻辑 2】：每次搜索前，强制重新插拔一次天线！保证回调绝对畅通！
	// ==========================================
	Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
	FindSessionsCompleteDelegateHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &ULANRoomPage::OnFindSessionsComplete)
	);

	// 重新配置搜索参数
	SessionSearch = MakeShareable(new FOnlineSessionSearch());
	SessionSearch->bIsLanQuery = true;
	SessionSearch->MaxSearchResults = 20;
	SessionSearch->TimeoutInSeconds = 2.0f;
	SessionSearch->QuerySettings.Set(FName("PRESENCESEARCH"), true, EOnlineComparisonOp::Equals);
	
	Sessions->FindSessions(0, SessionSearch.ToSharedRef());

}

// 引擎搜完之后，会带着一堆数据来到这个函数
void ULANRoomPage::OnFindSessionsComplete(bool bWasSuccessful)
{
	// ==========================================
	// 1. 声明这两个关键的数组（修复 C2065 报错）
	// ==========================================
	TArray<FString> NewlyFoundRooms;
	TArray<FString> NewlyFoundRoomSignatures;

	// ==========================================
	// 2. 收到消息的第一件事：立刻注销天线，防止重复接收！
	// ==========================================
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub && OnlineSub->GetSessionInterface().IsValid())
	{
		OnlineSub->GetSessionInterface()->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
	}

	// ==========================================
	// 3. 打印我们期待已久的终极回调状态！
	// ==========================================
	int32 FoundNum = SessionSearch.IsValid() ? SessionSearch->SearchResults.Num() : 0;
	if (GEngine)
	{
		FString Msg = FString::Printf(TEXT("回调触发了！成功状态: %d, 搜到 %d 个"), bWasSuccessful, FoundNum);
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, bWasSuccessful ? FColor::Cyan : FColor::Red, Msg);
	}

	// ==========================================
	// 4. 解析结果并生成 UI
	// ==========================================
	if (bWasSuccessful && SessionSearch.IsValid())
	{
		// 第一遍遍历：提取名字和人数，制作“签名”
		for (const FOnlineSessionSearchResult& Result : SessionSearch->SearchResults)
		{
			if (Result.IsValid())
			{
				FString FoundRoomName = TEXT("未命名房间");
				Result.Session.SessionSettings.Get(FName("ROOM_NAME"), FoundRoomName);
				NewlyFoundRooms.Add(FoundRoomName);

				int32 MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
				if (MaxPlayers <= 0) MaxPlayers = 10; 

				int32 CurrentRealPlayers = MaxPlayers - Result.Session.NumOpenPublicConnections;
				if (CurrentRealPlayers <= 0) CurrentRealPlayers = 1;

				int32 TotalPlayersWithAI = CurrentRealPlayers; 
				Result.Session.SessionSettings.Get(FName("TOTAL_PLAYERS_WITH_AI"), TotalPlayersWithAI);
				
				if (TotalPlayersWithAI <= 0) TotalPlayersWithAI = 1; 

				bool bIsPlaying = false;
				Result.Session.SessionSettings.Get(FName("ROOM_STATE"), bIsPlaying);

				FString Signature = FString::Printf(TEXT("%s_%d_%d_%d"), *FoundRoomName, TotalPlayersWithAI, MaxPlayers, bIsPlaying ? 1 : 0);
				NewlyFoundRoomSignatures.Add(Signature);
			}
		}
		
		bool bIsDifferent = false;
		
		if (NewlyFoundRoomSignatures.Num() != CurrentRoomSignatures.Num())
		{
			bIsDifferent = true; 
		}
		else
		{
			for (int32 i = 0; i < NewlyFoundRoomSignatures.Num(); ++i)
			{
				if (NewlyFoundRoomSignatures[i] != CurrentRoomSignatures[i])
				{
					bIsDifferent = true; 
					break;
				}
			}
		}
		
		if (bIsDifferent)
		{
			CurrentDisplayedRooms = NewlyFoundRooms; 
			CurrentRoomSignatures = NewlyFoundRoomSignatures;

			if (List_Rooms)
			{
				List_Rooms->ClearChildren();

				if (RoomLabelClass)
				{
					for (const FOnlineSessionSearchResult& Result : SessionSearch->SearchResults)
					{
						if (Result.IsValid())
						{
							URoomLabelWidget* RoomLabel = CreateWidget<URoomLabelWidget>(GetWorld(), RoomLabelClass);
							if (RoomLabel)
							{
								FString RoomName;
								Result.Session.SessionSettings.Get(FName("ROOM_NAME"), RoomName);
								RoomLabel->SetRoomName(RoomName);

								int32 MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
								if (MaxPlayers <= 0) MaxPlayers = 10;

								int32 CurrentRealPlayers = MaxPlayers - Result.Session.NumOpenPublicConnections;
								if (CurrentRealPlayers <= 0) CurrentRealPlayers = 1;

								int32 TotalPlayersWithAI = CurrentRealPlayers; 
								Result.Session.SessionSettings.Get(FName("TOTAL_PLAYERS_WITH_AI"), TotalPlayersWithAI);
								
								if (TotalPlayersWithAI <= 0) TotalPlayersWithAI = 1; 

								RoomLabel->SetPlayerCount(TotalPlayersWithAI, MaxPlayers);

								bool bIsPlaying = false;
								Result.Session.SessionSettings.Get(FName("ROOM_STATE"), bIsPlaying);
								RoomLabel->SetRoomState(bIsPlaying);

								bool bIsHighlight = (RoomName == CurrentSelectedRoomName);
								RoomLabel->SetHighlight(bIsHighlight);
								
								RoomLabel->OnRoomSelected.AddDynamic(this, &ULANRoomPage::HandleRoomSelected);
								
								List_Rooms->AddChild(RoomLabel);
							}
						}
					}
				}
			}
		}
	}
	
	// 最后别忘了重置搜索状态，防止死循环
	bIsSearching = false;
}

// ==========================================
// 创房层逻辑
// ==========================================

void ULANRoomPage::OnConfirmCreateRoomClicked()
{
	// 1. 获取用户输入的房间名，并使用 TrimStartAndEnd() 去掉前后的无用空格，防止用户只敲了几个空格
	PendingRoomName = Input_RoomName->GetText().ToString().TrimStartAndEnd();
	PendingRoomPassword = Input_RoomPassword->GetText().ToString();
	
	// ==========================================
	// 2. 获取选择的游戏模式和地图
	// ==========================================
	if (ComboBox_GameMode)
	{
		PendingGameMode = ComboBox_GameMode->GetSelectedOption();
	}
	
	if (ComboBox_MapSelect)
	{
		// 获取选中的地图显示名称
		FString SelectedMapDisplayName = ComboBox_MapSelect->GetSelectedOption();
		
		// 从MapInfoDataTable中查找对应的LevelName
		if (MapInfoDataTable)
		{
			static const FString ContextString(TEXT("MapInfo Context"));
			TArray<FMapInfoRow*> AllMaps;
			MapInfoDataTable->GetAllRows<FMapInfoRow>(ContextString, AllMaps);
			
			for (FMapInfoRow* MapInfo : AllMaps)
			{
				if (MapInfo && MapInfo->DisplayName.ToString() == SelectedMapDisplayName)
				{
					PendingMapLevelName = MapInfo->LevelName;
					break;
				}
			}
		}
	}

	// ==========================================
	// 【核心校验 1】：房间名是否为空？
	// ==========================================
	if (PendingRoomName.IsEmpty())
	{
		if (Text_CreateRoomHint)
		{
			Text_CreateRoomHint->SetText(FText::FromString(TEXT("房间名称不能为空！")));
			Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
		}
		return; // 拦截操作，不往下执行！
	}
	
	// ==========================================
	// 2. 【核心新增】真实重名校验：检查房间名是否已被占用
	// ==========================================
	// CurrentDisplayedRooms 是我们通过 3秒定时器 实时从局域网搜回来的真实房间名列表
	if (CurrentDisplayedRooms.Contains(PendingRoomName))
	{
		if (Text_CreateRoomHint) 
		{ 
			Text_CreateRoomHint->SetText(FText::FromString(TEXT("该房间名已存在，请换一个名称！"))); 
			Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible); 
		}
		return; // 发现重名，直接拦截，绝对不往下执行创房代码！
	}

	// 2. 准备调用引擎底层接口创建真实的局域网会话 (Session)
	if (Text_CreateRoomHint)
	{
		Text_CreateRoomHint->SetText(FText::FromString(TEXT("正在创建房间，请稍候...")));
		Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
	}

	// 获取在线子系统 (OnlineSubsystem)
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub)
	{
		// 获取会话接口
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid())
		{
			// 【修复Bug的核心】：检查是不是内存里卡着一个叫 NAME_GameSession 的旧房间
			if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
			{
				// 如果有旧的，先绑定销毁回调，然后强制销毁它！
				DestroySessionCompleteDelegateHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
					FOnDestroySessionCompleteDelegate::CreateUObject(this, &ULANRoomPage::OnDestroySessionComplete));
				Sessions->DestroySession(NAME_GameSession);
			}
			else
			{
				// 如果没有旧的，直接开始创建
				HostRealSession();
			}
		}
	}
}

// 销毁旧房间完毕后，引擎会调这个函数
void ULANRoomPage::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub && OnlineSub->GetSessionInterface().IsValid())
	{
		// 注销委托
		OnlineSub->GetSessionInterface()->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
	}
	// 旧垃圾清理完毕，真正开始创建新房间！
	HostRealSession();
}

// 真正执行创建房间的代码（从以前的代码剥离出来的）
void ULANRoomPage::HostRealSession()
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub && OnlineSub->GetSessionInterface().IsValid())
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		
		// 绑定回调：创房结束（无论成功失败）时通知 UI
		CreateSessionCompleteDelegateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
			FOnCreateSessionCompleteDelegate::CreateUObject(this, &ULANRoomPage::OnCreateSessionComplete));

		FOnlineSessionSettings SessionSettings;
		SessionSettings.bIsLANMatch = true;          // 开启局域网模式
		SessionSettings.NumPublicConnections = 10;   // 房间最大人数
		SessionSettings.bShouldAdvertise = true;     // 允许局域网广播被搜到
		SessionSettings.bUsesPresence = true;        // 开启在线状态存在(Presence)
		SessionSettings.bAllowJoinInProgress = true; // 允许中途加入（JIP）

		// ==========================================
		// 【架构规范】：将用户选择的所有核心元数据写入 SessionSettings。
		// EOnlineDataAdvertisementType::ViaOnlineServiceAndPing 确保这些数据会随着心跳包被其他客户端搜到。
		// ==========================================
		SessionSettings.Set(FName("ROOM_NAME"), PendingRoomName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		SessionSettings.Set(FName("GAME_MODE"), PendingGameMode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		
		// 【核心修复】：必须将地图名称存入 Session，供大厅 UI 查询
		SessionSettings.Set(FName("MAP_NAME"), PendingMapLevelName.ToString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		
		// 默认状态标签
		SessionSettings.Set(FName("ROOM_STATE"), false, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing); // false=等待中
		SessionSettings.Set(FName("TOTAL_PLAYERS_WITH_AI"), 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing); // 默认人数
		
		// 执行底层创建命令，NAME_GameSession 是引擎默认的当前游戏会话宏
		Sessions->CreateSession(0, NAME_GameSession, SessionSettings);
	}
}

// ==========================================
// 创房结果回调
// ==========================================
void ULANRoomPage::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	// ==========================================
	// 【内存管理规范】：无论结果如何，第一时间注销委托句柄，防止内存泄漏或野指针触发
	// ==========================================
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid())
		{
			Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		}
	}

	if (bWasSuccessful)
	{
		bIsHost = true; // 确立房主权威身份
		
		// 发放免死金牌！告诉 NativeDestruct 我们是进行关卡跳转，禁止误杀 Session
		bIsTraveling = true;
		
		// ==========================================
		// 【核心修复】：摒弃硬编码，使用玩家在 UI 选择的数据驱动关卡名
		// 增加防呆设计：如果解析出的名字为空，则回退到保底大厅 "L_Room"
		// ==========================================
		FName TargetMapName = PendingMapLevelName.IsNone() ? FName("L_Room") : PendingMapLevelName;
		
		// ==========================================
		// 构建网络传输的 URL Options
		// ?listen : 告诉引擎以 Listen Server（监听服务器）模式打开此关卡
		// ==========================================
		FString URLOptions = TEXT("?listen");
		
		/* * 【高级架构扩展建议】：
		 * 如果你需要根据 PendingGameMode 强行替换当前地图的 GameMode，可以拼接到 URL 中：
		 * 例如: URLOptions += FString::Printf(TEXT("?game=%s"), *TargetGameModeClassPath);
		 * 目前若你的地图已经在编辑器 World Settings 里配置了正确的 GameMode，则无需这行。
		 */

		// 日志追踪：帮助快速定位多端跳转问题
		if (GEngine) 
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, FString::Printf(TEXT("创房成功！正在作为服务器跳转至关卡: %s"), *TargetMapName.ToString()));
		}

		// 执行绝对跳转 (TRAVEL_Absolute)
		UGameplayStatics::OpenLevel(GetWorld(), TargetMapName, true, URLOptions);
	}
	else
	{
		// 失败提示处理保持不变
		if (Text_CreateRoomHint)
		{
			Text_CreateRoomHint->SetText(FText::FromString(TEXT("创建房间失败，请检查网络或重启游戏重试！")));
			Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
		}
	}
}

void ULANRoomPage::OnHideCreateRoomClicked()
{
	// 取消创房，隐藏面板
	if (Overlay_CreateRoom) Overlay_CreateRoom->SetVisibility(ESlateVisibility::Hidden);
}

void ULANRoomPage::OnLeaveRoomClicked()
{
	
	// ==========================================
	// 【核心逻辑】：如果是房主退出，必须销毁房间，不留垃圾！
	// ==========================================
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub && OnlineSub->GetSessionInterface().IsValid())
	{
		if (bIsHost)
		{
			// 房主退房，真正摧毁这个 Session
			OnlineSub->GetSessionInterface()->DestroySession(NAME_GameSession);
			bIsHost = false; // 剥夺房主身份
		}
	}
}

void ULANRoomPage::OnToggleReadyClicked()
{
	// 翻转状态
	bIsReady = !bIsReady;
}





