// MetalSlug01. All Rights Reserved.
#pragma once

/**
 * @file LANRoomPresenter.h
 * @brief 大厅页面的 Presenter (ViewModel) — 状态机 + 业务逻辑
 *
 * 大厂原则 — MVVM 模式:
 *   - View (LANRoomPage) 只负责展示, 不持有业务状态
 *   - Presenter (本类) 持有会话状态机 + 缓存
 *   - Model (SessionManagerSubsystem) 负责 OnlineSubsystem 操作
 *
 * 架构升级历史:
 *   - v22-v54: UObject 派生, 手动 NewObject 创建
 *   - v54+: UGameInstanceSubsystem, 全局单例自动管理
 */

// ==========================================
// 标准库 & UE 引擎头文件
// ==========================================
#include "CoreMinimal.h"
// 【架构升级】从 UObject 升级为 UGameInstanceSubsystem
// 原因: 让 ViewModel 自动成为全局单例, View 不再需要 NewObject
#include "Subsystems/GameInstanceSubsystem.h"
#include "SessionResult.h"
#include "UI/Framework/IViewModel.h"
#include "LANRoomPresenter.generated.h"

// ==========================================
// 前向声明
// ==========================================
class USessionManagerSubsystem;

// ==========================================
// 状态枚举
// ==========================================

/** 大厅页面状态机 */
UENUM(BlueprintType)
enum class ELANRoomState : uint8
{
	Idle,           // 空闲（初始状态）
	Searching,       // 搜索中
	AccountChecking, // 账号冲突检测中
	Creating,       // 创建房间中
	Joining,        // 加入房间中
	Error           // 错误状态
};

// ==========================================
// 委托声明
// ==========================================

/** 状态变化通知 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPresenterStateChanged);

/** 房间列表刷新通知 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomListRefreshed);

/** 账号冲突检测通知 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAccountConflictDetected);

/** 错误通知 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnErrorOccurred, const FString&, ErrorMessage);

/** 加入房间成功通知 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJoinRoomSuccess, const FString&, ConnectString);

/**
 * @class ULANRoomPresenter
 * @brief 大厅页面的展示器，负责会话状态机和业务逻辑
 *
 * @section 设计目的
 * - 承接 UI 层与 SessionManagerSubsystem 之间的所有业务逻辑
 * - 管理大厅页面状态机（Idle/Searching/AccountChecking/Creating/Joining/Error）
 * - 处理账号冲突检测（创房前检查同号）
 * - 生成防闪烁签名，比对房间变化
 *
 * @section 职责边界
 * - 持有会话相关的 FDelegateHandle（通过 SessionManagerSubsystem 间接持有）
 * - 不直接操作 UI 控件
 * - 不持有 FOnlineSessionSearchResult 原始数据（由 SessionManagerSubsystem 处理）
 * - 存储房间列表缓存（FRoomSessionResult）
 *
 * @section 使用方式
 * - 由 ALoginPlayerController 或 UGameFlowSubsystem 创建
 * - 通过 Initialize() 注入 SessionManagerSubsystem 依赖
 * - Widget 通过监听多播委托获取状态变化通知
 */
UCLASS(Blueprintable, BlueprintType)
class METALSLUG01_API ULANRoomPresenter
    : public UGameInstanceSubsystem
    , public IViewModel
{
    GENERATED_BODY()

public:
    // ==========================================
    // Subsystem 生命周期（替代原 Initialize(USessionManagerSubsystem*)）
    // ==========================================

    /**
     * @brief Subsystem 启动时自动调用
     * 职责: 自动注入 SessionManagerSubsystem 依赖（无需外部 Initialize）
     */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief Subsystem 销毁时自动调用
     * 职责: 解绑所有 SessionManager 委托
     */
    virtual void Deinitialize() override;

    // ==========================================
    // IViewModel 接口实现
    // ==========================================

    virtual void OnWidgetShow() override;
    virtual void OnWidgetHide() override;
    virtual void BindView(UUserWidget* InView) override;
    /**
     * @brief 解绑 View (IViewModel 接口) — 触发时机: Widget 销毁或 Hide
     */
    virtual void UnbindView() override;
    /** @brief 检查是否已绑定 View (IViewModel 接口) */
    virtual bool IsBoundToView() const override { return BoundView.IsValid(); }
    virtual FName GetViewModelType() const override { return TEXT("LANRoomPresenter"); }

	// ==========================================
	// 【公开】玩家操作入口（由 Widget 调用）
	// ==========================================

	/** 刷新房间列表 */
	UFUNCTION(BlueprintCallable, Category = "LANRoomPresenter")
	void RequestRefreshRoomList();

	/** 创建房间 */
	UFUNCTION(BlueprintCallable, Category = "LANRoomPresenter")
	void RequestCreateRoom(const FString& RoomName, const FString& Password,
		const FString& GameMode, const FString& MapName, FName LevelName);

	// ==========================================
	// 【P0 架构升级】房主身份通知 - 由 SessionManager 创房/加房回调触发
	// ==========================================

	/**
	 * 通知 Presenter: 本地玩家已成为房主
	 * 职责:
	 *  - 内部调用 URoomService::BroadcastHostChanged(WorldContext, true)
	 *  - RoomInsidePage 订阅 OnHostChanged 后会立即刷新按钮可见性
	 *
	 * 调用时机:
	 *  - SessionManager.OnCreateRoomComplete 成功 (自己创房)
	 *  - 加入房间后, 服务器自动转让房主 (未来扩展)
	 */
	UFUNCTION(BlueprintCallable, Category = "LANRoomPresenter|RoomService")
	void NotifyBecameHost();

	/**
	 * 通知 Presenter: 本地玩家已成为普通客户端
	 * 职责:
	 *  - 内部调用 URoomService::BroadcastHostChanged(WorldContext, false)
	 *
	 * 调用时机:
	 *  - SessionManager.OnJoinRoomComplete 成功 (加入他人房间)
	 *  - 主动转让房主 (未来扩展)
	 */
	UFUNCTION(BlueprintCallable, Category = "LANRoomPresenter|RoomService")
	void NotifyBecameClient();

	/** 选中房间 */
	UFUNCTION(BlueprintCallable, Category = "LANRoomPresenter")
	void OnRoomSelected(int32 RoomIndex);

	/** 加入选中的房间 */
	UFUNCTION(BlueprintCallable, Category = "LANRoomPresenter")
	void RequestJoinSelectedRoom();

	// ==========================================
	// 【公开】数据查询（Widget 显示用）
	// ==========================================

	UFUNCTION(BlueprintPure, Category = "LANRoomPresenter|Data")
	ELANRoomState GetCurrentState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "LANRoomPresenter|Data")
	const TArray<FRoomSessionResult>& GetCachedRoomList() const { return CachedRoomList; }

	/** @brief 当前选中的房间索引 (INDEX_NONE 表示未选) */
	UFUNCTION(BlueprintPure, Category = "LANRoomPresenter|Data")
	int32 GetSelectedRoomIndex() const { return SelectedRoomIndex; }

	UFUNCTION(BlueprintPure, Category = "LANRoomPresenter|Data")
	FRoomSessionResult GetSelectedRoom() const;

	UFUNCTION(BlueprintPure, Category = "LANRoomPresenter|Data")
	FString GetLastErrorMessage() const { return LastErrorMessage; }

	UFUNCTION(BlueprintPure, Category = "LANRoomPresenter|Data")
	bool HasAccountConflict() const { return bHasAccountConflict; }

	// ==========================================
	// 【公开】多播事件（Widget 监听）
	// ==========================================

	UPROPERTY(BlueprintAssignable, Category = "LANRoomPresenter|Events")
	FOnPresenterStateChanged OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "LANRoomPresenter|Events")
	FOnRoomListRefreshed OnRoomListRefreshed;

	UPROPERTY(BlueprintAssignable, Category = "LANRoomPresenter|Events")
	FOnAccountConflictDetected OnAccountConflictDetected;

	UPROPERTY(BlueprintAssignable, Category = "LANRoomPresenter|Events")
	FOnErrorOccurred OnErrorOccurred;

	UPROPERTY(BlueprintAssignable, Category = "LANRoomPresenter|Events")
	FOnJoinRoomSuccess OnJoinRoomSuccess;

	/** 创建房间成功通知 */
	UPROPERTY(BlueprintAssignable, Category = "LANRoomPresenter|Events")
	FOnJoinRoomSuccess OnCreateRoomSuccess;

protected:
	// ==========================================
	// 【受保护】状态转换
	// ==========================================

	/** 切换状态 */
	void TransitionTo(ELANRoomState NewState);

	/** 实际创建房间（账号冲突检查通过或无需检查时调用） */
	void ProceedToCreateRoom();

	// ==========================================
	// 【受保护】内部回调
	// ==========================================

	/** 搜索完成回调 */
	UFUNCTION()
	void OnFindRoomsComplete(bool bWasSuccessful, const TArray<FRoomSessionResult>& Rooms);

	/** 账号冲突检查搜索完成回调 */
	UFUNCTION()
	void OnAccountCheckFindComplete(bool bWasSuccessful, const TArray<FRoomSessionResult>& Rooms);

	/** 创建房间完成回调 */
	UFUNCTION()
	void OnCreateRoomComplete(bool bWasSuccessful, const FString& ErrorMessage);

	/** 加入房间完成回调 */
	UFUNCTION()
	void OnJoinRoomComplete(bool bWasSuccessful, const FString& ConnectString);

	/** 创房前的销毁完成回调（Handle 销毁后的连锁创房） */
	UFUNCTION()
	void OnDestroyRoomBeforeCreate(bool bWasSuccessful, const FString& ErrorMessage);

private:
    // ==========================================
    // 【私有】依赖
    // ==========================================

    /**
     * 会话管理器（由 Subsystem::Initialize 自动注入）
     * 用 TObjectPtr + UPROPERTY, UE 自动管理 GC
     * 为什么不用 TWeakObjectPtr: 同属 GameInstance 子系统, 生命周期一致, 无需 weak 防御
     */
    UPROPERTY(Transient)
    TObjectPtr<USessionManagerSubsystem> SessionManager = nullptr;

    /**
     * IViewModel: 当前绑定的 View
     * 用 TWeakObjectPtr, 避免 View 销毁后野指针（View 是 Widget, 可能比 Subsystem 早销毁）
     */
    UPROPERTY(Transient)
    TWeakObjectPtr<UUserWidget> BoundView;

	// ==========================================
	// 【私有】状态
	// ==========================================

	UPROPERTY()
	ELANRoomState CurrentState = ELANRoomState::Idle;

	FString LastErrorMessage;
	bool bHasAccountConflict = false;

	// ==========================================
	// 【私有】数据缓存
	// ==========================================

	TArray<FRoomSessionResult> CachedRoomList;
	TArray<FString> CachedSignatures;
	int32 SelectedRoomIndex = INDEX_NONE;

	/** 创房参数（账号冲突检查通过后用于实际创建） */
	FRoomCreationParams PendingCreationParams;

	/** 账号冲突检测完成后的回调（避免重复搜索） */
	bool bAccountCheckPassed = false;

	// ==========================================
	// 【私有】标志位
	// ==========================================

	bool bIsWidgetVisible = false;
};
