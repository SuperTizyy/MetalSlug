// 13

#pragma once

#include "CoreMinimal.h"
#include "UI/Activity/Pages/ActivityPageBase.h"
#include "DailyLoginPage.generated.h"


class UListView;
class UDailyLoginTrack;

/**
 * 每日登录页面
 */
UCLASS()
class METALSLUG01_API UDailyLoginPage : public UActivityPageBase
{
	GENERATED_BODY()

protected:
	virtual void OnPageShow_Implementation() override;

private:
	UPROPERTY(meta = (BindWidget))
	UListView* LoginDayList;

	// 数据源
	UPROPERTY()
	UDailyLoginTrack* LoginTrack = nullptr;

private:
	void BuildList();
};

