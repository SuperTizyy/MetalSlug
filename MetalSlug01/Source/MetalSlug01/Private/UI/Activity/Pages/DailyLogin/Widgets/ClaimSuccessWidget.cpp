//普通领取成功弹窗UI类

#include "UI/Activity/Pages/DailyLogin/Widgets/ClaimSuccessWidget.h"

#include "Components/Button.h"

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
		UE_LOG(LogTemp, Error, TEXT("ClaimSuccessWidget: 未找到关闭按钮Btn_Close"));
	}
}

void UClaimSuccessWidget::OnCloseClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("ClaimSuccessWidget: 关闭按钮被点击"));
	RemoveFromParent(); // 关闭弹窗
}