// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Login/Pages/MultiplayerMode/RoomLabelWidget.h"
// 包含按钮和文本框的头文件
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * URoomLabelWidget::Initialize
 *
 * 1. 绑定 Btn_Background 点击 -> OnRoomItemClicked
 */
bool URoomLabelWidget::Initialize()
{
	if (!Super::Initialize()) return false;

	// 绑定背景按钮的点击事件
	if (Btn_Background)
	{
		Btn_Background->OnClicked.AddDynamic(this, &URoomLabelWidget::OnRoomItemClicked);
	}

	return true;
}


// ==========================================
// 2. 公共接口
// ==========================================

/**
 * URoomLabelWidget::SetRoomName
 *
 * 设置房间名: 同时写 UI 和数据缓存
 * 设计理由: Model-View 分离, View 内部缓存数据, 不依赖 UI 反读
 */
void URoomLabelWidget::SetRoomName(const FString& InRoomName)
{
	// 【Bug2 修复】先写缓存 (数据源)
	CachedRoomName = InRoomName;

	// 再写 UI
	if (Text_RoomName)
	{
		Text_RoomName->SetText(FText::FromString(InRoomName));
	}
	else
	{
		// 【大厂诊断】: TextBlock 漏绑会导致房间名不显示, 必须给出明确告警
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLabelWidget] Text_RoomName 控件未绑定! 请检查 WBP_RoomLabel 里是否拖入了同名的 TextBlock (类型必须为 UTextBlock)"));
	}
}


/**
 * URoomLabelWidget::GetRoomName
 *
 * 【Bug2 修复】直接读数据缓存, 不再从 TextBlock 反读
 * 优势: 即使 Text_RoomName 未绑定, 房间名也能正确返回
 */
FString URoomLabelWidget::GetRoomName() const
{
	return CachedRoomName;
}


/**
 * URoomLabelWidget::SetPlayerCount
 *
 * 格式化为 (当前/总人数) e.g. (3/10)
 */
void URoomLabelWidget::SetPlayerCount(int32 CurrentPlayers, int32 MaxPlayers)
{
	if (Text_PlayerCount)
	{
		// 格式化为: (当前人数/总人数), 例如 (3/10)
		FString CountString = FString::Printf(TEXT("(%d/%d)"), CurrentPlayers, MaxPlayers);
		Text_PlayerCount->SetText(FText::FromString(CountString));
	}
}


/**
 * URoomLabelWidget::SetHighlight
 *
 * 选中: SelfHitTestInvisible（可见但不挡点击, 让点击穿透到 Btn_Background）
 * 未选: Hidden（隐藏, 不占空间）
 */
void URoomLabelWidget::SetHighlight(bool bIsHighlight)
{
	if (Image_Highlight)
	{
		// 亮起时可见（且不阻挡点击），熄灭时隐藏
		Image_Highlight->SetVisibility(bIsHighlight ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
	}
}


/**
 * URoomLabelWidget::SetRoomState
 *
 * 游戏中: 暗红色 (0.6, 0, 0, 1)
 * 等待中: 纯白色
 */
void URoomLabelWidget::SetRoomState(bool bIsPlaying)
{
	if (Text_RoomStatus)
	{
		if (bIsPlaying)
		{
			Text_RoomStatus->SetText(FText::FromString(TEXT("游戏中")));
			// 设置为暗红色 (R, G, B, Alpha)
			Text_RoomStatus->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.0f, 0.0f, 1.0f)));
		}
		else
		{
			Text_RoomStatus->SetText(FText::FromString(TEXT("等待中")));
			// 设置为纯白色
			Text_RoomStatus->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)));
		}
	}
}


// ==========================================
// 3. 内部回调
// ==========================================

/**
 * URoomLabelWidget::OnRoomItemClicked
 *
 * 玩家点击这个条目时触发
 * 1. 广播 OnRoomSelected 事件, 参数为 widget 自身引用
 * 2. 屏幕调试
 *
 * 【架构升级】: 由广播字符串改为广播 widget 引用
 * 原因: 旧设计 GetRoomName() 依赖 CachedRoomName, 若 CachedRoomName 为空外层收不到房间名
 *       新设计: 外层通过 widget 引用直接调用 GetRoomName(), 数据源单一可信
 */
void URoomLabelWidget::OnRoomItemClicked()
{
	// 【架构升级】广播 widget 自身引用, 不广播字符串
	OnRoomSelected.Broadcast(this);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
			FString::Printf(TEXT("你点击了房间: %s"), *GetRoomName()));
	}
}
