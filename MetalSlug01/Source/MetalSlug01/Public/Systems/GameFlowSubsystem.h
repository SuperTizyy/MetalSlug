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

// UE 自动生成的头文件（必须放在最后一行）
#include "GameFlowSubsystem.generated.h"

/**
 * @enum EMatchState
 * @brief 全局游戏链路状态机
 *
 * 严格定义玩家在游戏生命周期中可能处于的每一个核心阶段。
 * 任何"我下一步该显示什么 UI / 进入哪个关卡"的逻辑都必须以本枚举作为唯一切换依据。
 */
UENUM(BlueprintType)
enum class EMatchState : uint8
{
	PreLogin		UMETA(DisplayName = "Pre-Login"),       // 预加载/Logo 阶段（目前预留，用于启动 Logo 展示）
	Login			UMETA(DisplayName = "Login"),           // 登录态：在此展示账号输入界面
	MainMenu		UMETA(DisplayName = "Main Menu"),       // 主菜单态：单人/多人模式选择（不跳转地图，留在 L_Login）
	MainLobby		UMETA(DisplayName = "Main Lobby"),      // 大厅态：局域网房间列表/匹配（不跳转地图，留在 L_Login）
	InRoom			UMETA(DisplayName = "In Room"),         // 房间态：局域网房间等待对战
	Battleing		UMETA(DisplayName = "Battleing"),       // 战斗态：核心对战关卡内
	PostBattle		UMETA(DisplayName = "Post Battle")      // 结算态：战斗结束展示成绩
};

// 【架构规范：事件驱动】
// 声明一个动态多播委托。当状态改变时，广播给所有关心的 UI 或 Controller，让他们自己去刷新表现
// 参数 NewState: 即将切换到的新状态
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameFlowStateChanged, EMatchState, NewState);

/**
 * @class UGameFlowSubsystem
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
};
