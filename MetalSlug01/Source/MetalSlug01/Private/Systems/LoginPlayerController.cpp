#include "Systems/LoginPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "UI/Login/Core/AccountSubsystem.h"

void ALoginPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 1. 在主菜单里，强行把鼠标显示出来，并限制只响应 UI 操作
	bShowMouseCursor = true;
	SetInputMode(FInputModeUIOnly());

	// 2. 找子系统要车票
	bool bSkipLogin = false;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			// 读取是否是退房回来的状态
			bSkipLogin = AccountSub->bIsReturningFromRoom;
			
			// 【极其关键】：验完票后立刻销毁车票！防止玩家点退出游戏后，下次进来也跳过登录。
			AccountSub->bIsReturningFromRoom = false; 
		}
	}

	// 3. 核心分发逻辑：根据车票决定生成哪个界面
	if (bSkipLogin)
	{
		// 有车票：生成局域网大厅
		if (LANRoomUIClass)
		{
			UUserWidget* LANRoomWidget = CreateWidget<UUserWidget>(this, LANRoomUIClass);
			if (LANRoomWidget)
			{
				LANRoomWidget->AddToViewport();
			}
		}
	}
	else
	{
		// 没车票：走正常流程，生成登录页
		if (LoginUIClass)
		{
			UUserWidget* LoginWidget = CreateWidget<UUserWidget>(this, LoginUIClass);
			if (LoginWidget)
			{
				LoginWidget->AddToViewport();
			}
		}
	}
}
