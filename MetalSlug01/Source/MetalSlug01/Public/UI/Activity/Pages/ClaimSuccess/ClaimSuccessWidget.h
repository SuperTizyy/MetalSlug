// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ClaimSuccessWidget.generated.h"


/**
 * @class UClaimSuccessWidget
 * @brief 普通领取成功弹窗 UI 类
 *
 * 职责说明:
 * - 玩家领取奖励成功后的提示弹窗
 * - 提供一个"关闭"按钮
 * - 点击关闭按钮后从视口移除
 *
 * 架构理念:
 * 1. 单一职责: 极简弹窗, 只做"显示"和"关闭"
 * 2. 防御性: 清理可能存在的旧绑定
 * 3. 复用: 通用领取成功提示, 各活动共用
 */
UCLASS()
class METALSLUG01_API UClaimSuccessWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	// ==========================================
	// 1. 生命周期
	// ==========================================

	/**
	 * Native 构造
	 * 1. 清理可能存在的旧绑定
	 * 2. 绑定 Btn_Close 点击
	 */
	virtual void NativeConstruct() override;

	// ==========================================
	// 2. UI 组件
	// ==========================================

	/**
	 * 绑定关闭按钮
	 * 蓝图中的按钮命名必须为 Btn_Close
	 */
	UPROPERTY(meta = (BindWidget))
	class UButton* Btn_Close;

	// ==========================================
	// 3. 内部回调
	// ==========================================

	/**
	 * 关闭按钮点击事件
	 * 1. 日志
	 * 2. RemoveFromParent 关闭弹窗
	 */
	UFUNCTION()
	void OnCloseClicked();
};
