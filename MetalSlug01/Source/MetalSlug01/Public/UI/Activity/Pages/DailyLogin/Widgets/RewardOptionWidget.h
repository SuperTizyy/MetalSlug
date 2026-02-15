#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
#include "RewardOptionWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStoreToBag, int32, DayIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnOpenNow);

UCLASS()
class METALSLUG01_API URewardOptionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** * 修改点：去掉指针 *，改用 const TArray<FDailyLoginConfigRow>& 
	 * UHT 不允许在 UFUNCTION 参数中暴露原始结构体指针数组
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void InitSelection(const TArray<FDailyLoginConfigRow>& Options);
	
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnStoreToBag OnStoreToBag; //

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnOpenNow OnOpenNow; //

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	class UButton* StoreBtn; //

	UPROPERTY(meta = (BindWidget))
	class UButton* OpenBtn; //

	// 只保留声明，不要在这里写 { ... }
	UFUNCTION()
	void HandleStoreClicked();

	UFUNCTION()
	void HandleOpenClicked();
	
private:
	/** 当前处理的天数 */
	UPROPERTY()
	int32 CurrentDayIndex = 0;
	
	/** 防重复调用标志 */
	UPROPERTY(Transient)
	bool bStoreClickedHandled = false;
	
	UPROPERTY(Transient)
	bool bOpenClickedHandled = false;
};