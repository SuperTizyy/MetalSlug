// MetalSlug01. All Rights Reserved.

#pragma once

// ==========================================
// UE 引擎 & 标准库头文件
// ==========================================
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AutoTestRoomCoordinatorSubsystem.generated.h"

// ==========================================
// 前置声明
// ==========================================
class USessionManagerSubsystem;

/**
 * @class UAutoTestRoomCoordinatorSubsystem
 * @brief 自动测试房间协调器（按 PIEWindowID 启动顺序指定方案）
 *
 * @section 激活条件
 * 全部配置都通过 UMetalSlugTestSettings 控制:
 *   - bSkipLoginDirectToLobby = true   必须开启
 *   - bAutoCreateTestRoom    = true   必须开启
 *
 * 由 GameFlowSubsystem::BootToLogin() 在 bSkipLoginDirectToLobby=true 分支中
 * 调用 StartAutoTestRoom() 启动.
 *
 * @section 大厂架构设计: 按 PIEWindowID 启动顺序指定
 *
 * 核心思想: 利用 UE PIE 模式下的 -PIEWindowID=X 命令行参数作为全局唯一实例编号,
 *           把"第一个启动的客户端"(PIEWindowID=1) 硬性指定为房主, 其他为加入者.
 *
 * 为什么用 PIEWindowID 而不是 GetIndexInGameInstance():
 *   - GetIndexInGameInstance(): 每个独立进程的 LocalPlayer[0] 都返回 0 → 三个客户端全部当房主
 *   - PIEWindowID: UE 全局分配的窗口编号, 跨进程唯一 → 1=房主, 2,3...=加入者
 *
 * 行为矩阵:
 *   - PIEWindowID=1 (第一个启动的):  创房 + OpenLevel(地图, ?listen) → 成为房主
 *   - PIEWindowID>1 (后续启动的):   搜索 AutoTestRoom → JoinRoom + ClientTravel → 加入
 *   - 无参数 (非 PIE/Standalone):   默认为 PIEWindowID=0 → 当房主 (单客户端场景)
 *
 * @section 委托绑定规范（UE Dynamic Delegate）
 * SessionManager 暴露的委托全部是 DECLARE_DYNAMIC_* 系列:
 *   - OnRoomsFound (动态多播) → AddDynamic / RemoveDynamic
 *   - FOnFindRoomsComplete (动态单播) → BindDynamic
 *   - FOnCreateRoomComplete (动态单播) → BindDynamic
 *   - FOnJoinRoomComplete (动态单播) → BindDynamic
 */
UCLASS()
class METALSLUG01_API UAutoTestRoomCoordinatorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ==========================================
	// 公共接口
	// ==========================================

	/**
	 * 启动自动测试房间协调流程
	 * @param InTestRoomName 要搜索/创建的房间名称
	 */
	UFUNCTION(BlueprintCallable, Category = "AutoTestRoom")
	void StartAutoTestRoom(const FString& InTestRoomName);

	/**
	 * 停止协调流程 (可随时调用)
	 */
	UFUNCTION(BlueprintCallable, Category = "AutoTestRoom")
	void StopAutoTestRoom();

	/** 是否正在协调中 */
	UFUNCTION(BlueprintPure, Category = "AutoTestRoom")
	bool IsAutoTestRunning() const { return bIsRunning; }

	/**
	 * 获取本客户端的 GameInstance 索引 (调试用)
	 * PIE NumberOfClients=3 时, 返回 0/1/2 按启动顺序
	 */
	UFUNCTION(BlueprintPure, Category = "AutoTestRoom")
	int32 GetMyGameInstanceIndex() const;

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	// ==========================================
	// 内部: World 事件监听 (解决 PIE 热切换地图时 Timer 被清空问题 2026.06.30)
	// ==========================================

	/**
	 * 当旧 World 被完全清理时调用
	 * 保存当前 timer 参数 (bIsHost, CachedPIEWindowID, TargetRoomName)
	 * 以便在新 World 就绪后重启
	 */
	void OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources);

	/**
	 * 当新 World 完成初始化时调用
	 * 检查是否有待重启的 timer 参数, 有则在新 World 上重启
	 */
	void OnPostWorldInitialization(UWorld* InWorld, const UWorld::InitializationValues IVS);

	/** 重启挂起的 timer (从保存的状态恢复) */

	// ==========================================
	// 内部: 状态机回调 (UFUNCTION 必加, 动态委托要求)
	// ==========================================

	/** 房间搜索完成回调 (订阅 SessionManager::OnRoomsFound 动态多播) */
	UFUNCTION()
	void OnRoomsFound(const TArray<struct FRoomSessionResult>& Rooms);

	/** 创建房间完成回调 */
	UFUNCTION()
	void OnCreateRoomComplete(bool bWasSuccessful, const FString& ErrorMessage);

	/** 加入房间完成回调 */
	UFUNCTION()
	void OnJoinRoomComplete(bool bWasSuccessful, const FString& ConnectString);

	/** FindRooms 单播回调的 No-Op handler (BindDynamic 要求 UFUNCTION) */
	UFUNCTION()
	void HandleFindRoomsNoOp(bool bWasSuccessful, const TArray<struct FRoomSessionResult>& Rooms);

	// ==========================================
	// 内部: 计时器回调 (普通函数)
	// ==========================================

	/** Index=0: 等待 World ready 后开始建房的延迟回调 */
	void OnHostDelayTimerFired();

	/** Index>0: 搜索计时器触发 (固定间隔, 简化版) */
	void OnSearchTimerFired();

	/** 第一次执行: 延迟 Index*0.5s 让 Index 0 先建好 */
	void OnFirstSearchTimerFired();

	// ==========================================
	// 内部: 执行动作
	// ==========================================

	/** 创建 AutoTestRoom 房间 (Index 0 客户端) */
	void HostAutoTestRoom();

	/** 执行一轮 FindRooms 搜索 (Index>0 客户端) */
	void TriggerFindRooms();

	/** 在结果中查找目标房间 */
	bool FindTargetRoomInResults(
		const TArray<struct FRoomSessionResult>& Rooms,
		struct FRoomSessionResult& OutRoom) const;

	/** 获取 SessionManager 子系统 */
	USessionManagerSubsystem* GetSessionManager() const;

	/** 内部停止 */
	void InternalStop();

	// ==========================================
	// 状态变量
	// ==========================================

	/** 是否正在运行 */
	UPROPERTY(Transient)
	bool bIsRunning = false;

	/** 目标房间名称 (所有客户端搜索同一名称) */
	FString TargetRoomName;

	/** 搜索间隔 (秒) */
	static constexpr float SearchIntervalSeconds = 1.0f;

	/** Index 0 建房前的延迟 (让 World + SessionManager 完全就绪) */
	static constexpr float HostDelaySeconds = 1.0f;

	/** Index N>0 第一次搜索的延迟 (N * 这个值, 给 Index 0 留出建房时间) */
	static constexpr float PerClientStaggerSeconds = 0.5f;

	/** 搜索计时器句柄 */
	FTimerHandle SearchTimerHandle;

	/** Host 延迟计时器句柄 */
	FTimerHandle HostDelayTimerHandle;

	/** SessionManager::OnRoomsFound 委托绑定标志 (避免重复绑定) */
	bool bIsDelegateBound = false;

	/** 解析到的 PIEInstanceIndex (0=房主, >0=加入者, INDEX_NONE=非PIE默认房主) */
	int32 CachedPIEWindowID = 0;

	// ==========================================
	// World 热切换保护状态 (2026.06.30)
	// ==========================================

	/** 标记: 是否有待重启的 timer 参数 (World 被清理前已设置 timer 但未触发) */
	bool bHasPendingTimerParams = false;

	/** 待重启角色: true=Host(创建房间), false=Joiner(搜索加入) */
	bool bPendingIsHost = false;

	/** 待重启的延迟时间 (秒) */
	float PendingDelay = 0.0f;

	/** World 事件委托句柄 (用于 RemoveDynamic) */
	FDelegateHandle OnWorldCleanupHandle;
	FDelegateHandle OnPostWorldInitHandle;
};
