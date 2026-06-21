// MetalSlug01. All Rights Reserved.
#include "Systems/Session/LANRoomPresenter.h"
#include "Systems/Session/SessionManagerSubsystem.h"
// 引入 AccountService (用于 NotifyBecame*Host 内部读取账号)
#include "Services/AccountService.h"
// 【P0 架构升级】房主变更广播依赖 URoomService
#include "Services/RoomService.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
// 【修复 C2679】TWeakObjectPtr<UUserWidget>::operator=(UUserWidget*) 需要 UUserWidget 完整定义
//                否则 SFINAE 检查 UE_REQUIRES(!TLosesQualifiersFromTo_V<U, T>) 失败, 找不到匹配
#include "Blueprint/UserWidget.h"

// ==========================================
// 模块一：Subsystem 生命周期 + IViewModel 实现
// ==========================================

void ULANRoomPresenter::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // 【架构升级】自动注入 SessionManager 依赖（无需外部 Initialize 调用）
    if (UGameInstance* GI = GetGameInstance())
    {
        SessionManager = GI->GetSubsystem<USessionManagerSubsystem>();
    }
}

void ULANRoomPresenter::Deinitialize()
{
	// 【P0 修复】解除所有等待中的 SessionManager 委托, 防止野指针回调
	// 1. 解绑 BoundView (UI Widget 可能先于 Subsystem 销毁)
	UnbindView();

	// 2. 清空内部状态, 防止回调在 Deinitialize 后访问已销毁对象
	CachedRoomList.Reset();
	CachedSignatures.Reset();
	SelectedRoomIndex = INDEX_NONE;
	bHasAccountConflict = false;
	bAccountCheckPassed = false;
	bIsWidgetVisible = false;
	PendingCreationParams = FRoomCreationParams();
	LastErrorMessage.Empty();

	// 3. 【P1 防御性重置】状态机回到 Idle, 避免 Presenter 死了但状态卡在中间
	//   (Dynamic 委托 ExecuteIfBound 在 Object dead 时自动 no-op, 所以 SessionManager
	//    持有的 Pending*Delegate 不会有野指针崩溃, 但显式重置更清晰)
	CurrentState = ELANRoomState::Idle;

	// 4. 清空 SessionManager 引用 (UE GC 自动清理 Pending*Delegate 中的 UFUNCTION 绑定)
	SessionManager = nullptr;

	Super::Deinitialize();
}

void ULANRoomPresenter::BindView(UUserWidget* InView)
{
    BoundView = InView;
}

void ULANRoomPresenter::UnbindView()
{
    BoundView.Reset();
}

void ULANRoomPresenter::OnWidgetShow()
{
    bIsWidgetVisible = true;

    // ---- Widget 显示时立即刷新一次房间列表 ----
    RequestRefreshRoomList();
}

void ULANRoomPresenter::OnWidgetHide()
{
    bIsWidgetVisible = false;
}

// ==========================================
// 模块二：玩家操作入口
// ==========================================

void ULANRoomPresenter::RequestRefreshRoomList()
{
	// ---- 状态检查：只有在 Idle 或 Error 状态才允许刷新 ----
	if (CurrentState != ELANRoomState::Idle && CurrentState != ELANRoomState::Error)
	{
		return;
	}

	if (!SessionManager) return;

	// ---- 切换到搜索状态 ----
	TransitionTo(ELANRoomState::Searching);

	FOnFindRoomsComplete FindDelegate;
	FindDelegate.BindDynamic(this, &ULANRoomPresenter::OnFindRoomsComplete);
	SessionManager->FindRooms(FindDelegate);
}

void ULANRoomPresenter::RequestCreateRoom(const FString& RoomName, const FString& Password,
	const FString& GameMode, const FString& MapName, FName LevelName)
{
	// ---- 状态检查 ----
	if (CurrentState != ELANRoomState::Idle)
	{
		return;
	}

	if (!SessionManager) return;

	// ---- 获取当前登录账号 ----
	FString CurrentAccount;
	if (UAccountService* AccountService = UAccountService::Get(this))
	{
		CurrentAccount = AccountService->GetCurrentUser();
	}

	// ---- 保存创房参数 ----
	PendingCreationParams.RoomName = RoomName;
	PendingCreationParams.Password = Password;
	PendingCreationParams.GameMode = GameMode;
	PendingCreationParams.MapName = MapName;
	PendingCreationParams.LevelName = LevelName;
	PendingCreationParams.HostAccount = CurrentAccount;

	// ---- 切换到账号检测状态 ----
	TransitionTo(ELANRoomState::AccountChecking);

	// ---- 先搜索一次，检查同号冲突 ----
	FOnFindRoomsComplete FindDelegate;
	FindDelegate.BindDynamic(this, &ULANRoomPresenter::OnAccountCheckFindComplete);
	SessionManager->FindRooms(FindDelegate);
}

void ULANRoomPresenter::OnRoomSelected(int32 RoomIndex)
{
	if (RoomIndex < 0 || RoomIndex >= CachedRoomList.Num())
	{
		SelectedRoomIndex = INDEX_NONE;
		return;
	}

	SelectedRoomIndex = RoomIndex;

	// ---- 刷新房间列表（更新高亮状态） ----
	OnRoomListRefreshed.Broadcast();
}

void ULANRoomPresenter::RequestJoinSelectedRoom()
{
	// ---- 状态检查 ----
	if (CurrentState != ELANRoomState::Idle)
	{
		return;
	}

	if (SelectedRoomIndex == INDEX_NONE)
	{
		LastErrorMessage = TEXT("请先选择一个房间");
		OnErrorOccurred.Broadcast(LastErrorMessage);
		return;
	}

	if (!SessionManager) return;

	FRoomSessionResult SelectedRoom = GetSelectedRoom();
	if (!SelectedRoom.IsValid())
	{
		LastErrorMessage = TEXT("房间数据无效");
		OnErrorOccurred.Broadcast(LastErrorMessage);
		return;
	}

	// ---- 检查是否在战斗中 ----
	if (SelectedRoom.bIsInBattle)
	{
		LastErrorMessage = TEXT("该房间正在战斗中，无法加入");
		OnErrorOccurred.Broadcast(LastErrorMessage);
		return;
	}

	// ---- 获取当前账号 ----
	FString CurrentAccount;
	if (UAccountService* AccountService = UAccountService::Get(this))
	{
		CurrentAccount = AccountService->GetCurrentUser();
	}

	// ---- 检查是否与房间内的账号冲突 ----
	TArray<FString> RoomAccounts;
	// 从房间结果中解析账号列表...

	// ---- 切换状态并发起加入 ----
	TransitionTo(ELANRoomState::Joining);

	FOnJoinRoomComplete JoinDelegate;
	JoinDelegate.BindDynamic(this, &ULANRoomPresenter::OnJoinRoomComplete);
	SessionManager->JoinRoom(SelectedRoom, JoinDelegate);
}

// ==========================================
// 模块三：数据查询
// ==========================================

FRoomSessionResult ULANRoomPresenter::GetSelectedRoom() const
{
	if (SelectedRoomIndex >= 0 && SelectedRoomIndex < CachedRoomList.Num())
	{
		return CachedRoomList[SelectedRoomIndex];
	}
	return FRoomSessionResult();
}

// ==========================================
// 模块四：内部回调
// ==========================================

void ULANRoomPresenter::OnFindRoomsComplete(bool bWasSuccessful, const TArray<FRoomSessionResult>& Rooms)
{
	// ---- 解析签名 ----
	TArray<FString> NewSignatures;
	for (const auto& Room : Rooms)
	{
		NewSignatures.Add(Room.GenerateSignature());
	}

	// ---- 比对签名，判断是否需要刷新 UI ----
	bool bNeedsRefresh = (NewSignatures.Num() != CachedSignatures.Num());

	if (!bNeedsRefresh)
	{
		for (int32 i = 0; i < NewSignatures.Num(); ++i)
		{
			if (NewSignatures[i] != CachedSignatures[i])
			{
				bNeedsRefresh = true;
				break;
			}
		}
	}

	// ---- 更新缓存 ----
	CachedRoomList = Rooms;
	CachedSignatures = NewSignatures;

	// ---- 刷新 UI（仅在 Widget 可见时） ----
	if (bNeedsRefresh && bIsWidgetVisible)
	{
		OnRoomListRefreshed.Broadcast();
	}

	// ---- 恢复到空闲状态 ----
	TransitionTo(ELANRoomState::Idle);
}

void ULANRoomPresenter::OnAccountCheckFindComplete(bool bWasSuccessful, const TArray<FRoomSessionResult>& Rooms)
{
	if (!bWasSuccessful)
	{
		// ---- 搜索失败，直接尝试创建 ----
		bAccountCheckPassed = true;
		ProceedToCreateRoom();
		return;
	}

	// ---- 检查账号冲突 ----
	FString CurrentAccount = PendingCreationParams.HostAccount;
	if (SessionManager->IsAccountInAnyRoom(Rooms, CurrentAccount))
	{
		// ---- 发现账号冲突 ----
		bHasAccountConflict = true;
		LastErrorMessage = TEXT("此账号已在其他房间中");
		TransitionTo(ELANRoomState::Error);
		OnAccountConflictDetected.Broadcast();
		return;
	}

	// ---- 无冲突，继续创建 ----
	bAccountCheckPassed = true;
	ProceedToCreateRoom();
}

void ULANRoomPresenter::OnDestroyRoomBeforeCreate(bool bWasSuccessful, const FString& ErrorMessage)
{
	if (!bWasSuccessful)
	{
		LastErrorMessage = ErrorMessage.IsEmpty() ? TEXT("清理旧房间失败") : ErrorMessage;
		TransitionTo(ELANRoomState::Error);
		OnErrorOccurred.Broadcast(LastErrorMessage);
		return;
	}

	// ---- 销毁成功后创建新房间 ----
	TransitionTo(ELANRoomState::Creating);
	FOnCreateRoomComplete CreateDelegate;
	CreateDelegate.BindDynamic(this, &ULANRoomPresenter::OnCreateRoomComplete);
	SessionManager->CreateRoom(PendingCreationParams, CreateDelegate);
}

void ULANRoomPresenter::OnCreateRoomComplete(bool bWasSuccessful, const FString& ErrorMessage)
{
	if (!bWasSuccessful)
	{
		LastErrorMessage = ErrorMessage.IsEmpty() ? TEXT("创建房间失败") : ErrorMessage;
		TransitionTo(ELANRoomState::Error);
		OnErrorOccurred.Broadcast(LastErrorMessage);
		return;
	}

	// 【P0 架构升级】创房成功 = 本机成为房主, 触发 URoomService.BroadcastHostChanged(true)
	NotifyBecameHost();

	// ---- 创房成功，通知 Controller 执行 OpenLevel ----
	OnCreateRoomSuccess.Broadcast(PendingCreationParams.LevelName.ToString());

	// ---- 恢复到空闲 ----
	TransitionTo(ELANRoomState::Idle);
}

void ULANRoomPresenter::OnJoinRoomComplete(bool bWasSuccessful, const FString& ConnectString)
{
	if (!bWasSuccessful)
	{
		LastErrorMessage = ConnectString; // ConnectString 在失败时是错误信息
		TransitionTo(ELANRoomState::Error);
		OnErrorOccurred.Broadcast(LastErrorMessage);
		return;
	}

	// 【P0 架构升级】加入他人房间 = 本机成为普通客户端, 触发 URoomService.BroadcastHostChanged(false)
	NotifyBecameClient();

	// ---- 加入成功，通知 Controller 执行跳转 ----
	OnJoinRoomSuccess.Broadcast(ConnectString);

	// ---- 恢复到空闲 ----
	TransitionTo(ELANRoomState::Idle);
}

// ==========================================
// 模块五：内部工具
// ==========================================

void ULANRoomPresenter::TransitionTo(ELANRoomState NewState)
{
	if (CurrentState == NewState) return;

	CurrentState = NewState;

	// 【修复 C2971】UE 5.6 FormatStringSan 在 MSVC 上对 *UEnum::GetValueAsString() 表达式报错
	//              改用 FString::Printf + GLog->Log 绕开编译期校验
	const FString StateStr = UEnum::GetValueAsString(NewState);
	GLog->Log(TEXT("[LANRoomPresenter] State: ") + StateStr);

	OnStateChanged.Broadcast();
}

void ULANRoomPresenter::ProceedToCreateRoom()
{
	if (!SessionManager) return;

	// ---- 先检查是否已存在房间（需要销毁） ----
	if (SessionManager->IsHosting())
	{
		// ---- 先销毁旧房间 ----
		FOnDestroyRoomComplete DestroyDelegate;
		DestroyDelegate.BindDynamic(this, &ULANRoomPresenter::OnDestroyRoomBeforeCreate);
		SessionManager->DestroyRoom(DestroyDelegate);
	}
	else
	{
		// ---- 直接创建 ----
		TransitionTo(ELANRoomState::Creating);
		FOnCreateRoomComplete CreateDelegate;
		CreateDelegate.BindDynamic(this, &ULANRoomPresenter::OnCreateRoomComplete);
		SessionManager->CreateRoom(PendingCreationParams, CreateDelegate);
	}
}


// ==========================================
// 【P0 架构升级】房主身份通知 - 走 URoomService.BroadcastHostChanged
// ==========================================

/**
 * NotifyBecameHost
 *
 * 触发时机: 本机 SessionManager 创房成功
 * 职责: 让 URoomService.BroadcastHostChanged(true) 触发, RoomInsidePage 收到后立即刷新按钮可见性
 *
 * 为什么需要这层封装:
 *  - 未来可能要在 NotifyBecameHost 里加本地状态锁 / 持久化 / 重连等逻辑
 *  - 现在直接转发到 URoomService 是过渡形态, 但 API 形态稳定
 */
void ULANRoomPresenter::NotifyBecameHost()
{
	UE_LOG(LogTemp, Log, TEXT("[LANRoomPresenter] NotifyBecameHost: 本机成为房主"));
	URoomService::BroadcastHostChanged(this, true);
}

/**
 * NotifyBecameClient
 *
 * 触发时机: 本机 SessionManager 加入他人房间成功
 * 职责: 让 URoomService.BroadcastHostChanged(false) 触发
 */
void ULANRoomPresenter::NotifyBecameClient()
{
	UE_LOG(LogTemp, Log, TEXT("[LANRoomPresenter] NotifyBecameClient: 本机成为普通客户端"));
	URoomService::BroadcastHostChanged(this, false);
}
