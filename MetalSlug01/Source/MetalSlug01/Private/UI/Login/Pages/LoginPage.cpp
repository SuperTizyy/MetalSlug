// 包含当前类的头文件
#include "UI/Login/Pages/LoginPage.h"
// 包含所需要的 UI 组件头文件，因为头文件里只做了前向声明
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
// 包含提供游戏基础静态函数的头文件（比如关卡跳转）
#include "Kismet/GameplayStatics.h"

// 实现初始化函数
bool ULoginPage::Initialize()
{
	// 先调用父类的初始化逻辑，如果父类初始化失败，则直接返回 false 终止创建
	if (!Super::Initialize())
	{
		return false;
	}

	// 检查蓝图中是否真的存在名为 Btn_Login 的按钮组件（防止引擎崩溃）
	if (Btn_Login)
	{
		// 将 C++ 的 OnLoginButtonClicked 函数绑定到按钮的点击事件上
		Btn_Login->OnClicked.AddDynamic(this, &ULoginPage::OnLoginButtonClicked);
	}

	// 检查蓝图中是否真的存在名为 Btn_Register 的按钮组件
	if (Btn_Register)
	{
		// 将 C++ 的 OnRegisterButtonClicked 函数绑定到按钮的点击事件上
		Btn_Register->OnClicked.AddDynamic(this, &ULoginPage::OnRegisterButtonClicked);
	}

	// 检查提示文本组件是否存在
	if (Text_Hint)
	{
		// 界面刚加载时，隐藏提示文本，防止玩家一开始就看到奇怪的字
		Text_Hint->SetVisibility(ESlateVisibility::Hidden);
	}

	// 初始化成功，返回 true
	return true;
}

// 实现登录按钮的点击逻辑
void ULoginPage::OnLoginButtonClicked()
{
	// 从账号输入框中获取玩家输入的文本，并转换为 C++ 的 FString 字符串
	FString Username = Input_Username->GetText().ToString();
	// 从密码输入框中获取玩家输入的文本，并转换为 C++ 的 FString 字符串
	FString Password = Input_Password->GetText().ToString();

	// 检查玩家输入的账号和密码是否为空
	if (Username.IsEmpty() || Password.IsEmpty())
	{
		// 如果为空，将提示文本设置为具体的错误信息
		Text_Hint->SetText(FText::FromString(TEXT("账号或密码不能为空！")));
		// 将提示文本的可见性设置为可见，让玩家看到
		Text_Hint->SetVisibility(ESlateVisibility::Visible);
		// 直接返回，不再往下执行登录验证逻辑
		return;
	}

	// 【模拟登录验证】
	// 判断玩家输入的账号是否是 "admin" 并且密码是否是 "123"
	if (Username == TEXT("admin") && Password == TEXT("123"))
	{
		// 验证通过，更新提示文本为成功信息
		Text_Hint->SetText(FText::FromString(TEXT("登录成功！正在进入游戏...")));
		// 显示提示文本
		Text_Hint->SetVisibility(ESlateVisibility::Visible);

		// 【未来可扩展点】如果要做场景跳转，可以取消下面这行代码的注释
		// 使用 UGameplayStatics 跳转到你的主游戏地图（假设地图名为 Test01）
		// UGameplayStatics::OpenLevel(GetWorld(), FName("Test01")); 
	}
	else
	{
		// 如果账号密码不匹配，更新提示文本为错误信息
		Text_Hint->SetText(FText::FromString(TEXT("账号或密码错误！")));
		// 显示提示文本
		Text_Hint->SetVisibility(ESlateVisibility::Visible);
	}
}

// 实现注册按钮的点击逻辑
void ULoginPage::OnRegisterButtonClicked()
{
	// 从账号输入框中获取玩家想要注册的账号名称
	FString Username = Input_Username->GetText().ToString();
	
	// 检查玩家是否输入了账号
	if (Username.IsEmpty())
	{
		// 如果为空，提示玩家必须输入账号
		Text_Hint->SetText(FText::FromString(TEXT("请输入要注册的账号！")));
		// 显示提示文本
		Text_Hint->SetVisibility(ESlateVisibility::Visible);
		// 直接返回，拦截注册流程
		return;
	}

	// 【模拟注册逻辑】
	// 使用 FString::Printf 格式化字符串，将玩家输入的账号拼接到提示文本中
	FString Msg = FString::Printf(TEXT("账号 %s 注册成功！请登录。"), *Username);
	
	// 更新提示文本内容
	Text_Hint->SetText(FText::FromString(Msg));
	// 将成功的提示展示给玩家
	Text_Hint->SetVisibility(ESlateVisibility::Visible);
}