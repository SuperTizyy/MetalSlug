#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "KillStreakWidget.generated.h"

class UImage;
class UTextBlock;

/**
 * 击杀图标类型枚举
 */
UENUM(BlueprintType)
enum class ECKillIconType : uint8
{
	NormalKill,     // 普通击杀
	Headshot,       // 爆头
	MultiKill,      // 多杀
	DoubleKill,     // 双杀
	TripleKill,     // 三杀
	MegaKill,      // 疯狂杀戮
	UltraKill,     // 终极击杀
	MonsterKill    // 怪物杀手
};

/**
 * 连杀显示组件
 * 负责显示爆头图标、击杀图标、连杀图标和连杀数
 */
UCLASS()
class METALSLUG01_API UKillStreakWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 更新连杀数
	UFUNCTION(BlueprintCallable, Category = "KillStreak")
	void UpdateKillCount(int32 Kills);

	// 显示图标类型
	UFUNCTION(BlueprintCallable, Category = "KillStreak")
	void ShowIcon(ECKillIconType IconType);

	// 隐藏所有图标
	UFUNCTION(BlueprintCallable, Category = "KillStreak")
	void HideAllIcons();

protected:
	virtual bool Initialize() override;

private:
	// ==========================================
	// 图标显示
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UImage* Image_Headshot;

	UPROPERTY(meta = (BindWidget))
	UImage* Image_Kill;

	UPROPERTY(meta = (BindWidget))
	UImage* Image_MultiKill;

	// ==========================================
	// 连杀数显示
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_KillCount;

	// 图标显示定时器
	FTimerHandle IconDisplayTimer;

	// 图标自动隐藏时间（秒）
	float IconDisplayDuration = 2.0f;

	// 隐藏图标定时回调
	UFUNCTION()
	void HideCurrentIcon();

	// 根据连杀数自动显示对应图标
	UFUNCTION()
	void UpdateKillStreakIcon();
};