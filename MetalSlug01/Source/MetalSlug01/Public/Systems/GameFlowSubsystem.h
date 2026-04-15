#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFlowSubsystem.generated.h"

/**
 * @enum EMatchState
 * @brief 全局游戏链路状态机
 * 严格定义玩家在游戏生命周期中可能处于的每一个核心阶段
 */
UENUM(BlueprintType)
enum class EMatchState : uint8
{
	PreLogin		UMETA(DisplayName = "Pre-Login"),       // 预加载/Logo阶段（目前预留）
	Login			UMETA(DisplayName = "Login"),           // 登录态：在此展示账号输入界面
	MainLobby		UMETA(DisplayName = "Main Lobby"),      // 大厅态：选角、匹配、主菜单
	InRoom			UMETA(DisplayName = "In Room"),         // 房间态：局域网房间等待对战
	Battleing		UMETA(DisplayName = "Battleing"),       // 战斗态：核心对战关卡内
	PostBattle		UMETA(DisplayName = "Post Battle")      // 结算态：战斗结束展示成绩
};

// 【架构规范：事件驱动】
// 声明一个动态多播委托。当状态改变时，广播给所有关心的 UI 或 Controller，让他们自己去刷新表现
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameFlowStateChanged, EMatchState, NewState);

/**
 * @class UGameFlowSubsystem
 * @brief 游戏全局流程控制中心 (Game Flow Manager)
 * 工业级规范：全权负责地图流转和宏观状态调度。UI 蓝图绝对不能直接调用 OpenLevel，必须通过本系统！
 */
UCLASS()
class METALSLUG01_API UGameFlowSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ==========================================
	// 核心状态调度接口
	// ==========================================

	/**
	 * @brief 切换游戏流程状态的主入口
	 * @param NewState 目标状态
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|GameFlow")
	void TransitToState(EMatchState NewState);

	/**
	 * @brief 获取当前的游戏链路状态
	 */
	UFUNCTION(BlueprintPure, Category = "MetalSlug|GameFlow")
	EMatchState GetCurrentState() const { return CurrentState; }

	// ==========================================
	// 事件广播
	// ==========================================

	// 当状态成功发生改变时，自动触发此事件。UI 界面和 Controller 应监听此事件以实现自动显示/隐藏。
	UPROPERTY(BlueprintAssignable, Category = "MetalSlug|GameFlow")
	FOnGameFlowStateChanged OnStateChanged;
	
	// ==========================================
	// 动态地图流转支持
	// ==========================================

	/**
	 * @brief 在准备进入房间前，设置目标对战地图的名字 (例如 "L_DesertGrayMap")
	 * 必须在调用 TransitToState(EMatchState::InRoom) 之前调用！
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|GameFlow")
	void SetTargetRoomMapName(FName MapName);

	UFUNCTION(BlueprintPure, Category = "MetalSlug|GameFlow")
	FName GetTargetRoomMapName() const { return TargetRoomMapName; }

private:
	// 内部记录当前状态，默认给一个未初始化的预加载状态
	UPROPERTY(Transient)
	EMatchState CurrentState = EMatchState::PreLogin;

	/**
	 * @brief 执行进入新状态时的底层物理操作（例如切换关卡地图）
	 * @param State 即将进入的新状态
	 */
	void HandleStateEntry(EMatchState State);
	
	// 缓存玩家即将进入的战斗/房间地图名称
	UPROPERTY(Transient)
	FName TargetRoomMapName = NAME_None;
};