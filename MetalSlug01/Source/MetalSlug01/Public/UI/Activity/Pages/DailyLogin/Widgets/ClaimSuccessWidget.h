#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ClaimSuccessWidget.generated.h"

UCLASS()
class METALSLUG01_API UClaimSuccessWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

	// 绑定关闭按钮，蓝图中的按钮命名必须为 Btn_Close
	UPROPERTY(meta = (BindWidget))
	class UButton* Btn_Close;

	UFUNCTION()
	void OnCloseClicked();
};