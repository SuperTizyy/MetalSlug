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
// 移除冗余 include: Components/Overlay.h (本页未使用 Overlay)
#include "Kismet/KismetSystemLibrary.h" // 核心: 包含 QuitGame 退出游戏的函数
// 【架构升级】移除 Kismet/GameplayStatics.h（此页不再需要）
// 【架构升级】移除 Systems/Account/AccountSubsystem.h（直接调 Service）
// 【架构升级】新增: AccountService 业务门面（项目内已有的版本, 路径: Public/Systems/Account/）
#include "Services/AccountService.h"
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

    // 【架构升级】所有业务调用走 UAccountService, 不再直接读 UAccountSubsystem
    // 注意: 现有 UAccountService (Public/Services/AccountService.h) 的方法名是 Login() 而非 TryLogin()
    //        且它内部 OnLoginSuccess() 已经自动触发 GameFlowSubsystem->TransitToState(MainMenu), V 层不需要重复切状态
    UAccountService* AccountService = UAccountService::Get(this);
    if (!AccountService)
    {
        if (Text_Hint)
        {
            Text_Hint->SetText(FText::FromString(TEXT("[致命错误] 账号服务不可用!")));
            Text_Hint->SetVisibility(ESlateVisibility::Visible);
        }
        return;
    }

    // 调用 Service 获取结构化结果
    const EAccountLoginResult Result = AccountService->Login(Username, Password);
    if (Result == EAccountLoginResult::Success)
    {
        // 登录成功 → 提示一下, 实际跳转由 AccountService 内部完成
        if (Text_Hint)
        {
            Text_Hint->SetText(FText::FromString(TEXT("登录成功! 正在进入游戏...")));
            Text_Hint->SetVisibility(ESlateVisibility::Visible);
        }
    }
    else
    {
        // 失败 → 根据枚举显示错误（文案由 View 层本地翻译, 便于 i18n 后续迁移）
        if (Text_Hint)
        {
            // 【修复】原 static FText 函数定义在调用点之后导致 C3861
            //         改为直接 inline 翻译逻辑（一次性代码, lambda 避免命名空间污染）
            auto TranslateResult = [](EAccountLoginResult R) -> FText
            {
                switch (R)
                {
                case EAccountLoginResult::Success:            return FText::FromString(TEXT("登录成功! 正在进入游戏..."));
                case EAccountLoginResult::EmptyInput:         return FText::FromString(TEXT("账号或密码不能为空!"));
                case EAccountLoginResult::InvalidCredentials: return FText::FromString(TEXT("账号不存在或密码错误!"));
                case EAccountLoginResult::AccountLocked:      return FText::FromString(TEXT("账号已被服务端锁定!"));
                case EAccountLoginResult::InternalError:      return FText::FromString(TEXT("登录失败, 请稍后再试!"));
                default:                                      return FText::FromString(TEXT("未知错误"));
                }
            };
            Text_Hint->SetText(TranslateResult(Result));
            Text_Hint->SetVisibility(ESlateVisibility::Visible);
        }
    }
}

/**
 * ULoginPage::OnViewShown
 *
 * View 绑定后由 UIViewService 调用
 * 当前职责: 聚焦用户名输入框 + 清空历史提示
 */
void ULoginPage::OnViewShown()
{
    if (Input_Username)
    {
        Input_Username->SetKeyboardFocus();
    }
    if (Text_Hint)
    {
        Text_Hint->SetVisibility(ESlateVisibility::Hidden);
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
        if (Text_Hint)
        {
            Text_Hint->SetText(FText::FromString(TEXT("注册时账号和密码不能为空!")));
            Text_Hint->SetVisibility(ESlateVisibility::Visible);
        }
        return;
    }

    // 【架构升级】所有业务调用走 UAccountService, 不再直接读 UAccountSubsystem
    UAccountService* AccountService = UAccountService::Get(this);
    if (!AccountService)
    {
        if (Text_Hint)
        {
            Text_Hint->SetText(FText::FromString(TEXT("[致命错误] 账号服务不可用!")));
            Text_Hint->SetVisibility(ESlateVisibility::Visible);
        }
        return;
    }

    if (AccountService->Register(Username, Password))
    {
        // 注册成功
        const FString Msg = FString::Printf(TEXT("账号 %s 注册成功! 请点击登录。"), *Username);
        if (Text_Hint)
        {
            Text_Hint->SetText(FText::FromString(Msg));
            Text_Hint->SetVisibility(ESlateVisibility::Visible);
        }
    }
    else
    {
        // 注册失败: 账号已存在
        if (Text_Hint)
        {
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
    // 【架构升级】退出流程:
    // 1. UAccountSubsystem::Deinitialize 会在 GameInstance 关闭时自动 Logout + SaveData
    //    View 不需要做任何手动调用, 这正是 Service/Subsystem 抽象的价值
    // 2. 直接调 UKismetSystemLibrary::QuitGame

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, TEXT("正在退出游戏..."));
    }

    // 获取当前的玩家控制器
    APlayerController* SpecificPlayer = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;

    // 调用虚幻引擎底层的退出函数（参数: 当前上下文，控制器，退出方式，是否忽略未保存的关卡直接退）
    UKismetSystemLibrary::QuitGame(this, SpecificPlayer, EQuitPreference::Quit, true);
}


