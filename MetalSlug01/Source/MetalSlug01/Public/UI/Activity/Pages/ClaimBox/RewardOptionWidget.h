// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Tables/DailyLoginTableRow.h"
#include "RewardOptionWidget.generated.h"


/**
 * @delegate FOnStoreToBag
 * @brief 玩家点击"放入背包"时广播
 * @param DayIndex 天数索引
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStoreToBag, int32, DayIndex);

/**
 * @delegate FOnOpenNow
 * @brief 玩家点击"立即打开"时广播
 * @param DayIndex 天数索引
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOpenNow, int32, DayIndex);


/**
 * @class URewardOptionWidget
 * @brief 奖励选项 Widget
 *
 * 职责说明:
 * - 在每日登录/升级奖励中, 玩家领取后弹出选项
 * - "放入背包" 或 "立即打开" 二选一
 * - 通知上层页面做出不同处理
 *
 * 架构理念:
 * 1. 双委托: FOnStoreToBag / FOnOpenNow
 * 2. 防重复调用: bStoreClickedHandled / bOpenClickedHandled
 * 3. 强制刷新: Store 后广播 UUpgradeActivitySubsystem->OnGlobalRefresh
 * 4. 防御性: 蓝图按钮不存在时 Log Error
 */
UCLASS()
class METALSLUG01_API URewardOptionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公共接口
	// ==========================================

	/**
	 * 初始化选项
	 * 修改点: 去掉指针 *, 改用 const TArray<FDailyLoginConfigRow>&
	 * 原因: UHT 不允许在 UFUNCTION 参数中暴露原始结构体指针数组
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyLogin")
	void InitSelection(const TArray<FDailyLoginConfigRow>& Options);

	/**
	 * "放入背包"事件（供外部订阅）
	 */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnStoreToBag OnStoreToBag;

	/**
	 * "立即打开"事件（供外部订阅）
	 */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnOpenNow OnOpenNow;

protected:
	// ==========================================
	// 2. 生命周期
	// ==========================================

	/**
	 * Native 构造
	 * 1. 初始化防重复标志
	 * 2. 检查 StoreBtn / OpenBtn 是否存在
	 * 3. 清理旧绑定, 重新绑定
	 */
	virtual void NativeConstruct() override;

	/**
	 * Native 析构
	 */
	virtual void NativeDestruct() override;

	// ==========================================
	// 3. UI 组件
	// ==========================================

	/** "放入背包"按钮 */
	UPROPERTY(meta = (BindWidget))
	class UButton* StoreBtn;

	/** "立即打开"按钮 */
	UPROPERTY(meta = (BindWidget))
	class UButton* OpenBtn;

	// ==========================================
	// 4. 内部回调
	// ==========================================

	/**
	 * Store 按钮点击
	 * 1. 防重复
	 * 2. 广播 OnStoreToBag
	 * 3. 强制全局刷新
	 * 4. RemoveFromParent
	 */
	UFUNCTION()
	void HandleStoreClicked();

	/**
	 * Open 按钮点击
	 * 1. 防重复
	 * 2. 广播 OnOpenNow
	 * 3. RemoveFromParent
	 */
	UFUNCTION()
	void HandleOpenClicked();

private:
	// ==========================================
	// 5. 私有成员
	// ==========================================

	/**
	 * 当前处理的天数
	 * 用途: 回调时回传给上层
	 */
	UPROPERTY()
	int32 CurrentDayIndex = 0;

	/**
	 * 防重复调用标志（Store）
	 * 注意: Transient 表示不参与序列化
	 */
	UPROPERTY(Transient)
	bool bStoreClickedHandled = false;

	/** 防重复调用标志（Open） */
	UPROPERTY(Transient)
	bool bOpenClickedHandled = false;
};
