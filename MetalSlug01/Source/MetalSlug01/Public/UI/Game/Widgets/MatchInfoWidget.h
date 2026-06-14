// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Data/StaticTable.h" // 引入 ERoomMatchMode
#include "MatchInfoWidget.generated.h"

// 前向声明
class ARoomGameState;
class UTextBlock;
class UHorizontalBox;
class UImage;


/**
 * @class UMatchInfoWidget
 * @brief 比赛信息组件
 *
 * 职责说明:
 * - 显示攻方人数、守方人数、单局倒计时、剩余局数
 * - 显示双方人数图标（最多 10 个）
 * - 倒计时最后 10 秒变红
 *
 * 架构理念:
 * 1. 延迟绑定: 在 NativeTick 中第一次找到 GameState 时才订阅事件
 * 2. 重绘优化: Dirty Flag 机制，避免每秒 60 次重设文本
 * 3. 缓存引用: CachedGameState 避免每帧 Cast 操作
 * 4. 风格解耦: 阈值/颜色由蓝图配置
 */
UCLASS()
class METALSLUG01_API UMatchInfoWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公共接口
	// ==========================================

	/**
	 * 更新攻方人数
	 * @param Count 当前攻方击杀数
	 */
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void UpdateAttackerCount(int32 Count);

	/**
	 * 更新守方人数
	 * @param Count 当前守方击杀数
	 */
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void UpdateDefenderCount(int32 Count);

	/**
	 * 更新剩余局数
	 * @param Rounds 剩余局数
	 */
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void UpdateRemainingRounds(int32 Rounds);

	/**
	 * 根据游戏模式设置 Text_RemainingRounds 的显示状态
	 * 刀战模式（Melee）下隐藏，生化模式（Zombie）下显示
	 * @param Mode 当前游戏模式
	 */
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void SetVisibilityByMode(ERoomMatchMode Mode);

	/**
	 * 添加攻方人数图标
	 * 用途: 超过 MaxIconDisplayCount 后不再添加
	 */
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void AddAttackerIcon(UTexture2D* Icon);

	/**
	 * 添加守方人数图标
	 */
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void AddDefenderIcon(UTexture2D* Icon);

protected:
	// ==========================================
	// 2. 生命周期
	// ==========================================

	/**
	 * 初始化默认值: 攻/守方数为 0, 倒计时 00:00, 剩余局数 0
	 * 防御性检查: 防止 BindWidget 绑定失败时访问 nullptr
	 */
	virtual bool Initialize() override;

	/**
	 * UE 标准做法: 如果 UI 频繁更新状态，使用 NativeTick
	 * 优化: 可改用内部 Timer
	 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ==========================================
	// 3. 内部回调
	// ==========================================

	/**
	 * 响应双方击杀人数变化的回调
	 * @param AttackerKills 攻方击杀数
	 * @param DefenderKills 守方击杀数
	 */
	UFUNCTION()
	void OnTeamKillCountChanged(int32 AttackerKills, int32 DefenderKills);

	// ==========================================
	// 4. 风格配置（蓝图可编辑）
	// ==========================================

	/**
	 * 倒计时红色阈值
	 * 默认 10 秒，最后 10 秒变红
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MatchInfo|Style")
	int32 WarningTimeThreshold = 10;

	/**
	 * 正常倒计时颜色（白色）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MatchInfo|Style")
	FSlateColor NormalTimeColor = FSlateColor(FLinearColor::White);

	/**
	 * 警告倒计时颜色（红色）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MatchInfo|Style")
	FSlateColor WarningTimeColor = FSlateColor(FLinearColor::Red);

private:
	// ==========================================
	// 5. UI 组件
	// ==========================================

	/**
	 * 攻方人数文本
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_AttackerCount;

	/**
	 * 守方人数文本
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_DefenderCount;

	/**
	 * 攻方人数图标容器（最多 10 个）
	 */
	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* HB_AttackerIcons;

	/**
	 * 守方人数图标容器
	 */
	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* HB_DefenderIcons;

	/**
	 * 单局倒计时文本（mm:ss 格式）
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_RoundCountdown;

	/**
	 * 剩余局数文本
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_RemainingRounds;

	/**
	 * 最大图标显示数量
	 * 超过后不再添加
	 */
	int32 MaxIconDisplayCount = 10;

	/**
	 * 是否已成功绑定 GameState 的标志位
	 * 防止每帧重复尝试绑定或重复订阅事件
	 */
	bool bIsBoundToGameState = false;

	/**
	 * 缓存 GameState 引用
	 * 用途: 避免每帧执行 Cast 或 Get 操作，提升性能
	 */
	UPROPERTY(Transient)
	ARoomGameState* CachedGameState;

	/**
	 * 用于记录上一秒的值
	 * 避免每帧都在重复更新 Text 和渲染（UI 重绘开销很大）
	 */
	int32 LastRenderedSeconds = -1;
};
