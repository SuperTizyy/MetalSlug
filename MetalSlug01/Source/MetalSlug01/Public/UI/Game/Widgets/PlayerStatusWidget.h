// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Enums/CombatEnums.h" // 引入 EACERankType
#include "PlayerStatusWidget.generated.h"

// 前向声明
class UProgressBar;
class UTextBlock;
class UImage;
class UHorizontalBox;


/**
 * @class UPlayerStatusWidget
 * @brief 角色状态组件
 *
 * 职责说明:
 * - 血条（颜色随百分比变化: 绿/黄/红）
 * - 能量条
 * - 角色头像 / 技能栏
 * - AC 值（防护服）与 AC 图标颜色
 * - ACE 值与排名颜色（白/金/灰）
 *
 * 架构理念:
 * 1. 主动拉取: NativeConstruct 时从 ABaseCharacter 拉取初始值，解决 UI 晚创建
 * 2. 数据驱动: AC/ACE 排名通过枚举控制颜色，无需蓝图里堆逻辑
 * 3. 防御性: 每个 BindWidget 调用前都做空指针检查
 * 4. 样式可配: 字体/颜色全部由蓝图配置
 */
UCLASS()
class METALSLUG01_API UPlayerStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公共接口
	// ==========================================

	/**
	 * 刷新血量显示
	 * 颜色规则: > 60% 绿 / > 30% 黄 / 否则 红
	 * @param Current 当前血量
	 * @param Max 最大血量
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateHealth(float Current, float Max);

	/**
	 * 刷新能量显示
	 * @param Current 当前能量
	 * @param Max 最大能量
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateEnergy(float Current, float Max);

	/**
	 * 更新血量文本（"X/Y" 格式）
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateHealthText(int32 Current, int32 Max);

	/**
	 * 更新能量文本（"X/Y" 格式）
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateEnergyText(int32 Current, int32 Max);

	/**
	 * 更新 AC 值（Assists / 助攻 -> 此处是防护服值）
	 * 副作用: 同时调用 RefreshACIconColor 同步刷新防护服图标颜色
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateACValue(int32 Value);

	/**
	 * 更新 ACE 值（默认白色）
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateACEValue(int32 Value);

	/**
	 * 更新 ACE 值并根据排名设置文字颜色
	 * @param Value ACE 数值
	 * @param RankType Gold/White/None
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void SetACEValueWithRank(int32 Value, EACERankType RankType);

	/**
	 * 更新角色图标
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateCharacterIcon(UTexture2D* Icon);

	/**
	 * 更新技能图标
	 * @param SkillIndex 技能槽索引
	 * @param Icon 图标贴图
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void UpdateSkillIcon(int32 SkillIndex, UTexture2D* Icon);

	/**
	 * 设置技能冷却状态
	 * @param SkillIndex 技能槽索引
	 * @param CooldownPercent 冷却百分比 (0~1, 1=冷却完毕)
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void SetSkillCooldown(int32 SkillIndex, float CooldownPercent);

	// ==========================================
	// 2. AC 图标变色接口
	// ==========================================

	/**
	 * 根据 AC 值查表得到对应颜色并应用
	 * 分档: 0-25 红 / 26-50 橙 / 51-75 黄 / 76+ 蓝白
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void RefreshACIconColor(int32 CurrentAC);

protected:
	// ==========================================
	// 3. 生命周期
	// ==========================================

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;

	/**
	 * Widget 构造完毕后主动从角色拉取初始数据
	 * 用途: 解决 UI 创建晚于角色初始化导致的初始值为 0 的问题
	 */
	void PullInitialDataFromCharacter();

private:
	// ==========================================
	// 4. UI 组件
	// ==========================================

	/** 血量进度条 */
	UPROPERTY(meta = (BindWidget))
	UProgressBar* PB_HealthBar;

	/** 血量文本 "X/Y" */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_HealthValue;

	/** 能量进度条 */
	UPROPERTY(meta = (BindWidget))
	UProgressBar* PB_EnergyBar;

	/** 能量文本 "X/Y" */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_EnergyValue;

	/** 角色头像 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_CharacterIcon;

	/** 技能栏（Horizontal Box, 内部包含 N 个技能图标） */
	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* HB_SkillBar;

	/**
	 * 技能冷却遮罩
	 * 用途: 通过子控件获取，每个槽位一个 Image 控制透明度
	 */
	UPROPERTY()
	TArray<UImage*> SkillCooldownOverlays;

	/** AC 值文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_ACValue;

	/** AC 防护服图标（颜色随 AC 等级变化） */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_ACIcon;

	/** ACE 值文本（颜色随排名变化） */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_ACEValue;
};
