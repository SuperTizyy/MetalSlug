// 版权声明：在项目设置的描述页面填写您的版权信息。

#include "Services/UIViewService.h"
#include "Systems/GameFlowSubsystem.h"
#include "Systems/Session/LANRoomPresenter.h"
#include "Services/RoomStateService.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Logs/MetalSlugLogChannels.h"
// 【大厂核心修复 2026.06.28】用下一帧延迟等 PC 就绪
#include "TimerManager.h"
// 【修复 UE_LOG 编译错误】UEnum::GetValueAsString 在 UObject/Class.h
#include "UObject/Class.h"

// ==========================================
// Subsystem 生命周期
// ==========================================

void UUIViewService::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ==========================================
	// 【大厂标准】面板配置初始化 (只执行一次)
	// ==========================================
	// 原因: UGameInstanceSubsystem 只会被创建一次，但 Initialize() 可能在某些边缘情况被调用多次
	// 方案: 用 static bool 保护，即使多次 Initialize 也安全
	static bool bConfigsInitialized = false;
	if (!bConfigsInitialized)
	{
		bConfigsInitialized = true;

		// Login 面板配置 (登录页)
		FPanelConfig LoginConfig;
		if (UClass* CLS = LoadObject<UClass>(nullptr, TEXT("/Game/UI/Login/WBP_LoginPage.WBP_LoginPage_C")))
		{
			LoginConfig.WidgetClass = CLS;
		}
		LoginConfig.bPreloadOnInit = true;
		LoginConfig.InputMode = EUIInputMode::UIOnly;
		PanelConfigs.Add(EUIPanel::Login, LoginConfig);

		// MainMenu 面板配置 (主菜单)
		FPanelConfig MainMenuConfig;
		if (UClass* CLS = LoadObject<UClass>(nullptr, TEXT("/Game/UI/Login/WBP_GameMenuPage.WBP_GameMenuPage_C")))
		{
			MainMenuConfig.WidgetClass = CLS;
		}
		MainMenuConfig.bPreloadOnInit = false;
		MainMenuConfig.InputMode = EUIInputMode::UIOnly;
		PanelConfigs.Add(EUIPanel::MainMenu, MainMenuConfig);

		// LANRoom 面板配置 (房间列表)
		FPanelConfig LANRoomConfig;
		if (UClass* CLS = LoadObject<UClass>(nullptr, TEXT("/Game/UI/Login/WBP_LANRoomPage.WBP_LANRoomPage_C")))
		{
			LANRoomConfig.WidgetClass = CLS;
		}
		LANRoomConfig.bPreloadOnInit = false;
		LANRoomConfig.InputMode = EUIInputMode::UIOnly;
		PanelConfigs.Add(EUIPanel::LANRoom, LANRoomConfig);

		// RoomInside 面板配置 (房间内等待)
		FPanelConfig RoomInsideConfig;
		if (UClass* CLS = LoadObject<UClass>(nullptr, TEXT("/Game/UI/Login/WBP_RoomInsidePage.WBP_RoomInsidePage_C")))
		{
			RoomInsideConfig.WidgetClass = CLS;
		}
		RoomInsideConfig.bPreloadOnInit = false;
		RoomInsideConfig.InputMode = EUIInputMode::UIOnly;
		PanelConfigs.Add(EUIPanel::RoomInside, RoomInsideConfig);

		// BattleHUD 面板配置 (战斗 HUD)
		FPanelConfig BattleHUDConfig;
		if (UClass* CLS = LoadObject<UClass>(nullptr, TEXT("/Game/UI/Game/WBP_GameHUDWidget.WBP_GameHUDWidget_C")))
		{
			BattleHUDConfig.WidgetClass = CLS;
		}
		BattleHUDConfig.bPreloadOnInit = false;
		BattleHUDConfig.InputMode = EUIInputMode::GameAndUI;
		PanelConfigs.Add(EUIPanel::BattleHUD, BattleHUDConfig);

		// 状态 → 面板映射
		StateToPanelMap.Add(EMatchState::Login, EUIPanel::Login);
		StateToPanelMap.Add(EMatchState::MainMenu, EUIPanel::MainMenu);
		StateToPanelMap.Add(EMatchState::MainLobby, EUIPanel::LANRoom);
		StateToPanelMap.Add(EMatchState::InRoom, EUIPanel::RoomInside);
	}

	// 订阅 GameFlow 状态变化
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSubsystem->OnStateChanged.AddDynamic(this, &UUIViewService::OnGameFlowStateChanged);

			// 【大厂架构 - 独立中断通道】订阅中断信号
			// 独立于 OnStateChanged 通道，专门响应网络失败等强制中断
			FlowSubsystem->OnInterrupted.AddDynamic(this, &UUIViewService::OnGameInterrupted);

			UE_LOG(LogUI, Log, TEXT("[UIViewService] OnStateChanged + OnInterrupted 订阅成功, CurrentState=%d"),
				(int32)FlowSubsystem->GetCurrentState());

			// 【大厂核心修复 2026.06.28】订阅后立即同步当前状态
			// 问题:
			//   GameFlowSubsystem::Initialize → BootToLogin → Broadcast(Login) 发生在
			//   UIViewService::Initialize 完成订阅之前 → UIViewService 错过 BootToLogin 广播
			//   PostLoadMapWithWorld (路径 B 延迟同步) 也因为时序问题未触发
			//   → 玩家永远看不到登录页
			//
			// 方案: 订阅后立即查询当前状态，如果当前状态需要面板，直接拉起
			//       这是"观察者模式"的最佳实践: 订阅后主动同步初始状态
			EMatchState CurrentState = FlowSubsystem->GetCurrentState();
			if (EUIPanel* PanelPtr = StateToPanelMap.Find(CurrentState))
			{
				// 【大厂核心修复 2026.06.28】用 ShowPanelWhenPCReady 包装
				// PC 可能尚未就绪，由 ShowPanelWhenPCReady 负责等待
				ShowPanelWhenPCReady(*PanelPtr);

				// 【大厂核心修复 2026.06.28 - 补充】
				// 如果 ShowPanelWhenPCReady 设置了 PendingShowPanel（PC 还未就绪），
				// 此时我们在 Initialize 中，PC 大概率已可用。直接清掉定时器并同步一次。
				if (PendingShowPanel != EUIPanel::None)
				{
					UWorld* World = GetWorld();
					if (World) World->GetTimerManager().ClearTimer(PCReadyTimerHandle);
					PCReadyCheckCount = 0;
					ExecuteShowPanel(PendingShowPanel);
					PendingShowPanel = EUIPanel::None;
				}
			}
			else if (CurrentState == EMatchState::Battleing)
			{
				// Battleing 状态: 隐藏所有 UI（由 MyGameHUD 处理 HUD 显示）
				HideAllPanels();
				SetInputMode(EUIInputMode::GameOnly);
			}

			// 【大厂标准】在 Subsystem 启动时预创建高频面板（避免战斗中卡顿）
			PreloadConfiguredPanels();
		}
	}

	UE_LOG(LogUI, Log, TEXT("[UIViewService] 初始化完成"));
}

void UUIViewService::Deinitialize()
{
	// 【P1 修复】解绑 GameFlow 订阅, 防止野指针回调
	// AddDynamic 的委托句柄 GC 不自动清理, 必须显式 RemoveDynamic
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSubsystem->OnStateChanged.RemoveDynamic(this, &UUIViewService::OnGameFlowStateChanged);
			FlowSubsystem->OnInterrupted.RemoveDynamic(this, &UUIViewService::OnGameInterrupted);
		}
	}

	// 清理所有 Widget
	if (ActiveWidget)
	{
		ActiveWidget->RemoveFromParent();
		ActiveWidget = nullptr;
	}
	for (auto& Pair : PreloadedWidgets)
	{
		if (Pair.Value)
		{
			Pair.Value->RemoveFromParent();
		}
	}
	PreloadedWidgets.Empty();

	Super::Deinitialize();
}

// ==========================================
// 公共 API
// ==========================================

void UUIViewService::ShowPanel(EUIPanel Panel)
{
	ShowPanelWhenPCReady(Panel);
}

void UUIViewService::ShowPanelWhenPCReady(EUIPanel Panel)
{
	if (Panel == EUIPanel::None) return;

	// ==========================================
	// 【DEBUG-SET-4-C】ShowPanel 守卫检查: 这次走到 Show 分支还是被守卫跳过？
	// ==========================================
	UE_LOG(LogTemp, Error,
		TEXT("[DEBUG-S4-C][ShowPanelWhenPCReady] PID=%u WorldName=%s Panel=%d ActivePanel=%d bWidgetStillValid=%d ActiveWidgetIsInViewport=%d"),
		FPlatformProcess::GetCurrentProcessId(),
		*GetWorld()->GetName(),
		(int32)Panel,
		(int32)ActivePanel,
		(ActiveWidget && ActiveWidget->IsValidLowLevel()) ? 1 : 0,
		(ActiveWidget && ActiveWidget->IsInViewport()) ? 1 : 0);

	// ==========================================
	// 【大厂 P0 修复 v2】Widget 损坏检测
	// ==========================================
	// 场景: BroadcastNetworkFailure (?closed) 触发 BeginTearingDown,
	//       OnGameFlowStateChanged(3) 被调用 → ShowPanelWhenPCReady(3)
	//       此时 ActiveWidget 已被 Slate::InvalidateAllWidgets 标记失效，
	//       但 ActivePanel 仍为 3。
	//       如果仅用 "ActivePanel == Panel" 做守卫，会跳过 ExecuteShowPanel，
	//       导致 ActiveWidget 永远为 null，UI 消失。
	// 修复: 即使目标 Panel 与当前相同，也检查 Widget 是否有效，
	//       无效则强制重建。
	// ==========================================
	const bool bWidgetStillValid = ActiveWidget && ActiveWidget->IsValidLowLevel();

	// 【大厂 P0 修复 v2】Widget 损坏时强制重建（即使 Panel ID 相同）
	if (ActivePanel == Panel && !bWidgetStillValid)
	{
		UE_LOG(LogUI, Log,
			TEXT("[UIViewService] ShowPanelWhenPCReady: Widget 已损坏但 ActivePanel 相同，强制重建 Panel=%d"),
			(int32)Panel);
		// 不 return，继续走 ExecuteShowPanel 重建
	}

	// 相同面板守卫: 仅当 Widget 有效时才跳过
	if (ActivePanel == Panel && bWidgetStillValid)
	{
		return;
	}

	// PC 就绪检查同上
	APlayerController* PC = GetLocalPlayerController();
	if (PC && PC->IsValidLowLevel())
	{
		// PC 已就绪，立即执行真正的 ShowPanel 逻辑
		ExecuteShowPanel(Panel);
		return;
	}

	// PC 尚未就绪（Subsystem Initialize 早于 World BeginPlay）
	// 暂存面板，等下一帧 PC 就绪后自动显示
	if (PendingShowPanel == EUIPanel::None)
	{
		PendingShowPanel = Panel;
		PCReadyCheckCount = 0;

		UWorld* World = GetWorld();
		if (World)
		{
			World->GetTimerManager().SetTimer(PCReadyTimerHandle, this,
				&UUIViewService::TryShowPendingPanel, 0.1f, true);
			UE_LOG(LogUI, Log,
				TEXT("[UIViewService] ShowPanelWhenPCReady: PC 未就绪，暂存 Panel=%d，等待下一帧..."),
				(int32)Panel);
		}
		else
		{
			UE_LOG(LogUI, Error,
				TEXT("[UIViewService] ShowPanelWhenPCReady: World 为空，无法启动等待定时器"));
		}
	}
	else
	{
		// 已有待显示面板，更新为最新
		PendingShowPanel = Panel;
		PCReadyCheckCount = 0;
	}
}

void UUIViewService::TryShowPendingPanel()
{
	// 【大厂标准】防止重入：正在显示时就不要再处理定时器回调
	if (PendingShowPanel == EUIPanel::None)
	{
		return;
	}

	PCReadyCheckCount++;

	// 防无限等待
	if (PCReadyCheckCount > MaxPCReadyChecks)
	{
		UWorld* World = GetWorld();
		if (World) World->GetTimerManager().ClearTimer(PCReadyTimerHandle);
		UE_LOG(LogUI, Error,
			TEXT("[UIViewService] TryShowPendingPanel: PC 就绪检查超时 (尝试了 %d 次), 放弃"),
			PCReadyCheckCount);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(902, 5.0f, FColor::Red,
				FString::Printf(TEXT("[UIViewService] PC 就绪超时，登录页显示失败")));
		}
		return;
	}

	APlayerController* PC = GetLocalPlayerController();
	if (!PC || !PC->IsValidLowLevel())
	{
		// PC 尚未就绪，继续等待
		return;
	}

	// PC 已就绪！
	UWorld* World = GetWorld();
	if (World) World->GetTimerManager().ClearTimer(PCReadyTimerHandle);

	EUIPanel PanelToShow = PendingShowPanel;
	PendingShowPanel = EUIPanel::None;
	PCReadyCheckCount = 0;

	UE_LOG(LogUI, Log,
		TEXT("[UIViewService] TryShowPendingPanel: PC 已就绪 (检查了 %d 次)，显示 Panel=%d"),
		PCReadyCheckCount, (int32)PanelToShow);

	ExecuteShowPanel(PanelToShow);
}

void UUIViewService::ExecuteShowPanel(EUIPanel Panel)
{
	// ==========================================
	// 【DEBUG-SET-4-D】真正进入 Show 逻辑: 即将销毁旧面板+显示新面板
	// ==========================================
	UE_LOG(LogTemp, Error,
		TEXT("[DEBUG-S4-D][ExecuteShowPanel] PID=%u WorldName=%s Panel=%d HasPreloaded=%d"),
		FPlatformProcess::GetCurrentProcessId(),
		*GetWorld()->GetName(),
		(int32)Panel,
		PreloadedWidgets.Contains(Panel) ? 1 : 0);

	// 原有 ShowPanel 逻辑
	DestroyActivePanel();

	// 优先从预创建缓存拿
	UUserWidget* WidgetToShow = PreloadedWidgets.FindRef(Panel);
	if (!WidgetToShow)
	{
		// 缓存中没有，当场创建
		CreateAndShowPanel(Panel);
		return;
	}

	// 使用预创建的 Widget
	ActiveWidget = WidgetToShow;
	ActivePanel = Panel;

	// 注入 ViewModel（如未注入）
	InjectViewModelForPanel(Panel, ActiveWidget);

	ActiveWidget->AddToViewport();
	ActiveWidget->SetVisibility(ESlateVisibility::Visible);

	// 应用输入模式
	if (FPanelConfig* Config = PanelConfigs.Find(Panel))
	{
		SetInputMode(Config->InputMode);
	}
	else
	{
		SetInputMode(EUIInputMode::UIOnly);
	}

	UE_LOG(LogUI, Log, TEXT("[UIViewService] 显示预创建面板: %d"), (int32)Panel);
}

void UUIViewService::HideAllPanels()
{
	DestroyActivePanel();
	ActivePanel = EUIPanel::None;
}

void UUIViewService::SetInputMode(EUIInputMode Mode)
{
	APlayerController* PC = GetLocalPlayerController();
	if (!PC) return;

	switch (Mode)
	{
	case EUIInputMode::UIOnly:
		PC->bShowMouseCursor = true;
		PC->SetInputMode(FInputModeUIOnly());
		break;
	case EUIInputMode::GameOnly:
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
		break;
	case EUIInputMode::GameAndUI:
		PC->bShowMouseCursor = true;
		PC->SetInputMode(FInputModeGameAndUI());
		break;
	}
}

// ==========================================
// 内部方法
// ==========================================

void UUIViewService::OnGameFlowStateChanged(EMatchState NewState)
{
	// ==========================================
	// 【DEBUG-SET-4-A】GameFlow 状态变化: 谁动了 UI？
	// ==========================================
	UE_LOG(LogTemp, Error,
		TEXT("[DEBUG-S4-A][OnGameFlowStateChanged] PID=%u WorldName=%s NewState=%d ActivePanel=%d ActiveWidget=%p ActiveWidgetIsInViewport=%d"),
		FPlatformProcess::GetCurrentProcessId(),
		*GetWorld()->GetName(),
		(int32)NewState,
		(int32)ActivePanel,
		ActiveWidget.Get(),
		(ActiveWidget && ActiveWidget->IsInViewport()) ? 1 : 0);

	// 【大厂可观测性】OnScreenDebugMessage 不受控制台日志过滤器影响，确保随时可见
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(901, 5.0f, FColor::Cyan,
			FString::Printf(TEXT("[UIViewService] GameFlowStateChanged: NewState=%d"), (int32)NewState));
	}
	UE_LOG(LogUI, Log, TEXT("[UIViewService] GameFlowStateChanged: NewState=%d, StateToPanelMap.Num()=%d"),
		(int32)NewState, StateToPanelMap.Num());

	// 【大厂 P0 修复 v2】自愈：跨地图残留检测
	// 当 TryShowPendingPanel 的重入守卫生效时（PendingShowPanel 未清），此时如果又有新状态广播，
	// 说明正在处理 ClientTravel 等跨地图场景。主动清空残留状态，让本次新状态正常执行。
	if (PendingShowPanel != EUIPanel::None)
	{
		UE_LOG(LogUI, Log,
			TEXT("[UIViewService] 检测到跨地图残留状态 PendingShowPanel=%d，主动清空"),
			(int32)PendingShowPanel);
		PendingShowPanel = EUIPanel::None;
		PCReadyCheckCount = 0;

		UWorld* World = GetWorld();
		if (World) World->GetTimerManager().ClearTimer(PCReadyTimerHandle);

		// 强制销毁旧面板（ClientTravel 后 ActivePanel 可能残留）
		DestroyActivePanel();
		// 【大厂 P0 修复 v2】Widget 损坏时 ActivePanel 已被清，
		// 但 DestroyActivePanel 已在 if (ActiveWidget) 块中设置 ActivePanel = None，
		// 无需额外处理。
	}

	if (EUIPanel* PanelPtr = StateToPanelMap.Find(NewState))
	{
		// 【大厂核心修复】用 ShowPanelWhenPCReady 包装，确保 PC 未就绪时也能正确等待
		ShowPanelWhenPCReady(*PanelPtr);
	}
	else if (NewState == EMatchState::Battleing)
	{
		// 战斗态：隐藏所有非战斗 UI，切游戏输入
		HideAllPanels();
		SetInputMode(EUIInputMode::GameOnly);
	}
}

// ==========================================
// 内部辅助方法
// ==========================================

void UUIViewService::PreloadConfiguredPanels()
{
	for (auto& Pair : PanelConfigs)
	{
		if (Pair.Value.bPreloadOnInit && Pair.Value.WidgetClass)
		{
			PreCreatePanel(Pair.Key, Pair.Value);
		}
	}
}

void UUIViewService::PreCreatePanel(EUIPanel Panel, const FPanelConfig& Config)
{
	if (!Config.WidgetClass) return;
	if (PreloadedWidgets.Contains(Panel)) return;

	APlayerController* PC = GetLocalPlayerController();
	if (!PC) return;

	UUserWidget* Widget = CreateWidget<UUserWidget>(PC, Config.WidgetClass);
	if (!Widget) return;

	// 注入 ViewModel（如果适用）
	InjectViewModelForPanel(Panel, Widget);

	// 隐藏 + 加入 viewport（保留在内存中）
	Widget->AddToViewport(0);
	Widget->SetVisibility(ESlateVisibility::Collapsed);

	PreloadedWidgets.Add(Panel, Widget);
	UE_LOG(LogUI, Log, TEXT("[UIViewService] 预创建面板: %d"), (int32)Panel);
}

void UUIViewService::CreateAndShowPanel(EUIPanel Panel)
{
	FPanelConfig* Config = PanelConfigs.Find(Panel);
	if (!Config || !Config->WidgetClass)
	{
		UE_LOG(LogUI, Warning, TEXT("[UIViewService] Panel %d 未配置 WidgetClass"), (int32)Panel);
		return;
	}

	APlayerController* PC = GetLocalPlayerController();
	if (!PC)
	{
		// 【大厂架构 - P0 修复】CreateAndShowPanel 也需要防御 (GameFlowSubsystem 会兜底延迟同步)
		UE_LOG(LogUI, Error,
			TEXT("[UIViewService] CreateAndShowPanel(%d): PC 不可用 (异常情况)"),
			(int32)Panel);
		return;
	}

	UUserWidget* NewWidget = CreateWidget<UUserWidget>(PC, Config->WidgetClass);
	if (!NewWidget) return;

	// 【大厂标准】ViewModel 注入完全在 Service 层完成
	InjectViewModelForPanel(Panel, NewWidget);

	NewWidget->AddToViewport();
	ActiveWidget = NewWidget;
	ActivePanel = Panel;

	SetInputMode(Config->InputMode);

	UE_LOG(LogUI, Log, TEXT("[UIViewService] 现场创建并显示面板: %d"), (int32)Panel);
}

void UUIViewService::InjectViewModelForPanel(EUIPanel Panel, UUserWidget* NewWidget)
{
    if (!NewWidget) return;

    UGameInstance* GI = GetGameInstance();
    if (!GI) return;

    // 【大厂标准】根据 EUIPanel 派发对应 ViewModel（已实现 IViewModel 的 Subsystem）
    IViewModel* ViewModel = nullptr;
    switch (Panel)
    {
    case EUIPanel::LANRoom:
        // 大厅面板 → LANRoomPresenter（已升级为 UGameInstanceSubsystem）
        ViewModel = GI->GetSubsystem<ULANRoomPresenter>();
        break;

    // case EUIPanel::RoomInside:
    //     未来扩展: URoomInsidePresenter（待新建）
    //     break;

    // case EUIPanel::BattleHUD:
    //     未来扩展: UGameHUDViewModel
    //     break;

    default:
        // Login / MainMenu 当前不强制 ViewModel（数据简单, View 直接读 AccountService）
        UE_LOG(LogUI, Verbose, TEXT("[UIViewService] Panel=%d 无强制 ViewModel, 跳过注入"), (int32)Panel);
        return;
    }

    if (!ViewModel)
    {
        UE_LOG(LogUI, Warning, TEXT("[UIViewService] Panel=%d 找不到 ViewModel"), (int32)Panel);
        return;
    }

    // 解绑旧 View（如果有）→ 绑定新 View
    if (ViewModel->IsBoundToView())
    {
        ViewModel->UnbindView();
    }
    ViewModel->BindView(NewWidget);

    // 【修复 C2100/C2131/C2971/C2672】UE 5.6 的 FormatStringSan 在 MSVC 上对 %s + FString 解引用表达式
    //                                 报 C2971（constexpr 变量作为模板参数失败）。这是已知编译器问题。
    //                                 解决: 避开 UE_LOG 的编译期校验, 改用 FString::Printf + GLog 直接记录。
    const FString LogMsg = FString::Printf(
        TEXT("[UIViewService] InjectViewModel: Panel=%s VM=%s View=%s"),
        *UEnum::GetValueAsString(Panel),
        *ViewModel->GetViewModelType().ToString(),
        *NewWidget->GetName());
    // 【修复 C2665】GLog->Log() 第一个参数是 FName, LogUI 是 FLogCategory 不可直接转换。
    //              直接用无 category 签名: void Log(const TCHAR* Str)
    GLog->Log(*LogMsg);
}

void UUIViewService::DestroyActivePanel()
{
    // ==========================================
    // 【DEBUG-SET-4-B】销毁前快照: 谁在什么时候把 ActiveWidget 干掉的？
    // ==========================================
    UE_LOG(LogTemp, Error,
        TEXT("[DEBUG-S4-B][DestroyActivePanel] PID=%u WorldName=%s ActivePanel=%d ActiveWidget=%p ActiveWidgetIsInViewport=%d"),
        FPlatformProcess::GetCurrentProcessId(),
        *GetWorld()->GetName(),
        (int32)ActivePanel,
        ActiveWidget.Get(),
        (ActiveWidget && ActiveWidget->IsInViewport()) ? 1 : 0);

    if (ActiveWidget)
    {
        // 如果是预创建的 Widget，移回隐藏状态而非销毁
        EUIPanel Panel = ActivePanel;

        // 【大厂标准】隐藏前通知 ViewModel 解绑 View
        if (UGameInstance* GI = GetGameInstance())
        {
            IViewModel* ViewModel = nullptr;
            switch (Panel)
            {
            case EUIPanel::LANRoom:
                ViewModel = GI->GetSubsystem<ULANRoomPresenter>();
                break;
            default:
                break;
            }
            if (ViewModel && ViewModel->IsBoundToView())
            {
                ViewModel->OnWidgetHide();
                ViewModel->UnbindView();
            }
        }

        ActiveWidget->SetVisibility(ESlateVisibility::Collapsed);
        ActiveWidget = nullptr;
    }
    ActivePanel = EUIPanel::None;
}

APlayerController* UUIViewService::GetLocalPlayerController() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	return World->GetFirstPlayerController();
}

// ==========================================
// 【大厂架构 - 独立中断通道】
// ==========================================

void UUIViewService::OnGameInterrupted(EUIPanel TargetPanel)
{
	// 【大厂架构 - 独立中断通道 P0 修复】
	// 问题: HostClosedConnection 会触发 2 次 HandleNetworkFailure (FailureReceived + ConnectionLost)
	//       每次都 Broadcast OnInterrupted → OnGameInterrupted x2 执行 → LANRoomPage x2 次重建 (内存泄漏)
	// 修复: 加防重入守卫，确保整个中断处理流程在任意时刻只执行一次
	if (bIsInInterrupted)
	{
		UE_LOG(LogUI, Warning,
			TEXT("[UIViewService] OnGameInterrupted: 忽略重复触发 (bIsInInterrupted=true) Panel=%d"),
			(int32)TargetPanel);
		return;
	}
	bIsInInterrupted = true;

	// 强制清理残留状态 (跨地图场景可能有 PendingShowPanel 残留)
	if (PendingShowPanel != EUIPanel::None)
	{
		UWorld* World = GetWorld();
		if (World) World->GetTimerManager().ClearTimer(PCReadyTimerHandle);
		PendingShowPanel = EUIPanel::None;
		PCReadyCheckCount = 0;
	}

	// 幂等检查：如果目标 Panel 已经在显示，直接跳过重建
	if (ActivePanel == TargetPanel && ActiveWidget && ActiveWidget->IsValidLowLevel())
	{
		UE_LOG(LogUI, Log,
			TEXT("[UIViewService] OnGameInterrupted: 目标 Panel=%d 已在显示，跳过重建 (ActiveWidget=%p)"),
			(int32)TargetPanel, ActiveWidget.Get());
		bIsInInterrupted = false;
		return;
	}

	// 强制销毁旧面板
	DestroyActivePanel();

	// 直接调用 ExecuteShowPanel，不走 PC 就绪检查
	// 原因: OnGameInterrupted 触发时 World 已就绪 (从旧地图来), PC 一定可用
	ExecuteShowPanel(TargetPanel);

	bIsInInterrupted = false;
}