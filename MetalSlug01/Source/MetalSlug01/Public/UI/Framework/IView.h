// 版权声明：在项目设置的描述页面填写您的版权信息。

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
	/** 绑定 ViewModel（由 UIViewService 调用） */
	virtual void BindViewModel(TScriptInterface<IViewModel> InViewModel) = 0;

	/** 获取当前绑定的 ViewModel */
	virtual TScriptInterface<IViewModel> GetViewModel() const = 0;

	/** 是否已绑定 ViewModel */
	virtual bool HasViewModel() const = 0;
};