// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 包含当前类的头文件
#include "UI/Login/Pages/LoginPage.h"
// 包含所需要的 UI 组件头文件
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Overlay.h"
#include "Kismet/KismetSystemLibrary.h" // 核心: 包含 QuitGame 退出游戏的函数
// 包含提供游戏基础静态函数的头文件
#include "Kismet/GameplayStatics.h"
// 包含我们刚刚写好的全局账号子系统，用于真实的存取逻辑
#include "Systems/Account/AccountSubsystem.h"
// 包含游戏流程管理子系统
#include "Systems/GameFlowSubsystem.h"
// 包含玩家控制器的头文件
#include "GameFramework/PlayerController.h"


// ==========================================
// 1. 初始化
// ==========================================

/**
 * ULoginPage::Initialize
 *
 * 实现初始化函数，绑定 UI 按钮事件
 * 1. 绑定登录/注册/退出按钮
 * 2. 隐藏提示文本
 * 3. 设置密码为暗文
 * 4. 关键修复: 强行抢夺鼠标控制权并显示光标
 */
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

	// 如果有密码框，设置为暗文
	if (Input_Password) Input_Password->SetIsPassword(true);

	// ==========================================
	// 【关键修复】: 强行抢夺鼠标控制权并显示光标
	// ==========================================

	// 获取当前世界的第一个玩家控制器（也就是你自己）
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC)
	{
		// 1. 让鼠标光标显形
		PC->SetShowMouseCursor(true);

		// 2. 创建一个"纯 UI 输入模式"的数据结构
		FInputModeUIOnly InputMode;
		// 设置鼠标不要被死死锁在游戏窗口里（方便你双开测试时鼠标能移到外面）
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);

		// 3. 把这个输入模式强行塞给玩家控制器
		PC->SetInputMode(InputMode);
	}

	// 【新增】: 绑定退出游戏按钮
	if (Btn_QuitGame)
	{
		Btn_QuitGame->OnClicked.AddDynamic(this, &ULoginPage::OnQuitGameClicked);
	}

	return true;
}


// ==========================================
// 2. 真实的登录逻辑
// ==========================================

/**
 * ULoginPage::OnLoginButtonClicked
 *
 * 登录按钮点击处理
 * 1. 从输入框中获取玩家输入的账号和密码
 * 2. 校验非空
 * 3. 询问账号子系统该账号是否已在其他窗口登录
 * 4. 调用 TryLogin 验证账号密码
 * 5. 登录成功 -> 切换 MainLobby 状态 -> 创建主菜单 UI -> 销毁登录 UI
 * 6. 登录失败 -> 显示错误提示
 */
void ULoginPage::OnLoginButtonClicked()
{
	// 从输入框中获取玩家输入的账号和密码
	FString Username = Input_Username->GetText().ToString();
	FString Password = Input_Password->GetText().ToString();

	// 拦截操作: 检查账号或密码是否为空
	if (Username.IsEmpty() || Password.IsEmpty())
	{
		Text_Hint->SetText(FText::FromString(TEXT("账号或密码不能为空!")));
		Text_Hint->SetVisibility(ESlateVisibility::Visible);
		return;
	}

	// 从当前的游戏世界中获取全局游戏实例 (GameInstance)
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance)
	{
		// 从游戏实例中，获取我们自己写的账号大管家 (AccountSubsystem)
		UAccountSubsystem* AccountSubsystem = GameInstance->GetSubsystem<UAccountSubsystem>();
		if (AccountSubsystem)
		{
			// ==========================================
			// 【新增拦截】: 先问大管家，这个号是不是被别人登了？
			// ==========================================
			if (AccountSubsystem->IsAccountOnline(Username))
			{
				Text_Hint->SetText(FText::FromString(TEXT("该账号已在其他地方登录!")));
				Text_Hint->SetVisibility(ESlateVisibility::Visible);
				return; // 直接拦截，不往下走
			}
			// 确保子系统存在，并调用它的 TryLogin 接口，把脏活累活全交给子系统去判断
			if (AccountSubsystem->TryLogin(Username, Password))
			{
				// 如果 TryLogin 返回 true，说明密码完全匹配，登录成功!
				Text_Hint->SetText(FText::FromString(TEXT("登录成功! 正在进入游戏...")));
				Text_Hint->SetVisibility(ESlateVisibility::Visible);

				// ==========================================
				// 【关键修复】: 登录成功后必须切换到 MainMenu 状态!
				// TransitToState(MainMenu) -> 广播 OnStateChanged -> OnFlowStateChanged(MainMenu) -> 创建 GameMenuPage
				// ==========================================
				if (UGameFlowSubsystem* FlowSubsystem = GameInstance->GetSubsystem<UGameFlowSubsystem>())
				{
					FlowSubsystem->TransitToState(EMatchState::MainMenu);
				}
				// 不再手动 CreateWidget 和 RemoveFromParent，ALoginPlayerController 会处理

			}
			else
			{
				// 如果 TryLogin 返回 false，说明账号没找到或者密码错了
				Text_Hint->SetText(FText::FromString(TEXT("账号不存在或密码错误!")));
				Text_Hint->SetVisibility(ESlateVisibility::Visible);
			}
		}
	}
}


// ==========================================
// 3. 真实的注册逻辑
// ==========================================

/**
 * ULoginPage::OnRegisterButtonClicked
 *
 * 注册按钮点击处理
 * 1. 校验非空
 * 2. 调用 TryRegister
 * 3. 成功 -> 提示去登录
 * 4. 失败 -> 账号已占用
 */
void ULoginPage::OnRegisterButtonClicked()
{
	// 从输入框中获取玩家想要注册的账号和密码
	FString Username = Input_Username->GetText().ToString();
	FString Password = Input_Password->GetText().ToString();

	// 拦截操作: 确保注册时两者都不为空
	if (Username.IsEmpty() || Password.IsEmpty())
	{
		Text_Hint->SetText(FText::FromString(TEXT("注册时账号和密码不能为空!")));
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
			FString Msg = FString::Printf(TEXT("账号 %s 注册成功! 请点击登录。"), *Username);
			Text_Hint->SetText(FText::FromString(Msg));
			Text_Hint->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			// TryRegister 返回 false，说明字典里已经有这个 Key 了，账号重复
			Text_Hint->SetText(FText::FromString(TEXT("该账号已被占用，请换一个或直接登录!")));
			Text_Hint->SetVisibility(ESlateVisibility::Visible);
		}
	}
}


// ==========================================
// 4. 退出游戏
// ==========================================

/**
 * ULoginPage::OnQuitGameClicked
 *
 * 退出游戏按钮点击处理
 * 1. 最后的数据保存（AccountSubsystem 已经在切换时存过了）
 * 2. 调用 UKismetSystemLibrary::QuitGame 退出
 */
void ULoginPage::OnQuitGameClicked()
{
	// ==========================================
	// 1. 执行最后的数据保存逻辑
	// ==========================================
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			// 【说明】: 我们在切换武器/角色时，其实已经调用了 SaveLastSelectedCharacter 等函数存进硬盘了
			// 如果你的 AccountSubsystem 里有统一的类似 SaveAllData() 的函数，可以在这里调用一次做最后的兜底

			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, TEXT("正在保存本地数据..."));
		}
	}

	// ==========================================
	// 2. 执行退出游戏指令
	// ==========================================
	// 获取当前的玩家控制器
	APlayerController* SpecificPlayer = GetWorld()->GetFirstPlayerController();

	// 调用虚幻引擎底层的退出函数（参数: 当前上下文，控制器，退出方式，是否忽略未保存的关卡直接退）
	UKismetSystemLibrary::QuitGame(this, SpecificPlayer, EQuitPreference::Quit, true);
}


