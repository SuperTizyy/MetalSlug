#pragma once

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
 * 游戏主菜单页面
 * 负责处理单人模式、双人模式、排行榜及退出登录的逻辑跳转
 */
UCLASS()
class METALSLUG01_API UGameMenuPage : public UUserWidget
{
	GENERATED_BODY()

public:
	
	// 暴露给蓝图的变量，用于在编辑器中选择你要跳转的局域网大厅蓝图类（WBP_LANRoom）
	// EditDefaultsOnly 表示只能在蓝图的“类默认值”面板中修改
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<class UUserWidget> LANRoomClass;
	
protected:
	// 重写初始化函数，用于绑定各个按钮的点击事件
	virtual bool Initialize() override;

	// ==========================================
	// UI 组件绑定区域 (名称必须与蓝图中的控件严格一致)
	// ==========================================

	// 绑定蓝图中的“单人模式”按钮
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_SinglePlayer;

	// 绑定蓝图中的“多人模式”按钮
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_MultiplePlayer;

	// 绑定蓝图中的“排行榜”按钮
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_Leaderboard;

	// 绑定蓝图中的“返回登录界面”按钮
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_BackToLogin;

	// 绑定一个文本提示框（用于在功能未开发完时给玩家弹文字提示）
	// 使用 BindWidgetOptional 表示这个控件在蓝图里是可选的，没有也不会报错崩溃
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Text_Hint;

private:
	// ==========================================
	// 按钮点击响应函数
	// ==========================================

	// 点击单人模式触发
	UFUNCTION()
	void OnSinglePlayerClicked();

	// 点击多人模式触发
	UFUNCTION()
	void OnMultiplePlayerClicked();

	// 点击排行榜触发
	UFUNCTION()
	void OnLeaderboardClicked();

	// 点击返回登录界面触发
	UFUNCTION()
	void OnBackToLoginClicked();
};
