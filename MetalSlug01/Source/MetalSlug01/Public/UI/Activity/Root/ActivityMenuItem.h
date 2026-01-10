#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ActivityMenuItem.generated.h"

class UButton;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnActivityMenuClicked,
	FName,
	PageId
);

UCLASS()
class METALSLUG01_API UActivityMenuItem : public UUserWidget
{
	GENERATED_BODY()

public:
	// 初始化菜单项
	void Init(FName InPageId, const FText& InDisplayName);

public:
	UPROPERTY(BlueprintAssignable)
	FOnActivityMenuClicked OnClicked;

protected:
	virtual void NativeOnInitialized() override;

protected:
	UPROPERTY(meta = (BindWidget))
	UButton* ClickButton;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* TitleText;

private:
	FName PageId;

	UFUNCTION()
	void HandleClicked();
};
