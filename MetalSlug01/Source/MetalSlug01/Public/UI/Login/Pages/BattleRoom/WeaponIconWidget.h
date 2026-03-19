#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Data/StaticTable.h" // 【极其重要】：包含武器结构体定义
#include "WeaponIconWidget.generated.h"

// 顶部前向声明（写在 generated.h 旁边）
class UButton;
class UImage;
// 在顶部前向声明大厅类
class URoomInsidePage;

/**
 * 【武器棋盘格 Item 控件逻辑类】
 * 代表网格里的单一格子。负责接收数据、处理点击事件、控制高亮框显示/隐藏。
 */
UCLASS()
class METALSLUG01_API UWeaponIconWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	
	// ==========================================
	// 高亮框控制接口 (供大厅调用)
	// ==========================================
	void SetHighlightFrameVisibility(bool bIsVisible);
	
	// 把大厅的指针也传进来，方便以后“打小报告”
	void SetupWeaponItem(const FName& InWeaponRowName, const FWeaponInfo& InWeaponData, URoomInsidePage* InParentPage);

	// 【新增】：让大厅随时能查出这个格子代表的是哪把武器
	FName GetWeaponRowName() const { return RepresentedWeaponRowName; }
	
protected:
	// 生命周期函数：初始化 (用于绑定点击事件)
	virtual bool Initialize() override;

	// ==========================================
	// 【核心控件绑定】：与蓝图一一对应
	// meta = (BindWidget) 意味着蓝图里必须有同名控件，否则编译或运行时报错
	// ==========================================

	// 武器图标 (虽然是 Button 控件，但我们会用它的正常状态背景来放图片)
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_WeaponIcon;

	// 高亮框图片
	UPROPERTY(meta = (BindWidget))
	UImage* Image_HighlightBox;

private:
	// ==========================================
	// 逻辑变量
	// ==========================================
	
	// 这个格子代表的武器 ID (数据表的 RowName)
	FName RepresentedWeaponRowName;

	// 监听此格子按钮的点击事件
	UFUNCTION()
	void OnWeaponIconClicked();
	
	// 记住谁是我的顶头上司 (大厅)
	UPROPERTY()
	URoomInsidePage* ParentRoomPage;
};