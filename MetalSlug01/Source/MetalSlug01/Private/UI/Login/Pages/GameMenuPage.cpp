// 包含当前类的头文件
#include "UI/Login/Pages/GameMenuPage.h"
// 包含按钮和文本块的头文件
#include "Components/Button.h"
#include "Components/TextBlock.h"
// 包含场景跳转的工具头文件
#include "Kismet/GameplayStatics.h"

// 实现初始化，绑定事件
bool UGameMenuPage::Initialize()
{
	if (!Super::Initialize()) return false;

	// 如果按钮存在，则将其与对应的 C++ 函数绑定
	if (Btn_SinglePlayer) Btn_SinglePlayer->OnClicked.AddDynamic(this, &UGameMenuPage::OnSinglePlayerClicked);
	if (Btn_MultiplePlayer) Btn_MultiplePlayer->OnClicked.AddDynamic(this, &UGameMenuPage::OnMultiplePlayerClicked);
	if (Btn_Leaderboard) Btn_Leaderboard->OnClicked.AddDynamic(this, &UGameMenuPage::OnLeaderboardClicked);
	if (Btn_BackToLogin) Btn_BackToLogin->OnClicked.AddDynamic(this, &UGameMenuPage::OnBackToLoginClicked);

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
	// 【局域网联机预留口】
	// 未来在这里写创建监听服务器 (Listen Server) 或加入房间的代码
	if (Text_Hint)
	{
		Text_Hint->SetText(FText::FromString(TEXT("多人联机模式开发中，敬请期待！")));
		Text_Hint->SetVisibility(ESlateVisibility::Visible);
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
	// 玩家点击返回，直接重新加载登录地图，瞬间回到解放前！
	// 注意：这里的 "Map_Login" 必须替换为你实际的登录关卡名字
	UGameplayStatics::OpenLevel(GetWorld(), FName("Map_Login")); 
}
