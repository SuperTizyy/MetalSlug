// ==========================================
// 版权声明：在项目设置的描述页面填写您的版权信息。
// ==========================================
//
// @file RespawnProgressWidget.h
// @brief 复活进度条 Widget — 显示无敌时间进度
//
// 【架构定位 — 2026.07.14 重构】
//   用于游戏开始时和玩家复活时，显示无敌时间进度。
//
// 【大厂架构 - 单一订阅点原则 (2026.07.14)】
//   - UGameHUDWidget 是唯一订阅 OnInvincibilityChanged 的地方
//   - UGameHUDWidget 直接控制本 Widget 的 Visibility (Show/Hide)
//   - 本 Widget 只负责进度条和倒计时文本的**内容更新**
//
// 【职责 (What)】
//   1. 在 NativeTick 中轮询 HealthComponent 数据
//   2. 更新进度条（随时间从 100% 减到 0%）
//   3. 更新倒计时文字（精确秒数）
//
// 【不负责 (What NOT)】
//   - 无敌期数据本身（在 UHealthComponent）
//   - 无敌期事件订阅（在 UGameHUDWidget）
//   - 本 Widget 的显示/隐藏（由 UGameHUDWidget 控制）
//
// 【大厂原则 - 单一真理源】
//   - 进度条进度 = GetWorld()->GetTimeSeconds() 与 InvincibilityExpiresAtWorldTime 的差值
//   - 不缓存进度，每帧重新计算（确保精确）
//
// 【大厂原则 - 零兜底】
//   - 无 HealthComponent → Log Error + 不更新
//   - 配置错误 → 显式报错，不静默跳过
//
// 【调用链路 (2026.07.14 重构后)】
//   HealthComponent.ActivateInvincibility()
//         ↓
//   CharacterEvents.OnInvincibilityChanged(true)
//         ↓ (唯一订阅点)
//   UGameHUDWidget.OnInvincibilityChanged(true)
//         ↓ SetVisibility(Visible)
//   Widget_RespawnProgress 显示
//         ↓ NativeTick (轮询)
//   每帧更新进度条 + 倒计时文字
//         ↓
//   HealthComponent.ExpireInvincibility_Internal()
//         ↓
//   CharacterEvents.OnInvincibilityChanged(false)
//         ↓ (唯一订阅点)
//   UGameHUDWidget.OnInvincibilityChanged(false)
//         ↓ SetVisibility(Collapsed)
//   Widget_RespawnProgress 隐藏
// ==========================================
#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RespawnProgressWidget.generated.h"

// 前向声明
class UProgressBar;
class UTextBlock;

/**
 * @class URespawnProgressWidget
 * @brief 复活进度条 Widget — 显示无敌时间进度
 *
 * 【2026.07.14 重构说明】
 * 设计模式: 轮询模式，内容驱动
 *   - 不订阅任何事件
 *   - 在 NativeTick 中轮询 HealthComponent 数据
 *   - 显示/隐藏由父容器 (UGameHUDWidget) 控制
 *
 * 使用方法:
 *   1. 在 GameHUDWidget 中作为子控件
 *   2. Blueprint 中绑定 ProgressBar 和 TextBlock 控件
 *   3. UGameHUDWidget 控制本 Widget 的 Visibility
 */
UCLASS()
class METALSLUG01_API URespawnProgressWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 生命周期
	// ==========================================

	/**
	 * Widget 构造完毕并加入视口后调用
	 * 用途: 初始化默认状态
	 */
	virtual void NativeConstruct() override;

	/**
	 * 每帧驱动进度条更新
	 * 用途: 轮询 HealthComponent 数据，更新进度条和倒计时文字
	 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ==========================================
	// 2. 公开接口
	// ==========================================

	/**
	 * 显示复活进度条
	 * 调用时机: UGameHUDWidget::OnInvincibilityChanged(true) 时调用
	 */
	UFUNCTION(BlueprintCallable, Category = "RespawnProgress")
	void Show();

	/**
	 * 隐藏复活进度条
	 * 调用时机: UGameHUDWidget::OnInvincibilityChanged(false) 时调用
	 */
	UFUNCTION(BlueprintCallable, Category = "RespawnProgress")
	void Hide();

	/**
	 * 是否当前正在显示
	 */
	UFUNCTION(BlueprintCallable, Category = "RespawnProgress")
	bool IsShowingProgress() const { return bIsShowing; }

	/**
	 * 【v201.6 大厂架构新增】显示移动锁定倒计时
	 * 调用时机: UGameHUDWidget::OnMovementLockChanged(true) 时调用
	 * 与 Show() 的区别：Show() 用于无敌期，ShowMovementLock() 用于移动锁定
	 */
	UFUNCTION(BlueprintCallable, Category = "RespawnProgress")
	void ShowMovementLock(float DurationSeconds);

	/**
	 * 【v201.6 大厂架构新增】隐藏移动锁定倒计时
	 * 调用时机: UGameHUDWidget::OnMovementLockChanged(false) 时调用
	 */
	UFUNCTION(BlueprintCallable, Category = "RespawnProgress")
	void HideMovementLock();

private:
	// ==========================================
	// 3. 内部辅助
	// ==========================================

	/**
	 * 获取当前无敌期剩余秒数
	 * @return 剩余秒数（<= 0 表示无敌期已结束）
	 */
	float GetRemainingSeconds() const;

	/**
	 * 获取无敌期总时长
	 * @return 总时长秒数
	 */
	float GetTotalDuration() const;

	/**
	 * 更新进度条和倒计时文字
	 * @param RemainingSeconds 剩余秒数
	 * @param TotalDuration 总时长秒数
	 */
	void UpdateProgress(float RemainingSeconds, float TotalDuration);

	/**
	 * 【v201.6 大厂架构新增】更新移动锁定进度条和倒计时文字
	 * 注意：不带参数，直接使用成员变量 bListeningToMovementLock / RecordedTotalDuration / MovementLockRemainingSeconds
	 */
	void UpdateMovementLockProgress();

protected:
	// ==========================================
	// 4. UI 组件绑定 (BindWidget)
	// ==========================================

	/** 无敌期进度条 */
	UPROPERTY(meta = (BindWidget))
	UProgressBar* PB_InvincibilityProgress;

	/** 倒计时文字 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Countdown;

private:
	// ==========================================
	// 5. 状态
	// ==========================================

	/** 是否正在显示 */
	bool bIsShowing = false;

	/** 记录激活时的总时长（秒） */
	float RecordedTotalDuration = 3.0f;

	/** 【v201.6 大厂架构新增】当前是否监听移动锁定（区别于无敌期） */
	bool bListeningToMovementLock = false;

	/** 【v201.6 大厂架构新增】剩余锁定时长（毫秒，用于精确倒计时） */
	float MovementLockRemainingSeconds = 0.0f;
};
