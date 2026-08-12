// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/Framework/IViewModel.h"
#include "TimerManager.h"
#include "Enums/CoreEnums.h"

class UUserWidget;
class URoomInsidePage;
class ULANRoomPage;
class ULANRoomPresenter;
class ULoginPage;
class UGameMenuWidget;

/**
 * @enum EUIInputMode
 * @brief 输入模式
 */
UENUM(BlueprintType)
enum class EUIInputMode : uint8
{
	UIOnly      UMETA(DisplayName = "UI Only"),
	GameAndUI   UMETA(DisplayName = "Game and UI"),
	GameOnly    UMETA(DisplayName = "Game Only")
};

// UE 自动生成的头文件（必须放在最后一行）
#include "UIViewService.generated.h"

/**
 * @struct FPanelConfig
 * @brief 面板配置（Widget 类 + 预创建标志 + 输入模式）
 */
USTRUCT(BlueprintType)
struct FPanelConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIView")
	TSubclassOf<UUserWidget> WidgetClass;

	/** 是否在 GameInstance 启动时预创建（Lyra CommonGameUI 模式：避免战斗中卡顿） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIView")
	bool bPreloadOnInit = false;

	/** 默认输入模式 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UIView")
	EUIInputMode InputMode = EUIInputMode::UIOnly;
};

/**
 * @class UUIViewService
 * @brief UI 视图服务（大厂标准 Lyra CommonGameUI 实现）
 *
 * 【大厂标准架构：L2 Service Layer】
 * - 订阅 UGameFlowSubsystem::OnStateChanged
 * - 自动创建/销毁 Widget（View 自己不 CreateWidget）
 * - 自动注入 ViewModel（Presenter）（View 自己不 NewObject）
 * - 自动应用输入模式
 * - 预创建机制：高频面板预加载，避免战斗中卡顿
 *
 * 设计原则（修正版）:
 * 1. 单一职责: 只管 UI 显示/隐藏/输入模式
 * 2. View 完全无感知: View 不知道自己的 Widget 类是如何被创建的
 * 3. ViewModel 完全无感知: View 不知道 ViewModel 怎么来
 * 4. 全局唯一: GameInstance 子系统
 *
 * 【大厂对应】
 * - Lyra: UCommonGameUI / UCommonActivatableWidget
 * - Riot: UIManager
 * - EA Frostbite: UIViewController
 */
UCLASS()
class METALSLUG01_API UUIViewService : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ==========================================
	// Subsystem 生命周期
	// ==========================================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ==========================================
	// 公共 API
	// ==========================================

	/** 显示指定面板（自动隐藏其他面板，自动注入 ViewModel） */
	UFUNCTION(BlueprintCallable, Category = "UIView")
	void ShowPanel(EUIPanel Panel);

	/** 隐藏所有面板 */
	UFUNCTION(BlueprintCallable, Category = "UIView")
	void HideAllPanels();

	/** 设置输入模式（鼠标 + 输入类型） */
	UFUNCTION(BlueprintCallable, Category = "UIView")
	void SetInputMode(EUIInputMode Mode);

	UFUNCTION(BlueprintPure, Category = "UIView")
	EUIPanel GetActivePanel() const { return ActivePanel; }

	/** 获取当前激活的 Widget（供业务层订阅事件时使用） */
	UFUNCTION(BlueprintPure, Category = "UIView")
	UUserWidget* GetActiveWidget() const { return ActiveWidget; }

	// ==========================================
	// 面板变化事件（供 MusicManager 等业务层订阅）
	// ==========================================

	/** 面板激活时广播（参数：旧面板 → 新面板） */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPanelChanged, EUIPanel, OldPanel, EUIPanel, NewPanel);
	UPROPERTY(BlueprintAssignable, Category = "UIView|Events")
	FOnPanelChanged OnPanelChanged;

	// ==========================================
	// 配置（蓝图可改）
	// ==========================================

	/** 各面板配置（WidgetClass + 预创建 + 输入模式） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UIView|Config")
	TMap<EUIPanel, FPanelConfig> PanelConfigs;

	/** 状态 → 面板自动映射 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UIView|Config")
	TMap<EMatchState, EUIPanel> StateToPanelMap;

private:
	// ==========================================
	// 内部方法
	// ==========================================

	UFUNCTION()
	void OnGameFlowStateChanged(EMatchState NewState);

	/** 公共入口：如果 PC 已就绪则立即 ShowPanel，否则暂存到 PendingShowPanel 等下一帧 */
	void ShowPanelWhenPCReady(EUIPanel Panel);

	/** 执行真正的面板显示逻辑（PC 已验证就绪） */
	void ExecuteShowPanel(EUIPanel Panel);

	/** 预创建配置中标记为 bPreloadOnInit=true 的面板 */
	void PreloadConfiguredPanels();

	/** 创建并显示指定面板（核心：含 ViewModel 注入） */
	void CreateAndShowPanel(EUIPanel Panel);

	/** 仅创建面板（不显示，用于预加载） */
	void PreCreatePanel(EUIPanel Panel, const FPanelConfig& Config);

	/** 销毁当前面板 */
	void DestroyActivePanel();

	/**
	 * 【大厂 P0 修复 2026.07.03】清理 PreloadedWidgets 中所有不属于当前 World 的残留 widget
	 *
	 * 触发场景:
	 *   PIE 启动 → 新 World 创建 → 旧 World 的预创建 widget 还残留在 PreloadedWidgets 中
	 *   这些 widget 属于旧 World, 即使 IsValidLowLevel() 也返回 true
	 *   → 后续 ExecuteShowPanel 用缓存里的"老" widget 失败 (World 已切换)
	 *
	 * 设计: 在 ShowPanelWhenPCReady 检测到跨 World widget 时调用
	 */
	void PurgePreloadedWidgetsForCurrentWorld();

	/** 为指定面板注入 ViewModel（核心解耦点） */
	void InjectViewModelForPanel(EUIPanel Panel, UUserWidget* NewWidget);

	/** 设置输入模式（内部方法） — 已在 public 区声明，此处不再重复 */

	/** 定时器回调：PC 就绪后尝试显示 PendingShowPanel */
	UFUNCTION()
	void TryShowPendingPanel();

	APlayerController* GetLocalPlayerController() const;

	// ==========================================
	// 私有数据成员
	// ==========================================

	/** 当前显示的面板 */
	UPROPERTY(Transient)
	EUIPanel ActivePanel = EUIPanel::None;

	/** 当前显示的 Widget */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ActiveWidget = nullptr;

	/** 预创建的 Widget 缓存（隐藏状态） */
	UPROPERTY(Transient)
	TMap<EUIPanel, TObjectPtr<UUserWidget>> PreloadedWidgets;

	/** 待显示的面板（PC 未就绪时暂存，等下一帧 PC 就绪后自动显示） */
	UPROPERTY(Transient)
	EUIPanel PendingShowPanel = EUIPanel::None;

	/** 等待 PC 就绪的定时器句柄（超时后自动取消） */
	FTimerHandle PCReadyTimerHandle;

	/** PC 就绪检查计数器（防无限等待，上限 MaxPCReadyChecks 次） */
	int32 PCReadyCheckCount = 0;
	static constexpr int32 MaxPCReadyChecks = 10;

	/** 【大厂架构 - 独立中断通道】OnInterrupted 委托句柄 */
	FDelegateHandle OnInterruptedDelegateHandle;

	/** 【大厂架构 - 独立中断通道】防重入守卫：OnGameInterrupted 执行期间置 true，防止嵌套触发（HostClosed 会触发 2 次回调） */
	bool bIsInInterrupted = false;

	/** 【大厂架构 - 独立中断通道】中断处理回调 */
	UFUNCTION()
	void OnGameInterrupted(EUIPanel TargetPanel);
};