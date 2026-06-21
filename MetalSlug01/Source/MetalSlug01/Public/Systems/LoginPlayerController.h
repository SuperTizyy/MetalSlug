// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 APlayerController 类（基类）
#include "GameFramework/PlayerController.h"

// UE 自动生成的头文件
#include "LoginPlayerController.generated.h"

// 前置声明: 避免直接包含 UserWidget 头文件，加快编译速度
class UUserWidget;

/**
 * @class ALoginPlayerController
 * @brief 专门负责主菜单/登录地图的玩家控制器
 *
 * 职责说明:
 * - 监听 UGameFlowSubsystem 的全局状态变化
 * - 根据状态自动决定向玩家展示登录页还是局域网大厅
 * - 是登录地图的"UI 总管"
 *
 * 架构理念:
 * 1. 单一职责: 本类只管登录/大厅 UI 切换，不参与任何游戏内逻辑
 * 2. 事件驱动: 不主动轮询状态，而是订阅 OnStateChanged 自动响应
 * 3. 解耦: 不直接引用 UGameFlowSubsystem，而是由 BeginPlay 中 GetSubsystem 获取
 */
UCLASS()
class METALSLUG01_API ALoginPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	/**
	 * UE 原生生命周期函数: 在 Actor 首次被初始化时调用
	 * 用途: 获取 GameFlowSubsystem 引用并订阅其 OnStateChanged 事件
	 */
	virtual void BeginPlay() override;

	/**
	 * 登录界面的蓝图类（在 BP_LoginPlayerController 中配置）
	 * 目的: 动态 CreateWidget 创建登录页 UI
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<UUserWidget> LoginUIClass;

	/**
	 * 主菜单界面的蓝图类（在 BP_LoginPlayerController 中配置）
	 * 目的: 登录成功后进入 MainLobby 状态时，动态创建主菜单 UI
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<UUserWidget> GameMenuUIClass;

	/**
	 * 局域网房间界面的蓝图类（在 BP_LoginPlayerController 中配置）
	 * 目的: 点击"多人模式"时，动态创建局域网房间 UI
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<UUserWidget> LANRoomUIClass;

	/**
	 * 监听全局状态变化的事件回调
	 * @param NewState 新的全局流程状态
	 * 触发时机: UGameFlowSubsystem::TransitToState 被调用时
	 */
	
	UFUNCTION()
	void OnFlowStateChanged(EMatchState NewState);

private:
	/**
	 * 缓存当前创建的登录 UI（避免重复创建）
	 * 标记 Transient: 不参与序列化，纯运行时引用
	 */
	UPROPERTY(Transient)
	UUserWidget* ActiveLoginWidget = nullptr;

	/**
	 * 缓存当前创建的大厅 UI（避免重复创建）
	 * 标记 Transient: 不参与序列化，纯运行时引用
	 */
	UPROPERTY(Transient)
	UUserWidget* ActiveLobbyWidget = nullptr;
};
