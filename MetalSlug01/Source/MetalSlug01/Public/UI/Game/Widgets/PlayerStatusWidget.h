// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// PlayerStatusWidget 头文件 — 角色状态组件
// ==========================================
//
// 文件作用:
//   1. 声明 UPlayerStatusWidget — 玩家头部状态栏 (血量/能量/头像/技能)
//   2. 血条颜色随百分比变化: 绿/黄/红
//   3. AC 值 + ACE 排名颜色 (白/金/灰)
//   4. 主动拉取初始值: NativeConstruct 时拉取解决 UI 晚创建
//
// 大厂原则:
//   - 主动拉取: PullInitialDataFromCharacter 解决 UI 创建晚于 Pawn 数据的问题
//   - 数据驱动: AC/ACE 排名通过枚举控制颜色, BP 不堆逻辑
//   - 防御性: BindWidget 调用前空指针检查
//   - 样式可配: 字体/颜色全部由 WBP 蓝图配置
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Enums/CombatEnums.h" // 引入 EACERankType
#include "Engine/StreamableManager.h" // FStreamableHandle (异步加载回调)
#include "PlayerStatusWidget.generated.h"

// 前向声明
class UProgressBar;
class UTextBlock;
class UImage;
class UHorizontalBox;
class UOverlay;
class UTexture2D;
class UMotherSkillComponent;


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
	 * 【v121.3 重构】自动从 MotherSkillComponent 获取冷却数据计算倒计时
	 *
	 * 冷却进度逻辑:
	 *   - 冷却中: CDProgress 从 1.0 逐渐减小到 0.0
	 *   - 冷却结束: CDProgress = 0.0 (无遮罩)
	 *   - 冷却未开始: CDProgress = 0.0 (无遮罩)
	 *
	 * 实现:
	 *   - 从 Character 获取 MotherSkillComponent
	 *   - 读取 SkillCooldownEndTime 和 TotalCooldownDuration
	 *   - 计算: CDProgress = RemainingTime / TotalDuration
	 *   - 设置 CDProgress 参数到动态材质
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus|MotherSkill")
	void UpdateMotherSkillCooldownProgress();

	/**
	 * 【v121 大厂架构新增】设置母体技能图标显隐
	 *
	 * @param bIsMother 是否为母体 (true=显示, false=隐藏)
	 *
	 * 显隐规则:
	 *   - 母体 (bIsMother=true) → 显示技能图标
	 *   - 非母体 (bIsMother=false) → 隐藏技能图标
	 *
	 * 调用方: UGameHUDWidget::OnMotherSkillCooldownChanged (由 CharacterEvents.OnMotherSkillCooldownChanged 触发)
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus|MotherSkill")
	void SetMotherSkillIconVisibility(bool bIsMother);

protected:
	// ==========================================
	// 【v121.3 新增】Tick 更新冷却进度
	// ==========================================
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ==========================================
	// 2. 受保护的方法
	// ==========================================

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

	/**
	 * 【v119 大厂架构新增】更新母体加速技能冷却状态
	 *
	 * @param CooldownRemaining 冷却剩余秒数 (>0 = 冷却中, 0 = 就绪)
	 * @param bIsActive         是否正在加速中
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void RefreshACIconColor(int32 CurrentAC);

	// ==========================================
	// 内部: 异步加载与画刷应用
	// ==========================================

	/**
	 * 【2026-07-01 新增】应用头像贴图到 Image_CharacterIcon
	 * 内部方法: 被 UpdateCharacterIcon 和 OnCharacterIconLoaded 共享
	 * 含重复贴图短路 + 显式刷颜色 (White) 防 tint 残留
	 */
	void ApplyCharacterIconBrush(class UTexture2D* Icon);

	/**
	 * 【2026-07-01 新增】异步加载头像完成回调 (StreamableManager 回调)
	 * @param AssetPath   触发加载的资源路径 (用于匹配, 防止旧请求覆盖新请求)
	 * @param LoadedHandle StreamableManager 句柄
	 */
	void OnCharacterIconLoaded(FSoftObjectPath AssetPath, FStreamableHandle* LoadedHandle);

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

public:
	// ==========================================
	// 4. 公开访问器 (给 MotherSkillComponent 等外部系统使用)
	// ==========================================

	/** 【v121.5 新增】获取母体技能图标控件，供 C++ 直接设置材质参数 */
	UFUNCTION(BlueprintCallable, Category = "MotherSkill")
	UImage* GetMotherSkillIcon() { return Image_MotherSkillIcon; }

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

	// ==========================================
	// 【v238 大厂架构重构】母体加速技能冷却控件 (仅母体显示)
	// ==========================================

	/**
	 * 母体技能图标覆层 — 控制整个母体技能区域的显隐
	 *
	 * 职责: 替代 Image_MotherSkillIcon 控制显隐
	 *       Overlay 包含 Image_MotherSkillIcon 及相关子控件
	 *
	 * 显隐规则:
	 *   - 母体 (bIsMother=true) → 显示 Overlay (含冷却遮罩 + 技能图标)
	 *   - 非母体 (bIsMother=false) → 隐藏 Overlay
	 */
	UPROPERTY(meta = (BindWidget))
	UOverlay* Overlay_MotherSkillIcon;

	/**
	 * 母体加速技能图标 — 仅母体可见 (现在是 Overlay 的子控件)
	 *
	 * 冷却显示:
	 *   - CooldownRemaining > 0 → 显示冷却倒计时 (Image 作为遮罩 + Text 显示秒数)
	 *   - CooldownRemaining = 0 → 显示技能就绪 (无遮罩)
	 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_MotherSkillIcon;

	// ==========================================
	// 异步加载支持 (2026-07-01 新增)
	// ==========================================

	/**
	 * 【2026-07-01 新增】当前正在异步加载的头像软引用
	 * 用途: 如果收到多个 UpdateCharacterIcon 调用, 取消上一个未完成的加载任务
	 * 避免: 旧请求的回调在新请求之后到达, 导致显示错误的角色头像
	 */
	UPROPERTY()
	TSoftObjectPtr<UTexture2D> PendingCharacterIconSoftRef;
};
