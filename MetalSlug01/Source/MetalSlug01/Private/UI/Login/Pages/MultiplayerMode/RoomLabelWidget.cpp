#include "UI/Login/Pages/MultiplayerMode/RoomLabelWidget.h"
// 包含按钮和文本框的头文件
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

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

void URoomLabelWidget::SetRoomName(const FString& InRoomName)
{
	if (Text_RoomName)
	{
		Text_RoomName->SetText(FText::FromString(InRoomName));
	}
}

FString URoomLabelWidget::GetRoomName() const
{
	// 如果文本框有效，返回里面的文本内容；否则返回空字符串
	if (Text_RoomName)
	{
		return Text_RoomName->GetText().ToString();
	}
	return FString();
}

void URoomLabelWidget::OnRoomItemClicked()
{
	// 【新增】当玩家点击这个条目时，把自己的房间名广播出去
	OnRoomSelected.Broadcast(GetRoomName());
	
	if (GEngine) 
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan, FString::Printf(TEXT("你点击了房间: %s"), *GetRoomName()));
	}
}

void URoomLabelWidget::SetPlayerCount(int32 CurrentPlayers, int32 MaxPlayers)
{
	if (Text_PlayerCount)
	{
		// 格式化为：(当前人数/总人数)，例如 (3/10)
		FString CountString = FString::Printf(TEXT("(%d/%d)"), CurrentPlayers, MaxPlayers);
		Text_PlayerCount->SetText(FText::FromString(CountString));
	}
}

void URoomLabelWidget::SetHighlight(bool bIsHighlight)
{
	if (Image_Highlight)
	{
		// 亮起时可见（且不阻挡点击），熄灭时隐藏
		Image_Highlight->SetVisibility(bIsHighlight ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
	}
}

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