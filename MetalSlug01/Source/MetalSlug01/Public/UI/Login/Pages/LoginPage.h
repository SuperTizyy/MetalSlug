#pragma once

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

// 声明这是一个暴露给引擎反射系统的 UI 类
// 注意：METALSLUG01_API 需要替换为你项目实际的模块名（宏名称通常是你的工程名大写加上_API）
UCLASS()
class METALSLUG01_API ULoginPage : public UUserWidget
{
	GENERATED_BODY()
	
public:	
	// 暴露给蓝图的变量，用于在编辑器中选择你要跳转的大厅菜单蓝图类（WBP_GameMenu）
	// EditDefaultsOnly 表示只能在蓝图的“类默认值”面板中修改
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<class UUserWidget> GameMenuClass;

protected:
	// 重写 UserWidget 的初始化函数，这是 UI 创建时最先调用的地方，我们在这里绑定按钮事件
	virtual bool Initialize() override;

	// ==========================================
	// UI 组件绑定区域
	// 注意：变量名称必须与蓝图（Widget Blueprint）中的组件名称一模一样！
	// ==========================================
	
	// 绑定蓝图中的账号输入框
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_Username;

	// 绑定蓝图中的密码输入框
	UPROPERTY(meta = (BindWidget))
	UEditableTextBox* Input_Password;

	// 绑定蓝图中的登录按钮
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Login;

	// 绑定蓝图中的注册按钮
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Register;

	// 绑定蓝图中的提示文本组件，用于给玩家显示“密码错误”、“注册成功”等信息
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Hint; 

private:
	// ==========================================
	// 按钮点击响应函数区域
	// 必须加上 UFUNCTION() 宏，否则无法与蓝图中的底层委托事件绑定
	// ==========================================

	// 当玩家点击“登录”按钮时触发的函数
	UFUNCTION()
	void OnLoginButtonClicked();

	// 当玩家点击“注册”按钮时触发的函数
	UFUNCTION()
	void OnRegisterButtonClicked();
};