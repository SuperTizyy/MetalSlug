#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Data/StaticTable.h"
#include "KillFeedEntryWidget.generated.h"

class UTextBlock;
class UImage;

/**
 * 击杀信息条目子控件
 * 用于显示单条击杀信息，包含击杀者、被击杀者、击杀图标
 * 
 * 显示逻辑：
 * 1. 先判断是什么武器类型产生的击杀（主武器/副武器/近战武器）
 * 2. 再判断是爆头击杀还是普通击杀
 * 3. 根据击杀方式和爆头状态获取对应的击杀图标
 */
UCLASS()
class METALSLUG01_API UKillFeedEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 供外部调用的公共接口
	// ==========================================

	// 设置击杀信息
	UFUNCTION(BlueprintCallable, Category = "KillFeedEntry")
	void SetKillInfo(const FString& InKillerName, const FString& InVictimName, EKillMethod InKillMethod);

	// 设置击杀者名称
	UFUNCTION(BlueprintCallable, Category = "KillFeedEntry")
	void SetKillerName(const FString& InKillerName);

	// 设置被击杀者名称
	UFUNCTION(BlueprintCallable, Category = "KillFeedEntry")
	void SetVictimName(const FString& InVictimName);

	// 设置击杀方式并更新图标
	UFUNCTION(BlueprintCallable, Category = "KillFeedEntry")
	void SetKillMethod(EKillMethod InKillMethod);

protected:
	// ==========================================
	// UI 组件绑定区域
	// ==========================================

	// 击杀者名称文本
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_KillerName;

	// 被击杀者名称文本
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_VictimName;

	// 击杀图标
	UPROPERTY(meta = (BindWidget))
	UImage* Image_KillIcon;

private:
	// 击杀图标数据表引用
	UPROPERTY()
	class UDataTable* KillIconDataTable;

public:
	// 设置击杀图标数据表（供父控件调用）
	UFUNCTION(BlueprintCallable, Category = "KillFeedEntry")
	void SetKillIconDataTable(class UDataTable* InDataTable);

	// 根据击杀方式查找对应的击杀图标
	class UTexture2D* FindKillIcon(EKillMethod InKillMethod);

	// 判断是否为爆头击杀
	bool IsHeadshotKill(EKillMethod InKillMethod) const;

	// 根据击杀方式获取图标颜色
	FLinearColor GetIconColor(EKillMethod InKillMethod) const;
};
