// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Data/StaticTable.h" // 引入 EKillMethod
#include "KillFeedEntryWidget.generated.h"

// 前向声明
class UTextBlock;
class UImage;


/**
 * @class UKillFeedEntryWidget
 * @brief 击杀信息条目子控件
 *
 * 显示单条击杀信息，包含击杀者、被击杀者、击杀图标
 *
 * 显示逻辑:
 * 1. 先判断是什么武器类型产生的击杀（主武器/副武器/近战武器）
 * 2. 再判断是爆头击杀还是普通击杀
 * 3. 根据击杀方式和爆头状态获取对应的击杀图标
 *
 * 颜色规范:
 * - 击杀者名: 青色 (0, 1, 1)
 * - 被击杀者名: 橙色 (1, 0.5, 0)
 * - 主武器: 绿色
 * - 副武器: 蓝色
 * - 近战: 红色
 */
UCLASS()
class METALSLUG01_API UKillFeedEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公共接口（供外部调用）
	// ==========================================

	/**
	 * 设置击杀信息
	 * @param InKillerName 击杀者名
	 * @param InVictimName 被击杀者名
	 * @param InKillMethod 击杀方式
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeedEntry")
	void SetKillInfo(const FString& InKillerName, const FString& InVictimName, EKillMethod InKillMethod);

	/**
	 * 设置击杀者名称
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeedEntry")
	void SetKillerName(const FString& InKillerName);

	/**
	 * 设置被击杀者名称
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeedEntry")
	void SetVictimName(const FString& InVictimName);

	/**
	 * 设置击杀方式并更新图标
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeedEntry")
	void SetKillMethod(EKillMethod InKillMethod);

	// ==========================================
	// 2. UI 组件绑定
	// ==========================================

protected:
	/**
	 * 击杀者名称文本
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_KillerName;

	/**
	 * 被击杀者名称文本
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_VictimName;

	/**
	 * 击杀图标
	 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_KillIcon;

private:
	/**
	 * 击杀图标数据表引用
	 * 关联: DT_KillIconInfo
	 */
	UPROPERTY()
	class UDataTable* KillIconDataTable;

public:
	// ==========================================
	// 3. 数据表注入
	// ==========================================

	/**
	 * 设置击杀图标数据表
	 * 用途: 供父控件 (UKillFeedWidget) 调用
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeedEntry")
	void SetKillIconDataTable(class UDataTable* InDataTable);

	/**
	 * 根据击杀方式查找对应的击杀图标
	 * 内部使用 ForeachRow 遍历数据表
	 * @param InKillMethod 击杀方式
	 * @return 图标贴图
	 */
	class UTexture2D* FindKillIcon(EKillMethod InKillMethod);

	/**
	 * 判断是否为爆头击杀
	 * 规则: PrimaryHeadshot/SecondaryHeadshot/MeleeHeadshot -> true
	 */
	bool IsHeadshotKill(EKillMethod InKillMethod) const;

	/**
	 * 根据击杀方式获取图标颜色
	 * - 主武器: 绿
	 * - 副武器: 蓝
	 * - 近战: 红
	 * - 默认: 白
	 */
	FLinearColor GetIconColor(EKillMethod InKillMethod) const;
};
