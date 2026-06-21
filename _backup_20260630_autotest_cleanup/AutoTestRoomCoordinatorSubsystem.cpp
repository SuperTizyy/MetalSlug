// MetalSlug01. All Rights Reserved.

// ==========================================
// 头文件包含区
// ==========================================
#include "Systems/AutoTestRoomCoordinatorSubsystem.h"
#include "Systems/Session/SessionManagerSubsystem.h"
#include "Systems/GameFlowSubsystem.h"
#include "Systems/Account/AccountSubsystem.h"
#include "Tools/MetalSlugTestSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
// 【大厂架构 - 读取 GameInstance 缓存的 PIE Instance Index 2026.06.30】
#include "MetalSlugGameInstance.h"

// 【大厂架构 - 从 WorldContext 获取 PIEInstance 2026.06.30】
#include "Engine/Engine.h"

// ==========================================
// 【大厂架构 - 文件日志探针 2026.06.30】
// 解决: PIE 子进程早期 LogTemp 丢失问题, 全部 [AutoTestRoom] 日志同步落盘
// 落地路径: <项目 Saved>/AutoTestRoom.log (全局追加, 不论哪个 PIE 进程)
// ==========================================
static void AutoTestRoomLogToFile(const FString& Message)
{
	const FString LogFilePath = FPaths::ProjectSavedDir() / TEXT("AutoTestRoom.log");
	const FString Line = FString::Printf(TEXT("[%s][PID=%u] %s\n"),
		*FDateTime::Now().ToString(),
		FPlatformProcess::GetCurrentProcessId(),
		*Message);
	FFileHelper::SaveStringToFile(
		FString::Printf(TEXT("%s"), *Line),
		*LogFilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(),
		FILEWRITE_Append);
}

// ==========================================
// 子系统生命周期
// ==========================================

void UAutoTestRoomCoordinatorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const FString Msg = FString::Printf(
		TEXT("[AutoTestRoom] Subsystem::Initialize 触发 (PID=%u)"),
		FPlatformProcess::GetCurrentProcessId());
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	AutoTestRoomLogToFile(Msg);

	// ==========================================
	// 【大厂架构 - World 热切换监听 2026.06.30】
	// 问题: PIE 子进程在 L_Login 地图加载时触发 World 热切换
	//   BeginTearingDown(OldWorld) → LoadMap(NewWorld) → TimerManager 被清空
	//   导致 StartAutoTestRoom 设置的 timer 从未触发
	//
	// 方案:
	//   - OnWorldCleanup: World 被清理前保存当前 timer 参数 (bIsHost, Delay)
	//   - OnPostWorldInitialization: 新 World 初始化后检查是否有待重启参数
	//     → 有则在新 World 的 TimerManager 上重新 SetTimer
	//
	// 时序（PIE ListenServer）:
	//   1. Subsystem::Initialize (此时 World=Untitled)
	//   2. StartAutoTestRoom (在 BootToLogin 中) 设置 timer
	//   3. WorldCleanup(Old=Untitled, bSessionEnded=true)
	//   4. LoadMap(L_Login) 加载新地图
	//   5. PostWorldInit(New=L_Login) → 重启 timer
	//   6. Timer 在新 World 的 TimerManager 上正常触发 ✅
	// ==========================================
	OnWorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
		this, &UAutoTestRoomCoordinatorSubsystem::OnWorldCleanup);
	OnPostWorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(
		this, &UAutoTestRoomCoordinatorSubsystem::OnPostWorldInitialization);

	{
		FString Msg2 = FString::Printf(TEXT("[AutoTestRoom] World 事件监听已注册"));
		UE_LOG(LogTemp, Log, TEXT("%s"), *Msg2);
		AutoTestRoomLogToFile(Msg2);
	}
}

void UAutoTestRoomCoordinatorSubsystem::Deinitialize()
{
	const FString Msg = TEXT("[AutoTestRoom] Subsystem::Deinitialize 触发");
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	AutoTestRoomLogToFile(Msg);

	// 移除 World 事件监听
	FWorldDelegates::OnWorldCleanup.Remove(OnWorldCleanupHandle);
	FWorldDelegates::OnPostWorldInitialization.Remove(OnPostWorldInitHandle);

	InternalStop();
	Super::Deinitialize();
}

// ==========================================
// 公共接口
// ==========================================

int32 UAutoTestRoomCoordinatorSubsystem::GetMyGameInstanceIndex() const
{
	// ==========================================
	// 【大厂架构 - 跨模式 PIE 实例识别 2026.06.30】
	// ==========================================
	// 【方案历史】
	//   v1: ULocalPlayer::GetIndexInGameInstance()  → 跨进程 PIE 失效 (永远=0)
	//   v2: FParse::Value("-PIEWindowID=")            → Run Under One Process=true 才有效
	//   v3: UMetalSlugGameInstance 缓存 PIEInstanceIndex
	//        → InitializeForPlayInEditor 在跨进程 PIE 子进程不被调用
	//   v4 (当前): 直接读 WorldContext->PIEInstance
	//      → 通过 StartPlayInEditorGameInstance 缓存后, AutoTestRoom 可直接读
	//      → 同时 GetWorldContextFromWorld(GetWorld()) 作为实时来源
	//
	// 【PIEInstance 分配规则】
	//   - UE 编辑器在 StartPlayInEditorSession 时按实例顺序分配 0,1,2,3...
	//   - Editor 内 ListenServer 通常是 0, Client 依次 1, 2, 3...
	//   - Run Under One Process=false: 每个独立进程都各自拿到唯一编号 (0,1,2)
	//   - 非 PIE 环境: INDEX_NONE
	//
	// 【返回值含义】
	//   - 0: 第一个 PIE 实例 (Server / ListenServer) → 房主
	//   - 1,2,3...: 后续 PIE 实例 (Clients)        → 加入者
	//   - INDEX_NONE: 非 PIE 环境, 由调用方按"默认房主"处理
	// ==========================================

	// 第一优先: 从 GameInstance 缓存读取（StartPlayInEditorGameInstance 中已缓存）
	if (const UMetalSlugGameInstance* GI = Cast<UMetalSlugGameInstance>(GetGameInstance()))
	{
		const int32 PIEIdx = GI->GetCachedPIEInstanceIndex();
		if (PIEIdx != INDEX_NONE)
		{
			return PIEIdx;
		}
	}

	// 兜底: 从 WorldContext 实时读取（跨 Editor 和子进程都有效）
	if (UWorld* World = GetWorld())
	{
		if (const FWorldContext* WC = GEngine->GetWorldContextFromWorld(World))
		{
			const int32 PIEIdx = WC->PIEInstance;
			UE_LOG(LogTemp, Log,
				TEXT("[AutoTestRoom] GetMyGameInstanceIndex: WorldContext->PIEInstance=%d (实时读取)"),
				PIEIdx);
			return PIEIdx;
		}
	}

	// 极端兜底: 命令行 -PIEWindowID
	FString PIEValue;
	if (FParse::Value(FCommandLine::Get(), TEXT("-PIEWindowID="), PIEValue))
	{
		const int32 PIEWndId = FCString::Atoi(*PIEValue);
		UE_LOG(LogTemp, Warning,
			TEXT("[AutoTestRoom] 退回到 -PIEWindowID 兜底: %d"),
			PIEWndId);
		return PIEWndId;
	}

	// 非 PIE 环境
	UE_LOG(LogTemp, Warning,
		TEXT("[AutoTestRoom] GetMyGameInstanceIndex: 无法获取 PIE Instance, 返回 INDEX_NONE"));
	return INDEX_NONE;
}

void UAutoTestRoomCoordinatorSubsystem::StartAutoTestRoom(const FString& InTestRoomName)
{
	if (bIsRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AutoTestRoom] 已在运行中, 忽略重复启动"));
		return;
	}

	TargetRoomName = InTestRoomName;
	bIsRunning = true;

	// ==========================================
	// 解析 PIEInstanceIndex (跨进程唯一编号, UE 官方通过 InitializeForPlayInEditor 注入)
	// ==========================================
	CachedPIEWindowID = GetMyGameInstanceIndex();
	const FString StartMsg = FString::Printf(
		TEXT("[AutoTestRoom] StartAutoTestRoom 触发: PID=%u, PIEInstanceIndex=%d, 目标房间=%s"),
		FPlatformProcess::GetCurrentProcessId(),
		CachedPIEWindowID,
		*TargetRoomName);
	UE_LOG(LogTemp, Log, TEXT("%s"), *StartMsg);
	AutoTestRoomLogToFile(StartMsg);

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[AutoTestRoom] World 不可用, 启动失败"));
		AutoTestRoomLogToFile(TEXT("[AutoTestRoom] World 不可用, 启动失败"));
		bIsRunning = false;
		return;
	}

	// 订阅 SessionManager 的 OnRoomsFound (一次性绑定)
	if (!bIsDelegateBound)
	{
		if (USessionManagerSubsystem* SessionMgr = GetSessionManager())
		{
			SessionMgr->OnRoomsFound.AddDynamic(this, &UAutoTestRoomCoordinatorSubsystem::OnRoomsFound);
			bIsDelegateBound = true;
			UE_LOG(LogTemp, Log, TEXT("[AutoTestRoom] 已订阅 SessionManager::OnRoomsFound"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[AutoTestRoom] SessionManager 不可用, 启动失败"));
			bIsRunning = false;
			return;
		}
	}

	// ==========================================
	// 按 PIE Instance Index 指定角色
	// ==========================================
	// PIEInstanceIndex=0: 第一个实例 → Server (ListenServer) → 房主
	// PIEInstanceIndex>0: 后续实例 → Client → 加入者
	// 非 PIE 环境 (INDEX_NONE) → 默认当房主
	// ==========================================
	const bool bIsHost = (CachedPIEWindowID == 0) || (CachedPIEWindowID == INDEX_NONE);
	if (bIsHost)
	{
		// ---- 房主: 延迟后创房 ----
		UE_LOG(LogTemp, Log,
			TEXT("[AutoTestRoom] [PIEInstanceIndex=%d] 房主, 将在 %.1fs 后创建房间"),
			CachedPIEWindowID,
			HostDelaySeconds);

		World->GetTimerManager().SetTimer(
			HostDelayTimerHandle, this,
			&UAutoTestRoomCoordinatorSubsystem::OnHostDelayTimerFired,
			HostDelaySeconds, false);
	}
	else
	{
		// ---- 加入者: 延迟后开始搜索 ----
		// 给 Server 留出建房间时间 + 网络广播扩散
		const float InitialDelay = CachedPIEWindowID * PerClientStaggerSeconds + 1.0f;
		UE_LOG(LogTemp, Log,
			TEXT("[AutoTestRoom] [PIEInstanceIndex=%d] 加入者, 将在 %.1fs 后开始搜索 + 加入"),
			CachedPIEWindowID, InitialDelay);

		World->GetTimerManager().SetTimer(
			SearchTimerHandle, this,
			&UAutoTestRoomCoordinatorSubsystem::OnFirstSearchTimerFired,
			InitialDelay, false);
	}
}

void UAutoTestRoomCoordinatorSubsystem::StopAutoTestRoom()
{
	InternalStop();
}

void UAutoTestRoomCoordinatorSubsystem::InternalStop()
{
	if (!bIsRunning)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[AutoTestRoom] 停止协调"));

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HostDelayTimerHandle);
		World->GetTimerManager().ClearTimer(SearchTimerHandle);
	}

	if (bIsDelegateBound)
	{
		if (USessionManagerSubsystem* SessionMgr = GetSessionManager())
		{
			SessionMgr->OnRoomsFound.RemoveDynamic(this, &UAutoTestRoomCoordinatorSubsystem::OnRoomsFound);
		}
		bIsDelegateBound = false;
	}

	bIsRunning = false;
}

// ==========================================
// World 热切换保护 (2026.06.30)
// ==========================================

void UAutoTestRoomCoordinatorSubsystem::OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources)
{
	if (!bIsRunning)
	{
		return;
	}

	// 仅当 World 即将被清理且有活跃 timer 时才保存状态
	UWorld* MyWorld = GetWorld();
	if (!MyWorld || InWorld != MyWorld)
	{
		return;
	}

	// 检查是否有活跃的 timer
	FTimerManager& TM = MyWorld->GetTimerManager();
	const bool bHasHostTimer = TM.IsTimerActive(HostDelayTimerHandle);
	const bool bHasSearchTimer = TM.IsTimerActive(SearchTimerHandle);

	if (!bHasHostTimer && !bHasSearchTimer)
	{
		// timer 还没设置或已触发，无需处理
		return;
	}

	// 保存当前 timer 参数
	bPendingIsHost = (CachedPIEWindowID == 0) || (CachedPIEWindowID == INDEX_NONE);
	if (bHasHostTimer)
	{
		// Host: 完整延迟 = HostDelaySeconds
		PendingDelay = HostDelaySeconds;
	}
	else
	{
		// Searcher: 完整延迟 = Index * 0.5s + 1.0s
		PendingDelay = CachedPIEWindowID * PerClientStaggerSeconds + 1.0f;
	}

	bHasPendingTimerParams = true;

	const FString Msg = FString::Printf(
		TEXT("[AutoTestRoom] OnWorldCleanup: World=%s bSessionEnded=%d 挂起 timer (Host=%d, Delay=%.2fs)"),
		*InWorld->GetMapName(), bSessionEnded ? 1 : 0, bPendingIsHost ? 1 : 0, PendingDelay);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	AutoTestRoomLogToFile(Msg);
}

void UAutoTestRoomCoordinatorSubsystem::OnPostWorldInitialization(
	UWorld* InWorld,
	const UWorld::InitializationValues IVS)
{
	// 【大厂架构 - 懒更新 PIEInstanceIndex 2026.06.30】
	// 在 World 切换完成后，从 WorldContext 读取最新的 PIEInstance
	// 这解决 InitializeForPlayInEditor 在子进程不被调用导致的 INDEX_NONE 问题
	if (UWorld* MyWorld = GetWorld())
	{
		if (const FWorldContext* WC = GEngine->GetWorldContextFromWorld(MyWorld))
		{
			if (WC->PIEInstance != INDEX_NONE)
			{
				CachedPIEWindowID = WC->PIEInstance;
				const FString IdxMsg = FString::Printf(
					TEXT("[AutoTestRoom] OnPostWorldInit: 更新 CachedPIEWindowID=%d (来自 WorldContext)"),
					WC->PIEInstance);
				UE_LOG(LogTemp, Log, TEXT("%s"), *IdxMsg);
				AutoTestRoomLogToFile(IdxMsg);
			}
		}
	}

	if (!bIsRunning || !bHasPendingTimerParams)
	{
		return;
	}

	// 确保是新世界的 TimerManager
	UWorld* MyWorld = GetWorld();
	if (!MyWorld || InWorld != MyWorld)
	{
		return;
	}

	const FString Msg = FString::Printf(
		TEXT("[AutoTestRoom] OnPostWorldInitialization: World=%s 重启挂起 timer (Host=%d, Delay=%.2fs)"),
		*InWorld->GetMapName(), bPendingIsHost ? 1 : 0, PendingDelay);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	AutoTestRoomLogToFile(Msg);

	// 【P0】先取值再清标志（避免 race condition）
	const bool bWasHost = bPendingIsHost;
	const float Delay = PendingDelay;
	bHasPendingTimerParams = false;

	// 在新 World 上重新订阅 OnRoomsFound（如果之前绑定过）
	if (!bIsDelegateBound)
	{
		if (USessionManagerSubsystem* SessionMgr = GetSessionManager())
		{
			SessionMgr->OnRoomsFound.AddDynamic(this, &UAutoTestRoomCoordinatorSubsystem::OnRoomsFound);
			bIsDelegateBound = true;
			UE_LOG(LogTemp, Log, TEXT("[AutoTestRoom] OnPostWorldInit: 重新订阅 SessionManager::OnRoomsFound"));
		}
	}

	// 【大厂架构 - 文件落盘探针 2026.06.30】
	{
		FString Msg2 = FString::Printf(
			TEXT("[AutoTestRoom] OnPostWorldInit: timer 重启成功 (Host=%d, Delay=%.1fs)"),
			bWasHost ? 1 : 0, Delay);
		UE_LOG(LogTemp, Log, TEXT("%s"), *Msg2);
		AutoTestRoomLogToFile(Msg2);
	}

	// 重启 timer（用完整延迟重新倒计时，确保新 World 上行为一致）
	if (bWasHost)
	{
		MyWorld->GetTimerManager().SetTimer(
			HostDelayTimerHandle, this,
			&UAutoTestRoomCoordinatorSubsystem::OnHostDelayTimerFired,
			Delay, false);
	}
	else
	{
		MyWorld->GetTimerManager().SetTimer(
			SearchTimerHandle, this,
			&UAutoTestRoomCoordinatorSubsystem::OnFirstSearchTimerFired,
			Delay, false);
	}
}

// ==========================================
// 状态机回调
// ==========================================

void UAutoTestRoomCoordinatorSubsystem::OnHostDelayTimerFired()
{
	if (!bIsRunning)
	{
		return;
	}
	const FString Msg = FString::Printf(
		TEXT("[AutoTestRoom] [PIEInstanceIndex=%d] ★ OnHostDelayTimerFired: 延迟结束, 开始创建房间"),
		CachedPIEWindowID);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	AutoTestRoomLogToFile(Msg);
	HostAutoTestRoom();
}

void UAutoTestRoomCoordinatorSubsystem::OnFirstSearchTimerFired()
{
	if (!bIsRunning)
	{
		return;
	}
	const FString Msg = FString::Printf(
		TEXT("[AutoTestRoom] [PIEInstanceIndex=%d] ★ OnFirstSearchTimerFired: 首次搜索触发, 房间名=%s"),
		CachedPIEWindowID, *TargetRoomName);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	AutoTestRoomLogToFile(Msg);
	TriggerFindRooms();
}

void UAutoTestRoomCoordinatorSubsystem::OnSearchTimerFired()
{
	if (!bIsRunning)
	{
		return;
	}
	TriggerFindRooms();
}

void UAutoTestRoomCoordinatorSubsystem::OnRoomsFound(const TArray<FRoomSessionResult>& Rooms)
{
	if (!bIsRunning)
	{
		return;
	}

	// 找到目标房间 → Join
	FRoomSessionResult TargetRoom;
	if (FindTargetRoomInResults(Rooms, TargetRoom))
	{
	UE_LOG(LogTemp, Log,
		TEXT("[AutoTestRoom] [PIEInstanceIndex=%d] 发现目标房间 [%s], 房主=%s, 开始加入..."),
		CachedPIEWindowID, *TargetRoomName, *TargetRoom.HostAccount);

		USessionManagerSubsystem* SessionMgr = GetSessionManager();
		if (!SessionMgr)
		{
			UE_LOG(LogTemp, Error, TEXT("[AutoTestRoom] SessionManager 不可用, 无法 Join"));
			return;
		}

		FOnJoinRoomComplete Delegate;
		Delegate.BindDynamic(this, &UAutoTestRoomCoordinatorSubsystem::OnJoinRoomComplete);
		SessionMgr->JoinRoom(TargetRoom, Delegate);
		return;
	}

	// 未找到 → 继续轮询
	UE_LOG(LogTemp, Verbose,
		TEXT("[AutoTestRoom] [PIEInstanceIndex=%d] 未找到房间, 继续轮询..."),
		CachedPIEWindowID);
}

void UAutoTestRoomCoordinatorSubsystem::OnCreateRoomComplete(bool bWasSuccessful, const FString& ErrorMessage)
{
	if (!bIsRunning)
	{
		return;
	}

	if (bWasSuccessful)
	{
		UE_LOG(LogTemp, Log,
		TEXT("[AutoTestRoom] [PIEInstanceIndex=%d] 房间创建成功, 跳转地图"),
		CachedPIEWindowID);

		// 读取目标地图
		FString TargetMap = TEXT("L_Room");
		if (const UMetalSlugTestSettings* Settings = GetDefault<UMetalSlugTestSettings>())
		{
			TargetMap = Settings->AutoTestRoomMapName;
		}

		// OpenLevel 带 ?listen, 以 Host 身份进入
		UGameplayStatics::OpenLevel(this, FName(*TargetMap), true, TEXT("?listen"));
	}
	else
	{
		UE_LOG(LogTemp, Error,
		TEXT("[AutoTestRoom] [PIEInstanceIndex=%d] 房间创建失败: %s"),
		CachedPIEWindowID, *ErrorMessage);
		// 失败后停止协调
		InternalStop();
	}
}

void UAutoTestRoomCoordinatorSubsystem::OnJoinRoomComplete(bool bWasSuccessful, const FString& ConnectString)
{
	if (!bIsRunning)
	{
		return;
	}

	if (bWasSuccessful)
	{
	UE_LOG(LogTemp, Log,
		TEXT("[AutoTestRoom] [PIEInstanceIndex=%d] 加入成功, ConnectString=%s, 跳转地图..."),
		CachedPIEWindowID, *ConnectString);

		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			PC->ClientTravel(ConnectString, TRAVEL_Absolute);
		}

		// 完成
		InternalStop();
	}
	else
	{
	UE_LOG(LogTemp, Warning,
		TEXT("[AutoTestRoom] [PIEInstanceIndex=%d] 加入失败: %s, 继续轮询搜索..."),
		CachedPIEWindowID, *ConnectString);

		// 重新启动搜索计时器
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				SearchTimerHandle, this,
				&UAutoTestRoomCoordinatorSubsystem::OnSearchTimerFired,
				SearchIntervalSeconds, false);
		}
	}
}

void UAutoTestRoomCoordinatorSubsystem::HandleFindRoomsNoOp(
	bool /*bWasSuccessful*/, const TArray<FRoomSessionResult>& /*Rooms*/)
{
	// 空操作: 业务逻辑由 OnRoomsFound 多播统一处理
}

// ==========================================
// 执行动作
// ==========================================

void UAutoTestRoomCoordinatorSubsystem::HostAutoTestRoom()
{
	USessionManagerSubsystem* SessionMgr = GetSessionManager();
	if (!SessionMgr)
	{
		UE_LOG(LogTemp, Error, TEXT("[AutoTestRoom] SessionManager 不可用, 建房失败"));
		InternalStop();
		return;
	}

	// 获取当前测试账号
	FString MyAccount = TEXT("TestUser_Unknown");
	if (UAccountSubsystem* AccountSub = GetGameInstance()->GetSubsystem<UAccountSubsystem>())
	{
		MyAccount = AccountSub->GetCurrentLoggedInUser();
	}

	// 读取配置
	FString MapName = TEXT("L_Room");
	FString GameMode = TEXT("刀战模式");
	int32 MaxPlayers = 4;
	if (const UMetalSlugTestSettings* Settings = GetDefault<UMetalSlugTestSettings>())
	{
		MapName = Settings->AutoTestRoomMapName;
		MaxPlayers = Settings->AutoTestRoomMaxPlayers;
	}

	// 构造创房参数
	FRoomCreationParams Params;
	Params.RoomName = TargetRoomName;
	Params.Password = TEXT("");
	Params.GameMode = GameMode;
	Params.MapName = MapName;
	Params.LevelName = FName(*MapName);
	Params.MaxPlayers = MaxPlayers;
	Params.HostAccount = MyAccount;

	UE_LOG(LogTemp, Log,
		TEXT("[AutoTestRoom] [PIEInstanceIndex=%d] 创建房间: 房间名=%s, 地图=%s, 最大人数=%d, 房主=%s"),
		CachedPIEWindowID, *TargetRoomName, *MapName, MaxPlayers, *MyAccount);

	FOnCreateRoomComplete Delegate;
	Delegate.BindDynamic(this, &UAutoTestRoomCoordinatorSubsystem::OnCreateRoomComplete);
	SessionMgr->CreateRoom(Params, Delegate);
}

void UAutoTestRoomCoordinatorSubsystem::TriggerFindRooms()
{
	USessionManagerSubsystem* SessionMgr = GetSessionManager();
	if (!SessionMgr)
	{
		return;
	}

	// BindDynamic 要求 UFUNCTION, 用 NoOp 接收单播回调
	FOnFindRoomsComplete DummyDelegate;
	DummyDelegate.BindDynamic(this, &UAutoTestRoomCoordinatorSubsystem::HandleFindRoomsNoOp);
	SessionMgr->FindRooms(DummyDelegate);

	// 启动下一次搜索计时器
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			SearchTimerHandle, this,
			&UAutoTestRoomCoordinatorSubsystem::OnSearchTimerFired,
			SearchIntervalSeconds, false);
	}
}

bool UAutoTestRoomCoordinatorSubsystem::FindTargetRoomInResults(
	const TArray<FRoomSessionResult>& Rooms,
	FRoomSessionResult& OutRoom) const
{
	for (const FRoomSessionResult& Room : Rooms)
	{
		if (Room.RoomName == TargetRoomName)
		{
			OutRoom = Room;
			return true;
		}
	}
	return false;
}

USessionManagerSubsystem* UAutoTestRoomCoordinatorSubsystem::GetSessionManager() const
{
	if (!GetGameInstance())
	{
		return nullptr;
	}
	return GetGameInstance()->GetSubsystem<USessionManagerSubsystem>();
}
