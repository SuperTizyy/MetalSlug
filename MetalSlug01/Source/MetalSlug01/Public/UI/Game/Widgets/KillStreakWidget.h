// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// KillStreakWidget 头文件 — 连杀图标组件
// ==========================================
//
// 文件作用:
//   1. 声明 UKillStreakWidget — 连杀数图标 + 计时器
//   2. 数据驱动: KillStreakIconDataTable + KillStreakConfig 由 GameHUDWidget 注入
//   3. 5 秒超时: ShowKillStreakTimeout() → 重置为单人击杀图标 (0 兜底)
//   4. 单一职责: 只管连杀 UI, 不持有配置资产引用
//
// 大厂原则 (v41 大厂架构):
//   - 数据驱动: 所有参数通过 SetKillStreakConfig / SetKillStreakIconDataTable 注入
//   - 0 兜底: 超时后必须显示单人击杀图标, 不能静默消失
//   - 单一职责: 连杀 UI 逻辑
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Enums/CombatEnums.h"
#include "KillStreakWidget.generated.h"

class UImage;

/**
 * @class UKillStreakWidget
 * @brief 击杀图标组件
 *
 * v41 大厂架构重构:
 * 1. 数据驱动: 所有参数通过 SetKillStreakConfig 注入
 * 2. 零兜底: 超时后必须显示单人击杀图标, 不能静默消失
 * 3. 单一职责: 只负责连杀 UI 逻辑, 不持有配置资产引用
 *
 * Bug 修复记录 (v41):
 * - 旧版: OnKillStreakExpired 只重置计数器, 不更新图标
 *         → 超时后显示的是旧连杀图标 (如三杀), 而不是单人击杀
 * - 新版: OnKillStreakExpired 重置后立即显示单人击杀图标
 *
 * @note KillStreakIconDataTable 由 GameHUDWidget 通过 SetKillStreakIconDataTable 注入
 * @note KillStreakConfig 参数由 GameHUDWidget 通过 SetKillStreakConfig 注入
 */
UCLASS()
class METALSLUG01_API UKillStreakWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公共接口
	// ==========================================

	/**
	 * 记录一次击杀
	 * 时机: 每次玩家击杀时由 GameHUDWidget 调用
	 * @param bIsHeadshot 是否为爆头
	 * 流程: 累加 CurrentKillStreak -> 重置计时 -> 更新图标 -> 启动隐藏计时
	 */
	UFUNCTION(BlueprintCallable, Category = "KillStreak")
	void RecordKill(bool bIsHeadshot);

	/**
	 * 获取当前连杀数
	 * @return 当前连杀计数
	 */
	UFUNCTION(BlueprintCallable, Category = "KillStreak")
	int32 GetCurrentKillStreak() const { return CurrentKillStreak; }

	/**
	 * 设置连杀图标数据表
	 * 用途: 由 GameHUDWidget 注入
	 * @param InDataTable 关联 DT_KillStreakIconInfo
	 */
	UFUNCTION(BlueprintCallable, Category = "KillStreak")
	void SetKillStreakIconDataTable(class UDataTable* InDataTable);

	/**
	 * 设置连杀系统配置
	 * 用途: 由 GameHUDWidget 注入 (KillStreakDuration / KillStreakIconDisplayDuration)
	 * @param InConfig 连杀配置
	 */
	UFUNCTION(BlueprintCallable, Category = "KillStreak")
	void SetKillStreakConfig(float InKillStreakDuration, float InIconDisplayDuration);

protected:
	// ==========================================
	// 2. 生命周期
	// ==========================================

	/**
	 * 初始化
	 * 1. 隐藏 Image_Kill
	 * 2. 初始化连杀计数为 0
	 */
	virtual bool Initialize() override;

private:
	// ==========================================
	// 3. UI 组件
	// ==========================================

	/**
	 * 唯一图标显示组件
	 * 显示当前连杀/爆头图标
	 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_Kill;

	// ==========================================
	// 4. 连杀状态
	// ==========================================

	/** 当前连杀计数 */
	int32 CurrentKillStreak = 0;

	/** 上一次是否爆头 */
	bool bLastKillWasHeadshot = false;

	// ==========================================
	// 5. 计时器
	// ==========================================

	/** 连杀计时器 */
	FTimerHandle KillStreakTimer;

	/** 图标显示定时器 */
	FTimerHandle IconDisplayTimer;

	/** 连杀超时回调 */
	UFUNCTION()
	void OnKillStreakExpired();

	/** 隐藏图标回调 */
	UFUNCTION()
	void HideIcon();

	// ==========================================
	// 6. 数据表
	// ==========================================

	/**
	 * 连杀图标数据表
	 * 关联: DT_KillStreakIconInfo
	 */
	UPROPERTY()
	class UDataTable* KillStreakIconDataTable;

	/**
	 * 连杀系统配置 (v41 新增 - 数据驱动)
	 * 由 GameHUDWidget 通过 SetKillStreakConfig 注入
	 */
	float KillStreakDuration = 10.0f;
	float KillStreakIconDisplayDuration = 3.0f;

	// ==========================================
	// 7. 核心逻辑
	// ==========================================

	/**
	 * 根据击杀情况更新图标
	 * @param Kills 当前连杀数
	 * @param bIsHeadshot 是否爆头
	 */
	void UpdateKillIcon(int32 Kills, bool bIsHeadshot);

	/**
	 * 从数据表获取图标
	 * @param StreakType 连杀类型
	 * @return 图标贴图
	 */
	UTexture2D* GetKillStreakIcon(EKillStreakType StreakType);

	/**
	 * 根据击杀数转换为连杀类型
	 * @param Kills 当前连杀数
	 * @param bIsHeadshot 是否爆头
	 */
	EKillStreakType GetKillStreakType(int32 Kills, bool bIsHeadshot) const;

	/**
	 * 显示图标 (封装 Show/Hide)
	 * @param bVisible 是否显示
	 */
	void SetIconVisibility(bool bVisible);
};
