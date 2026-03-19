// 包含当前类的头文件（根据你的文件路径）
#include "UI/Login/Pages/LoginPage.h"
// 包含所需要的 UI 组件头文件
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
// 包含提供游戏基础静态函数的头文件
#include "Kismet/GameplayStatics.h"
// 【关键新增】包含我们刚刚写好的全局账号子系统，用于真实的存取逻辑
#include "UI/Login/Core/AccountSubsystem.h"

// 实现初始化函数，绑定 UI 按钮事件
bool ULoginPage::Initialize()
{
	// 先调用父类的初始化逻辑，如果失败直接返回 false
	if (!Super::Initialize())
	{
		return false;
	}

	// 检查并绑定登录按钮点击事件
	if (Btn_Login)
	{
		Btn_Login->OnClicked.AddDynamic(this, &ULoginPage::OnLoginButtonClicked);
	}

	// 检查并绑定注册按钮点击事件
	if (Btn_Register)
	{
		Btn_Register->OnClicked.AddDynamic(this, &ULoginPage::OnRegisterButtonClicked);
	}

	// 界面刚加载时，隐藏提示文本
	if (Text_Hint)
	{
		Text_Hint->SetVisibility(ESlateVisibility::Hidden);
	}

	return true;
}

// ==========================================
// 真实的登录逻辑
// ==========================================
void ULoginPage::OnLoginButtonClicked()
{
	// 从输入框中获取玩家输入的账号和密码
	FString Username = Input_Username->GetText().ToString();
	FString Password = Input_Password->GetText().ToString();

	// 拦截操作：检查账号或密码是否为空
	if (Username.IsEmpty() || Password.IsEmpty())
	{
		Text_Hint->SetText(FText::FromString(TEXT("账号或密码不能为空！")));
		Text_Hint->SetVisibility(ESlateVisibility::Visible);
		return;
	}

	// 从当前的游戏世界中获取全局游戏实例 (GameInstance)
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance)
	{
		// 从游戏实例中，获取我们自己写的账号大管家 (AccountSubsystem)
		UAccountSubsystem* AccountSubsystem = GameInstance->GetSubsystem<UAccountSubsystem>();
		
		// 确保子系统存在，并调用它的 TryLogin 接口，把脏活累活全交给子系统去判断
		if (AccountSubsystem && AccountSubsystem->TryLogin(Username, Password))
		{
			// 如果 TryLogin 返回 true，说明密码完全匹配，登录成功！
			Text_Hint->SetText(FText::FromString(TEXT("登录成功！正在进入游戏...")));
			Text_Hint->SetVisibility(ESlateVisibility::Visible);

			// 【下一步解开注释即可进入游戏】
			// 使用 UGameplayStatics 跳转到你的主战斗地图（假设叫 Test01 或 NewMap）
			// UGameplayStatics::OpenLevel(GetWorld(), FName("Test01")); 
			
			//动态生成大厅 UI 并销毁登录 UI
			// 检查我们是否在蓝图里配置了大厅菜单的类
			if (GameMenuClass)
			{
				// 使用 CreateWidget 在内存中生成大厅菜单的实例
				UUserWidget* GameMenuWidget = CreateWidget<UUserWidget>(GetWorld(), GameMenuClass);
				
				// 确保生成成功
				if (GameMenuWidget)
				{
					// 将大厅菜单添加到玩家的屏幕上
					GameMenuWidget->AddToViewport();
					
					// 【过河拆桥】把自己（当前的登录页面）从屏幕上彻底销毁！
					this->RemoveFromParent();
				}
			}
			else
			{
				// 防呆设计：如果你忘了在蓝图里配置，就在屏幕上打印红字警告你
				if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, TEXT("警告：GameMenuClass 未配置！请去 WBP_LoginMenu 蓝图里设置！"));
			}
		}
		else
		{
			// 如果 TryLogin 返回 false，说明账号没找到或者密码错了
			Text_Hint->SetText(FText::FromString(TEXT("账号不存在或密码错误！")));
			Text_Hint->SetVisibility(ESlateVisibility::Visible);
		}
	}
}

// ==========================================
// 真实的注册逻辑
// ==========================================
void ULoginPage::OnRegisterButtonClicked()
{
	// 从输入框中获取玩家想要注册的账号和密码
	FString Username = Input_Username->GetText().ToString();
	FString Password = Input_Password->GetText().ToString();
	
	// 拦截操作：确保注册时两者都不为空
	if (Username.IsEmpty() || Password.IsEmpty())
	{
		Text_Hint->SetText(FText::FromString(TEXT("注册时账号和密码不能为空！")));
		Text_Hint->SetVisibility(ESlateVisibility::Visible);
		return;
	}

	// 获取全局游戏实例
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance)
	{
		// 获取账号子系统
		UAccountSubsystem* AccountSubsystem = GameInstance->GetSubsystem<UAccountSubsystem>();

		// 确保子系统存在，并调用它的 TryRegister 接口进行真实注册
		if (AccountSubsystem && AccountSubsystem->TryRegister(Username, Password))
		{
			// TryRegister 返回 true，说明内存里以前没这个号，注册并自动保存成功
			FString Msg = FString::Printf(TEXT("账号 %s 注册成功！请点击登录。"), *Username);
			Text_Hint->SetText(FText::FromString(Msg));
			Text_Hint->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			// TryRegister 返回 false，说明字典里已经有这个 Key 了，账号重复！
			Text_Hint->SetText(FText::FromString(TEXT("该账号已被占用，请换一个或直接登录！")));
			Text_Hint->SetVisibility(ESlateVisibility::Visible);
		}
	}
}