// 版权声明：在项目设置的描述页面填写您的版权信息。

// 普通领取成功弹窗 UI 类

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Activity/Pages/ClaimSuccess/ClaimSuccessWidget.h"

#include "Components/Button.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UClaimSuccessWidget::NativeConstruct
 *
 * 1. 清理可能存在的旧绑定（防御性）
 * 2. 绑定 Btn_Close -> OnCloseClicked
 * 3. 失败时 Log Error 提示
 */
void UClaimSuccessWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 清理可能存在的旧绑定
	if (Btn_Close)
	{
		Btn_Close->OnClicked.RemoveAll(this);
		Btn_Close->OnClicked.AddDynamic(this, &UClaimSuccessWidget::OnCloseClicked);
		UE_LOG(LogTemp, Warning, TEXT("ClaimSuccessWidget: 成功绑定关闭按钮事件"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ClaimSuccessWidget: 未找到关闭按钮 Btn_Close"));
	}
}


// ==========================================
// 2. 内部回调
// ==========================================

/**
 * UClaimSuccessWidget::OnCloseClicked
 *
 * 1. 日志
 * 2. RemoveFromParent 关闭弹窗
 */
void UClaimSuccessWidget::OnCloseClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("ClaimSuccessWidget: 关闭按钮被点击"));
	RemoveFromParent(); // 关闭弹窗
}
