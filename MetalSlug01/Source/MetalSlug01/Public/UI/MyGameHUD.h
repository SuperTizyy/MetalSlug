#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MyGameHUD.generated.h"

UCLASS()
class METALSLUG01_API AMyGameHUD : public AHUD
{
	GENERATED_BODY()

public:
	AMyGameHUD();

	/** 核心接口：切换签到界面的显示状态 */
	UFUNCTION(BlueprintCallable, Category = "GameUI")
	void ToggleRewardUI(bool bShow);

protected:
	virtual void BeginPlay() override;

	/** 签到界面的蓝图类引用（在编辑器 HUD 蓝图中指派） */
	UPROPERTY(EditAnywhere, Category = "UI Config")
	TSubclassOf<class UGameRewardMainWidget> RewardWidgetClass;
	
	/** 活动系统根 Widget（左侧菜单 + 右侧页面） */
	UPROPERTY(EditAnywhere, Category="UI Config")
	TSubclassOf<class UActivityRoot> ActivityRootClass;

private:
	/** 缓存 Widget 实例，避免重复创建销毁带来的开销 */
	UPROPERTY()
	class UGameRewardMainWidget* CachedRewardWidget;
	
	/** 缓存实例 */
	UPROPERTY()
	class UActivityRoot* CachedActivityRootWidget;
};

