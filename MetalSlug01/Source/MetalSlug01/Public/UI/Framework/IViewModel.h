// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// IViewModel 头文件 — UI ViewModel 通用接口 (MVVM ViewModel 基类契约)
// ==========================================
//
// 文件作用:
//   1. 声明 IViewModel 接口 — 所有 ViewModel 的最小契约
//   2. 提供 View 生命周期管理方法 (OnShow/OnHide/BindView/UnbindView)
//   3. 提供类型标识 GetViewModelType, 让 View 决定用哪个具体接口 Cast
//
// 设计理念 (大厂 MVVM 接口最小化原则):
//   - 基接口只暴露生命周期, 不暴露业务方法
//   - 业务调用和数据查询通过 VM 的具体接口 (ILANRoomViewModel/IRoomInsideViewModel)
//   - 事件订阅通过 VM 的具体多播委托
//
// 实际工程做法:
//   ViewModel 基接口 = 生命周期管理
//   具体业务接口 = 各业务模块自己定义 (ILANRoomViewModel、IRoomInsideViewModel 等)
//
// 大厂对应:
//   - Lyra: UCommonActivatableWidget / MVVM 框架
//   - Frostbite: UIViewModel 基类
// ==========================================

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IViewModel.generated.h"

class UUserWidget;

/**
 * @interface IViewModel
 * @brief ViewModel 通用接口（最薄一层）
 *
 * 【大厂架构原则 - 接口最小化】
 * - 只暴露生命周期接口（OnShow/OnHide/BindView）
 * - 业务调用和数据查询通过 VM 的具体接口（ILANRoomViewModel/IRoomInsideViewModel）
 * - 事件订阅通过 VM 的具体多播委托
 *
 * 实际工程做法:
 * ViewModel 基接口 = 生命周期管理
 * 具体业务接口 = 各业务模块自己定义（ILANRoomViewModel、IRoomInsideViewModel 等）
 *
 * 对应 Lyra 的 UCommonActivatableWidget / MVVM 框架
 */
UINTERFACE(BlueprintType, MinimalAPI)
class UViewModel : public UInterface
{
	GENERATED_BODY()
};

class METALSLUG01_API IViewModel
{
	GENERATED_BODY()

public:
	/**
	 * OnWidgetShow — Widget 显示时回调
	 *
	 * @brief  UIViewService 在 ShowPanel 时调用, VM 订阅数据层/绑定事件
	 * @note   与 OnWidgetHide 配对使用, 确保订阅不残留 (避免内存泄漏)
	 */
	virtual void OnWidgetShow() = 0;

	/**
	 * OnWidgetHide — Widget 隐藏时回调
	 *
	 * @brief  UIViewService 在 HidePanel 时调用, VM 取消订阅/解绑事件
	 * @note   必须幂等 — 多次调用 OnHide 不应报错 (大厂原则)
	 */
	virtual void OnWidgetHide() = 0;

	/**
	 * BindView — 绑定 View
	 *
	 * @brief  UIViewService 在 Widget 创建后调用, VM 拿到 View 引用
	 * @param  InView  被绑定的 View (UUserWidget)
	 * @note   VM 应缓存 InView 用于反向调用 (如显示 Toast)
	 */
	virtual void BindView(UUserWidget* InView) = 0;

	/**
	 * UnbindView — 解绑 View
	 *
	 * @brief  在 View 被销毁前调用, VM 释放 View 引用
	 * @note   必须与 BindView 配对 (大厂原则 - 防止野指针)
	 */
	virtual void UnbindView() = 0;

	/**
	 * IsBoundToView — 是否已绑定 View
	 *
	 * @brief  业务查询 — 用于判断 VM 当前是否处于"可用"状态
	 * @return true=已绑定, false=未绑定
	 */
	virtual bool IsBoundToView() const = 0;

	/**
	 * GetViewModelType — 获取 VM 类型标识
	 *
	 * @brief  View 用此决定用哪个具体接口 Cast (避免硬编码类型)
	 * @return FName 类型标识 (如 "LANRoomViewModel" / "RoomInsideViewModel")
	 */
	virtual FName GetViewModelType() const = 0;
};