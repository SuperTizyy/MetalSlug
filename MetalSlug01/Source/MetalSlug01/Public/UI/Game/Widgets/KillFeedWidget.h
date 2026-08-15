// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Data/Enums/CombatEnums.h" // 引入 EKillMethod 枚举
#include "KillFeedWidget.generated.h"

// 前向声明
class UVerticalBox;
class UTextBlock;
class UKillFeedEntryWidget;


/**
 * @class UKillFeedWidget
 * @brief 击杀信息组件
 *
 * 职责说明:
 * - 显示击杀消息列表
 * - 支持多种击杀方式的图标和文本显示
 * - 支持系统消息
 * - 【v229.x v4-v8 大厂架构】每条消息自动消失 (时间由 Entry 自己管理)
 * - 【v229.x v4 大厂架构】新插入的子控件 HorizontalAlignment = HAlign_Right
 *
 * 【v229.x v8 大厂架构重构 — 真理源迁移: MessageLifetime 字段从父控件 → Entry 子控件】
 *
 * 修复的反模式 (v22-v228):
 *   1. SetTimer 局部变量: FTimerHandle CleanupTimer 在 Initialize 局部声明,函数返回被析构 → Timer 失效
 *      - 现象: 5 秒消失根本不起作用,消息永远不消失
 *   2. RemoveExpiredMessages 逻辑错: 检测数量 > MaxKillMessages,不是按时间过期
 *      - 现象: 5 秒消失被数量上限取代,变成"超过 10 条删最早的"
 *   3. 4 个 AddKillInfo 函数复制粘贴: 实现 MaxKillMessages 限制循环,4 处重复
 *      - 修复: 单一入口 InsertKillMessageWidget,4 个 public API 各自调用
 *   4. CreateKillMessage 兜底: if (KillFeedEntryWidgetClass) 走配置, else CreateKillMessageOld
 *      - 修复: 必须配 KillFeedEntryWidgetClass, Log Error + return nullptr
 *   5. 字体颜色 SetColorAndOpacity: 字体设置应在 WBP 蓝图
 *      - 修复: 委托 WBP_ScoreboardWidget (这条修了 KillFeedEntryWidget)
 *   6. IsHeadshotKill 在两个文件重复实现: Entry + KillFeedWidget
 *      - 修复: 删除 KillFeedWidget 内的 IsHeadshotFromKillMethod, 走进入路线分支直接组合
 *   7. CreateKillMessageOld 是死代码: 旧版 NewObject<UTextBlock> 创建方式, 与 WBP_KillFeedEntryWidget 重复
 *      - 修复: 完整删除 (3 个函数: CreateKillMessageOld / AddKillInfoWithHeadshot / GetIconColor)
 *   8. MaxKillMessages 字段: 数量上限逻辑是兜底, 且 5 秒消失是更好的清理机制
 *      - 修复: 删除字段, 5 秒消失是唯一清理机制
 *
 * 【v229.x v8 真理源迁移】MessageLifetime 字段从父控件 → Entry 子控件:
 *   旧 (v229.x v4-v7): MessageLifetime 在父控件 UKillFeedWidget
 *     - 现象 1: 用户在 WBP_KillFeedEntryWidget (子控件蓝图) 上找 MessageLifetime 字段找不到
 *     - 现象 2: 即使用户在父控件上改 MessageLifetime=600, WBP_KillFeedEntryWidget 蓝图若有动画结束
 *              自动 RemoveFromParent 逻辑, C++ Timer 600 秒被覆盖 (大厂反模式: 真理源分裂)
 *   新 (v229.x v8): MessageLifetime 在 Entry 子控件 UKillFeedEntryWidget
 *     - WBP_KillFeedEntryWidget 蓝图 Details 面板直接配 MessageLifetime=600 → 10 分钟后消失
 *     - Entry 自己 SetKillInfo 启动 Timer, 父控件不参与生命周期管理
 *     - 大厂原则: 单一真理源 — Entry 是生命周期权威, 父控件只管容器布局
 *
 * 算反复杂度:
 *   - 消失 = 每条 Entry 自己的 Timer (用 Entry.MessageLifetime)
 *   - 添加 = O(1) (VB_KillFeed->AddChild)
 *   - 清理 = 单点定时清理 (每条 Entry Timer 到时 RemoveFromParent)
 *
 * 大厂原则 — 0 兜底:
 *   - KillFeedEntryWidgetClass 必须配, 未配 Log Error (强制修复 WBP)
 *   - VB_KillFeed 必须绑, 未绑 Log Error
 *   - 字体颜色委托 WBP 详情面板, 代码不干预
 *   - 生命周期由 Entry 自己管, 父控件不参与 (职责单一)
 */
UCLASS()
class METALSLUG01_API UKillFeedWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公共接口（供外部调用）
	// ==========================================

	/**
	 * 【v229.x v4 单一入口】添加击杀信息
	 *
	 * 推荐使用方式: 直接传 EKillMethod (枚举里已含爆头信息)
	 * 历史函数 AddKillInfoEx / AddKillInfoWithHeadshot 已在 v229.x v4 删除
	 * (避免重复架构, 避免老调用点占位)
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void AddKillInfo(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod);

	/**
	 * 【v229.x v4】添加系统消息（如回合开始、炸弹放置等）
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void AddSystemMessage(const FString& Message);

	/**
	 * 清空所有击杀信息
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void ClearKillFeed();

	/**
	 * 设置击杀图标数据表（可选，用于数据驱动）
	 * 用途: 由 GameHUDWidget 注入
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void SetKillIconDataTable(class UDataTable* InDataTable);

protected:
	// ==========================================
	// 2. 生命周期
	// ==========================================

	virtual bool Initialize() override;
	virtual void NativeDestruct() override;

private:
	// ==========================================
	// 3. 私有成员变量
	// ==========================================

	// 【v229.x v8 大厂架构 — 真理源迁移】MessageLifetime 字段已迁移到 UKillFeedEntryWidget
	// 旧 (v229.x v4-v7): 字段在父控件 UKillFeedWidget, 用户在 WBP_KillFeedEntryWidget 蓝图上找不到
	// 新 (v229.x v8): 字段在 Entry 子控件 UKillFeedEntryWidget::MessageLifetime
	//   - WBP_KillFeedEntryWidget 蓝图 Details 面板直接可配
	//   - Entry 自己启动 Timer, 父控件不参与生命周期管理
	// 大厂原则: 单一真理源 — Entry 是生命周期权威

	/**
	 * 击杀信息容器
	 * 用途: 动态 AddChild UKillFeedEntryWidget
	 */
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* VB_KillFeed;

	/**
	 * 击杀信息条目类引用
	 * 用途: 动态创建子控件
	 * 必填: WBP_KillFeedWidget 蓝图必须配置为 WBP_KillFeedEntryWidget
	 * 如果未配, AddKillInfo 会 Log Error (大厂 0 兜底)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KillFeed", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UKillFeedEntryWidget> KillFeedEntryWidgetClass;

	/**
	 * 击杀图标数据表引用
	 * 关联: DT_KillIconInfo
	 */
	UPROPERTY()
	class UDataTable* KillIconDataTable;

	// ==========================================
	// 4. 私有方法
	// ==========================================

	/**
	 * 【v229.x v4 大厂架构】单一入口: 创建 + 插入 + 5 秒消失 Timer 全部一处
	 *
	 * 流程:
	 *   1. 验证 KillFeedEntryWidgetClass (0 兜底)
	 *   2. CreateWidget<UKillFeedEntryWidget> (构造 Entry)
	 *   3. Entry->SetKillInfo(Killer, Victim, Method) (业务数据)
	 *   4. Entry->SetKillIconDataTable(KillIconDataTable) (图标配置)
	 *   5. VB_KillFeed->AddChild(Entry) (添加到 UI)
	 *   6. UVerticalBoxSlot 设置 HorizontalAlignment = HAlign_Right (用户规则)
	 *   7. SetTimer 5 秒 → Lambda 调 Entry->RemoveFromParent() (5 秒消失)
	 *
	 * 大厂原则 — 单一入口:
	 *   - 所有 AddKillInfo 路径 (AddKillInfo / AddSystemMessage) 走本函数
	 *   - 任何"创建 entry"逻辑都集中此处 (不再有 CreateKillMessageOld / CreateKillMessage 双函数)
	 *
	 * @param KillerName 击杀者名 (系统消息传空)
	 * @param VictimName 被击杀者名 (系统消息 = 消息内容)
	 * @param KillMethod 击杀方式 (系统消息 = EKillMethod::None)
	 * @return 创建的 Entry Widget, nullptr 表示失败 (Log Error 已先调)
	 */
	UKillFeedEntryWidget* InsertKillMessageWidget(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod);
};
