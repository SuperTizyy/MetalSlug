// 包含当前类的头文件
#include "UI/Login/Pages/GameMenuPage.h"
// 包含按钮和文本块的头文件
#include "Components/Button.h"
#include "Components/TextBlock.h"
// 包含场景跳转的工具头文件
#include "Kismet/GameplayStatics.h"
#include "Systems/GameFlowSubsystem.h"
#include "UI/Login/Core/AccountSubsystem.h"

// 实现初始化，绑定事件
bool UGameMenuPage::Initialize()
{
	if (!Super::Initialize()) return false;

	// 如果按钮存在，则将其与对应的 C++ 函数绑定
	if (Btn_SinglePlayer)
	{
		Btn_SinglePlayer->OnClicked.AddDynamic(this, &UGameMenuPage::OnSinglePlayerClicked);
	}
	else
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, TEXT("[GameMenuPage 致命错误] Btn_SinglePlayer 为空！请检查蓝图绑定！"));
	}

	if (Btn_MultiplePlayer) Btn_MultiplePlayer->OnClicked.AddDynamic(this, &UGameMenuPage::OnMultiplePlayerClicked);
	if (Btn_Leaderboard) Btn_Leaderboard->OnClicked.AddDynamic(this, &UGameMenuPage::OnLeaderboardClicked);

	// 绑定返回登录按钮事件
	if (Btn_BackToLogin)
	{
		// 【防呆设计】：确保先清理旧的绑定，防止多重绑定导致触发多次
		Btn_BackToLogin->OnClicked.RemoveDynamic(this, &UGameMenuPage::OnBackToLoginClicked);
		Btn_BackToLogin->OnClicked.AddDynamic(this, &UGameMenuPage::OnBackToLoginClicked);
	}
	else
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, TEXT("[GameMenuPage 致命错误] Btn_BackToLogin 为空！请检查蓝图变量名是否完全匹配（区分大小写）！"));
	}

	// 初始状态下隐藏提示文本
	if (Text_Hint) Text_Hint->SetVisibility(ESlateVisibility::Hidden);

	return true;
}

// ==========================================
// 核心逻辑实现
// ==========================================

void UGameMenuPage::OnSinglePlayerClicked()
{
	// 【未来解开注释即可跳转地图】
	// 跳转到单人闯关地图，假设地图名为 Test01
	// UGameplayStatics::OpenLevel(GetWorld(), FName("Test01")); 

	if (Text_Hint)
	{
		Text_Hint->SetText(FText::FromString(TEXT("正在载入单人模式...")));
		Text_Hint->SetVisibility(ESlateVisibility::Visible);
	}
}

void UGameMenuPage::OnMultiplePlayerClicked()
{
	
	// 【核心跳转逻辑】：动态生成局域网大厅 UI 并销毁当前主菜单
	// 检查我们是否在蓝图里配置了局域网大厅的类
	if (LANRoomClass)
	{
		// 使用 CreateWidget 在内存中生成局域网大厅菜单的实例
		UUserWidget* LANRoomWidget = CreateWidget<UUserWidget>(GetWorld(), LANRoomClass);
		
		// 确保生成成功
		if (LANRoomWidget)
		{
			// 将局域网大厅添加到玩家的屏幕上
			LANRoomWidget->AddToViewport();
			
			// 【过河拆桥】把自己（当前的主菜单页面）从屏幕上彻底销毁！
			this->RemoveFromParent();
		}
	}
	
}

void UGameMenuPage::OnLeaderboardClicked()
{
	// 弹出排行榜 UI（未来可以从服务器获取排名并展示）
	if (Text_Hint)
	{
		Text_Hint->SetText(FText::FromString(TEXT("正在向服务器请求排行榜数据...")));
		Text_Hint->SetVisibility(ESlateVisibility::Visible);
	}
}

void UGameMenuPage::OnBackToLoginClicked()
{
	// ==========================================
	// 【新增防线】：点完瞬间把按钮禁用，彻底物理隔绝玩家的连环夺命点！
	// ==========================================
	if (Btn_BackToLogin)
	{
		Btn_BackToLogin->SetIsEnabled(false);
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		// 步骤 A：底层数据层清理
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			AccountSub->Logout();
		}

		// 步骤 B：全局状态机流转
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			// 【调试】：先输出当前状态
			EMatchState CurrentState = FlowSubsystem->GetCurrentState();

			FlowSubsystem->TransitToState(EMatchState::Login);
		}
	}

	// ==========================================
	// 步骤 C：【核心修复】UI 的自我销毁 (Self-Destruct)
	// 因为这个菜单页通常是作为一个弹窗 (Popup) 加到屏幕上的。
	// 当我们通知管家切状态后，必须手动把这个菜单自己从屏幕上扒下来，
	// 否则它会永远挡在 Login 界面上面！
	// ==========================================
	this->RemoveFromParent();
}
