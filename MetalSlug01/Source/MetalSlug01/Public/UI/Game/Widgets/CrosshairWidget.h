#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CrosshairWidget.generated.h"

/**
 * 准星Widget
 * 负责显示游戏准星，根据状态显示/隐藏
 */
UCLASS()
class METALSLUG01_API UCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Widget 构造完毕并加入视口后调用
	virtual void NativeConstruct() override;

	// 显示准星
	UFUNCTION(BlueprintCallable, Category = "Crosshair")
	void ShowCrosshair();

	// 隐藏准星
	UFUNCTION(BlueprintCallable, Category = "Crosshair")
	void HideCrosshair();

	// 更新准星样式（根据武器类型）
	UFUNCTION(BlueprintCallable, Category = "Crosshair")
	void UpdateCrosshairStyle(int32 WeaponType);
};
