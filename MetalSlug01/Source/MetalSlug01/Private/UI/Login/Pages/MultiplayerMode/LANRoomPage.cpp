// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Login/Pages/MultiplayerMode/LANRoomPage.h"
// 房间条目 UI
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
#include "Systems/Account/AccountSubsystem.h"
#include "Systems/GameFlowSubsystem.h"
#include "UI/Login/Pages/BattleRoom/PlayerLabelWidget.h"
#include "Data/Enums/RoomEnums.h"
#include "Data/Tables/MapTableRow.h"


// ==========================================
// 1. 初始化
// ==========================================

/**
 * ULANRoomPage::Initialize
 *
 * 1. 订阅 FindSessions 委托（用 AddOnFindSessionsCompleteDelegate_Handle）
 * 2. 绑定大厅层 / 创房层 按钮
 * 3. 初始化 UI 面板状态（隐藏创房弹窗）
 * 4. 开启 3 秒一次自动刷新定时器
 * 5. 初始化游戏模式下拉框（刀战/生化）
 * 6. 初始化地图下拉框（从 MapInfoDataTable 读）
 */
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
			// 【修复 1】: 删掉旧的两行绑定代码，换成这极简的一行
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
	if (Btn_EnterRoom)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LANRoomPage] Btn_EnterRoom 绑定成功"));
		Btn_EnterRoom->OnClicked.AddDynamic(this, &ULANRoomPage::OnEnterRoomClicked);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] Btn_EnterRoom 为空! 请检查蓝图中的变量名是否匹配"));
	}
	if (Btn_BackToMenu) Btn_BackToMenu->OnClicked.AddDynamic(this, &ULANRoomPage::OnBackToMenuClicked);

	// ==========================================
	// 1.5 账号冲突模态对话框初始化（从 LoginPage 迁移）
	// ==========================================
	// 默认隐藏冲突弹框
	if (Overlay_LANRoomConflict)
	{
		Overlay_LANRoomConflict->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 绑定确认按钮(防呆: 先清理旧绑定, 防止多重绑定)
	if (Btn_ConfirmLANRoomConflict)
	{
		Btn_ConfirmLANRoomConflict->OnClicked.RemoveDynamic(this, &ULANRoomPage::OnConfirmLANRoomConflictClicked);
		Btn_ConfirmLANRoomConflict->OnClicked.AddDynamic(this, &ULANRoomPage::OnConfirmLANRoomConflictClicked);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LANRoomPage] Btn_ConfirmLANRoomConflict 为空! 请检查 WBP_LANRoomPage 里是否拖了同名按钮"));
	}

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

	// 【核心修复1】: 在开启定时器前，先强杀可能残留的幽灵定时器
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


// ==========================================
// 2. 界面销毁时，必须关掉定时器
// ==========================================

/**
 * ULANRoomPage::NativeDestruct
 *
 * 1. 关闭自动刷新定时器
 * 2. 退订 FindSessions 委托（持有句柄凭证）
 * 3. 核心修复: 房主 + 非传送销毁时，强制摧毁 Session
 */
void ULANRoomPage::NativeDestruct()
{
	// 关闭自动刷新定时器
	GetWorld()->GetTimerManager().ClearTimer(SearchTimerHandle);

	// // 2. 【核心修复】: 拿着凭证去退订！做到人走茶凉
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();

	// ==========================================
	// 【核心修复 2】: 只有当你既是房主，【并且】又不是因为传送而销毁 UI 时，才炸毁房间
	// ==========================================
	if (bIsHost && !bIsTraveling)
	{

		if (OnlineSub && OnlineSub->GetSessionInterface().IsValid())
		{
			OnlineSub->GetSessionInterface()->DestroySession(NAME_GameSession);
			bIsHost = false;

			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow, TEXT("检测到房主离开，后台房间已强制解散!"));
		}
	}

	Super::NativeDestruct();
}


// ==========================================
// 3. 大厅层逻辑
// ==========================================

/**
 * OnShowCreateRoomClicked
 *
 * 打开创房弹窗
 * 体验优化: 每次打开时清空输入框和错误提示
 */
void ULANRoomPage::OnShowCreateRoomClicked()
{
	// 弹出创房面板
	if (Overlay_CreateRoom) Overlay_CreateRoom->SetVisibility(ESlateVisibility::Visible);

	// 【体验优化】: 每次打开创房面板时，把上一次的错误提示隐藏，并清空输入框
	if (Text_CreateRoomHint) Text_CreateRoomHint->SetVisibility(ESlateVisibility::Hidden);
	if (Input_RoomName) Input_RoomName->SetText(FText::GetEmpty());
	if (Input_RoomPassword) Input_RoomPassword->SetText(FText::GetEmpty());
}


// ==========================================
// 4. 处理房间选中与加入逻辑
// ==========================================

/**
 * HandleRoomSelected
 *
 * 房间条目被点击的回调
 * 1. 记录当前选中的房间名
 * 2. 遍历所有条目，更新高亮状态（多选一）
 */
void ULANRoomPage::HandleRoomSelected(FString RoomName)
{
	UE_LOG(LogTemp, Warning, TEXT("[LANRoomPage] HandleRoomSelected 被调用，RoomName=[%s]"), *RoomName);

	// 记录当前选中的房间名
	CurrentSelectedRoomName = RoomName;

	// ==========================================
	// 【新增核心魔法】: 遍历列表，刷新所有条目的高亮状态
	// ==========================================
	if (List_Rooms)
	{
		TArray<UWidget*> AllRoomLabels = List_Rooms->GetAllChildren();
		for (UWidget* ChildWidget : AllRoomLabels)
		{
			if (URoomLabelWidget* RoomLabel = Cast<URoomLabelWidget>(ChildWidget))
			{
				// 如果这个条目的名字等于玩家刚才点击的名字，就高亮! 否则熄灭!
				bool bShouldHighlight = (RoomLabel->GetRoomName() == CurrentSelectedRoomName);
				RoomLabel->SetHighlight(bShouldHighlight);
			}
		}
	}
}


/**
 * OnEnterRoomClicked
 *
 * 加入房间按钮
 * 1. 拦截: 是否有选中房间
 * 2. 从 SessionSearch 中找到对应的 FOnlineSessionSearchResult
 * 3. 调用 JoinSession 加入
 */
void ULANRoomPage::OnEnterRoomClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("[LANRoomPage] OnEnterRoomClicked 被调用，CurrentSelectedRoomName=[%s]"), *CurrentSelectedRoomName);

	// 1. 拦截: 有没有选中房间？
	if (CurrentSelectedRoomName.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LANRoomPage] 房间名为空，无法加入"));
		return;
	}

	// 2. 从底层的搜索结果 (SessionSearch->SearchResults) 里，找到这个名字对应的真实房间数据
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

	// 3. 找到目标房间后，调用底层接口加入它
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
// 5. 成功加入房间后的"队伍自动平衡分配"算法
// ==========================================

/**
 * OnJoinSessionComplete
 *
 * 加入会话完成回调
 * 1. 退订委托
 * 2. 成功后获取 ConnectString（房主 IP）
 * 3. 调用 ClientTravel 传送到房主世界
 * 注意: 本地测试写死 127.0.0.1:7777
 */
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

		// 【核心修改】: 获取底层的连接字符串（其实就是房主的局域网 IP 地址）
		FString ConnectString;
		if (OnlineSub->GetSessionInterface()->GetResolvedConnectString(NAME_GameSession, ConnectString))
		{
			// 让当前玩家的控制器带着 IP 地址，瞬间飞进房主的世界
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				ConnectString = TEXT("127.0.0.1:7777"); // 【终极本地测试写死大法】
				PC->ClientTravel(ConnectString, TRAVEL_Absolute);
			}
		}
	}
}


/**
 * OnBackToMenuClicked
 *
 * 返回主菜单按钮
 * 销毁当前大厅，生成主菜单
 */
void ULANRoomPage::OnBackToMenuClicked()
{
	// ==========================================
	// 【核心跳转逻辑】: 动态生成主菜单 UI 并销毁当前局域网大厅
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

			// 把自己（当前的局域网大厅）从屏幕上彻底销毁
			this->RemoveFromParent();
		}
	}
}


// ==========================================
// 6. 搜索房间并显示到列表的逻辑
// ==========================================

/**
 * FindLANRooms
 *
 * 触发局域网房间搜索
 * 1. 状态锁: 引擎自带的 EOnlineAsyncTaskState::InProgress 检查
 * 2. 重新插拔委托: 保证回调畅通
 * 3. 配置 SessionSearch（LAN、20 结果、2 秒超时）
 * 4. 调用 FindSessions
 */
void ULANRoomPage::FindLANRooms()
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (!OnlineSub || !OnlineSub->GetSessionInterface().IsValid()) return;

	IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();

	// ==========================================
	// 【无敌逻辑 1】: 使用引擎官方的状态锁，彻底抛弃我们自己写的 bIsSearching
	// ==========================================
	if (SessionSearch.IsValid() && SessionSearch->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		return;
	}

	// ==========================================
	// 【无敌逻辑 2】: 每次搜索前，强制重新插拔一次天线!保证回调绝对畅通
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


/**
 * OnFindSessionsComplete
 *
 * 搜索完成回调
 * 1. 退订委托
 * 2. 解析所有搜索结果，生成 NewlyFoundRoomSignatures 签名
 * 3. 与 CurrentRoomSignatures 对比，决定是否需要重绘 UI
 * 4. 真正需要重绘时，ClearChildren + 重新 AddChild 房间条目
 * 5. 设置高亮（针对已选中的房间名）
 */
void ULANRoomPage::OnFindSessionsComplete(bool bWasSuccessful)
{
	// ==========================================
	// 1. 声明这两个关键的数组（修复 C2065 报错）
	// ==========================================
	TArray<FString> NewlyFoundRooms;
	TArray<FString> NewlyFoundRoomSignatures;

	// ==========================================
	// 2. 收到消息的第一件事: 立刻注销天线，防止重复接收
	// ==========================================
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub && OnlineSub->GetSessionInterface().IsValid())
	{
		OnlineSub->GetSessionInterface()->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
	}

	// ==========================================
	// 3. 打印我们期待已久的终极回调状态
	// ==========================================
	int32 FoundNum = SessionSearch.IsValid() ? SessionSearch->SearchResults.Num() : 0;
	if (GEngine)
	{
		FString Msg = FString::Printf(TEXT("回调触发了! 成功状态: %d, 搜到 %d 个"), bWasSuccessful, FoundNum);
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, bWasSuccessful ? FColor::Cyan : FColor::Red, Msg);
	}

	// ==========================================
	// 4. 解析结果并生成 UI
	// ==========================================
	if (bWasSuccessful && SessionSearch.IsValid())
	{
		// 第一遍遍历: 提取名字和人数，制作"签名"
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
// 7. 创房层逻辑
// ==========================================

/**
 * OnConfirmCreateRoomClicked
 *
 * 确认创建房间按钮
 * 1. 提取 Trim 后的房间名/密码
 * 2. 读取游戏模式/地图名（从 MapInfoDataTable 反查 LevelName）
 * 3. 校验: 房间名非空
 * 4. 校验: 不与已存在房间重名
 * 5. 检查是否有残留 Session，有则先销毁再创建
 * 6. 调用 HostRealSession
 */
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

		// 从 MapInfoDataTable 中查找对应的 LevelName
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
	// 【核心校验 1】: 房间名是否为空？
	// ==========================================
	if (PendingRoomName.IsEmpty())
	{
		if (Text_CreateRoomHint)
		{
			Text_CreateRoomHint->SetText(FText::FromString(TEXT("房间名称不能为空!")));
			Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
		}
		return; // 拦截操作，不往下执行
	}

	// ==========================================
	// 2. 【核心新增】真实重名校验: 检查房间名是否已被占用
	// ==========================================
	// CurrentDisplayedRooms 是我们通过 3秒定时器 实时从局域网搜回来的真实房间名列表
	if (CurrentDisplayedRooms.Contains(PendingRoomName))
	{
		if (Text_CreateRoomHint)
		{
			Text_CreateRoomHint->SetText(FText::FromString(TEXT("该房间名已存在，请换一个名称!")));
			Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
		}
		return; // 发现重名，直接拦截，绝对不往下执行创房代码
	}

	// 2. 准备调用引擎底层接口创建真实的局域网会话 (Session)
	if (Text_CreateRoomHint)
	{
		Text_CreateRoomHint->SetText(FText::FromString(TEXT("正在检查同账号房间，请稍候...")));
		Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
	}

	// ==========================================
	// 【核心修复】: 创房前先 FindSessions 查大厅
	//                防止"同账号已有建房"的情况
	// ==========================================
	// 不在这里直接创房, 而是把"创房"动作放到 OnAccountCheckFindSessionsComplete 里
	// (类似 OnCreateSessionComplete 的延迟逻辑)
	FindSessionsForAccountCheck();
}


/**
 * OnDestroySessionComplete
 *
 * 销毁旧房间完成回调
 * 1. 退订委托
 * 2. 调用 HostRealSession 真正开始创建新房间
 */
void ULANRoomPage::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub && OnlineSub->GetSessionInterface().IsValid())
	{
		// 注销委托
		OnlineSub->GetSessionInterface()->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
	}
	// 旧垃圾清理完毕，真正开始创建新房间
	HostRealSession();
}


/**
 * HostRealSession
 *
 * 真正执行创建房间的代码
 * 1. 绑定 CreateSession 回调
 * 2. 配置 FOnlineSessionSettings（LAN、10 人、广告、Presence、JIP）
 * 3. 通过 SessionSettings.Set 写入 ROOM_NAME/GAME_MODE/MAP_NAME/ROOM_STATE/TOTAL_PLAYERS_WITH_AI
 * 4. 调用 CreateSession
 */
void ULANRoomPage::HostRealSession()
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub && OnlineSub->GetSessionInterface().IsValid())
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();

		// 绑定回调: 创房结束（无论成功失败）时通知 UI
		CreateSessionCompleteDelegateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
			FOnCreateSessionCompleteDelegate::CreateUObject(this, &ULANRoomPage::OnCreateSessionComplete));

		FOnlineSessionSettings SessionSettings;
		SessionSettings.bIsLANMatch = true;          // 开启局域网模式
		SessionSettings.NumPublicConnections = 10;   // 房间最大人数
		SessionSettings.bShouldAdvertise = true;     // 允许局域网广播被搜到
		SessionSettings.bUsesPresence = true;        // 开启在线状态存在 (Presence)
		SessionSettings.bAllowJoinInProgress = true; // 允许中途加入 (JIP)

		// ==========================================
		// 【架构规范】: 将用户选择的所有核心元数据写入 SessionSettings
		// EOnlineDataAdvertisementType::ViaOnlineServiceAndPing 确保这些数据会随着心跳包被其他客户端搜到
		// ==========================================
		SessionSettings.Set(FName("ROOM_NAME"), PendingRoomName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		SessionSettings.Set(FName("GAME_MODE"), PendingGameMode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

		// 【核心修复】: 必须将地图名称存入 Session，供大厅 UI 查询
		SessionSettings.Set(FName("MAP_NAME"), PendingMapLevelName.ToString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

		// 默认状态标签
		SessionSettings.Set(FName("ROOM_STATE"), false, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing); // false=等待中
		SessionSettings.Set(FName("TOTAL_PLAYERS_WITH_AI"), 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing); // 默认人数

		// ==========================================
		// 【新增】: 把"房主账号名"也写进 SessionSettings
		// 用途: 其他人创房前先 FindSessions,
		//      看到 HOST_ACCOUNT == 自己的账号 → 阻止创房
		// ==========================================
		FString HostAccountName = TEXT("");
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
			{
				HostAccountName = AccountSub->GetCurrentLoggedInUser();
			}
		}
		SessionSettings.Set(FName("HOST_ACCOUNT"), HostAccountName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

		// 执行底层创建命令，NAME_GameSession 是引擎默认的当前游戏会话宏
		Sessions->CreateSession(0, NAME_GameSession, SessionSettings);
	}
}


// ==========================================
// 8. 创房结果回调
// ==========================================

/**
 * OnCreateSessionComplete
 *
 * 创建会话完成回调
 * 1. 内存管理规范: 无论结果如何，第一时间退订委托
 * 2. 成功 -> 标记 bIsHost=true、bIsTraveling=true（防误杀）
 * 3. 成功 -> 计算 TargetMapName（有就玩家选，否则回退 L_Room）
 * 4. 成功 -> URLOptions = "?listen" 打开监听服务器
 * 5. 成功 -> UGameplayStatics::OpenLevel 以 ListenServer 模式开图
 * 6. 失败 -> 显示错误提示
 */
void ULANRoomPage::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	// ==========================================
	// 【内存管理规范】: 无论结果如何，第一时间注销委托句柄，防止内存泄漏或野指针触发
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

		// 发放免死金牌! 告诉 NativeDestruct 我们是进行关卡跳转，禁止误杀 Session
		bIsTraveling = true;

		// ==========================================
		// 【核心修复】: 摒弃硬编码，使用玩家在 UI 选择的数据驱动关卡名
		// 增加防呆设计: 如果解析出的名字为空，则回退到保底大厅 "L_Room"
		// ==========================================
		FName TargetMapName = PendingMapLevelName.IsNone() ? FName("L_Room") : PendingMapLevelName;

		// ==========================================
		// 构建网络传输的 URL Options
		// ?listen : 告诉引擎以 Listen Server（监听服务器）模式打开此关卡
		// ==========================================
		FString URLOptions = TEXT("?listen");

		/* * 【高级架构扩展建议】:
		 * 如果你需要根据 PendingGameMode 强行替换当前地图的 GameMode，可以拼接到 URL 中
		 * 例如: URLOptions += FString::Printf(TEXT("?game=%s"), *TargetGameModeClassPath);
		 * 目前若你的地图已经在编辑器 World Settings 里配置了正确的 GameMode，则无需这行
		 */

		// 日志追踪: 帮助快速定位多端跳转问题
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, FString::Printf(TEXT("创房成功! 正在作为服务器跳转至关卡: %s"), *TargetMapName.ToString()));
		}

		// 执行绝对跳转 (TRAVEL_Absolute)
		UGameplayStatics::OpenLevel(GetWorld(), TargetMapName, true, URLOptions);
	}
	else
	{
		// 失败提示处理保持不变
		if (Text_CreateRoomHint)
		{
			Text_CreateRoomHint->SetText(FText::FromString(TEXT("创建房间失败，请检查网络或重启游戏重试!")));
			Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
		}
	}
}


/**
 * OnHideCreateRoomClicked
 *
 * 取消按钮: 隐藏创房弹窗
 */
void ULANRoomPage::OnHideCreateRoomClicked()
{
	// 取消创房，隐藏面板
	if (Overlay_CreateRoom) Overlay_CreateRoom->SetVisibility(ESlateVisibility::Hidden);
}


/**
 * OnLeaveRoomClicked
 *
 * 离开房间按钮
 * 核心逻辑: 如果是房主退出，必须销毁房间，不留垃圾
 */
void ULANRoomPage::OnLeaveRoomClicked()
{

	// ==========================================
	// 【核心逻辑】: 如果是房主退出，必须销毁房间，不留垃圾
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


/**
 * OnToggleReadyClicked
 *
 * 切换准备状态按钮
 */
void ULANRoomPage::OnToggleReadyClicked()
{
	// 翻转状态
	bIsReady = !bIsReady;
}


// ==========================================
// 10. 创房前"同号检查"搜索（新增）
// ==========================================

/**
 * FindSessionsForAccountCheck
 *
 * 创房前专用搜索: 查大厅是否有"我的账号"已建好的房间
 *
 * 设计:
 *  - 走独立委托句柄 (AccountCheckFindSessionsDelegateHandle)
 *    不与常规 FindLANRooms 冲突
 *  - 不刷新 CurrentDisplayedRooms (本函数不显示列表, 只查)
 *  - 不弹 UI 提示, 回调里集中处理
 */
void ULANRoomPage::FindSessionsForAccountCheck()
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (!OnlineSub || !OnlineSub->GetSessionInterface().IsValid())
	{
		// 在线子系统都没, 直接放行让 HostRealSession 自己报错
		ProceedToCreateRoomAfterCheck();
		return;
	}

	IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();

	// 防御: 清旧句柄, 防重复订阅
	Sessions->ClearOnFindSessionsCompleteDelegate_Handle(AccountCheckFindSessionsDelegateHandle);

	// 绑新回调
	AccountCheckFindSessionsDelegateHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &ULANRoomPage::OnAccountCheckFindSessionsComplete));

	// 准备搜索配置
	AccountCheckSessionSearch = MakeShareable(new FOnlineSessionSearch());
	AccountCheckSessionSearch->bIsLanQuery = true;       // 局域网模式
	AccountCheckSessionSearch->MaxSearchResults = 100;   // 多查一些, 防止漏
	AccountCheckSessionSearch->PingBucketSize = 50;

	// 设置查询过滤: 只查 GameSession 类型
	// 注意: 用字符串 "PRESENCESEARCH" 而非 SEARCH_PRESENCE 宏
	//       因为原 FindLANRooms 也在用这个字符串, 保证一致性
	AccountCheckSessionSearch->QuerySettings.Set(FName("PRESENCESEARCH"), true, EOnlineComparisonOp::Equals);

	// 执行搜索
	Sessions->FindSessions(0, AccountCheckSessionSearch.ToSharedRef());

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
			TEXT("[CreateRoom] 已发起同号检查搜索, 等待回调..."));
	}
}


/**
 * OnAccountCheckFindSessionsComplete
 *
 * 同号检查的搜索回调
 * 1. 遍历所有结果, 比对 HOST_ACCOUNT 字段
 * 2. 若有同账号建房 → 弹"已有此账户创建的房间"提示, 不创房
 * 3. 若无 → 继续走真正的创房流程
 */
void ULANRoomPage::OnAccountCheckFindSessionsComplete(bool bWasSuccessful)
{
	// 退订委托(无论结果如何)
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub && OnlineSub->GetSessionInterface().IsValid())
	{
		OnlineSub->GetSessionInterface()->ClearOnFindSessionsCompleteDelegate_Handle(AccountCheckFindSessionsDelegateHandle);
	}

	// 搜索失败: 不当误创房, 让 HostRealSession 自己处理失败
	if (!bWasSuccessful)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow,
				TEXT("[CreateRoom] 同号检查搜索失败, 放行创房"));
		}
		ProceedToCreateRoomAfterCheck();
		return;
	}

	// 拿到当前账号名
	FString MyAccountName = TEXT("");
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			MyAccountName = AccountSub->GetCurrentLoggedInUser();
		}
	}

	if (MyAccountName.IsEmpty())
	{
		// 拿不到账号名, 放行让 HostRealSession 处理
		ProceedToCreateRoomAfterCheck();
		return;
	}

	// 遍历所有搜索结果, 查 HOST_ACCOUNT
	if (AccountCheckSessionSearch.IsValid())
	{
		for (const FOnlineSessionSearchResult& Result : AccountCheckSessionSearch->SearchResults)
		{
			FString HostAccount = TEXT("");
			Result.Session.SessionSettings.Get(FName("HOST_ACCOUNT"), HostAccount);

			// ==========================================
			// 【核心校验】: 同账号已有建房 → 拦截!
			// ==========================================
			if (HostAccount.Equals(MyAccountName, ESearchCase::IgnoreCase))
			{
				// 弹提示, 阻止创房
				if (Text_CreateRoomHint)
				{
					Text_CreateRoomHint->SetText(FText::FromString(
						FString::Printf(TEXT("已有此账户 [%s] 创建的房间，请更换账户重试"), *MyAccountName)));
					Text_CreateRoomHint->SetVisibility(ESlateVisibility::Visible);
				}

				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red,
						FString::Printf(TEXT("[CreateRoom] 拦截: 账号 [%s] 已存在建房"), *MyAccountName));
				}

				// 清缓存, 不创房
				AccountCheckSessionSearch.Reset();
				return;
			}
		}
	}

	// 清缓存
	AccountCheckSessionSearch.Reset();

	// 没有同账号建房, 放行创房
	ProceedToCreateRoomAfterCheck();
}


/**
 * ProceedToCreateRoomAfterCheck
 *
 * 同号检查通过后, 真正开始执行创房流程
 * (从原 OnConfirmCreateRoomClicked 末尾搬过来)
 */
void ULANRoomPage::ProceedToCreateRoomAfterCheck()
{
	// 更新提示
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
			// 【修复Bug的核心】: 检查是不是内存里卡着一个叫 NAME_GameSession 的旧房间
			if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
			{
				// 如果有旧的，先绑定销毁回调，然后强制销毁它
				// 用 Lambda 包裹避免 OnDestroySessionComplete 的私有访问问题
				DestroySessionCompleteDelegateHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
					FOnDestroySessionCompleteDelegate::CreateLambda([WeakThis = TWeakObjectPtr<ULANRoomPage>(this)](FName SessionName, bool bWasSuccessful)
					{
						if (ULANRoomPage* Self = WeakThis.Get())
						{
							Self->OnDestroySessionComplete(SessionName, bWasSuccessful);
						}
					}));
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


// ==========================================
// 11. 账号冲突模态对话框（从 LoginPage 迁移）
// ==========================================

/**
 * ShowLANRoomConflictDialog
 *
 * 外部调用入口: 弹模态对话框
 * 被 ARoomPlayerController::HandleForcedKickNotification 反射调用
 *
 * 行为:
 *  1. 显示 Overlay_LANRoomConflict + Text_ConflictMsg
 *  2. 改输入模式为 UIOnly(确保鼠标能点按钮)
 */
void ULANRoomPage::ShowLANRoomConflictDialog(const FString& Reason)
{
	// 1. 显示对话框容器
	if (Overlay_LANRoomConflict)
	{
		Overlay_LANRoomConflict->SetVisibility(ESlateVisibility::Visible);
	}

	// 2. 显示提示文本
	if (Text_ConflictMsg)
	{
		// 给玩家清晰的提示: 冲突原因 + 操作
		const FString FullMessage = FString::Printf(
			TEXT("%s\n\n点 [确认] 返回大厅"),
			*Reason);
		Text_ConflictMsg->SetText(FText::FromString(FullMessage));
	}

	// 3. 输入模式切到 UIOnly + 显示鼠标
	// 优先用 GetOwningPlayer()，拿不到时用 World->GetFirstPlayerController() 兜底
	APlayerController* PC = GetOwningPlayer();
	if (!PC && GetWorld())
	{
		PC = GetWorld()->GetFirstPlayerController();
	}

	if (PC)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(true);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow,
			FString::Printf(TEXT("[LANRoomPage] 弹冲突对话框: %s"), *Reason));
	}
}


/**
 * OnConfirmLANRoomConflictClicked
 *
 * 玩家点 [确认] → 切回大厅(不退出账号!)
 *
 * 注意: 用户想退出登录, 应该自己到 GameMenuPage 点 Btn_BackToLogin
 *       这里只负责把玩家带回大厅, 账号保持登录态
 */
void ULANRoomPage::OnConfirmLANRoomConflictClicked()
{
	// 1. 关闭对话框
	if (Overlay_LANRoomConflict)
	{
		Overlay_LANRoomConflict->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 2. 切回大厅(不退出账号)
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSub = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSub->TransitToState(EMatchState::MainLobby);
		}

		// 故意不调 AccountSubsystem->Logout()
		// 用户的账号保持登录态, 切回大厅后可继续操作或自己点 Btn_BackToLogin 换号
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green,
			TEXT("[LANRoomPage] 玩家确认, 已返回大厅(账号保持登录)"));
	}
}
