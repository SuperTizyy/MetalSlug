// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// MyGameHUD 头文件 — 自定义 HUD 类
// ==========================================
//
// 文件作用:
//   1. 声明 AMyGameHUD 类 — 继承 AHUD
//   2. 在战斗中持有 UGameHUDWidget 实例
//   3. 监听 GameFlowSubsystem::OnStateChanged 自动响应游戏状态变化
//
// 设计理念 (大厂原则 - 预加载思维):
//   1. HUD 只负责 UI 元素的展示与隐藏, 鼠标控制权交还给 PlayerController
//   2. 提前在后台预创建所有游戏 HUD 并隐藏 (对象池) — 避免战斗瞬间卡顿
//   3. 工业级防泄漏: EndPlay 中解绑委托, 防止野指针
//
// 大厂对应:
//   - Lyra: ULyraHUD
//   - Fortnite: AHUDBase
// ==========================================

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 AHUD 类（基类）
#include "GameFramework/HUD.h"

// UE 自动生成的头文件
#include "MyGameHUD.generated.h"

// 前置声明: 加快编译速度
class UUserWidget;
class UGameHUDWidget;

/**
 * @class AMyGameHUD
 * @brief 项目自定义 HUD 类
 *
 * 职责说明:
 * - 继承自 UE 原生 AHUD，在战斗中持有 UGameHUDWidget 实例
 * - 通过监听 UGameFlowSubsystem::OnStateChanged 自动响应游戏状态变化
 * - 提前在后台创建所有游戏 HUD 并隐藏（对象池/预加载思维）
 *
 * 架构理念:
 * 1. HUD 只负责 UI 元素的展示与隐藏，鼠标控制权交还给 PlayerController
 * 2. 提前预创建 HUD，避免战斗瞬间卡顿
 * 3. 工业级防泄漏: 在 EndPlay 中解绑委托
 *
 * 【关键字段说明】
 * - GameHUDWidgetClass: EditAnywhere, 美术在 BP_MyGameHUD 中配置 Widget 蓝图类
 * - GameHUDWidget:       UPROPERTY 持有 Widget 实例 — BeginPlay 时预创建
 */
UCLASS()
class METALSLUG01_API AMyGameHUD : public AHUD
{
	GENERATED_BODY()

protected:
	// ==========================================
	// 生命周期
	// ==========================================

	/**
	 * BeginPlay — UE 原生生命周期
	 *
	 * @brief  Actor 首次被初始化时由引擎调用
	 * @note   用途: 创建 HUD 预加载 + 订阅 GameFlowSubsystem 状态变化
	 */
	virtual void BeginPlay() override;

	/**
	 * EndPlay — UE 原生生命周期
	 *
	 * @brief  Actor 被销毁时由引擎调用
	 * @note   用途: 解绑 GameFlowSubsystem 委托, 防止野指针/内存泄漏
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// ==========================================
	// 公共接口
	// ==========================================

	/**
	 * GetGameHUDWidget — 获取游戏 HUD Widget
	 *
	 * @brief  供 RoomPlayerController 调用, 获取已创建的 HUD 实例
	 * @return GameHUDWidget 指针 (未创建时返回 nullptr)
	 */
	UGameHUDWidget* GetGameHUDWidget() const { return GameHUDWidget; }

protected:
	// ==========================================
	// 蓝图配置
	// ==========================================

	/**
	 * 游戏 HUD Widget 类（在 BP_MyGameHUD 中配置）
	 * 用途: 战斗内 HUD 容器（血条/准星/计分板等）
	 * 数据流向: 蓝图配置 → C++ 构造期绑定 → BeginPlay 用它创建实例
	 */
	UPROPERTY(EditAnywhere, Category = "UI")
	TSubclassOf<UGameHUDWidget> GameHUDWidgetClass;

private:
	// ==========================================
	// 私有成员
	// ==========================================

	/**
	 * 游戏 HUD Widget 实例缓存
	 * 用途: 在 BeginPlay 时预创建（Collapsed），战斗开始时显示
	 * 真理源: AMyGameHUD::GameHUDWidget — 唯一权威引用
	 */
	UPROPERTY()
	UGameHUDWidget* GameHUDWidget;

	/**
	 * 创建游戏 HUD（内部辅助方法）
	 * 流程: 获取本地 PC -> CreateWidget -> SetVisibility(Collapsed) -> AddToViewport
	 */
	void CreateGameHUD();

	/**
	 * GameFlowSubsystem 状态变化回调
	 * @param NewState 新的全局状态
	 * Battleing: 显示 HUD + 准星 + 刷新倒计时
	 * 其他: 隐藏 HUD + 准星
	 */
	UFUNCTION()
	void OnGameFlowStateChanged(EMatchState NewState);
};
