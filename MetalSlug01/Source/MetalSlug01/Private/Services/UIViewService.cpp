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
	// 【v54.5.1 Bug 修复】删除 static bool bConfigsInitialized 守卫
	//   旧实现: static bool 静态守卫让配置只在第一次 Initialize 执行
	//     根因: PIE 模式下 GameInstance 被销毁重建，第二次 Initialize 不再填充 PanelConfigs
	//     → StateToPanelMap.Num()=0 → OnGameFlowStateChanged 全都不显示
	//   新实现: 直接填充（UGameInstanceSubsystem 生命周期内只 Initialize 一次，静态守卫是多余防御）
	// ==========================================

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

		// 【大厂 P0 修复 2026.07.03】RoomInside 面板由 RoomPlayerController 接管创建
		// ==========================================
		// 旧设计: UIViewService 也创建 RoomInside, 导致 RoomPC 又创建一遍 → 重复 UI
		// 新设计: UIViewService 不创建 RoomInside, 完全交给 RoomPlayerController 负责
		//   → RoomInside UI 与 RoomPC 的聊天消息系统绑定紧密 (AddChatMessage 等)
		//   → RoomPC 自己创建 + 自己管理生命周期, 避免跨服务同步状态
		// ==========================================
		// 注意: 不再添加 PanelConfigs[EUIPanel::RoomInside]
		//       不再添加 StateToPanelMap[EMatchState::InRoom]
		//       RoomPC::OnFlowStateChanged(InRoom) 是唯一创建入口

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
		// 【大厂 P0 修复 2026.07.03】InRoom 不再映射到 RoomInside (由 RoomPC 接管)
		StateToPanelMap.Add(EMatchState::Login, EUIPanel::Login);
		StateToPanelMap.Add(EMatchState::MainMenu, EUIPanel::MainMenu);
		StateToPanelMap.Add(EMatchState::MainLobby, EUIPanel::LANRoom);
		// 故意不添加: InRoom → RoomInside (RoomPlayerController 负责创建)

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

			// ==========================================
			// 【大厂 P0 修复 2026.07.03】删除 Initialize 阶段的"立即同步"逻辑
			// ==========================================
			// 旧代码 (大厂事故复盘 2026.07.03):
			//   Initialize 阶段主动调 ShowPanelWhenPCReady 创建 widget
			//   此时 PC 可能未就绪, 即使 PC 存在, GetWorld() 返回的是 GameInstance 关联的"当前 World"
			//   在 PIE 启动过程中, "当前 World"可能是 Editor World 或 半初始化的 PIE World
			//   → 创建的 widget 绑到错误的 World
			//   → widget 加入了错误 World 的 GameViewport
			//   → 当真正的 PIE World 完全加载, OnGameFlowStateChanged 再次触发
			//   → ShowPanelWhenPCReady 守卫看到 ActivePanel==Panel && bWidgetStillValid=true
			//   → 跳过 ExecuteShowPanel, 玩家永远看不到 LoginPage (widget 在错误 World 中)
			//
			// 新规则 (Single Source of Truth):
			//   1. Initialize 只挂管线 (订阅 OnStateChanged)
			//   2. 不创建任何 widget, 不预加载任何面板
			//   3. 状态推进完全由 OnGameFlowStateChanged 驱动
			//   4. OnGameFlowStateChanged 通过 PostLoadMapWithWorld 触发
			//      → 此时 World 已就绪, PC 已 ready, 100% 能正确拉起
			//   5. 跨 World 复用检查由 ShowPanelWhenPCReady 守卫承担
			//      (检查 widget 是否属于当前 World, 否则强制重建)
			//
			// 旧代码 (完全删除):
			//   EMatchState CurrentState = FlowSubsystem->GetCurrentState();
			//   if (EUIPanel* PanelPtr = StateToPanelMap.Find(CurrentState))
			//   {
			//       ShowPanelWhenPCReady(*PanelPtr);
			//       ...
			//   }
			//   PreloadConfiguredPanels();  // ← 同样删除 (PC 未就绪)
			// ==========================================
		}
	}

	UE_LOG(LogUI, Log, TEXT("[UIViewService] 初始化完成 (状态推进完全由 OnGameFlowStateChanged 驱动)"));
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

	// 【大厂 P0 修复 v4 2026.07.03】重置全部状态, 防止下次 PIE 启动时残留
	//   旧设计只重置 ActiveWidget, 但 ActivePanel/PendingShowPanel 仍残留
	//   → 下次 PIE: ActivePanel==Login && bWidgetStillValid==true(假阳性) → 守卫拦截 → UI 不显示
	ActivePanel = EUIPanel::None;
	PendingShowPanel = EUIPanel::None;
	PCReadyCheckCount = 0;
	bIsInInterrupted = false;

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
	// 【大厂 P0 修复 v4 2026.07.03】彻底重构守卫: 用 IsInViewport 作为唯一"已显示"凭证
	// ==========================================
	// 旧问题 (v3):
	//   bWidgetInCurrentWorld = (Widget->GetWorld() == CurrentWorld)
	//   → 但 UE 5.6 的 UUserWidget 并不强绑 World (Epic 官方: "user widgets are never bound to a world")
	//   → 即使在错误 World 创建, 仍可能 GetWorld()==CurrentWorld
	//   → 守卫永远命中, ExecuteShowPanel 永远走不到 → Login UI 永远不显示
	//
	// 大厂架构真理:
	//   唯一可靠判断"widget 已显示"的凭证 = IsInViewport() == true
	//   - IsValidLowLevel()==true 只是"对象没死", 不能证明"已显示"
	//   - GetWorld()==CurrentWorld 只是"世界一致", 不能证明"已显示"
	//   - 只有 IsInViewport()==true 才能证明"真的在 viewport 中"
	//
	// 新守卫:
	//   bWidgetReallyShown = IsValidLowLevel() && IsInViewport()
	//   若 ActivePanel==Panel && bWidgetReallyShown → 跳过 (用户已在看 UI)
	//   否则 → 走 ExecuteShowPanel (强制重建/重 Add)
	// ==========================================
	UWorld* CurrentWorld = GetWorld();

	const bool bObjectAlive = ActiveWidget && ActiveWidget->IsValidLowLevel();
	const bool bWidgetReallyShown = bObjectAlive && ActiveWidget->IsInViewport();
	const bool bWidgetInSameWorld = bObjectAlive && ActiveWidget->GetWorld() == CurrentWorld;

	// 【DEBUG-SET-4-C】日志: 增加 IsInViewport 字段 (核心诊断点)
	UE_LOG(LogTemp, Error,
		TEXT("[DEBUG-S4-C][ShowPanelWhenPCReady] PID=%u WorldName=%s Panel=%d ActivePanel=%d "
			 "bObjectAlive=%d bWidgetReallyShown=%d bWidgetInSameWorld=%d "
			 "ActiveWidgetWorld=%s CurrentWorld=%s"),
		FPlatformProcess::GetCurrentProcessId(),
		CurrentWorld ? *CurrentWorld->GetName() : TEXT("NULL"),
		(int32)Panel,
		(int32)ActivePanel,
		bObjectAlive ? 1 : 0,
		bWidgetReallyShown ? 1 : 0,
		bWidgetInSameWorld ? 1 : 0,
		bObjectAlive && ActiveWidget->GetWorld() ? *ActiveWidget->GetWorld()->GetName() : TEXT("NULL"),
		CurrentWorld ? *CurrentWorld->GetName() : TEXT("NULL"));

	// 【大厂 P0 修复 v4】跨 World 缓存清理 (即使对象"看起来活着")
	// 场景: PreloadedWidgets 里残留旧 World 创建的 widget
	//       这些 widget 当前可能在 IsValidLowLevel 但 IsInViewport==false (世界已切换)
	PurgePreloadedWidgetsForCurrentWorld();

	// 【大厂 P0 修复 v4】核心守卫: 只有"真的在 viewport 中"才算已显示
	if (ActivePanel == Panel && bWidgetReallyShown)
	{
		UE_LOG(LogUI, Verbose,
			TEXT("[UIViewService] ShowPanelWhenPCReady: Panel=%d 已在 viewport 中显示, 跳过 (幂等保护)"),
			(int32)Panel);
		return;
	}

	// 【大厂 P0 修复 v4】自愈: 状态匹配但 widget 未显示 → 强制重建
	if (ActivePanel == Panel && !bWidgetReallyShown && bObjectAlive)
	{
		UE_LOG(LogUI, Warning,
			TEXT("[UIViewService] ShowPanelWhenPCReady: Panel=%d 状态匹配但 widget 未显示 (IsInViewport=false), 强制销毁重建"),
			(int32)Panel);
		DestroyActivePanel();
		// 不 return, 继续走 ExecuteShowPanel
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
	UWorld* CurrentWorld = GetWorld();
	UE_LOG(LogTemp, Error,
		TEXT("[DEBUG-SET-4-D][ExecuteShowPanel] PID=%u WorldName=%s Panel=%d HasPreloaded=%d"),
		FPlatformProcess::GetCurrentProcessId(),
		CurrentWorld ? *CurrentWorld->GetName() : TEXT("NULL"),
		(int32)Panel,
		PreloadedWidgets.Contains(Panel) ? 1 : 0);

	// 原有 ShowPanel 逻辑
	DestroyActivePanel();

	// 优先从预创建缓存拿
	UUserWidget* WidgetToShow = PreloadedWidgets.FindRef(Panel);

	// 【大厂 P0 修复 2026.07.03】跨 World 检查
	// 缓存里的 widget 可能是旧 World 的, 即使存在也不能用
	// 这种情况在 PIE 重启 / 跨图加载时常见
	if (WidgetToShow && (WidgetToShow->GetWorld() != CurrentWorld || !WidgetToShow->IsValidLowLevel()))
	{
		UE_LOG(LogUI, Warning,
			TEXT("[UIViewService] ExecuteShowPanel: 预创建 widget 跨 World 或失效 (Panel=%d), 清除缓存走现场创建路径"),
			(int32)Panel);
		PreloadedWidgets.Remove(Panel);
		WidgetToShow = nullptr;
	}

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

	// 【大厂 P0 修复 v4 2026.07.03】AddToViewport 后立即验证
	//   若 IsInViewport 仍为 false, 说明 Slate 状态异常 (InvalidateAllWidgets 时序竞争)
	//   强制二次 AddToViewport 兜底
	if (!ActiveWidget->IsInViewport())
	{
		UE_LOG(LogUI, Warning,
			TEXT("[UIViewService] ExecuteShowPanel: AddToViewport 后仍未显示 (Panel=%d), 二次重试"),
			(int32)Panel);
		ActiveWidget->AddToViewport();
	}

	UE_LOG(LogUI, Log,
		TEXT("[DEBUG-S4-E][ExecuteShowPanel-Show] Panel=%d Widget=%s IsInViewport=%d"),
		(int32)Panel, *ActiveWidget->GetName(), ActiveWidget->IsInViewport() ? 1 : 0);

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
	UWorld* CurrentWorld = GetWorld();
	UE_LOG(LogTemp, Error,
		TEXT("[DEBUG-S4-A][OnGameFlowStateChanged] PID=%u WorldName=%s NewState=%d ActivePanel=%d ActiveWidget=%p ActiveWidgetIsInViewport=%d ActiveWidgetWorld=%s"),
		FPlatformProcess::GetCurrentProcessId(),
		CurrentWorld ? *CurrentWorld->GetName() : TEXT("NULL"),
		(int32)NewState,
		(int32)ActivePanel,
		ActiveWidget.Get(),
		(ActiveWidget && ActiveWidget->IsInViewport()) ? 1 : 0,
		(ActiveWidget && ActiveWidget->GetWorld()) ? *ActiveWidget->GetWorld()->GetName() : TEXT("NULL"));

	// ==========================================
	// 【大厂 P0 修复 2026.07.03】World 切换检测: 销毁任何跨 World 残留
	// ==========================================
	// 场景: PIE 启动/重开, 旧 World 的 widget 残留在 UIViewService 中
	//       (之前是 Editor World 的 widget, 之后 PIE 创建新 World, 但 ActiveWidget 还指向 Editor World 的 widget)
	// 修复: 任何 GameFlow 状态变化时, 检测 ActiveWidget 是不是当前 World 的, 不是就清掉
	//
	// 为什么在 OnGameFlowStateChanged 里做 (而不是只 ShowPanelWhenPCReady)?
	//   ShowPanelWhenPCReady 守卫只针对"目标 Panel == ActivePanel"的情况
	//   但 PostLoadMapWithWorld 第一次 broadcast (NewState=Login, ActivePanel=None) 时不会触发守卫
	//   ActiveWidget 此时可能是旧 World 的脏数据, 不清理就会用脏数据 CreateAndShowPanel
	// ==========================================
	if (ActiveWidget && ActiveWidget->IsValidLowLevel())
	{
		UWorld* WidgetWorld = ActiveWidget->GetWorld();
		if (WidgetWorld != CurrentWorld)
		{
			UE_LOG(LogUI, Warning,
				TEXT("[UIViewService] OnGameFlowStateChanged: 检测到跨 World widget 残留 (WidgetWorld=%s, CurrentWorld=%s), 主动销毁"),
				WidgetWorld ? *WidgetWorld->GetName() : TEXT("NULL"),
				CurrentWorld ? *CurrentWorld->GetName() : TEXT("NULL"));
			// 强制销毁, 不走 DestroyActivePanel (它会调 SetVisibility, 但 widget 在旧 World 已失效)
			ActiveWidget = nullptr;
			ActivePanel = EUIPanel::None;
			// 同步清理 PreloadedWidgets
			PurgePreloadedWidgetsForCurrentWorld();
		}
	}

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

		// ==========================================
		// 【大厂 P0 修复 v4 2026.07.03】终极防御性自愈 (Last-Resort Reconciliation)
		// ==========================================
		// 场景: 即便 ShowPanelWhenPCReady 全部走完, ActiveWidget 仍可能处于
		//       "对象存在 + ActivePanel==Panel + IsInViewport==false" 的诡异状态
		// 根因: 可能是 Slate 在某帧 InvalidateAllWidgets 导致 widget 被踢出 viewport,
		//       或 HUD BeginPlay 主动同步状态时序竞争
		// 大厂模式: 同步广播路径末尾再做一次 Reconcile (协调/再对齐)
		//          → 若 ActivePanel 匹配但 IsInViewport==false → 强制 AddToViewport
		//          → 这是 State 触发的最后一道防线, 不依赖任何缓存
		// ==========================================
		if (ActiveWidget && ActiveWidget->IsValidLowLevel() &&
			ActivePanel == *PanelPtr && !ActiveWidget->IsInViewport())
		{
			UE_LOG(LogUI, Warning,
				TEXT("[UIViewService] Reconcile: ActivePanel=%d 但 widget 未显示, 强制 AddToViewport (Widget=%s)"),
				(int32)*PanelPtr, *ActiveWidget->GetName());
			ActiveWidget->AddToViewport();
			ActiveWidget->SetVisibility(ESlateVisibility::Visible);
		}
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

	// 【大厂 P0 修复 v4 2026.07.03】AddToViewport 后立即验证
	//   Slate::InvalidateAllWidgets 时序竞争可能导致 AddToViewport 后 IsInViewport 仍为 false
	//   强制二次 AddToViewport 兜底
	if (!NewWidget->IsInViewport())
	{
		UE_LOG(LogUI, Warning,
			TEXT("[UIViewService] CreateAndShowPanel: AddToViewport 后仍未显示 (Panel=%d), 二次重试"),
			(int32)Panel);
		NewWidget->AddToViewport();
	}

	ActiveWidget = NewWidget;
	ActivePanel = Panel;

	SetInputMode(Config->InputMode);

	UE_LOG(LogUI, Log,
		TEXT("[DEBUG-S4-E][CreateAndShowPanel-Show] Panel=%d Widget=%s IsInViewport=%d"),
		(int32)Panel, *NewWidget->GetName(), NewWidget->IsInViewport() ? 1 : 0);
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

void UUIViewService::PurgePreloadedWidgetsForCurrentWorld()
{
    // 【大厂 P0 修复 2026.07.03】跨 World 残留清理
    // 思路: 遍历 PreloadedWidgets, 移除所有"不属于当前 World"的 widget
    // 这些 widget 是 PIE 启动前或上一个 PIE World 留下来的, 现在已经无效
    UWorld* CurrentWorld = GetWorld();
    if (!CurrentWorld) return;

    TArray<EUIPanel> ToRemove;
    for (const auto& Pair : PreloadedWidgets)
    {
        UUserWidget* Widget = Pair.Value;
        if (!Widget || !Widget->IsValidLowLevel() || Widget->GetWorld() != CurrentWorld)
        {
            ToRemove.Add(Pair.Key);
        }
    }

    for (EUIPanel Panel : ToRemove)
    {
        UUserWidget* Widget = PreloadedWidgets.FindRef(Panel);
        if (Widget)
        {
            // 从 viewport 移除 (如果还在)
            // 【修复 C4996 弃用警告 2026.07.03】RemoveFromViewport 已弃用, 改用 RemoveFromParent
            if (Widget->IsInViewport())
            {
                Widget->RemoveFromParent();
            }
        }
        PreloadedWidgets.Remove(Panel);
        UE_LOG(LogUI, Log,
            TEXT("[UIViewService] PurgePreloadedWidgetsForCurrentWorld: 清理 Panel=%d (跨 World)"),
            (int32)Panel);
    }
}

APlayerController* UUIViewService::GetLocalPlayerController() const
{
	// 【v54.5 Bug 修复】不能用 World->GetFirstPlayerController()
	//   旧实现: NM_ListenServer 模式下 GetFirstPlayerController() 返回服务器 PlayerController
	//           服务器 PC 绑到服务器 World → widget 不在客户端 Viewport 中 → Login 页面不显示
	//   新实现: 用 GameInstance.GetFirstGamePlayer() → GetPlayerController() 正确返回本地客户端 PlayerController
	//   原理: GetFirstGamePlayer() 是 UE 5.x GameInstance 层的权威玩家 API, 不依赖 NetMode
	UGameInstance* GI = GetGameInstance();
	if (!GI) return nullptr;

	ULocalPlayer* FirstPlayer = GI->GetFirstGamePlayer();
	if (!FirstPlayer) return nullptr;

	return FirstPlayer->GetPlayerController(GetWorld());
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