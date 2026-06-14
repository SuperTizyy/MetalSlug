// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// 包含核心最小化头文件
#include "CoreMinimal.h"
// 包含用户控件基类头文件
#include "Blueprint/UserWidget.h"
// 自动生成的反射头文件，必须放在最后一行
#include "GameMenuPage.generated.h"

// 前向声明 UI 组件，避免在头文件引入多余代码
class UButton;
class UTextBlock;

/**
 * @class UGameMenuPage
 * @brief 游戏主菜单页面
 *
 * 职责说明:
 * - 处理单人模式、双人模式、排行榜、活动中心及退出登录的逻辑跳转
 * - 在编辑器中配置要跳转的局域网大厅蓝图类（WBP_LANRoom）和活动中心（WBP_ActivityNavMenu）
 * - 绑定各按钮点击事件
 *
 * 架构理念:
 * 1. UI 解耦: 通过类引用（TSubclassOf）动态创建目标页面
 * 2. 蓝图可配置: 跳转目标在编辑器中配置，代码无需硬编码
 * 3. 容错: BindWidgetOptional 让部分 UI 控件可以缺失而不报错
 */
UCLASS()
class METALSLUG01_API UGameMenuPage : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 蓝图可配置
	// ==========================================

	/**
	 * 局域网大厅页面类
	 * 用途: 在编辑器中选择 WBP_LANRoom 蓝图类
	 * 点击"多人模式"按钮后将动态创建并显示此页面
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<class UUserWidget> LANRoomClass;

	/**
	 * 活动中心页面类
	 * 用途: 在编辑器中选择 WBP_ActivityNavMenu 蓝图类
	 * 点击"活动中心"按钮后将动态创建并显示此页面
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<class UUserWidget> ActivityNavMenuClass;

protected:
	// ==========================================
	// 生命周期
	// ==========================================

	/**
	 * 重写初始化函数
	 * 用途: 绑定各个按钮的点击事件
	 */
	virtual bool Initialize() override;

	// ==========================================
	// UI 组件绑定 (名称必须与蓝图中的控件严格一致)
	// ==========================================

	/**
	 * 绑定蓝图中的"单人模式"按钮
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_SinglePlayer;

	/**
	 * 绑定蓝图中的"多人模式"按钮
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_MultiplePlayer;

	/**
	 * 绑定蓝图中的"排行榜"按钮
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Leaderboard;

	/**
	 * 绑定蓝图中的"活动中心"按钮
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_ActivityCenter;

	/**
	 * 绑定蓝图中的"返回登录界面"按钮
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_BackToLogin;

	/**
	 * 绑定一个文本提示框
	 * 用途: 在功能未开发完时给玩家弹文字提示
	 * 使用 BindWidgetOptional: 此控件在蓝图里是可选的，没有也不会报错崩溃
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Text_Hint;

private:
	// ==========================================
	// 按钮点击响应函数
	// ==========================================

	/**
	 * 点击单人模式触发
	 */
	UFUNCTION()
	void OnSinglePlayerClicked();

	/**
	 * 点击多人模式触发
	 */
	UFUNCTION()
	void OnMultiplePlayerClicked();

	/**
	 * 点击排行榜触发
	 */
	UFUNCTION()
	void OnLeaderboardClicked();

	/**
	 * 点击活动中心触发
	 */
	UFUNCTION()
	void OnActivityCenterClicked();

	/**
	 * 点击返回登录界面触发
	 */
	UFUNCTION()
	void OnBackToLoginClicked();
};
