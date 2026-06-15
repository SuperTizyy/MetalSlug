// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Data/Enums/CombatEnums.h" // 引入 EKillMethod 枚举
#include "KillFeedWidget.generated.h"

// 前向声明
class UVerticalBox;
class UTextBlock;
class UKillFeedEntryWidget;


/**
 * @class UKillFeedWidget
 * @brief 击杀信息组件
 *
 * 职责说明:
 * - 显示击杀消息列表（最多 MaxKillMessages = 10 条）
 * - 支持多种击杀方式的图标和文本显示
 * - 自动移除 5 秒前的老消息
 * - 支持系统消息
 *
 * 架构理念:
 * 1. 列表管理: 维护动态生成的 UKillFeedEntryWidget 列表
 * 2. 消息寿命: 每条消息绑定 5 秒寿命，到期自动清除
 * 3. 数据驱动: 图标通过 DT_KillIconInfo 查找
 * 4. 滚动容器: 使用 UVerticalBox 自动布局
 */
UCLASS()
class METALSLUG01_API UKillFeedWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公共接口（供外部调用）
	// ==========================================

	/**
	 * 添加击杀信息（使用击杀方式枚举）
	 * @param KillerName 击杀者名
	 * @param VictimName 被击杀者名
	 * @param KillMethod 击杀方式
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void AddKillInfo(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod);

	/**
	 * 添加击杀信息（带武器类型和爆头标识）
	 * @param KillMethod 基础击杀方式
	 * @param bIsHeadshot 是否爆头
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void AddKillInfoEx(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod, bool bIsHeadshot);

	/**
	 * 添加击杀信息（保留旧的 bool 版本以兼容）
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void AddKillInfoWithHeadshot(const FString& KillerName, const FString& VictimName, bool bIsHeadshot);

	/**
	 * 添加系统消息（如回合开始、炸弹放置等）
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void AddSystemMessage(const FString& Message);

	/**
	 * 清空所有击杀信息
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void ClearKillFeed();

	/**
	 * 设置击杀图标数据表（可选，用于数据驱动）
	 * 用途: 由 GameHUDWidget 注入
	 */
	UFUNCTION(BlueprintCallable, Category = "KillFeed")
	void SetKillIconDataTable(class UDataTable* InDataTable);

protected:
	// ==========================================
	// 2. 生命周期
	// ==========================================

	/**
	 * 初始化
	 */
	virtual bool Initialize() override;

private:
	// ==========================================
	// 3. 私有成员变量
	// ==========================================

	/**
	 * 最大显示的击杀消息数量
	 * 超过则自动移除最老的
	 */
	static constexpr int32 MaxKillMessages = 10;

	/**
	 * 消息自动消失时间（秒）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KillFeed", meta = (AllowPrivateAccess = "true"))
	float MessageLifetime = 5.0f;

	/**
	 * 击杀信息容器
	 * 用途: 动态 AddChild UKillFeedEntryWidget
	 */
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* VB_KillFeed;

	/**
	 * 击杀信息条目类引用
	 * 用途: 动态创建子控件
	 * 默认在蓝图里配置 WBP_KillFeedEntryWidget
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KillFeed", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UKillFeedEntryWidget> KillFeedEntryWidgetClass;

	/**
	 * 击杀图标数据表引用
	 * 关联: DT_KillIconInfo
	 */
	UPROPERTY()
	class UDataTable* KillIconDataTable;

	// ==========================================
	// 4. 私有方法
	// ==========================================

	/**
	 * 创建单条击杀消息（使用新的子控件）
	 * @return 创建的 UWidget 指针
	 */
	UWidget* CreateKillMessage(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod);

	/**
	 * 创建单条击杀消息（旧版本，使用 bool 判断爆头）
	 */
	UWidget* CreateKillMessageOld(const FString& KillerName, const FString& VictimName, bool bIsHeadshot);

	/**
	 * 创建单条系统消息
	 */
	UWidget* CreateSystemMessage(const FString& Message);

	/**
	 * 移除过期消息（定时调用）
	 * 用途: 5 秒寿命到期自动清除
	 */
	UFUNCTION()
	void RemoveExpiredMessages();

	/**
	 * 根据击杀方式判断是否为爆头
	 */
	bool IsHeadshotFromKillMethod(EKillMethod KillMethod) const;

	/**
	 * 根据击杀方式和爆头状态获取最终的击杀方法
	 * 规则: 如果爆头则选择 Headshot 版本
	 */
	EKillMethod GetFinalKillMethod(EKillMethod BaseMethod, bool bIsHeadshot);
};
