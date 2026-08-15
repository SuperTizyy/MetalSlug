// 版权声明：在项目设置的描述页面填写您的版权信息。

#include "UI/Game/Widgets/KillFeedWidget.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
// 【v229.x v8】不再 include Engine/World.h + TimerManager.h — 父控件不参与 Entry 生命周期
#include "UI/Game/Widgets/SubWidgets/KillFeedEntryWidget.h"


// ==========================================
// 1. 公共接口
// ==========================================

/**
 * UKillFeedWidget::AddKillInfo
 *
 * 【v229.x v4 单一入口】添加击杀信息
 * 推荐: 直接传 EKillMethod (枚举内已含爆头信息)
 */
void UKillFeedWidget::AddKillInfo(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod)
{
	InsertKillMessageWidget(KillerName, VictimName, KillMethod);
}

/**
 * UKillFeedWidget::AddSystemMessage
 *
 * 【v229.x v4】添加系统消息
 * 注意: 系统消息当作特殊的 KillMessage 处理, KillerName = 空, VictimName = 消息内容
 */
void UKillFeedWidget::AddSystemMessage(const FString& Message)
{
	InsertKillMessageWidget(FString(), Message, EKillMethod::None);
}

/**
 * UKillFeedWidget::ClearKillFeed
 *
 * 清空所有击杀信息
 */
void UKillFeedWidget::ClearKillFeed()
{
	if (VB_KillFeed)
	{
		// 大厂原则 — 0 兜底: 不用 try/catch, 直接 Clear
		VB_KillFeed->ClearChildren();
	}
}

/**
 * UKillFeedWidget::SetKillIconDataTable
 *
 * 注入数据表到本控件
 *
 * 【v229.x v5 诊断】增加诊断日志, 验证 HUD → KillFeed → Entry 注入链路
 */
void UKillFeedWidget::SetKillIconDataTable(class UDataTable* InDataTable)
{
	KillIconDataTable = InDataTable;

	UE_LOG(LogTemp, Log,
		TEXT("[KillFeed] SetKillIconDataTable: KillFeedWidget='%s', InDataTable='%s' (RowNum=%d)"),
		*GetNameSafe(this), *GetNameSafe(InDataTable),
		InDataTable ? InDataTable->GetRowMap().Num() : 0);
}


// ==========================================
// 2. 生命周期
// ==========================================

/**
 * UKillFeedWidget::Initialize
 *
 * 【v229.x v4 修复】原本在 Initialize 中创建 SetTimer 是局部变量, 函数返回即析构
 * - 修复: 改为没有任何 Timer 启动 (5 秒消失由 Entry 自己的 Timer 负责)
 * - 大厂原则: 单一入口, 每条 Entry 自己管自己的生命周期
 */
bool UKillFeedWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 【v229.x v8 大厂架构】不再启动任何 Timer
	// Entry 生命周期由 Entry 自己的 SetKillInfo 启动 Timer 负责 (用 Entry.MessageLifetime)
	// 父控件不参与生命周期管理 (大厂原则 — 职责单一)
	// 真理源迁移: MessageLifetime 字段在 Entry 子控件, 不再在本类

	return true;
}

/**
 * UKillFeedWidget::NativeDestruct
 *
 * 【v229.x v8 大厂架构】清理时清空所有 Entry
 * - 各 Entry 自己的 Timer 会在 Widget 销毁时自动失效 (UE Timer 自动清空)
 * - 但 ClearChildren 显式触发, 防止 Widget 残留
 */
void UKillFeedWidget::NativeDestruct()
{
	// 大厂原则: 0 兜底 — VB_KillFeed 可能未绑, 不强制
	if (VB_KillFeed)
	{
		VB_KillFeed->ClearChildren();
	}

	Super::NativeDestruct();
}


// ==========================================
// 3. 私有方法 — 单一入口
// ==========================================

/**
 * UKillFeedWidget::InsertKillMessageWidget
 *
 * 【v229.x v8 大厂架构】单一入口: 创建 + 插入 + 布局
 *
 * 流程:
 *   1. 验证 KillFeedEntryWidgetClass (0 兜底 — 大厂原则)
 *   2. 验证 VB_KillFeed (0 兜底 — 大厂原则)
 *   3. CreateWidget<UKillFeedEntryWidget> (构造 Entry)
 *   4. Entry->SetKillInfo(Killer, Victim, Method) (业务数据 + 启动 MessageLifetime Timer)
 *   5. Entry->SetKillIconDataTable(KillIconDataTable) (图标配置)
 *   6. VB_KillFeed->AddChild(Entry) (添加到 UI)
 *   7. UVerticalBoxSlot 设置 HorizontalAlignment = HAlign_Right + Padding + VAlign_Top (用户规则)
 *   8. 强制重布局 (Invalidate + ForceLayoutPrepass) — 确保多条消息同时可见
 *
 * 【v229.x v8 真理源迁移】Timer 不再在父控件启动:
 *   - 旧 (v229.x v4-v7): 父控件 InsertKillMessageWidget 末尾启动 Timer + 读父控件 MessageLifetime
 *   - 新 (v229.x v8): Entry 子控件 SetKillInfo 内部启动 Timer + 读 Entry.MessageLifetime
 *   - 大厂原则: 父控件只管容器布局, Entry 是生命周期权威
 *
 * @param KillerName 击杀者名 (系统消息传空)
 * @param VictimName 被击杀者名 (系统消息 = 消息内容)
 * @param KillMethod 击杀方式 (系统消息 = EKillMethod::None)
 * @return 创建的 Entry Widget, nullptr 表示失败 (Log Error 已先调)
 */
UKillFeedEntryWidget* UKillFeedWidget::InsertKillMessageWidget(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod)
{
	// 1. 验证 KillFeedEntryWidgetClass (0 兜底 — 大厂原则)
	// 必填: WBP_KillFeedWidget 蓝图必须配置 KillFeedEntryWidgetClass = WBP_KillFeedEntryWidget
	// 未配 → Log Error + return nullptr (强制修复 WBP)
	if (!KillFeedEntryWidgetClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillFeed] InsertKillMessageWidget: KillFeedEntryWidgetClass 未配置! ")
			TEXT("【大厂 0 兜底】必须在 WBP_KillFeedWidget 详情面板 → KillFeed 分类 → ")
			TEXT("Kill Feed Entry Widget Class 字段 配 WBP_KillFeedEntryWidget."));
		return nullptr;
	}

	// 2. 验证 VB_KillFeed (0 兜底 — 大厂原则)
	// 必填: WBP_KillFeedWidget 必须有 VB_KillFeed 控件
	if (!VB_KillFeed)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillFeed] InsertKillMessageWidget: VB_KillFeed 未绑定! ")
			TEXT("【大厂 0 兜底】WBP_KillFeedWidget 蓝图必须有名为 VB_KillFeed 的 VerticalBox."));
		return nullptr;
	}

	// 3. 构造 Entry
	UKillFeedEntryWidget* EntryWidget = CreateWidget<UKillFeedEntryWidget>(this, KillFeedEntryWidgetClass);
	if (!EntryWidget)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillFeed] InsertKillMessageWidget: CreateWidget<UKillFeedEntryWidget> 失败! ")
			TEXT("KillFeedEntryWidgetClass=%s"), *GetNameSafe(KillFeedEntryWidgetClass));
		return nullptr;
	}

	// 4. 业务数据 (系统消息: KillerName 空, 这样 Entry 内部走系统消息分支)
	EntryWidget->SetKillInfo(KillerName, VictimName, KillMethod);

	// 5. 图标配置 — 必须在 SetKillInfo 之后, 否则 SetKillInfo 拿不到 DataTable
	// 【v229.x v5 大厂架构】时序竞争修复: SetKillIconDataTable 内部会自动重放 FindAndSetKillIcon
	// 解决: 父控件调用顺序 SetKillInfo → SetKillIconDataTable 不会导致图标丢失
	if (KillIconDataTable)
	{
		EntryWidget->SetKillIconDataTable(KillIconDataTable);
	}

	// 6. 添加到 UI
	UPanelSlot* AddedSlot = VB_KillFeed->AddChild(EntryWidget);
	if (!AddedSlot)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillFeed] InsertKillMessageWidget: VB_KillFeed->AddChild 失败! ")
			TEXT("Entry=%s"), *GetNameSafe(EntryWidget));
		return nullptr;
	}

	// 【v229.x v6 大厂架构 — Bug 2 诊断】AddChild 后立即查 VerticalBox 子控件数 + Entry DesiredSize
	// 根因假设: WBP_KillFeedEntryWidget 内 Image/Text 配置导致 DesiredSize=0,
	//          VerticalBox 把子控件叠在同一位置(看上去"只显示一条")
	// 验证: 3 次 AddChild 后, VB_KillFeed->GetChildrenCount() 应该 = 3
	//        每个 Entry 的 DesiredSize 应该有合理非零值
	const int32 ChildCount = VB_KillFeed->GetChildrenCount();
	const FVector2D EntryDesiredSize = EntryWidget->GetDesiredSize();

	// 【v229.x v6 修复 — 编译错误根因】UUserWidget 没有 GetChildAt API
	// GetChildAt 是 UPanelWidget 的 API, 而 EntryWidget = UKillFeedEntryWidget (继承 UUserWidget)
	// 正确做法: 用 UUserWidget::GetRootWidget() 拿 WidgetTree 的根 PanelWidget, 上面有 GetChildAt
	// 0 兜底: RootPanel 可能为 nullptr (Entry Widget 还没 NativeConstruct)
	UWidget* RootWidget = EntryWidget->GetRootWidget();
	UPanelWidget* RootPanel = Cast<UPanelWidget>(RootWidget);
	const int32 RootChildCount = RootPanel ? RootPanel->GetChildrenCount() : -1;
	UE_LOG(LogTemp, Log,
		TEXT("[KillFeed] InsertKillMessageWidget: AddChild OK. ")
		TEXT("VB_KillFeed 子控件数=%d, Entry DesiredSize=(%.1f, %.1f), EntryVisibility=%d, ")
		TEXT("EntryRootPanel=%s, RootPanel子控件数=%d"),
		ChildCount, EntryDesiredSize.X, EntryDesiredSize.Y,
		(int32)EntryWidget->GetVisibility(),
		*GetNameSafe(RootWidget), RootChildCount);

	// 7. 【v229.x v4 用户规则】HorizontalAlignment = HAlign_Right
	// 大厂原则: 单一权威来源 — 添加时设置, 不依赖 PIE 蓝图默认值
	// UVerticalBox AddChild 返回的是 UVerticalBoxSlot, 强制转换
	if (UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(AddedSlot))
	{
		VBoxSlot->SetHorizontalAlignment(HAlign_Right);

		// 【v229.x v6 大厂架构 — Bug 2 修复】强制 VerticalBox 子控件顶部对齐 + 2px 间距
		// 根因推测: VerticalBox 子控件在 WBP 蓝图里某项默认对齐可能错
		// 影响: 子控件 Display 偏移异常, 后续 Entry 覆盖之前 Entry
		// 修复: 显式 SetVerticalAlignment(VAlign_Top) — 让 Entry 顶部对齐到 VerticalBox 子控件 slot 顶部
		// 大厂 0 兜底: 不假设 WBP 默认值, 显式声明对齐方式
		VBoxSlot->SetPadding(FMargin(0.0f, 2.0f));
		VBoxSlot->SetVerticalAlignment(VAlign_Top);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[KillFeed] InsertKillMessageWidget: AddChild 返回的不是 UVerticalBoxSlot, ")
			TEXT("无法设置 HAlign_Right. Entry=%s"), *GetNameSafe(EntryWidget));
	}

	// 【v229.x v6 大厂架构 — Bug 2 修复】强制重布局
	// 根因: VerticalBox 子控件 0 尺寸 + bIsVolatile=false → Slate 不自动刷新
	// 修复: 每次 AddChild 后强制 InvalidateLayoutAndVolatility, 让 Slate 立即重排子控件 + 重绘
	// 大厂原则 — 0 兜底: 不假设 Entry 默认布局正确, 主动验证 + 触发重布局
	// API: UWidget::InvalidateLayoutAndVolatility (UE 5.0+) — 替代 SWidget::Invalidate
	VB_KillFeed->InvalidateLayoutAndVolatility();
	EntryWidget->InvalidateLayoutAndVolatility();

	// 【v229.x v6 大厂架构 — Bug 2 修复】立即强制 layout pass
	// 根因: AddChild 后 widget 还没 tick 过, EntryWidget->GetDesiredSize() 返回 0
	// 修复: 强制 ForceLayoutPrepass 让 Slate 立即计算 Entry 实际尺寸
	// 大厂原则 — 0 兜底: 不假设 Slate 会自动 layout, 主动触发
	// 注: ForceLayoutPrepass 是 UWidget 公共 API, 不涉及尺寸设置
	EntryWidget->ForceLayoutPrepass();

	// 8. 【v229.x v8 大厂架构 — 真理源迁移】Timer 由 Entry 自己启动
	// 旧 (v229.x v4-v7): 父控件 InsertKillMessageWidget 末尾启动 SetTimer (用父控件的 MessageLifetime)
	//   - 反模式 1: 用户在 WBP_KillFeedEntryWidget 蓝图上找 MessageLifetime 字段找不到
	//   - 反模式 2: 父控件管 Entry 生命周期, 违反 "Entry 是真理源" 原则
	// 新 (v229.x v8): Entry 自己 SetKillInfo 启动 Timer (用 Entry.MessageLifetime)
	//   - WBP_KillFeedEntryWidget 蓝图 Details 面板直接配 MessageLifetime=600 → 10 分钟后消失
	//   - 父控件不参与生命周期管理, 只管容器布局 (大厂原则 — 职责单一)
	// 大厂原则: 单一真理源 — Entry 是生命周期权威, 父控件只管容器

	return EntryWidget;
}
