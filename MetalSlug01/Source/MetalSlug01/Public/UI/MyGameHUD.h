#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MyGameHUD.generated.h"

/**
 * 游戏HUD类 - 管理游戏界面上的用户界面元素
 * 负责处理游戏中的UI显示，包括奖励界面和活动系统界面
 */
UCLASS()
class METALSLUG01_API AMyGameHUD : public AHUD
{
	GENERATED_BODY()

public:
	// 构造函数 - 初始化HUD的基本属性和组件
	AMyGameHUD();

	/** 核心接口：切换奖励界面的显示状态
	 *  @param bShow - 是否显示奖励界面
	 */
	UFUNCTION(BlueprintCallable, Category = "GameUI")
	void ToggleRewardUI(bool bShow);

protected:
	// 重写BeginPlay函数，在HUD创建时执行初始化逻辑
	virtual void BeginPlay() override;

	/** 奖励界面的蓝图类引用（在编辑器 HUD 蓝图中指定）
	 *  用于动态创建奖励界面的Widget实例
	 */
	UPROPERTY(EditAnywhere, Category = "UI Config")
	TSubclassOf<class UGameRewardMainWidget> RewardWidgetClass;
	
	/** 活动系统根 Widget（左侧菜单 + 右侧页面）
	 *  用于动态创建活动系统界面的根Widget
	 */
	UPROPERTY(EditAnywhere, Category="UI Config")
	TSubclassOf<class UActivityRoot> ActivityRootClass;

private:
	/** 缓存奖励Widget实例，避免重复创建销毁带来的性能开销
	 *  保持对奖励界面的引用以便后续操作
	 */
	UPROPERTY()
	class UGameRewardMainWidget* CachedRewardWidget;
	
	/** 缓存活动系统根Widget实例，避免重复创建
	 *  保持对活动系统界面的引用以便后续操作
	 */
	UPROPERTY()
	class UActivityRoot* CachedActivityRootWidget;
};

