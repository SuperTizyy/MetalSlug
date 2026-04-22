#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "UI/Login/Data/StaticTable.h"
#include "KillFeedWidget.generated.h"

class UVerticalBox;
class UTextBlock;
class UKillFeedEntryWidget;

/**
 * 击杀信息组件
 * 负责显示击杀消息列表，支持多种击杀方式的图标和文本显示
 */
UCLASS()
class METALSLUG01_API UKillFeedWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 供外部调用的公共接口
	// ==========================================

	// 添加击杀信息（使用击杀方式枚举）
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void AddKillInfo(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod);

	// 添加击杀信息（带武器类型和爆头标识）
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void AddKillInfoEx(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod, bool bIsHeadshot);

	// 添加击杀信息（保留旧的 bool 版本以兼容）
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void AddKillInfoWithHeadshot(const FString& KillerName, const FString& VictimName, bool bIsHeadshot);

	// 添加系统消息（如回合开始、炸弹放置等）
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void AddSystemMessage(const FString& Message);

	// 清空所有击杀信息
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void ClearKillFeed();

	// 设置击杀图标数据表（可选，用于数据驱动）
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void SetKillIconDataTable(class UDataTable* InDataTable);

protected:
	virtual bool Initialize() override;

private:
	// ==========================================
	// 私有成员变量
	// ==========================================

	// 最大显示的击杀消息数量
	static constexpr int32 MaxKillMessages = 10;

	// 消息自动消失时间（秒）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KillFeed", meta = (AllowPrivateAccess = "true"))
	float MessageLifetime = 5.0f;

	// 击杀信息容器
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* VB_KillFeed;

	// 击杀信息条目类引用（用于动态创建子控件）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KillFeed", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UKillFeedEntryWidget> KillFeedEntryWidgetClass;

	// 击杀图标数据表引用（通过编辑器或代码指定）
	UPROPERTY()
	class UDataTable* KillIconDataTable;

	// ==========================================
	// 私有方法
	// ==========================================

	// 创建单条击杀消息（使用新的子控件）
	UWidget* CreateKillMessage(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod);

	// 创建单条击杀消息（旧版本，使用 bool 判断爆头）
	UWidget* CreateKillMessageOld(const FString& KillerName, const FString& VictimName, bool bIsHeadshot);

	// 创建单条系统消息
	UWidget* CreateSystemMessage(const FString& Message);

	// 移除过期消息（定时调用）
	UFUNCTION()
	void RemoveExpiredMessages();

	// 根据击杀方式判断是否为爆头
	bool IsHeadshotFromKillMethod(EKillMethod KillMethod) const;

	// 根据击杀方式和爆头状态获取最终的击杀方法
	EKillMethod GetFinalKillMethod(EKillMethod BaseMethod, bool bIsHeadshot);
};