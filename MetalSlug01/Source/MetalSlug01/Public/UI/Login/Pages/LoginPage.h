// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// 包含虚幻引擎核心最小化组件
#include "CoreMinimal.h"
// 包含用户控件基类，所有 UI 蓝图的 C++ 父类必须包含此文件
#include "Blueprint/UserWidget.h"
// 自动生成的反射头文件，必须放在最后一个包含
#include "LoginPage.generated.h"

// 前向声明 UI 组件类，避免在头文件中引入大量无用代码，加快编译速度
class UEditableTextBox;
class UButton;
class UTextBlock;
class UOverlay;

/**
 * @class ULoginPage
 * @brief 登录页面（双开测试下的"看门人"）
 *
 * 职责说明:
 * - 提供账号/密码输入与登录/注册按钮
 * - 调用 UAccountSubsystem 验证账号
 * - 登录成功后切换至 EMatchState::MainLobby 状态，并跳转主菜单
 * - 提供"退出游戏"按钮
 *
 * 架构理念:
 * 1. UI 解耦: 通过类引用（TSubclassOf）动态创建主菜单
 * 2. 全局唯一: 通过 GameInstance->GetSubsystem 获取账号子系统
 * 3. 工业级防呆: 双开同账号互踢检测
 * 4. 状态机驱动: 登录成功后通知 GameFlowSubsystem 切状态
 */
UCLASS()
class METALSLUG01_API ULoginPage : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 蓝图可配置
	// ==========================================

	/**
	 * 主菜单页面类
	 * 用途: 在编辑器中选择 WBP_GameMenu 蓝图类
	 * 登录成功后动态创建并显示此页面
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<class UUserWidget> GameMenuClass;

protected:
	// ==========================================
	// 生命周期
	// ==========================================

	/**
	 * 重写 UserWidget 的初始化函数
	 * 时机: UI 创建时最先调用的地方
	 * 用途: 绑定按钮事件、设置密码暗文、设置输入模式
	 */
	virtual bool Initialize() override;

	// ==========================================
	// UI 组件绑定
	// 注意: 变量名称必须与蓝图中的组件名称一模一样
	// ==========================================

	/**
	 * 账号输入框
	 */
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_Username;

	/**
	 * 密码输入框
	 */
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_Password;

	/**
	 * 登录按钮
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Login;

	/**
	 * 注册按钮
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Register;

	/**
	 * 提示文本组件
	 * 用途: 给玩家显示"密码错误"、"注册成功"等信息
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Hint;

	/**
	 * 关闭游戏按钮
	 */
	UPROPERTY(meta = (BindWidget))
	class UButton* Btn_QuitGame;

private:
	// ==========================================
	// 按钮点击响应函数
	// 必须加上 UFUNCTION() 宏，否则无法与蓝图中的底层委托事件绑定
	// ==========================================

	/**
	 * 点击"登录"按钮时触发
	 * 流程: 校验非空 -> 查在线 -> 查账号密码 -> 切状态 -> 跳主菜单
	 */
	UFUNCTION()
	void OnLoginButtonClicked();

	/**
	 * 点击"注册"按钮时触发
	 * 流程: 校验非空 -> TryRegister -> 提示成功/失败
	 */
	UFUNCTION()
	void OnRegisterButtonClicked();

	/**
	 * 关闭游戏按钮的点击响应
	 * 流程: 保存数据 -> QuitGame
	 */
	UFUNCTION()
	void OnQuitGameClicked();
};
