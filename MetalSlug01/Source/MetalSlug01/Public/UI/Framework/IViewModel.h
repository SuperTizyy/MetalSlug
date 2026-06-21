// 版权声明：在项目设置的描述页面填写您的版权信息。

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
	/** Widget 显示时调用（订阅数据层） */
	virtual void OnWidgetShow() = 0;

	/** Widget 隐藏时调用（取消订阅） */
	virtual void OnWidgetHide() = 0;

	/** 绑定 View（由 UIViewService 调用） */
	virtual void BindView(UUserWidget* InView) = 0;

	/** 解绑 View */
	virtual void UnbindView() = 0;

	/** 是否已绑定 View */
	virtual bool IsBoundToView() const = 0;

	/** 获取 VM 类型标识（用于 View 决定用哪个具体接口 Cast） */
	virtual FName GetViewModelType() const = 0;
};