// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Game/Widgets/SubWidgets/KillFeedEntryWidget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/World.h" // 【v229.x v8】GetWorld + Timer
#include "TimerManager.h" // 【v229.x v8】FTimerManager / FTimerHandle
#include "Data/Tables/KillIconTableRow.h" // FKillIconInfo


// ==========================================
// 1. 公共接口
// ==========================================

/**
 * UKillFeedEntryWidget::SetKillInfo
 *
 * 【v229.x v4 唯一 API】设置击杀消息
 *
 * 三种模式:
 *   1. 击杀消息 (KillerName 非空): 显示 [KillerName] [Icon] [VictimName]
 *   2. 系统消息 (KillerName 空): 仅显示 [VictimName] (当作文本), Killer Text/Icon 隐藏
 *   3. 击杀消息 + 爆头: KillMethod = PrimaryHeadshot / SecondaryHeadshot / MeleeHeadshot
 *
 * 【v229.x v4 大厂原则】字体颜色委托 WBP 蓝图, 代码不 SetColorAndOpacity
 *
 * 【v229.x v5 诊断】增加 Image_KillIcon 状态诊断日志, 让 PIE 期间一次定位"不显示"根因
 *
 * 【v229.x v8 大厂架构 — 真理源迁移】Entry 自己启动 MessageLifetime Timer
 *   - 旧 (v229.x v4-v7): Timer 在父控件 UKillFeedWidget::InsertKillMessageWidget 启动
 *     - 反模式: MessageLifetime 字段在父控件, 用户在子控件蓝图上找不到
 *     - 反模式: WBP_KillFeedEntryWidget 蓝图若有动画结束 RemoveFromParent, 会覆盖 C++ Timer
 *   - 新 (v229.x v8): Timer 在 Entry 自己的 SetKillInfo 启动
 *     - 真理源 = Entry.MessageLifetime (WBP 蓝图可直接配置)
 *     - 父控件 UKillFeedWidget 不再参与生命周期管理, 只管容器布局
 *     - 大厂原则 — 单一真理源: Entry 是生命周期权威
 */
void UKillFeedEntryWidget::SetKillInfo(const FString& InKillerName, const FString& InVictimName, EKillMethod InKillMethod)
{
	// 1. 系统消息 vs 击杀消息分支
	const bool bIsSystemMessage = InKillerName.IsEmpty();

	// 【v229.x v5 大厂架构】缓存 KillMethod — 用于 SetKillIconDataTable 重放
	CachedKillMethod = InKillMethod;
	bCachedShouldShowIcon = !bIsSystemMessage;

	// 【v229.x v5 诊断】入口日志 — 锚点, 确认 SetKillInfo 真的被调用
	UE_LOG(LogTemp, Log,
		TEXT("[KillFeedEntry] SetKillInfo ENTER: KillerName='%s', VictimName='%s', KillMethod=%d, bIsSystemMessage=%d, ")
		TEXT("Text_KillerName=%s, Text_VictimName=%s, Image_KillIcon=%s, KillIconDataTable=%s"),
		*InKillerName, *InVictimName, (int32)InKillMethod, bIsSystemMessage ? 1 : 0,
		*GetNameSafe(Text_KillerName), *GetNameSafe(Text_VictimName),
		*GetNameSafe(Image_KillIcon), *GetNameSafe(KillIconDataTable));

	if (Text_KillerName)
	{
		if (bIsSystemMessage)
		{
			// 系统消息: 击杀者文本 Block 隐藏
			Text_KillerName->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			// 击杀消息: 显示击杀者名
			Text_KillerName->SetVisibility(ESlateVisibility::HitTestInvisible);
			Text_KillerName->SetText(FText::FromString(InKillerName));
			// 【v229.x v4 大厂原则】字体颜色委托 WBP 详情面板, 代码不 SetColorAndOpacity
		}
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillFeedEntry] SetKillInfo: Text_KillerName 未绑定! ")
			TEXT("【大厂 0 兜底】WBP_KillFeedEntryWidget 必须有名为 Text_KillerName 的 TextBlock."));
	}

	// 2. 被击杀者名 (系统消息 = 消息内容)
	if (Text_VictimName)
	{
		Text_VictimName->SetVisibility(ESlateVisibility::HitTestInvisible);
		Text_VictimName->SetText(FText::FromString(InVictimName));
		// 【v229.x v4 大厂原则】字体颜色委托 WBP 详情面板, 代码不 SetColorAndOpacity
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillFeedEntry] SetKillInfo: Text_VictimName 未绑定! ")
			TEXT("【大厂 0 兜底】WBP_KillFeedEntryWidget 必须有名为 Text_VictimName 的 TextBlock."));
	}

	// 3. 击杀图标 (系统消息 → 隐藏)
	if (Image_KillIcon)
	{
		// 【v229.x v5 诊断】Image 控件状态详情 — 一次定位"控件有多大"/"是不是 Image"
		const FSlateBrush& CurrentBrush = Image_KillIcon->GetBrush();
		const FVector2D LocalSize = Image_KillIcon->GetDesiredSize();
		UE_LOG(LogTemp, Log,
			TEXT("[KillFeedEntry] SetKillInfo Image_KillIcon 状态: ")
			TEXT("BrushResourceObject=%s, BrushImageSize=(%.1f, %.1f), ")
			TEXT("BrushDrawAs=%d, Visibility=%d, ")
			TEXT("DesiredSize=(%.1f, %.1f)"),
			*GetNameSafe(CurrentBrush.GetResourceObject()),
			CurrentBrush.ImageSize.X, CurrentBrush.ImageSize.Y,
			(int32)CurrentBrush.DrawAs,
			(int32)Image_KillIcon->GetVisibility(),
			LocalSize.X, LocalSize.Y);

		if (bIsSystemMessage)
		{
			Image_KillIcon->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			Image_KillIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
			// 查找并设置图标
			FindAndSetKillIcon(InKillMethod);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillFeedEntry] SetKillInfo: Image_KillIcon 未绑定! ")
			TEXT("【大厂 0 兜底】WBP_KillFeedEntryWidget 必须有名为 Image_KillIcon 的 Image."));
	}

	// 4. 【v229.x v8 大厂架构 — 真理源迁移】Entry 自己启动 MessageLifetime Timer
	// 大厂原则: Entry 是生命周期权威, 父控件不参与
	// WBP 蓝图可配 MessageLifetime=600 → 10 分钟后消失
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillFeedEntry] SetKillInfo: GetWorld() 为空! ")
			TEXT("无法启动 MessageLifetime=%.1fs Timer. Entry=%s"),
			MessageLifetime, *GetName());
		return;
	}

	// 弱引用捕获 Entry, 防止 Entry 提前销毁 Lambda 野指针
	TWeakObjectPtr<UKillFeedEntryWidget> WeakEntry(this);

	World->GetTimerManager().SetTimer(
		ExpireTimerHandle,
		FTimerDelegate::CreateLambda([WeakEntry]()
		{
			// MessageLifetime 秒后: 弱指针校验 + RemoveFromParent
			if (UKillFeedEntryWidget* Entry = WeakEntry.Get())
			{
				Entry->RemoveFromParent();
			}
		}),
		MessageLifetime,
		/*bLoop=*/false);

	UE_LOG(LogTemp, Verbose,
		TEXT("[KillFeedEntry] SetKillInfo: 启动 MessageLifetime=%.1fs Timer. Entry=%s"),
		MessageLifetime, *GetName());
}


// ==========================================
// 2. 数据表注入
// ==========================================

/**
 * UKillFeedEntryWidget::SetKillIconDataTable
 *
 * 供父控件注入数据表引用
 *
 * 【v229.x v5 大厂架构】时序竞争修复:
 *   父控件调用顺序: SetKillInfo() → SetKillIconDataTable()
 *   旧版: SetKillInfo 内部调 FindAndSetKillIcon, 此时 KillIconDataTable 还没注入 → 永远找不到
 *   修复: 注入数据表后, 自动重放一次 (因为 SetKillInfo 缓存了 KillMethod)
 *
 * 【v229.x v5 诊断】增加诊断日志, 确认注入链路
 */
void UKillFeedEntryWidget::SetKillIconDataTable(class UDataTable* InDataTable)
{
	KillIconDataTable = InDataTable;

	// 【v229.x v5 诊断】注入日志 — 验证父控件→子控件数据流
	// 【v229.x v5.1 修复】GetOwner() 在 UE 5.6 UUserWidget 上没有 — 改用 GetOuter (UObject 基类稳定 API)
	UE_LOG(LogTemp, Log,
		TEXT("[KillFeedEntry] SetKillIconDataTable: DataTable='%s', Outer='%s', ")
		TEXT("CachedKillMethod=%d, bCachedShouldShowIcon=%d"),
		*GetNameSafe(InDataTable), *GetNameSafe(GetOuter()),
		(int32)CachedKillMethod, bCachedShouldShowIcon ? 1 : 0);

	// 【v229.x v5 大厂架构】时序竞争重放 — 数据表已就绪, 如果之前 SetKillInfo 调用时拿不到资源, 这里补一次
	if (KillIconDataTable && bCachedShouldShowIcon && CachedKillMethod != EKillMethod::None)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[KillFeedEntry] SetKillIconDataTable: 重放 FindAndSetKillIcon. KillMethod=%d"),
			(int32)CachedKillMethod);

		// 触发图标设置 (此时 KillIconDataTable 已就绪, DataTable→Image 全链路通)
		FindAndSetKillIcon(CachedKillMethod);
	}
}


// ==========================================
// 3. 数据表查找 (私有)
// ==========================================

/**
 * UKillFeedEntryWidget::FindAndSetKillIcon
 *
 * 遍历数据表查找匹配的击杀方式
 * 找到后立即设置 Image_KillIcon
 *
 * 【v229.x v4 大厂原则 — 0 兜底】
 * - KillIconDataTable 为空 / Image_KillIcon 为空 → Log Error + return false
 * - 数据表未匹配 → Log Error + return false (策划应检查 DT_KillIconInfo)
 *
 * 【v229.x v5 诊断】增加遍历日志, 看清每行匹配状态
 *
 * @return 是否找到并设置图标
 */
bool UKillFeedEntryWidget::FindAndSetKillIcon(EKillMethod InKillMethod)
{
	// 【v229.x v4 大厂 0 兜底】字段校验 — 强制要求配置
	if (!KillIconDataTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillFeedEntry] FindAndSetKillIcon: KillIconDataTable 为空! ")
			TEXT("【大厂 0 兜底】必须在 GameHUDWidget 或父控件 注入 DT_KillIconInfo."));
		return false;
	}

	if (!Image_KillIcon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[KillFeedEntry] FindAndSetKillIcon: Image_KillIcon 未绑定! ")
			TEXT("【大厂 0 兜底】WBP_KillFeedEntryWidget 必须有名为 Image_KillIcon 的 Image."));
		return false;
	}

	// 【v229.x v5 诊断】DataTable 行数 + RowName 列表
	// 大厂 0 兜底: 复用 GetRowMap() 拿行数, 不增加新 API
	const TMap<FName, uint8*>& RowMap = KillIconDataTable->GetRowMap();
	UE_LOG(LogTemp, Log,
		TEXT("[KillFeedEntry] FindAndSetKillIcon: DataTable='%s', RowNum=%d, LookingFor KillMethod=%d"),
		*KillIconDataTable->GetName(), RowMap.Num(), (int32)InKillMethod);

	// 遍历数据表查找匹配的击杀方式
	bool bFound = false;
	KillIconDataTable->ForeachRow<FKillIconInfo>(TEXT("LookupKillIcon"),
		[InKillMethod, this, &bFound](const FName& RowName, const FKillIconInfo& Row)
		{
			// 【v229.x v5 诊断】每行匹配详情 — 一次看清为什么找不到
			UE_LOG(LogTemp, Log,
				TEXT("[KillFeedEntry] FindAndSetKillIcon 行扫描: RowName='%s', Row.KillMethod=%d, Row.KillIcon=%s, Match=%d"),
				*RowName.ToString(), (int32)Row.KillMethod, *GetNameSafe(Row.KillIcon),
				(Row.KillMethod == InKillMethod && Row.KillIcon) ? 1 : 0);

			if (Row.KillMethod == InKillMethod && Row.KillIcon)
			{
				Image_KillIcon->SetBrushFromTexture(Row.KillIcon);
				bFound = true;

				// 【v229.x v5 诊断】SetBrushFromTexture 后状态 — 验证真的设置成功
				const FSlateBrush& AfterBrush = Image_KillIcon->GetBrush();
				UE_LOG(LogTemp, Log,
					TEXT("[KillFeedEntry] FindAndSetKillIcon SetBrushFromTexture OK: ")
					TEXT("RowName='%s', Texture='%s', ")
					TEXT("BrushResourceObject=%s, BrushImageSize=(%.1f, %.1f)"),
					*RowName.ToString(), *GetNameSafe(Row.KillIcon),
					*GetNameSafe(AfterBrush.GetResourceObject()),
					AfterBrush.ImageSize.X, AfterBrush.ImageSize.Y);
			}
		});

	if (!bFound)
	{
		// 【v229.x v4 大厂 0 兜底】数据表未匹配 → Log Error (策划强制修复)
		UE_LOG(LogTemp, Error,
			TEXT("[KillFeedEntry] FindAndSetKillIcon: DT_KillIconInfo 找不到 KillMethod=%d 对应的图标! ")
			TEXT("【大厂 0 兜底】策划必须在 DT_KillIconInfo 数据表补对应行 或者 ")
			TEXT("检查 KillMethod 枚举值是否正确."),
			(int32)InKillMethod);
	}

	return bFound;
}
