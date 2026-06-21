// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件（FString/TArray/基础宏）
#include "CoreMinimal.h"

// 引入 UGameInstanceSubsystem（子系统基类）头文件
// 作用: 本类继承自 UGameInstanceSubsystem，是跨越整个游戏生命周期的全局管理器
#include "Subsystems/GameInstanceSubsystem.h"

// 引入共享枚举定义（EMatchState, EUIPanel），打破 GameFlowSubsystem <-> UIViewService 循环依赖
#include "Enums/CoreEnums.h"

// 引入 SessionManager 头文件（GameInstance 级 Subsystem），订阅 OnSessionTerminated 事件
// 大厂架构: GameFlow 是 Session 生命周期 + 地图流转的统一调度, SessionManager 只负责 Session 单一职责
#include "Systems/Session/SessionManagerSubsystem.h"

// UE 自动生成的头文件（必须放在最后一行）
#include "GameFlowSubsystem.generated.h"

// 【架构规范：事件驱动】
// 【大厂架构 2026.06.28 - 修复 P0 编译错误】
// 修复: DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam 宏在 UE 5.6 中
//   必须出现在 *.generated.h 之后 (注意 RoomGameState.h 也是此顺序)。
//   原因: 该宏展开时引用 CURRENT_FILE_ID 拼接的 FID_*_DELEGATE 宏,
//         以及 FUNC_DECLARE_DYNAMIC_MULTICAST_DELEGATE 宏, 都在 .generated.h 中定义。
//   若放在 .generated.h 之前, 在 Subsystem 头文件这种 include 链较短的场景下,
//   编译器会报 "缺少 ; (在 <class-head> 的前面)" 错误。
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameFlowStateChanged, EMatchState, NewState);

// 【大厂架构 - 独立中断通道】
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameInterrupted, EUIPanel, TargetPanel);

/**
 * @brief 游戏全局流程控制中心 (Game Flow Manager)
 *
 * 工业级规范: 全权负责地图流转和宏观状态调度。UI 蓝图绝对不能直接调用 OpenLevel，必须通过本系统！
 *
 * 架构优势:
 * 1. 全局唯一: 作为 GameInstance 子系统，游戏中只有一份
 * 2. 状态机驱动: 杜绝跨模块直接调用 OpenLevel 造成的状态错乱
 * 3. 事件驱动: UI 通过订阅 OnStateChanged 自动响应，无需主动轮询
 * 4. 安全防御: 防止同一状态被重复切换造成地图无限重启
 */
UCLASS()
class METALSLUG01_API UGameFlowSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ==========================================
	// 生命周期
	// ==========================================

	/** 启动期一次性检查活动 DataTable 完整性 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 关闭期清理活动 DataTable 缓存 */
	virtual void Deinitialize() override;

	/**
	 * @brief 【大厂标准】从初始 PreLogin 状态引导到 Login 状态
	 *
	 * 调用时机: UGameInstance::Init() 末尾，所有 Subsystem 初始化完成之后
	 *
	 * 为什么要单独写一个方法而不是直接 TransitToState(Login)?
	 * - 大厂规范: Boot 入口只暴露这一个，外部不能随意调用
	 * - 内部仍然走 TransitToState 走安全校验
	 * - 方便未来在 BootToLogin 中插入启动期逻辑 (如 LoadingScreen、PlayerLogin 等)
	 *
	 * 工作流程:
	 * 1. 检查当前状态是否还是 PreLogin (防呆: 防止外部重复 Boot)
	 * 2. 调用 TransitToState(Login)
	 * 3. 广播 OnStateChanged(Login)
	 * 4. UIViewService 收到事件 → 显示 LoginPage
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|GameFlow")
	void BootToLogin();

public:
	// ==========================================
	// 核心状态调度接口
	// ==========================================

	/**
	 * @brief 切换游戏流程状态的主入口
	 * @param NewState 目标状态
	 *
	 * 流程:
	 * 1. 安全校验: 如果已在该状态则直接返回
	 * 2. 更新 CurrentState 成员
	 * 3. 广播 OnStateChanged 事件（让 UI/Controller 准备切换）
	 * 4. 调用 HandleStateEntry() 执行底层物理操作（如 OpenLevel）
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|GameFlow")
	void TransitToState(EMatchState NewState);

	/**
	 * @brief 获取当前的游戏链路状态
	 * @return CurrentState 当前状态
	 *
	 * 用于 UI 初始化时判断该显示哪个界面、Controller 判断该响应哪些输入等
	 */
	UFUNCTION(BlueprintPure, Category = "MetalSlug|GameFlow")
	EMatchState GetCurrentState() const { return CurrentState; }

	// ==========================================
	// 事件广播
	// ==========================================

	/**
	 * 当状态成功发生改变时自动触发此事件
	 * UI 界面和 Controller 应监听此事件以实现自动显示/隐藏
	 * 使用方式: 在 UI 蓝图中绑定 OnStateChanged -> Switch on EMatchState -> Show/Hide
	 */
	UPROPERTY(BlueprintAssignable, Category = "MetalSlug|GameFlow")
	FOnGameFlowStateChanged OnStateChanged;

	/**
	 * 【大厂架构 - 独立中断通道】
	 * 当发生网络失败、强制退出等中断信号时触发。
	 * 专门用于强制刷新 UI，不经过状态机的幂等保护。
	 *
	 * 使用场景:
	 *   - host 关闭房间 → 客户端网络失败 → HandleNetworkFailure
	 *   → 直接 Broadcast OnInterrupted(LANRoom) → UIViewService 强制 ShowPanel
	 *
	 * 设计优势:
	 *   - 独立于状态机，任何状态都能触发
	 *   - 广播参数是 uint8 (EUIPanel 的整数值)，避免循环依赖
	 *   - UIViewService 收到后强制 ShowPanel，无中间转换层
	 */
	UPROPERTY(BlueprintAssignable, Category = "MetalSlug|GameFlow")
	FOnGameInterrupted OnInterrupted;

	// ==========================================
	// 动态地图流转支持
	// ==========================================

	/**
	 * @brief 在准备进入房间前，设置目标对战地图的名字 (例如 "L_DesertGrayMap")
	 *
	 * 必须在调用 TransitToState(EMatchState::InRoom) 之前调用！
	 * 否则进入 InRoom 时会因为找不到地图名而报 Error
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|GameFlow")
	void SetTargetRoomMapName(FName MapName);

	/**
	 * @brief 获取当前缓存的目标地图名
	 * @return TargetRoomMapName 已缓存的目标地图名（未设置时为 NAME_None）
	 */
	UFUNCTION(BlueprintPure, Category = "MetalSlug|GameFlow")
	FName GetTargetRoomMapName() const { return TargetRoomMapName; }

	// ==========================================
	// 【P0 架构升级】跨地图"下一步状态"意图机制
	// ==========================================

	/**
	 * @brief 【大厂标准 - P0 修复】设置"下一张地图加载完后希望切入的状态"
	 *
	 * 使用场景:
	 *   - 退房 (RoomPC::ExecuteLeaveRoom) → OpenLevel(L_Login) → 新 GI 触发 BootToLogin → 显示 Login
	 *     这会导致玩家"退房"后看到登录页, 而非大厅页
	 *   - 解决: 业务方先调用本接口, 再 OpenLevel
	 *     → Subsystem 跨地图持久, 在 PostLoadMapWithWorld 回调中覆盖 BootToLogin 设的 Login
	 *
	 * 设计原则 (大厂三大铁律):
	 *   1. 意图持久化: 业务意图必须放在跨地图持久的层 (Subsystem), 不能放在即将销毁的 PC 上
	 *   2. 一次性消费: PostLoadMapWithWorld 触发后立即清零, 防止重复消费污染后续地图
	 *   3. 默认兜底: 不调用本接口时, 新地图加载完仍走 BootToLogin → Login (向后兼容)
	 *
	 * 调用示例:
	 *   Flow->RequestStateOnNextLoad(EMatchState::MainLobby);
	 *   UGameplayStatics::OpenLevel(this, FName("L_Login"));
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|GameFlow")
	void RequestStateOnNextLoad(EMatchState DesiredState);

	/**
	 * @brief 内部: PostLoadMapWithWorld 回调 (跨地图持久, 不会因 PC 销毁而丢失)
	 *
	 * 大厂设计: 把"地图加载完该干嘛"的逻辑放到 GameInstance 级别的 Subsystem,
	 *          因为 Subsystem 跨地图持久, 不会被 EndPlay 抢先解绑.
	 *
	 * @param LoadedWorld 引擎刚加载好的新 World
	 *
	 * 行为:
	 *   1. 如果 bHasPendingStateOnNextLoad == true:
	 *      - 强制 TransitToState(PendingPostLoadState), 覆盖 BootToLogin 的 Login
	 *      - 清零标志位, 防止重复消费
	 *   2. 否则: 不做任何事 (走默认 BootToLogin 流程)
	 */
	void HandlePostLoadMapWithWorld(UWorld* LoadedWorld);

private:
	/**
	 * 内部记录当前状态，默认给一个未初始化的预加载状态
	 * 标记 Transient: 不参与磁盘序列化，纯运行时数据
	 */
	UPROPERTY(Transient)
	EMatchState CurrentState = EMatchState::PreLogin;

	/**
	 * @brief 执行进入新状态时的底层物理操作（例如切换关卡地图）
	 * @param State 即将进入的新状态
	 *
	 * 内部通过 switch-case 分别处理登录/大厅/房间/战斗/结算的物理切换逻辑
	 */
	void HandleStateEntry(EMatchState State);

	/**
	 * 缓存玩家即将进入的战斗/房间地图名称
	 * 标记 Transient: 不参与磁盘序列化
	 */
	UPROPERTY(Transient)
	FName TargetRoomMapName = NAME_None;

	// ==========================================
	// 【P0 架构升级】跨地图"下一步状态"意图持久化字段
	// ==========================================

	/**
	 * 是否设置了"下一张地图加载完后希望切入的状态"
	 * 设计: 不参与磁盘序列化, 仅运行时使用
	 */
	UPROPERTY(Transient)
	bool bHasPendingStateOnNextLoad = false;

	/**
	 * 期望在下一张地图加载完毕后切入的状态
	 * 配合 bHasPendingStateOnNextLoad 一起使用
	 */
	UPROPERTY(Transient)
	EMatchState PendingPostLoadState = EMatchState::PreLogin;

	/**
	 * PostLoadMapWithWorld 委托句柄 (一次性, 处理完后会 Reset)
	 * 重要: 这个委托挂在 GameInstance 级别 Subsystem 上,
	 *       跨地图持久, 不会被 PC 的 EndPlay 抢先解绑.
	 */
	FDelegateHandle PostLoadMapHandle;

	/**
	 * 【大厂 P0 修复 2026.06.28】网络失败回调句柄
	 *
	 * 用于 AddUObject/Remove 配对, 确保解绑正确.
	 */
	FDelegateHandle NetworkFailureHandle;

	/** 【大厂 P0 修复 v2】网络失败冷却时间戳: 500ms 内忽略重复触发 (防止 HostClosedConnection 的双重回调) */
	double LastNetworkFailureTimestamp = 0.0;

private:
	/**
	 * 【大厂架构 - 独立中断通道】
	 * 强制中断入口。不经过状态机，不改变游戏状态，只刷新 UI。
	 *
	 * 调用时机: 网络断开、强制退出等中断信号触发时。
	 *
	 * 行为:
	 *   1. 调用 OnInterrupted.Broadcast((uint8)TargetPanel)
	 *   2. UIViewService 收到 → 强制 ShowPanel(TargetPanel)
	 *   3. 状态机 CurrentState 不变（等待地图加载完成后再通过 PostLoadMapWithWorld 同步）
	 *
	 * @param TargetPanel 中断后希望显示的 UI 面板
	 */
	UFUNCTION()
	void HandleInterrupt(uint8 TargetPanel);

	/**
	 * 【大厂 P0 修复 2026.06.28】网络失败回调（引擎级广播）
	 *
	 * 触发时机: 客户端与服务器断开连接（host 关闭房间 / 网络异常）
	 *
	 * @param InWorld        触发失败的 World
	 * @param NetDriver      失败的 NetDriver
	 * @param FailureType    失败类型 (ENetworkFailure)
	 * @param ErrorString    错误描述字符串
	 */
	UFUNCTION()
	void HandleNetworkFailure(UWorld* InWorld, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

	/**
	 * 【大厂 P0 架构】会话终止回调（订阅 USessionManagerSubsystem::OnSessionTerminated）
	 *
	 * 触发时机:
	 *   - Host 主动 DestroySession 成功
	 *   - Client JoinSession 失败且 SessionDoesNotExist / SessionIsFull / CouldNotRetrieveAddress
	 *
	 * 行为:
	 *   1. 抢占 OpenLevel(L_Login ?offline), 时序抢占 UE 引擎的 ?closed SetClientTravel
	 *   2. 触发 OnInterrupted.Broadcast(LANRoom), 让 UIViewService 强制刷新面板
	 *   3. 通过 RequestStateOnNextLoad 把目标状态持久化到下一张地图
	 *
	 * 设计原则 (大厂三大铁律):
	 *   1. 单一事件源: 所有"Session 终止"路径统一进入此方法
	 *   2. 跨地图持久: GameFlow 是 GameInstanceSubsystem, 切图不丢
	 *   3. 防重入: 1秒冷却, HostClosedConnection 会触发多次回调
	 *
	 * @param SuggestedState 业务方建议的下一张地图状态 (通常是 MainLobby)
	 */
	UFUNCTION()
	void HandleSessionTerminated(EMatchState SuggestedState);
};
