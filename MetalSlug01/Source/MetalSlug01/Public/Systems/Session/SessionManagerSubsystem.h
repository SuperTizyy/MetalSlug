// MetalSlug01. All Rights Reserved.
#pragma once

// ==========================================
// 标准库 & UE 引擎头文件
// ==========================================
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "SessionResult.h"
#include "Enums/CoreEnums.h" // EMatchState（大厂架构：事件参数用状态而非面板，避免循环依赖）
#include "SessionManagerSubsystem.generated.h"

// ==========================================
// 业务委托声明（Dynamic 类型，UFUNCTION 绑定）
// ==========================================

/** 搜索房间完成回调 */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnFindRoomsComplete, bool, bWasSuccessful, const TArray<FRoomSessionResult>&, Rooms);

/** 创建房间完成回调（Dynamic，单播，BindDynamic 绑定到 UFUNCTION） */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnCreateRoomComplete, bool, bWasSuccessful, const FString&, ErrorMessage);

/** 加入房间完成回调（Dynamic，单播） */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnJoinRoomComplete, bool, bWasSuccessful, const FString&, ConnectString);

/** 销毁房间完成回调（Dynamic，单播） */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnDestroyRoomComplete, bool, bWasSuccessful, const FString&, ErrorMessage);

/**
 * @class USessionManagerSubsystem
 * @brief 封装所有 OnlineSubsystem 会话操作的子系统
 *
 * @section 设计目的
 * - 单一职责：所有 Session 相关操作（搜索/创建/加入/销毁）集中在此
 * - 内存安全：所有 FDelegateHandle 在此管理，析构时统一清理
 * - UI 解耦：Widget 不再直接访问 OnlineSubsystem，通过此子系统间接操作
 *
 * @section 使用方式
 * - 通过 UGameInstance::GetSubsystem<USessionManagerSubsystem>() 获取
 * - 所有操作均为异步，通过委托回调通知结果
 * - 使用 BlueprintAssignable 多播委托可实现多人监听（如大厅 + 房间 UI 同时显示）
 */
UCLASS()
class METALSLUG01_API USessionManagerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ==========================================
	// 【公开】会话操作 API
	// ==========================================

	/**
	 * 搜索局域网房间
	 * @param Delegate 搜索完成回调
	 */
	UFUNCTION(BlueprintCallable, Category = "SessionManager")
	void FindRooms(const FOnFindRoomsComplete& Delegate);

	/**
	 * 创建房间（Host）
	 * @param Params 创房参数
	 * @param Delegate 创建完成回调
	 */
	UFUNCTION(BlueprintCallable, Category = "SessionManager")
	void CreateRoom(const FRoomCreationParams& Params, const FOnCreateRoomComplete& Delegate);

	/**
	 * 加入房间（Client）
	 * @param Room 要加入的房间
	 * @param Delegate 加入完成回调
	 */
	UFUNCTION(BlueprintCallable, Category = "SessionManager")
	void JoinRoom(const FRoomSessionResult& Room, const FOnJoinRoomComplete& Delegate);

	/**
	 * 销毁当前房间（Host）
	 * @param Delegate 销毁完成回调
	 */
	UFUNCTION(BlueprintCallable, Category = "SessionManager")
	void DestroyRoom(const FOnDestroyRoomComplete& Delegate);

	/**
	 * 检查当前账号是否在指定房间列表中
	 * @param Rooms 房间列表
	 * @param AccountName 账号名
	 * @return 是否存在于任意房间
	 */
	UFUNCTION(BlueprintPure, Category = "SessionManager")
	bool IsAccountInAnyRoom(const TArray<FRoomSessionResult>& Rooms, const FString& AccountName) const;

	// ==========================================
	// 【公开】状态查询
	// ==========================================

	/** 是否正在 Hosting */
	UFUNCTION(BlueprintPure, Category = "SessionManager|State")
	bool IsHosting() const { return bIsHost; }

	/** 是否在会话中 */
	UFUNCTION(BlueprintPure, Category = "SessionManager|State")
	bool IsInSession() const { return bIsInSession; }

	/** 是否正在搜索 */
	UFUNCTION(BlueprintPure, Category = "SessionManager|State")
	bool IsSearching() const { return bIsSearching; }

	/**
	 * 获取当前会话的房间显示信息（房间名 + 游戏模式）
	 * @param OutRoomName 房间名称（未设置时返回空字符串）
	 * @param OutGameMode 游戏模式（未设置时返回空字符串）
	 * @return 是否成功读取
	 *
	 * 【v54.5.1 新增】优先返回 skip-login 测试房间名（格式: 测试-{模式}-{地图}）
	 *   根因: bSkipLoginDirectToLobby=true 时不创建 Session, SessionSettings 里没有 ROOM_NAME
	 *   修复: skip-login 模式下 SessionManager 持有 SkipLoginRoomName/SkipLoginGameMode 字段
	 *         GetCurrentSessionDisplayInfo 优先返回这两个字段, 找不到才读 SessionSettings
	 */
	UFUNCTION(BlueprintCallable, Category = "SessionManager|State")
	bool GetCurrentSessionDisplayInfo(FString& OutRoomName, FString& OutGameMode) const;

	// ==========================================
	// 【v54.5.1 新增】skip-login 测试房间显示信息 (bSkipLoginDirectToLobby 专用)
	// ==========================================
	// 说明: bSkipLoginDirectToLobby=true 时 SessionManager 不创建真实 Session
	//       房间名/模式由 GameFlowSubsystem 在 BootToLogin Step3 写入这两个字段
	//       GetCurrentSessionDisplayInfo 优先返回这两个字段
	//
	// 格式: 测试-{模式}-{地图}
	// 示例: 测试-刀战模式-Japanese_Temple_Demo / 测试-生化模式-Demonstration
	//
	// 职责:
	//   - SetSkipLoginRoomDisplayInfo: GameFlowSubsystem BootToLogin 末尾调用 (唯一写入入口)
	//   - GetCurrentSessionDisplayInfo: RoomInsidePage NativeConstruct 读取 (唯一读取入口)
	//   - 离开 InRoom 状态时清空 (ResetSkipLoginRoomDisplayInfo)

	/** 设置 skip-login 测试房间的显示信息 */
	UFUNCTION(BlueprintCallable, Category = "SessionManager|SkipLogin")
	void SetSkipLoginRoomDisplayInfo(const FString& InRoomName, const FString& InGameMode);

	/** 清空 skip-login 测试房间显示信息 (离开 InRoom 时调用) */
	UFUNCTION(BlueprintCallable, Category = "SessionManager|SkipLogin")
	void ResetSkipLoginRoomDisplayInfo();

	// ==========================================
	// 【P0 大厂架构】会话状态实时更新 (Bug2 修复)
	// ==========================================
	// Bug: 大厅人数不会随房间内玩家/AI 变化而更新
	// 根因: TOTAL_PLAYERS_WITH_AI 在 BuildSessionSettings 时初始化为 1 (房主),
	//       整段代码库没有任何地方在玩家加入 / 离开 / AI 增删时调用 UpdateSession 更新这个值
	// 修复: 房主端通过本接口主动推送当前总人数到 SessionSettings,
	//       其他客户端在 FindSessions 后拿到的是最新值
	//
	// 调用时机:
	//   - 玩家 PC 完成登录 (AccountRoomAuthority.HandleLoginRequest 成功路径)
	//   - 玩家 PC 下线 (HandleLogoutRequest / HandleControllerDestroyed / SweepDeadSessions)
	//   - AI 添加 / 移除 (RoomInsidePage.OnConfirmAddAIClicked, 占位时也一样)
	//
	// 安全策略:
	//   - 只在房主端调用 (IsHosting() 为真时), 非房主端被静默忽略
	//   - 无活跃 Session 时直接 return, 不报错
	//   - 复用 IOnlineSession::UpdateSession 在 LAN 上广播
	// ==========================================

	/**
	 * 【核心 API】房主端专用: 把当前总人数 (真人 + AI) 推送到 SessionSettings
	 * @param TotalPlayersWithAI 房主维护的权威人数 (含 AI)
	 * @return 是否成功发起 UpdateSession (异步, 不会等待完成)
	 */
	UFUNCTION(BlueprintCallable, Category = "SessionManager|RoomState")
	bool BroadcastRoomPlayerCount(int32 TotalPlayersWithAI);

	/**
	 * 【静态便捷入口】无需 UObject* 即可触发, 内部自动定位 GameInstance 上的 SessionManager
	 * 用法: USessionManagerSubsystem::BroadcastRoomPlayerCountStatic(this, 5);
	 */
	UFUNCTION(BlueprintCallable, Category = "SessionManager|RoomState", meta = (WorldContext = "WorldContextObject"))
	static bool BroadcastRoomPlayerCountStatic(const UObject* WorldContextObject, int32 TotalPlayersWithAI);

	// ==========================================
	// 【公开】多播事件（Presenter/Widget 可直接监听）
	// ==========================================

	/** 房间列表更新事件（公开多播事件，供 Presenter/Widget 订阅） */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomsFound, const TArray<FRoomSessionResult>&, Rooms);
	UPROPERTY(BlueprintAssignable, Category = "SessionManager|Events")
	FOnRoomsFound OnRoomsFound;

	// ==========================================
	// 【大厂 P0 架构】会话终止事件
	// ==========================================
	// 触发时机（跨场景统一触发源）:
	//   1. Host 调用 DestroyRoom 成功（房主主动退房 / 销毁 LAN Session）
	//   2. Client 调用 JoinRoom 失败 + SessionDoesNotExist（目标 Session 已消失）
	//   3. Client 调用 JoinRoom 失败 + CouldNotRetrieveAddress（目标 Session 地址无效）
	//   4. Client 调用 JoinRoom 失败 + SessionIsFull（房间已满 — 也算"无法进入"，触发回退）
	//
	// 设计原则:
	//   - 单一事件源: SessionManager 是会话生命周期的权威, 不依赖 NetDriver / GameState
	//   - 跨地图持久: GameInstanceSubsystem, 切图不丢失
	//   - 业务解耦: 业务方（GameFlowSubsystem）订阅后转 UI 中断
	//   - 参数用 EMatchState 而非 EUIPanel, 避免循环依赖
	//
	// 数据流（场景 1: Client 未进房, Host 退房）:
	//   Host: DestroySession 成功 → OnSessionTerminated.Broadcast(MainLobby)
	//   Host 本进程: GameFlow 订阅 → 抢先 OpenLevel(L_Login) + 触发 UI 中断
	//   Client 进程: LAN UDP 心跳消失, Client 端 SessionManager 在下一次 Find/Join 时检测到 SessionDoesNotExist → 触发
	//
	// 数据流（场景 2: Client 已进房, Host 退房）:
	//   Client 进程: GameFlow.HandleNetworkFailure(HostClosed) → 走统一 HandleSessionTerminated 路径
	//
	// 为什么不直接用 EUIPanel 而用 EMatchState:
	//   SessionManager 不应该知道 UI 层的细节; 让 GameFlow 翻译状态到 UI 中断面板。
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSessionTerminated, EMatchState, SuggestedState);
	UPROPERTY(BlueprintAssignable, Category = "SessionManager|Events")
	FOnSessionTerminated OnSessionTerminated;

protected:
	// ==========================================
	// 【受保护】子系统生命周期
	// ==========================================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	// ==========================================
	// 【私有】内部回调
	// ==========================================

	/** FindSessions 完成回调 */
	void HandleFindSessionsComplete(bool bWasSuccessful);

	/** CreateSession 完成回调 */
	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);

	/** JoinSession 完成回调 */
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

	/** DestroySession 完成回调 */
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	// ==========================================
	// 【私有】工具函数
	// ==========================================

	/** 清理所有委托句柄 */
	void ClearAllDelegateHandles();

	/** 获取会话接口 */
	IOnlineSessionPtr GetSessionInterface() const;

	/** 创建会话设置 */
	TSharedPtr<FOnlineSessionSettings> BuildSessionSettings(const FRoomCreationParams& Params);

	/** 解析搜索结果为结构化数据 */
	TArray<FRoomSessionResult> ParseSearchResults() const;

	// ==========================================
	// 【私有】成员变量
	// ==========================================

	/** 在线子系统指针 */
	IOnlineSubsystem* OnlineSubsystem = nullptr;

	/** 会话接口指针 */
	IOnlineSessionPtr Sessions;

	/** 搜索结果缓存 */
	TSharedPtr<FOnlineSessionSearch> SessionSearch;

	/** 当前会话设置（用于创建/更新） */
	TSharedPtr<FOnlineSessionSettings> CurrentSessionSettings;

	/** 搜索完成回调（内部使用） */
	FOnFindRoomsComplete PendingFindDelegate;

	/** 创建完成回调（内部使用） */
	FOnCreateRoomComplete PendingCreateDelegate;

	/** 加入完成回调（内部使用） */
	FOnJoinRoomComplete PendingJoinDelegate;

	/** 销毁完成回调（内部使用） */
	FOnDestroyRoomComplete PendingDestroyDelegate;

	// ==========================================
	// 【私有】委托句柄
	// ==========================================

	FDelegateHandle FindSessionsHandle;
	FDelegateHandle CreateSessionHandle;
	FDelegateHandle JoinSessionHandle;
	FDelegateHandle DestroySessionHandle;

	// ==========================================
	// 【私有】状态标志
	// ==========================================

	bool bIsHost = false;
	bool bIsInSession = false;
	bool bIsSearching = false;

	// ==========================================
	// 【v54.5.1 新增】skip-login 测试房间显示信息 (bSkipLoginDirectToLobby 专用)
	// ==========================================
	// 格式: 测试-{模式}-{地图} (由 GameFlowSubsystem 在 BootToLogin 时写入)
	FString SkipLoginRoomName;
	FString SkipLoginGameMode;
};
