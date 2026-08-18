// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// IView 头文件 — UI View 通用接口 (MVVM View 基类契约)
// ==========================================
//
// 文件作用:
//   1. 声明 IView 接口 — 所有 View (UUserWidget 子类) 的最小契约
//   2. 声明 UView 接口类 (UE 反射需要) — 给 UCLASS 系统识别
//   3. 与 IViewModel 配对使用 — View 不直接读数据, 只读 ViewModel
//
// 设计理念 (大厂 MVVM 模式 - Lyra CommonUI / Frostbite UIViewController):
//   1. 业务逻辑通过 IViewModel 注入 (BindViewModel)
//   2. View 不知道 ViewModel 的具体类型 — 由 UIViewService 在运行时注入
//   3. UIViewService 只认 IView/IViewModel, 不关心具体 Widget (解耦关键)
//
// 大厂优势:
//   - 业务 Widget 可独立单元测试 (注入 mock ViewModel)
//   - 跨模块复用: 同一 View 可绑定不同 VM 实现不同业务
//   - 编译时解耦: View 不依赖业务头文件, 加速增量编译
// ==========================================

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IView.generated.h"

class IViewModel;

/**
 * @interface IView
 * @brief View 通用接口（大厂 MVVM 解耦点）
 *
 * 【大厂架构原则】
 * - 每个 UUserWidget 子类实现 IView
 * - 业务逻辑通过 IViewModel 注入
 * - UIViewService 只认 IView/IViewModel，不知道具体 Widget 类型
 *
 * 与 Lyra 的 CommonActivatableWidget / Frostbite 的 UIViewController 对标
 */
UINTERFACE(BlueprintType, MinimalAPI)
class UView : public UInterface
{
	GENERATED_BODY()
};

class METALSLUG01_API IView
{
	GENERATED_BODY()

public:
	/**
	 * BindViewModel — 绑定 ViewModel
	 *
	 * @brief  UIViewService 在 Widget 创建后调用, 将业务数据源注入 View
	 * @param  InViewModel  业务 ViewModel (实现了 IViewModel 接口)
	 * @note   由 UIViewService 调用, 业务侧不应直接调用
	 */
	virtual void BindViewModel(TScriptInterface<IViewModel> InViewModel) = 0;

	/**
	 * GetViewModel — 获取当前绑定的 ViewModel
	 *
	 * @brief  让 View 内部逻辑可以反向访问 ViewModel (如触发命令)
	 * @return 当前绑定的 ViewModel 接口 (未绑定时返回空)
	 */
	virtual TScriptInterface<IViewModel> GetViewModel() const = 0;

	/**
	 * HasViewModel — 判断是否已绑定 ViewModel
	 *
	 * @brief  业务查询 — 某些 View 在未绑定 VM 时应禁用交互
	 * @return true=已绑定, false=未绑定
	 */
	virtual bool HasViewModel() const = 0;
};