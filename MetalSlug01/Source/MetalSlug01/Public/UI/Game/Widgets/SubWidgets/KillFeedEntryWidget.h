// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
// 引入 EKillMethod
// 改造: 改为精确子表头, 不再 include 整个 432 行 StaticTable.h
#include "Data/Enums/CombatEnums.h"
#include "KillFeedEntryWidget.generated.h"

// 前向声明
class UTextBlock;
class UImage;


/**
 * @class UKillFeedEntryWidget
 * @brief 击杀信息条目子控件
 *
 * 显示单条击杀信息，包含击杀者、被击杀者、击杀图标
 *
 * 显示逻辑:
 * 1. 先判断是什么武器类型产生的击杀（主武器/副武器/近战武器）
 * 2. 再判断是爆头击杀还是普通击杀
 * 3. 根据击杀方式和爆头状态获取对应的击杀图标
 *
 * 颜色规范:
 * - 击杀者名: 青色 (0, 1, 1)
 * - 被击杀者名: 橙色 (1, 0.5, 0)
 * - 主武器: 绿色
 * - 副武器: 蓝色
 * - 近战: 红色
 *
 * 【v229.x v8 大厂架构重构 — MessageLifetime 真理源迁移 + 字体颜色委托 WBP + 0 兜底】
 *
 * 修复的反模式 (v22-v228):
 *   1. SetKillerName / SetVictimName 内联 SetColorAndOpacity — 字体设置违反 WBP 单一真理源
 *      - 修复: 颜色委托 WBP 蓝图详情面板, 代码不再设置
 *   2. IsHeadshotKill 函数 — 重复实现 (UKillFeedWidget::IsHeadshotFromKillMethod 也实现了一次)
 *      - 修复: 不再导出此函数, 内部判断 EKillMethod 是否含 Headshot 后缀
 *   3. GetIconColor 函数 — 死代码 (定义了但没调用)
 *      - 修复: 完整删除
 *   4. 不区分系统消息 / 击杀消息 — 业务唯一时 Sender 字段空
 *      - 修复: 在 SetKillInfo 内分支判定, KillerName 空 → 系统消息
 *
 * 【v229.x v8 大厂架构 — MessageLifetime 真理源迁移到 Entry (子控件)】
 *   旧 (v229.x v4-v7) 反模式: MessageLifetime 字段在父控件 UKillFeedWidget
 *     - 现象: 用户在 WBP_KillFeedEntryWidget (子控件蓝图) 上找 MessageLifetime 字段找不到
 *     - 现象: 即使用户改父控件 MessageLifetime=600, WBP_KillFeedEntryWidget 蓝图里若有动画结束自动
 *            RemoveFromParent 逻辑, C++ 600 秒 Timer 被覆盖 (大厂反模式: 真理源分裂)
 *     - 大厂原则: Entry 是真理源 — Entry 自己知道自己多久消失, 父控件只管容器布局
 *     - 修复: MessageLifetime 字段迁移到 Entry 子控件
 *            Timer 在 Entry 自己的 SetKillInfo 启动
 *            父控件 UKillFeedWidget 完全不参与生命周期管理
 *
 * 大厂原则 — 单一真理源:
 *   - 字体颜色 / 字体大小 / 字体属性: WBP 蓝图详情面板
 *   - 业务逻辑: C++ (SetKillInfo / SetKillMethod)
 *   - 图标查找: DT_KillIconInfo 数据表
 *   - 消失时间: Entry 子控件 MessageLifetime 字段 (WBP 蓝图详情面板可配)
 */
UCLASS()
class METALSLUG01_API UKillFeedEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公共接口（供外部调用）
	// ==========================================

	/**
	 * 【v229.x v4 唯一 API】设置击杀消息
	 *
	 * 支持系统消息: KillerName 为空 → 此消息是系统消息 (VictimName = 消息内容)
	 * 支持击杀消息: KillerName 非空 → 此消息是击杀消息
	 *
	 * @param InKillerName 击杀者名 (系统消息传空)
	 * @param InVictimName 被击杀者名 (系统消息 = 消息内容)
	 * @param InKillMethod 击杀方式 (系统消息 = EKillMethod::None)
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeedEntry")
	void SetKillInfo(const FString& InKillerName, const FString& InVictimName, EKillMethod InKillMethod);

	/**
	 * 设置击杀图标数据表
	 * 用途: 供父控件 (UKillFeedWidget) 调用
	 *
	 * 【v229.x v5 大厂架构】时序竞争修复:
	 *   父控件调用顺序: SetKillInfo() → SetKillIconDataTable()
	 *   旧版: SetKillInfo 内部调 FindAndSetKillIcon, 此时 KillIconDataTable 还没注入 → 永远找不到 → 而后即使注入也不再重试
	 *   修复: 注入数据表后, 如果缓存过 KillMethod, 自动重试一次 — 不再依赖调用顺序
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeedEntry")
	void SetKillIconDataTable(class UDataTable* InDataTable);

	// ==========================================
	// 2. UI 组件绑定
	// ==========================================

protected:
	/**
	 * 击杀者名称文本 (系统消息时, 此文本 Block 自动隐藏 / 也不显示)
	 * 字体颜色: WBP 蓝图详情面板配置 (青色)
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_KillerName;

	/**
	 * 被击杀者名称文本 (击杀消息 = 被击杀者名, 系统消息 = 消息内容)
	 * 字体颜色: WBP 蓝图详情面板配置 (橙色)
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_VictimName;

	/**
	 * 击杀图标 (系统消息时, 此图标 Block 自动隐藏)
	 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_KillIcon;

	/**
	 * 【v229.x v8 大厂架构 — 单一真理源】消息存活时间 (秒)
	 *
	 * Entry 自己管消失: Timer 在 SetKillInfo 启动, 到期自动 RemoveFromParent
	 * 大厂原则: 字段在 Entry 子控件上, WBP_KillFeedEntryWidget 蓝图 Details 面板直接可配
	 *
	 * 默认 5.0 秒 — 业务规则 (v229.x v4 用户规则)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KillFeedEntry", meta = (ClampMin = "0.1"))
	float MessageLifetime = 5.0f;

private:
	/**
	 * 击杀图标数据表引用
	 * 关联: DT_KillIconInfo
	 */
	UPROPERTY()
	class UDataTable* KillIconDataTable;

	/**
	 * 【v229.x v8 大厂架构 — Entry 自己管消失】MessageLifetime 到期 Timer 句柄
	 *
	 * 大厂原则 — 单一真理源: Entry 是生命周期权威
	 * - SetKillInfo 启动 Timer (用 MessageLifetime 字段)
	 * - Timer 到期 Lambda 调 RemoveFromParent
	 * - Entry 销毁时 Timer 自动失效 (UE Timer 机制)
	 * - 不需要 NativeDestruct 手动清理 (UE 自动处理 Timer Manager 失效)
	 */
	FTimerHandle ExpireTimerHandle;

	/**
	 * 【v229.x v5 大厂架构】缓存最近一次的 KillMethod
	 * 用途: 解决 SetKillInfo → SetKillIconDataTable 调用顺序导致的时序竞争
	 * 模式: 经典 "缓存失效" 模式 — 输入先到, 数据后到, 数据到时重放
	 * 大厂原则: 单一字段, 单一重放入口 (SetKillIconDataTable)
	 */
	UPROPERTY()
	EKillMethod CachedKillMethod = EKillMethod::None;

	/**
	 * 【v229.x v5 大厂架构】缓存最近一次是否需要显示图标
	 * (系统消息时不需要图标 — 此时即使 KillMethod 有值也不能重设)
	 */
	UPROPERTY()
	bool bCachedShouldShowIcon = false;

	/**
	 * 根据击杀方式查找对应的击杀图标
	 * 内部使用 ForeachRow 遍历数据表
	 * @param InKillMethod 击杀方式
	 * @return 是否找到并设置图标
	 */
	bool FindAndSetKillIcon(EKillMethod InKillMethod);
};
