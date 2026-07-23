// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Enums/RoomEnums.h" // 引入 ERoomMatchMode
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
 * - 【v92 新增】生化模式"母体变异倒计时"显示 (8s 重置倒计时)
 *
 * 架构理念:
 * 1. 延迟绑定: 在 NativeTick 中第一次找到 GameState 时才订阅事件
 * 2. 重绘优化: Dirty Flag 机制，避免每秒 60 次重设文本
 * 3. 缓存引用: CachedGameState 避免每帧 Cast 操作
 * 4. 风格解耦: 阈值/颜色由蓝图配置
 * 5. 【v92 新增】双倒计时镜像: 比赛倒计时 (MatchEndTime) + 母体变异倒计时 (MotherMutationStartTime/Duration)
 *    - 两个倒计时共用 NativeTick 钩子, 各自维护 DirtyFlag
 *    - 单一真理源都在 GameState, Widget 只是镜像
 * 6. 【v92 新增】按模式控制显示:
 *    - Text_RoundCountdown: 仅 Melee 显示 (用 MeleeMatchDurationSeconds)
 *    - Text_RemainingRounds: 仅 Zombie 显示 (用 TotalRounds 静态显示 "总局数：xx")
 *    - Text_MotherMutationCountdown: 仅 Zombie 模式每局开局 8s 内显示
 * 7. 【v92 新增】0秒后自动隐藏:
 *    - Text_MotherMutationCountdown 倒计时归零后, Widget 主动 SetVisibility(Collapsed)
 *    - 不依赖服务器 Reset RPC (避免依赖服务端发完, 自己看着自己)
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
	 * 【v92 大厂架构重构】更新总局数 (替换 UpdateRemainingRounds)
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 数据源: GameState.TotalRounds (Replicated)
	 *   - 调用方: GameHUDWidget 订阅 GameState.OnTotalRoundsUpdated → 转发到这里
	 *   - 显示格式: "总局数：xx" (静态显示, 不倒数)
	 *
	 * 为什么用 TotalRounds 不用 CurrentRound:
	 *   - 旧版 UpdateRemainingRounds 用 CurrentRound (倒数过程), UI 显示 "5,4,3..." 不合适
	 *   - 用户反馈: 用 TotalRounds 静态显示, 避免 UI 闪烁
	 *
	 * @param TotalRounds 总回合数 (来自 GameState.TotalRounds)
	 */
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void UpdateTotalRounds(int32 TotalRounds);

	/**
	 * 根据游戏模式设置 MatchInfoWidget 各控件的显示状态
	 *
	 * 大厂原则 — 模式驱动显示 (单一入口):
	 *   - Melee 模式: Text_RoundCountdown 显示 (倒计时), Text_RemainingRounds 隐藏, Text_MotherMutationCountdown 隐藏
	 *   - Zombie 模式: Text_RoundCountdown 显示 (倒计时), Text_RemainingRounds 显示 (总局数), Text_MotherMutationCountdown 按事件动态显示
	 *
	 * @param Mode 当前游戏模式
	 */
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void SetVisibilityByMode(ERoomMatchMode Mode);

	/**
	 * 【v92 大厂架构新增】生化模式母体变异倒计时更新
	 *
	 * 由 GameHUDWidget::OnMotherMutationChanged 委托回调
	 * 单一真理源 — GameState 写入 MotherMutationStartTime/Duration 后 OnRep 触发
	 * Widget 收到事件后:
	 *   1. StartTime/Duration = 0 → 隐藏 TextBlock (倒计时未启动或已重置)
	 *   2. StartTime/Duration > 0 → 显示 TextBlock + NativeTick 开始刷新数字
	 *
	 * 大厂原则 — 镜像 UpdateTotalRounds:
	 *   - 不在事件回调里直接 SetText (由 NativeTick 统一刷新, 避免与倒计时逻辑分裂)
	 *   - 只更新"是否显示"状态, 让 NativeTick 根据 GameState 计算剩余秒数
	 *
	 * @param StartTime 服务器写入的开始时间戳 (服务器权威时钟, <= 0 表示未启动)
	 * @param Duration 倒计时总秒数 (<= 0 表示未启动)
	 */
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void UpdateMotherMutationCountdown(float StartTime, float Duration);

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
	 * 【v92 大厂架构新增】生化模式母体变异倒计时文本
	 * 显示格式: "生化变异倒计时：XX秒"
	 * 仅生化模式每局开局 8 秒内可见, 倒计时结束后隐藏
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_MotherMutationCountdown;

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

	/**
	 * 【v92 大厂架构新增】母体变异倒计时上一秒的值 (Dirty Flag)
	 * 镜像 LastRenderedSeconds, 独立维护避免相互干扰
	 */
	int32 LastRenderedMotherMutationSeconds = -1;

	/**
	 * 【v92 大厂架构新增】母体变异倒计时是否正在显示
	 * 由 UpdateMotherMutationCountdown 事件回调控制
	 * NativeTick 检查此字段决定是否刷新 TextBlock
	 */
	bool bIsMotherMutationCountdownActive = false;
};
