#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "KillFeedWidget.generated.h"

class UVerticalBox;
class UTextBlock;

/**
 * 击杀信息组件
 * 负责显示击杀消息列表
 */
UCLASS()
class METALSLUG01_API UKillFeedWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 添加击杀信息
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void AddKillInfo(const FString& KillerName, const FString& VictimName, bool bIsHeadshot);

	// 添加系统消息（如回合开始、炸弹放置等）
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void AddSystemMessage(const FString& Message);

	// 清空所有击杀信息
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void ClearKillFeed();

protected:
	virtual bool Initialize() override;

private:
	// 最大显示的击杀消息数量
	static constexpr int32 MaxKillMessages = 10;

	// 消息自动消失时间（秒）
	float MessageLifetime = 5.0f;

	// 击杀信息容器
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* VB_KillFeed;

	// 创建单条击杀消息
	UWidget* CreateKillMessage(const FString& KillerName, const FString& VictimName, bool bIsHeadshot);

	// 创建单条系统消息
	UWidget* CreateSystemMessage(const FString& Message);

	// 移除过期消息（定时调用）
	UFUNCTION()
	void RemoveExpiredMessages();
};