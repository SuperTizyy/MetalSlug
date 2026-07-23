// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "UI/MyGameHUD.h"

// 引入 UE UserWidget 基类
#include "Blueprint/UserWidget.h"

// 引入统一日志通道
#include "Logs/MetalSlugLogChannels.h"

// 引入 GameFlowSubsystem（用于订阅状态变化）
#include "Systems/GameFlowSubsystem.h"

// 【架构升级】改为通过 URoomStateService 读取比赛数据, View 不再直引 RoomGameState
#include "Services/RoomStateService.h"

// 引入 GameHUDWidget（用于显示战斗 HUD）
#include "UI/Game/GameHUDWidget.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * AMyGameHUD::BeginPlay
 *
 * HUD 初始化入口
 * 1. 提前在后台创建所有游戏 HUD 并隐藏（对象池/预加载思维）
 * 2. 向 GameFlowSubsystem 订阅状态变化
 *
 * 【大厂 P0 修复 2026.07.03】移除主动同步初始状态
 *   旧设计: BeginPlay 末尾调 OnGameFlowStateChanged(GetCurrentState())
 *   问题: HUD 在 L_Login 地图的 BeginPlay 时, World 已就绪但 Slate 还未完成首次 paint
 *         → OnGameFlowStateChanged → UIViewService ShowPanel → AddToViewport
 *         → 紧接着 UE 的 InvalidateAllWidgets 把刚 Add 的 widget 踢出
 *         → UI 永远不可见
 *   新设计: 完全依赖 UIViewService::OnGameFlowStateChanged 的自愈/兜底
 *         → HUD 不主动同步, 避免与 PostLoadMapWithWorld 的 broadcast 竞争
 */
void AMyGameHUD::BeginPlay()
{
	Super::BeginPlay();

	// 提前在后台创建所有游戏 HUD 并隐藏，避免战斗瞬间卡顿（对象池/预加载思维）
	CreateGameHUD();

	// ==========================================
	// 向管家订阅状态改变的"报纸"
	// ==========================================
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSubsystem->OnStateChanged.AddDynamic(this, &AMyGameHUD::OnGameFlowStateChanged);

			// 【大厂 P0 修复 2026.07.03】不再主动同步初始状态
			//   由 UIViewService 通过 PostLoadMapWithWorld 路径 B 延迟 broadcast 兜底
			//   HUD 不抢 World-ready 信号, 避免与 Slate InvalidateAllWidgets 时序竞争
		}
	}
}


/**
 * AMyGameHUD::CreateGameHUD
 *
 * 创建游戏 HUD（私有方法）
 * 工业规范: UI 的创建必须严格绑定到真实的玩家控制器，而不是宽泛的 GetWorld()
 */
void AMyGameHUD::CreateGameHUD()
{
	// 【工业规范】: UI 的创建必须严格绑定到真实的玩家控制器，而不是宽泛的 GetWorld()
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		UE_LOG(LogUI, Error, TEXT("[MyGameHUD] 严重错误: 未获取到本地 PlayerController！"));
		return;
	}

	// 1. 创建并隐藏主 HUD（包含准星、计分板等所有游戏UI）
	if (GameHUDWidgetClass)
	{
		GameHUDWidget = CreateWidget<UGameHUDWidget>(PC, GameHUDWidgetClass);
		if (GameHUDWidget)
		{
			GameHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
			GameHUDWidget->AddToViewport(0); // Z-Order为0，贴在屏幕底层
		}
	}
	else
	{
		UE_LOG(LogUI, Warning, TEXT("[MyGameHUD] GameHUDWidgetClass 蓝图中未配置！"));
	}
}


/**
 * AMyGameHUD::EndPlay
 *
 * HUD 销毁时清理委托
 * 工业规范: Actor 被销毁前必须退订报纸，否则会报野指针崩溃
 */
void AMyGameHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 【工业规范】: Actor 被销毁前（例如切换地图或退出游戏），必须退订报纸，否则会报野指针崩溃
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSubsystem->OnStateChanged.RemoveDynamic(this, &AMyGameHUD::OnGameFlowStateChanged);
		}
	}
	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 2. 状态机核心调度逻辑
// ==========================================

/**
 * AMyGameHUD::OnGameFlowStateChanged
 *
 * GameFlowSubsystem 状态变化回调
 * Battleing: 显示 HUD + 准星 + 刷新倒计时和回合数
 * 其他: 隐藏 HUD + 准星
 *
 * 【架构重构】: HUD 只负责 UI 元素的展示与隐藏
 * 已将鼠标控制权交还给 PlayerController，避免代码职责互相覆盖
 */
void AMyGameHUD::OnGameFlowStateChanged(EMatchState NewState)
{
	UE_LOG(LogGameFlow, Log, TEXT("[MyGameHUD] OnGameFlowStateChanged called: NewState=%d"), (int32)NewState);

	if (NewState == EMatchState::Battleing)
	{
		if (GameHUDWidget)
		{
			UE_LOG(LogUI, Log, TEXT("[MyGameHUD] Setting HUD visible..."));

			// 规范: 使用 SelfHitTestInvisible，允许自身的按钮点击（如果有），但无视背景
			GameHUDWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

			// 显示准星（由 GameHUD 统一管理）
			GameHUDWidget->ShowCrosshair();

// 刷新倒计时和回合数的当前值（解决 HUD 显示后才绑定导致的初始值不刷新的问题）
		// 【架构升级】改为通过 URoomStateService 读取（不直引 RoomGameState）
		// 【v92 大厂架构重构】用 TotalRounds 替换 CurrentRound (大厂原则 — UI 显示用真理源, 不用内部计数)
		if (URoomStateService* StateService = URoomStateService::Get(this))
		{
			const FMatchSnapshot Snapshot = StateService->GetMatchSnapshot();
			GameHUDWidget->UpdateTotalRoundsText(Snapshot.TotalRounds);
			GameHUDWidget->OnMatchModeChangedForHUD(Snapshot.MatchMode);
		}
		else
		{
			UE_LOG(LogUI, Warning, TEXT("[MyGameHUD] RoomStateService 不可用, 无法刷新倒计时"));
		}
		}

		UE_LOG(LogUI, Log, TEXT("[MyGameHUD] 切换至战斗UI, 成功展示主HUD与准星"));
	}
	else
	{
		// 如果是房间态或其它状态，隐藏所有战斗 UI（包括准星）
		if (GameHUDWidget)
		{
			GameHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
			GameHUDWidget->HideCrosshair();
		}
	}
}
