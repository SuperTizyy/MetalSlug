#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Data/StaticTable.h"
#include "PlayerStatusWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UImage;
class UHorizontalBox;

/**
 * 角色状态组件
 * 负责显示血条、能量条、角色图标、技能、AC值、ACE等
 */
UCLASS()
class METALSLUG01_API UPlayerStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 刷新血量显示
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateHealth(float Current, float Max);

	// 刷新能量显示
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateEnergy(float Current, float Max);

	// 更新血量文本
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateHealthText(int32 Current, int32 Max);

	// 更新能量文本
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateEnergyText(int32 Current, int32 Max);

	// 更新AC值
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateACValue(int32 Value);

	// 更新ACE值
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateACEValue(int32 Value);

	// 更新ACE值并根据排名设置文字颜色
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void SetACEValueWithRank(int32 Value, EACERankType RankType);

	// 更新角色图标
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateCharacterIcon(UTexture2D* Icon);

	// 更新技能图标
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateSkillIcon(int32 SkillIndex, UTexture2D* Icon);

	// 设置技能冷却状态
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void SetSkillCooldown(int32 SkillIndex, float CooldownPercent);

	// ==========================================
	// AC 图标变色接口
	// ==========================================
	// 根据 AC 值查表得到对应颜色并应用
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void RefreshACIconColor(int32 CurrentAC);

protected:
	virtual bool Initialize() override;

private:
	// ==========================================
	// 血条相关控件
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UProgressBar* PB_HealthBar;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_HealthValue;

	// ==========================================
	// 能量条相关控件
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UProgressBar* PB_EnergyBar;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_EnergyValue;

	// ==========================================
	// 角色图标
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UImage* Image_CharacterIcon;

	// ==========================================
	// 技能栏（Horizontal Box）
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* HB_SkillBar;

	// 技能冷却遮罩（需要通过子控件获取）
	UPROPERTY()
	TArray<UImage*> SkillCooldownOverlays;

	// ==========================================
	// AC值相关
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_ACValue;

	UPROPERTY(meta = (BindWidget))
	UImage* Image_ACIcon;

	// ==========================================
	// ACE值
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_ACEValue;
};