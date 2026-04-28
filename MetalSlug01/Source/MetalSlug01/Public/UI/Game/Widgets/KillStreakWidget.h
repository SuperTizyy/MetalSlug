#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Data/StaticTable.h"
#include "KillStreakWidget.generated.h"

class UImage;

/**
 * 击杀图标组件
 * 简化逻辑：
 * - 只使用 Image_Kill 显示击杀图标
 * - 每次击杀开启1分钟计时，1分钟内持续击杀则累加连杀数
 * - 爆头击杀使用 Headshot 图标，也记录在总击杀数内
 * - 根据击杀数量查询数据表显示对应图标
 */
UCLASS()
class METALSLUG01_API UKillStreakWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 记录一次击杀（每次击杀时调用，自动管理1分钟连杀计时）
	UFUNCTION(BlueprintCallable, Category = "KillStreak")
	void RecordKill(bool bIsHeadshot);

	// 获取当前连杀数
	UFUNCTION(BlueprintCallable, Category = "KillStreak")
	int32 GetCurrentKillStreak() const { return CurrentKillStreak; }

	// 获取连杀图标数据表
	class UDataTable* GetKillStreakIconDataTable() const { return KillStreakIconDataTable; }

	// 设置连杀图标数据表
	UFUNCTION(BlueprintCallable, Category = "KillStreak")
	void SetKillStreakIconDataTable(class UDataTable* InDataTable) { KillStreakIconDataTable = InDataTable; }

protected:
	virtual bool Initialize() override;

private:
	// ==========================================
	// 唯一图标显示组件
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UImage* Image_Kill;

	// ==========================================
	// 连杀计时系统
	// ==========================================

	// 当前连杀计数
	int32 CurrentKillStreak = 0;

	// 连杀计时器（1分钟内持续击杀则累加）
	FTimerHandle KillStreakTimer;

	// 连杀有效时间（秒）
	float KillStreakDuration = 60.0f;

	// 上一次是否爆头（用于图标优先级判断）
	bool bLastKillWasHeadshot = false;

	// ==========================================
	// 数据表（由 GameHUDWidget 注入，不暴露到蓝图）
	// ==========================================
	class UDataTable* KillStreakIconDataTable;

	// ==========================================
	// 图标显示定时
	// ==========================================

	// 图标显示定时器（用于自动隐藏图标）
	FTimerHandle IconDisplayTimer;

	// 图标自动隐藏时间（秒）
	float IconDisplayDuration = 3.0f;

	// 隐藏图标回调
	UFUNCTION()
	void HideIcon();

	// 连杀超时回调（1分钟无击杀则重置）
	UFUNCTION()
	void OnKillStreakExpired();

	// 根据击杀情况更新图标（核心逻辑）
	void UpdateKillIcon();

	// 从数据表获取图标
	UTexture2D* GetKillStreakIcon(EKillStreakType StreakType);

	// 根据击杀数转换为连杀类型
	EKillStreakType GetKillStreakType(int32 Kills, bool bIsHeadshot) const;
};
