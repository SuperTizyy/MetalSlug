// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Data/StaticTable.h" // 引入 EKillStreakType 枚举
#include "KillStreakWidget.generated.h"

class UImage;


/**
 * @class UKillStreakWidget
 * @brief 击杀图标组件
 *
 * 简化逻辑:
 * - 只使用 Image_Kill 显示击杀图标
 * - 每次击杀开启 1 分钟计时，1 分钟内持续击杀则累加连杀数
 * - 爆头击杀使用 Headshot 图标，也记录在总击杀数内
 * - 根据击杀数量查询数据表显示对应图标
 *
 * 架构理念:
 * 1. 数据驱动: 图标资源全部从 DT_KillStreakIconInfo 读取
 * 2. 双定时器: 1 分钟连杀计时 + 3 秒图标自动隐藏
 * 3. 图标优先级: 爆头 > 一杀~五杀
 * 4. 注入式数据表: 由 GameHUDWidget 通过 SetKillStreakIconDataTable 注入
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
	 * 流程: 累加 CurrentKillStreak -> 重置 1 分钟计时 -> 更新图标 -> 启动 3 秒自动隐藏
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
	 * 获取连杀图标数据表
	 */
	class UDataTable* GetKillStreakIconDataTable() const { return KillStreakIconDataTable; }

	/**
	 * 设置连杀图标数据表
	 * 用途: 由 GameHUDWidget 注入
	 * @param InDataTable 关联 DT_KillStreakIconInfo
	 */
	UFUNCTION(BlueprintCallable, Category = "KillStreak")
	void SetKillStreakIconDataTable(class UDataTable* InDataTable) { KillStreakIconDataTable = InDataTable; }

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
	// 4. 连杀计时系统
	// ==========================================

	/** 当前连杀计数 */
	int32 CurrentKillStreak = 0;

	/**
	 * 连杀计时器
	 * 用途: 1 分钟内持续击杀则累加；超时则重置
	 */
	FTimerHandle KillStreakTimer;

	/** 连杀有效时间（秒） */
	float KillStreakDuration = 60.0f;

	/**
	 * 上一次是否爆头
	 * 用途: 图标优先级判断
	 */
	bool bLastKillWasHeadshot = false;

	// ==========================================
	// 5. 数据表（由 GameHUDWidget 注入，不暴露到蓝图）
	// ==========================================

	/**
	 * 连杀图标数据表
	 * 关联: DT_KillStreakIconInfo
	 */
	class UDataTable* KillStreakIconDataTable;

	// ==========================================
	// 6. 图标显示定时
	// ==========================================

	/**
	 * 图标显示定时器
	 * 用途: 3 秒后自动隐藏图标
	 */
	FTimerHandle IconDisplayTimer;

	/** 图标自动隐藏时间（秒） */
	float IconDisplayDuration = 3.0f;

	/**
	 * 隐藏图标回调
	 * 时机: IconDisplayTimer 到期
	 */
	UFUNCTION()
	void HideIcon();

	/**
	 * 连杀超时回调
	 * 时机: KillStreakTimer 到期（1 分钟内无击杀）
	 * 用途: 重置 CurrentKillStreak = 0
	 */
	UFUNCTION()
	void OnKillStreakExpired();

	/**
	 * 根据击杀情况更新图标（核心逻辑）
	 * 1. 获取 EKillStreakType
	 * 2. 从数据表取图标
	 * 3. 设置 Image_Kill 的画刷
	 */
	void UpdateKillIcon();

	/**
	 * 从数据表获取图标
	 * @param StreakType 连杀类型
	 * @return 图标贴图（找不到返回 nullptr）
	 */
	UTexture2D* GetKillStreakIcon(EKillStreakType StreakType);

	/**
	 * 根据击杀数转换为连杀类型
	 * 规则: 爆头优先 -> 否则按 1~5 杀
	 * @param Kills 当前连杀数
	 * @param bIsHeadshot 是否爆头
	 */
	EKillStreakType GetKillStreakType(int32 Kills, bool bIsHeadshot) const;
};
