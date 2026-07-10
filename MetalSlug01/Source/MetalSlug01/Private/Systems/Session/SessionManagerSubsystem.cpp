// MetalSlug01. All Rights Reserved.
#include "Systems/Session/SessionManagerSubsystem.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Kismet/GameplayStatics.h"
// 【Bug2 修复】BroadcastRoomPlayerCountStatic 静态入口需要 World/GameInstance 解析
#include "Engine/World.h"
#include "Engine/GameInstance.h"

// ==========================================
// 模块一：子系统生命周期
// ==========================================

void USessionManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ---- 获取在线子系统 ----
	OnlineSubsystem = IOnlineSubsystem::Get();
	if (OnlineSubsystem)
	{
		Sessions = OnlineSubsystem->GetSessionInterface();
	}

	UE_LOG(LogTemp, Warning, TEXT("[SessionManager] Initialize 完成"));
}

void USessionManagerSubsystem::Deinitialize()
{
	// ---- 清理所有委托句柄 ----
	ClearAllDelegateHandles();

	Super::Deinitialize();

	UE_LOG(LogTemp, Warning, TEXT("[SessionManager] Deinitialize 完成"));
}

// ==========================================
// 模块二：公开 API 实现
// ==========================================

void USessionManagerSubsystem::FindRooms(const FOnFindRoomsComplete& Delegate)
{
	if (!GetSessionInterface().IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[SessionManager] FindRooms 失败: SessionInterface 无效"));
		TArray<FRoomSessionResult> EmptyResults;
		Delegate.ExecuteIfBound(false, EmptyResults);
		return;
	}

	// ==========================================
	// 【大厂标准 - P0 修复】搜索锁保护: 同步回调放行
	// ==========================================
	// 原因: 大厅定时器每 3 秒一次搜索; 当玩家点创房时, 同号检查
	//       会再次调用 FindRooms. 如果定时器搜索正在进行, 旧逻辑会
	//       默默 return, 导致 View 的创房回调永远收不到 → 创房卡死.
	//
	// 大厂设计: 调用方不应该因为底层锁就永远得不到回调.
	// 方案: 锁住时**立即同步**回调 Delegate (空结果, bWasSuccessful=true
	//       表示 "成功拿到了结果, 只是结果是空"), 这样:
	//         1. 创房流程不会被卡死 (View 立刻收到回调, 进入创房)
	//         2. 定时器搜索不受影响 (它是另一条 PendingFindDelegate 链路)
	//         3. 同号检查本来就是防御性的, 错过一次也无妨 (下一次搜索会捕获)
	//
	// 注: SessionManager 本身已标记 bIsHost, 即便有同号建房, 后续
	//     ProceedToCreateRoomAfterCheck 里的 IsHosting 检查也会兜底.
	// ==========================================
	if (bIsSearching)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SessionManager] FindRooms 锁住: 当前搜索正在进行, 同步回调空结果放行"));
		TArray<FRoomSessionResult> EmptyResults;
		Delegate.ExecuteIfBound(true, EmptyResults);  // true=成功 (但结果空)
		return;
	}

	// ---- 保存回调 ----
	PendingFindDelegate = Delegate;

	// ---- 注销旧句柄并绑定新回调 ----
	IOnlineSessionPtr SessionPtr = GetSessionInterface();
	SessionPtr->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsHandle);
	FindSessionsHandle = SessionPtr->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &USessionManagerSubsystem::HandleFindSessionsComplete)
	);

	// ---- 配置搜索参数 ----
	SessionSearch = MakeShareable(new FOnlineSessionSearch());
	SessionSearch->bIsLanQuery = true;
	SessionSearch->MaxSearchResults = 20;
	SessionSearch->TimeoutInSeconds = 2.0f;
	SessionSearch->QuerySettings.Set(FName("PRESENCESEARCH"), true, EOnlineComparisonOp::Equals);

	bIsSearching = true;

	// ---- 发起搜索 ----
	SessionPtr->FindSessions(0, SessionSearch.ToSharedRef());
}

void USessionManagerSubsystem::CreateRoom(const FRoomCreationParams& Params, const FOnCreateRoomComplete& Delegate)
{
	if (!GetSessionInterface().IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[SessionManager] CreateRoom 失败: SessionInterface 无效"));
		Delegate.ExecuteIfBound(false, TEXT("会话系统不可用"));
		return;
	}

	// ---- 保存回调 ----
	PendingCreateDelegate = Delegate;

	// ---- 注销旧句柄并绑定新回调 ----
	IOnlineSessionPtr SessionPtr = GetSessionInterface();
	SessionPtr->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionHandle);
	CreateSessionHandle = SessionPtr->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &USessionManagerSubsystem::HandleCreateSessionComplete)
	);

	// ---- 构建会话设置 ----
	CurrentSessionSettings = BuildSessionSettings(Params);

	// ---- 发起创建 ----
	SessionPtr->CreateSession(0, NAME_GameSession, *CurrentSessionSettings);
}

void USessionManagerSubsystem::JoinRoom(const FRoomSessionResult& Room, const FOnJoinRoomComplete& Delegate)
{
	if (!GetSessionInterface().IsValid())
	{
		Delegate.ExecuteIfBound(false, TEXT("会话系统不可用"));
		return;
	}

	if (!Room.RawSearchResult.IsValid() || !Room.RawSearchResult->IsValid())
	{
		Delegate.ExecuteIfBound(false, TEXT("房间数据无效"));
		return;
	}

	// ---- 保存回调 ----
	PendingJoinDelegate = Delegate;

	// ---- 注销旧句柄并绑定新回调 ----
	IOnlineSessionPtr SessionPtr = GetSessionInterface();
	SessionPtr->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionHandle);
	JoinSessionHandle = SessionPtr->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &USessionManagerSubsystem::HandleJoinSessionComplete)
	);

	// ---- 发起加入 ----
	SessionPtr->JoinSession(0, NAME_GameSession, *Room.RawSearchResult);
}

void USessionManagerSubsystem::DestroyRoom(const FOnDestroyRoomComplete& Delegate)
{
	if (!GetSessionInterface().IsValid())
	{
		Delegate.ExecuteIfBound(false, TEXT("会话系统不可用"));
		return;
	}

	// ---- 保存回调 ----
	PendingDestroyDelegate = Delegate;

	// ---- 注销旧句柄并绑定新回调 ----
	IOnlineSessionPtr SessionPtr = GetSessionInterface();
	SessionPtr->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionHandle);
	DestroySessionHandle = SessionPtr->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &USessionManagerSubsystem::HandleDestroySessionComplete)
	);

	// ---- 发起销毁 ----
	SessionPtr->DestroySession(NAME_GameSession);
}

bool USessionManagerSubsystem::IsAccountInAnyRoom(const TArray<FRoomSessionResult>& Rooms, const FString& AccountName) const
{
	for (const auto& Room : Rooms)
	{
		if (Room.HostAccount == AccountName)
		{
			return true;
		}
	}
	return false;
}

// ==========================================
// 模块三：内部回调实现
// ==========================================

void USessionManagerSubsystem::HandleFindSessionsComplete(bool bWasSuccessful)
{
	// ---- 注销回调句柄 ----
	IOnlineSessionPtr SessionPtr = GetSessionInterface();
	if (SessionPtr.IsValid())
	{
		SessionPtr->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsHandle);
	}

	bIsSearching = false;

	// ---- 解析结果 ----
	TArray<FRoomSessionResult> ParsedRooms;
	if (bWasSuccessful && SessionSearch.IsValid())
	{
		ParsedRooms = ParseSearchResults();
	}

	// ---- 触发回调 ----
	PendingFindDelegate.ExecuteIfBound(bWasSuccessful, ParsedRooms);
	OnRoomsFound.Broadcast(ParsedRooms);
}

void USessionManagerSubsystem::HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	// ---- 注销回调句柄 ----
	IOnlineSessionPtr SessionPtr = GetSessionInterface();
	if (SessionPtr.IsValid())
	{
		SessionPtr->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionHandle);
	}

	if (bWasSuccessful)
	{
		bIsHost = true;
		bIsInSession = true;
		PendingCreateDelegate.ExecuteIfBound(true, TEXT(""));
	}
	else
	{
		PendingCreateDelegate.ExecuteIfBound(false, TEXT("创建房间失败"));
	}
}

void USessionManagerSubsystem::HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	// ---- 注销回调句柄 ----
	IOnlineSessionPtr SessionPtr = GetSessionInterface();
	if (SessionPtr.IsValid())
	{
		SessionPtr->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionHandle);
	}

	// 【Bug 修复】只有在 Join 真正成功时才标记 bIsInSession=true
	// 之前: 失败分支也设了 true, 导致 bIsInSession 状态错乱（后续 IsInSession() 检查失效）
	if (Result == EOnJoinSessionCompleteResult::Success)
	{
		bIsInSession = true;

		// ---- 获取连接字符串 ----
		FString ConnectString;
		if (SessionPtr.IsValid())
		{
			SessionPtr->GetResolvedConnectString(NAME_GameSession, ConnectString);
		}

		PendingJoinDelegate.ExecuteIfBound(true, ConnectString);
	}
	else
	{
		FString ErrorMsg;
		bool bShouldBroadcastTermination = false; // 【大厂 P0】是否触发 OnSessionTerminated

		switch (Result)
		{
		case EOnJoinSessionCompleteResult::SessionIsFull:
			ErrorMsg = TEXT("房间已满");
			bShouldBroadcastTermination = true; // 房间已满也算"无法进入", 触发回退到大厅
			break;
		case EOnJoinSessionCompleteResult::SessionDoesNotExist:
			ErrorMsg = TEXT("房间已不存在");
			bShouldBroadcastTermination = true; // 【大厂 P0】关键场景: Host 已退房, 目标 Session 已消失
			break;
		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
			ErrorMsg = TEXT("无法获取房间地址");
			bShouldBroadcastTermination = true; // Session 残留但地址无效, 触发回退
			break;
		default:
			ErrorMsg = TEXT("加入房间失败");
			// 其他错误 (UnknownError / AlreadyInSession) 不触发 — 这些是本地状态问题, 与 Session 终止无关
			break;
		}

		// 【大厂 P0】Session 终止广播 — 让业务方做统一 UI 中断
		if (bShouldBroadcastTermination)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[SessionManager] JoinSession 失败 (%s), 广播 OnSessionTerminated(MainLobby)"),
				*ErrorMsg);
			OnSessionTerminated.Broadcast(EMatchState::MainLobby);
		}

		PendingJoinDelegate.ExecuteIfBound(false, ErrorMsg);
	}
}

void USessionManagerSubsystem::HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	// ---- 注销回调句柄 ----
	IOnlineSessionPtr SessionPtr = GetSessionInterface();
	if (SessionPtr.IsValid())
	{
		SessionPtr->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionHandle);
	}

	if (bWasSuccessful)
	{
		bIsHost = false;
		bIsInSession = false;

		// 【大厂 P0 修复】业务方订阅此事件, 触发跨地图统一 UI 中断链路
		// 触发场景: Host 主动 DestroyRoom 成功（房主退出房间 / 房主关闭房间 / Session 超时）
		// 业务方链路: GameFlow.HandleSessionTerminated → OnInterrupted.Broadcast(LANRoom)
		UE_LOG(LogTemp, Log, TEXT("[SessionManager] DestroySession 成功, 广播 OnSessionTerminated(MainLobby)"));
		OnSessionTerminated.Broadcast(EMatchState::MainLobby);

		PendingDestroyDelegate.ExecuteIfBound(true, TEXT(""));
	}
	else
	{
		PendingDestroyDelegate.ExecuteIfBound(false, TEXT("销毁房间失败"));
	}
}

// ==========================================
// 模块四：工具函数实现
// ==========================================

void USessionManagerSubsystem::ClearAllDelegateHandles()
{
	IOnlineSessionPtr SessionPtr = GetSessionInterface();
	if (SessionPtr.IsValid())
	{
		SessionPtr->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsHandle);
		SessionPtr->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionHandle);
		SessionPtr->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionHandle);
		SessionPtr->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionHandle);
	}
}

IOnlineSessionPtr USessionManagerSubsystem::GetSessionInterface() const
{
	if (OnlineSubsystem)
	{
		return OnlineSubsystem->GetSessionInterface();
	}
	return nullptr;
}

bool USessionManagerSubsystem::GetCurrentSessionDisplayInfo(FString& OutRoomName, FString& OutGameMode) const
{
	OutRoomName.Reset();
	OutGameMode.Reset();

	// 【v54.5.1 P0 修复】优先返回 skip-login 测试房间名
	//   根因: bSkipLoginDirectToLobby=true 时 SessionManager 不创建真实 Session,
	//          SessionSettings 里 ROOM_NAME/GAME_MODE 都是空的
	//          但 SkipLoginRoomName/SkipLoginGameMode 已在 BootToLogin 时写入
	//   单一职责: GetCurrentSessionDisplayInfo 是"skip-login 专用 API + 正常 Session 共用查询" 统一入口
	//             不需要调用方区分"是不是 skip-login", 直接在这里处理
	if (!SkipLoginRoomName.IsEmpty())
	{
		OutRoomName = SkipLoginRoomName;
		OutGameMode = SkipLoginGameMode;
		return true;
	}

	// 正常 Session 路径
	IOnlineSessionPtr SessionPtr = GetSessionInterface();
	if (!SessionPtr.IsValid())
	{
		return false;
	}

	FNamedOnlineSession* Session = SessionPtr->GetNamedSession(NAME_GameSession);
	if (!Session)
	{
		return false;
	}

	Session->SessionSettings.Get(FName("ROOM_NAME"), OutRoomName);
	Session->SessionSettings.Get(FName("GAME_MODE"), OutGameMode);
	return true;
}

void USessionManagerSubsystem::SetSkipLoginRoomDisplayInfo(const FString& InRoomName, const FString& InGameMode)
{
	SkipLoginRoomName = InRoomName;
	SkipLoginGameMode = InGameMode;
	UE_LOG(LogTemp, Log,
		TEXT("[SessionManager] SetSkipLoginRoomDisplayInfo: RoomName=[%s] GameMode=[%s]"),
		*InRoomName, *InGameMode);
}

void USessionManagerSubsystem::ResetSkipLoginRoomDisplayInfo()
{
	SkipLoginRoomName.Reset();
	SkipLoginGameMode.Reset();
}

TSharedPtr<FOnlineSessionSettings> USessionManagerSubsystem::BuildSessionSettings(const FRoomCreationParams& Params)
{
	TSharedPtr<FOnlineSessionSettings> Settings = MakeShareable(new FOnlineSessionSettings());

	Settings->bIsLANMatch = true;
	Settings->bIsDedicated = false;
	Settings->bShouldAdvertise = true;
	Settings->bUseLobbiesIfAvailable = false;
	Settings->NumPublicConnections = Params.MaxPlayers;
	Settings->NumPrivateConnections = 0;

	// ==========================================
	// 【P0 修复】房间信息字段的广告类型: DontAdvertise → ViaOnlineServiceAndPing
	//
	// 根因:
	//   - 原代码使用 EOnlineDataAdvertisementType::DontAdvertise
	//   - 该类型会让 UE 引擎在 LAN Beacon 数据包中**剔除**该字段
	//   - 客户端 FindSessions 拿到的 SessionResult 中根本没有这些字段
	//   - 客户端 SessionSettings.Get("ROOM_NAME") 返回空字符串
	//   - 客户端 UI 显示空房间名 + 按钮永远禁用
	//
	// 修复: 改为 ViaOnlineServiceAndPing (最广广播, LAN + Online 都覆盖)
	// 设计权衡:
	//   - DontAdvertise 用于密码等敏感字段
	//   - 房间名/地图/游戏模式/房主账号都是公开信息, 必须广播
	// ==========================================

	// ---- 房间信息 ----
	Settings->Set(FName("ROOM_NAME"), Params.RoomName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings->Set(FName("GAME_MODE"), Params.GameMode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings->Set(FName("MAP_NAME"), Params.MapName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings->Set(FName("HOST_ACCOUNT"), Params.HostAccount, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// ---- 房间状态 ----
	Settings->Set(FName("ROOM_STATE"), false, EOnlineDataAdvertisementType::ViaOnlineService);
	Settings->Set(FName("TOTAL_PLAYERS_WITH_AI"), 1, EOnlineDataAdvertisementType::ViaOnlineService);

	return Settings;
}

TArray<FRoomSessionResult> USessionManagerSubsystem::ParseSearchResults() const
{
	TArray<FRoomSessionResult> Results;

	if (!SessionSearch.IsValid()) return Results;

	for (const auto& SearchResult : SessionSearch->SearchResults)
	{
		if (SearchResult.IsValid())
		{
			Results.Add(FRoomSessionResult(SearchResult));
		}
	}

	return Results;
}


// ==========================================
// 【P0 大厂架构】房间人数实时广播 (Bug2 修复)
// ==========================================

/**
 * USessionManagerSubsystem::BroadcastRoomPlayerCount
 *
 * 房主端专用: 把 TotalPlayersWithAI 写入 SessionSettings 并 UpdateSession 广播到 LAN
 *
 * 设计:
 *   - 只在 IsHosting() 时生效, 客户端调用被静默忽略 (RPC 错误溯源友好)
 *   - FMath::Clamp 兜底: 数据被异常值污染 (负数 / 超上限) 也不破坏 Session
 *   - UpdateSession 后, 其他客户端的下一次 FindSessions 回调里就能拿到最新值
 *     (LAN Beacon 每秒发一次, 最坏情况 1s 内同步)
 *
 * 与 AccountRoomAuthority 的关系:
 *   AccountAuthority 在玩家登录/登出时调本接口
 *   RoomInsidePage (房主端) 在 RefreshRoomUI 检测到 AI 数变化时也调
 */
bool USessionManagerSubsystem::BroadcastRoomPlayerCount(int32 TotalPlayersWithAI)
{
	// 1. 安全护栏: 非房主端/无活跃会话直接 return
	if (!bIsHost)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[SessionManager] BroadcastRoomPlayerCount: 非房主端, 忽略 (Total=%d)"),
			TotalPlayersWithAI);
		return false;
	}

	IOnlineSessionPtr SessionPtr = GetSessionInterface();
	if (!SessionPtr.IsValid())
	{
		return false;
	}

	FNamedOnlineSession* NamedSession = SessionPtr->GetNamedSession(NAME_GameSession);
	if (!NamedSession)
	{
		return false;
	}

	// 2. 数据校验: 必须 >= 1 (房主自己), 不能超过 NumPublicConnections
	const int32 MaxPlayers = FMath::Max(1, NamedSession->SessionSettings.NumPublicConnections);
	const int32 SafeTotal = FMath::Clamp(TotalPlayersWithAI, 1, MaxPlayers);

	// 3. 写入 SessionSettings
	NamedSession->SessionSettings.Set(
		FName("TOTAL_PLAYERS_WITH_AI"),
		SafeTotal,
		EOnlineDataAdvertisementType::ViaOnlineService);

	// 4. 广播更新
	SessionPtr->UpdateSession(NAME_GameSession, NamedSession->SessionSettings, true);

	UE_LOG(LogTemp, Log,
		TEXT("[SessionManager] BroadcastRoomPlayerCount: 推送 TOTAL_PLAYERS_WITH_AI=%d (Max=%d)"),
		SafeTotal, MaxPlayers);

	return true;
}


/**
 * 静态入口: 给游戏代码提供 WorldContext-友好的快捷方式
 *
 * @param WorldContextObject 任何 UObject* (this 也行), 用于定位 GameInstance
 * @param TotalPlayersWithAI 推送的总人数
 */
bool USessionManagerSubsystem::BroadcastRoomPlayerCountStatic(const UObject* WorldContextObject, int32 TotalPlayersWithAI)
{
	if (!WorldContextObject) return false;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World) return false;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return false;
	if (USessionManagerSubsystem* Mgr = GI->GetSubsystem<USessionManagerSubsystem>())
	{
		return Mgr->BroadcastRoomPlayerCount(TotalPlayersWithAI);
	}
	return false;
}
