// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// WeaponIconWidget 头文件 — 武器棋盘格 Item 控件
// ==========================================
//
// 文件作用:
//   1. 声明 UWeaponIconWidget — 网格里的单一武器格子
//   2. 接收数据 (武器 ID + FWeaponInfo + 父页面指针)
//   3. 处理点击事件 (向父页面打小报告)
//   4. 控制高亮框的显示/隐藏
//
// 架构理念:
//   - 单一职责: 一个小格子只管自己
//   - 反向引用: 持有 URoomInsidePage 指针方便回调
//   - 状态自描述: RepresentedWeaponRowName 让父页面能反查
//   - 防御性: 高亮框默认 Collapsed, 点击再显示
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Tables/WeaponTableRow.h" // 【极其重要】: 包含 FWeaponInfo 结构体定义
#include "WeaponIconWidget.generated.h"

// 前向声明
class UButton;
class UImage;
// 在顶部前向声明大厅类
class URoomInsidePage;


/**
 * @class UWeaponIconWidget
 * @brief 武器棋盘格 Item 控件逻辑类
 *
 * 职责说明:
 * - 代表网格里的单一格子
 * - 接收数据（武器 ID + FWeaponInfo + 父页面指针）
 * - 处理点击事件（向父页面打小报告）
 * - 控制高亮框的显示/隐藏
 *
 * 架构理念:
 * 1. 单一职责: 一个小格子只管自己
 * 2. 反向引用: 持有 URoomInsidePage 指针方便回调
 * 3. 状态自描述: RepresentedWeaponRowName 让父页面能反查
 * 4. 防御性: 高亮框默认 Collapsed, 点击再显示
 *
 * 关联:
 * - 上级: URoomInsidePage（创建/管理 Item）
 * - 数据源: FWeaponInfo（来自 DataTable）
 */
UCLASS()
class METALSLUG01_API UWeaponIconWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 高亮框控制接口 (供大厅调用)
	// ==========================================

	/**
	 * 设置高亮框的可见性
	 * @param bIsVisible true=SelfHitTestInvisible, false=Collapsed
	 */
	void SetHighlightFrameVisibility(bool bIsVisible);

	/**
	 * 初始化格子（由父页面创建格子后调用）
	 * @param InWeaponRowName 武器数据表行名
	 * @param InWeaponData 武器数据
	 * @param InParentPage 上级大厅页面（用于点击回调）
	 */
	void SetupWeaponItem(const FName& InWeaponRowName, const FWeaponInfo& InWeaponData, URoomInsidePage* InParentPage);

	/**
	 * 【新增】获取格子代表的武器 ID
	 * 用途: 父页面反查/排序
	 */
	FName GetWeaponRowName() const { return RepresentedWeaponRowName; }

protected:
	// ==========================================
	// 2. 生命周期
	// ==========================================

	/**
	 * 初始化: 绑定点击事件 + 隐藏高亮框
	 */
	virtual bool Initialize() override;

	// ==========================================
	// 3. 【核心控件绑定】: 与蓝图一一对应
	// meta = (BindWidget) 意味着蓝图里必须有同名控件, 否则编译或运行时报错
	// ==========================================

	/**
	 * 武器图标（虽然是 Button 控件, 但用其正常状态背景放图片）
	 * 点击后向父页面打小报告
	 */
	UPROPERTY(meta = (BindWidget))
	UButton* Btn_WeaponIcon;

	/** 高亮框图片（默认隐藏, 选中时显示） */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_HighlightBox;

private:
	// ==========================================
	// 4. 私有成员
	// ==========================================

	/**
	 * 逻辑变量: 这个格子代表的武器 ID（数据表的 RowName）
	 * 用途: 点击时回传给父页面
	 */
	FName RepresentedWeaponRowName;

	// ==========================================
	// 5. 内部回调
	// ==========================================

	/**
	 * 监听此格子按钮的点击事件
	 * 1. 校验 RowName 有效
	 * 2. 调用 ParentRoomPage->OnWeaponItemSelectedInGrid 通知父页面
	 * 3. 输出调试信息
	 */
	UFUNCTION()
	void OnWeaponIconClicked();

	/**
	 * 上级大厅页面指针（弱引用, 避免循环引用）
	 * 用途: 点击时打小报告
	 */
	UPROPERTY()
	URoomInsidePage* ParentRoomPage;
};
