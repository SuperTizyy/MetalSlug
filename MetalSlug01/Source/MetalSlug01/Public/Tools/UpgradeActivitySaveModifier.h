/**
 * @file UpgradeActivitySaveModifier.h
 * @brief 升级活动存档动态修改器
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 * 
 * @details 专门用于运行时修改UpgradeActivitySubsystem中的动态数据
 *          支持修改经验值、奖励图标、任务进度等，并自动保存到.sav文件
 *          与Subsystem解耦，提供独立的调试和修改功能
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "UI/Activity/Data/DailyLoginSave.h"
#include "Kismet/GameplayStatics.h"
#include "UpgradeActivitySaveModifier.generated.h"

// ==================== 常量定义 ====================

/** 最大宝箱数量 */
constexpr int32 MAX_CHEST_COUNT = 10;

/** 最大任务数量 */
constexpr int32 MAX_TASK_COUNT = 10;

/**
 * @brief 升级活动存档动态修改器
 * @details 提供运行时修改UpgradeActivitySaveGame数据的功能
 *          与UpgradeActivitySubsystem解耦，专注于数据修改和持久化
 */


UCLASS()
class METALSLUG01_API UUpgradeActivitySaveModifier : public UObject
{
	GENERATED_BODY()

public:
	// ==================== 生命周期管理 ====================

	/**
	 * @brief 初始化修改器
	 * @param WorldContext 世界上下文
	 * @param Subsystem 目标Subsystem（可选，如果不提供则尝试自动获取）
	 * @return 是否初始化成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Upgrade Activity Save Modifier")
	bool InitializeModifier(UObject* WorldContext, class UUpgradeActivitySubsystem* Subsystem = nullptr);

	/**
	 * @brief 销毁修改器
	 */
	UFUNCTION(BlueprintCallable, Category = "Upgrade Activity Save Modifier")
	void DestroyModifier();

	/**
	 * @brief 构造函数
	 */
	UUpgradeActivitySaveModifier();

	// ==================== 数据修改接口 ====================

	/**
	 * @brief 修改当前经验值
	 * @param RecordDate 记录日期
	 * @param NewExp 新的经验值
	 * @param bAutoSave 是否自动保存
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Modification")
	bool ModifyCurrentExperience(int32 RecordDate, int32 NewExp, bool bAutoSave = true);

	/**
	 * @brief 修改奖励图标索引
	 * @param RecordDate 记录日期
	 * @param NewIndex 新的图标索引
	 * @param bAutoSave 是否自动保存
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Modification")
	bool ModifyRewardIconIndex(int32 RecordDate, int32 NewIndex, bool bAutoSave = true);

	/**
	 * @brief 修改宝箱领取状态
	 * @param RecordDate 记录日期
	 * @param ChestIndex 宝箱索引
	 * @param IsClaimed 是否已领取(0=未领取, 1=已领取)
	 * @param bAutoSave 是否自动保存
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Modification")
	bool ModifyChestClaimStatus(int32 RecordDate, int32 ChestIndex, int32 IsClaimed, bool bAutoSave = true);

	/**
	 * @brief 修改任务完成次数
	 * @param RecordDate 记录日期
	 * @param TaskIndex 任务索引
	 * @param Count 完成次数
	 * @param bAutoSave 是否自动保存
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Modification")
	bool ModifyTaskCompleteCount(int32 RecordDate, int32 TaskIndex, int32 Count, bool bAutoSave = true);

	/**
	 * @brief 修改任务领取状态
	 * @param RecordDate 记录日期
	 * @param TaskIndex 任务索引
	 * @param IsClaimed 是否已领取(0=未领取, 1=已领取)
	 * @param bAutoSave 是否自动保存
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Modification")
	bool ModifyTaskClaimStatus(int32 RecordDate, int32 TaskIndex, int32 IsClaimed, bool bAutoSave = true);

	/**
	 * @brief 修改限时活动完成次数
	 * @param RecordDate 记录日期
	 * @param Count 完成次数
	 * @param bAutoSave 是否自动保存
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Modification")
	bool ModifyLimitedActivityCount(int32 RecordDate, int32 Count, bool bAutoSave = true);

	/**
	 * @brief 重置指定日期的所有数据
	 * @param RecordDate 记录日期
	 * @param bAutoSave 是否自动保存
	 * @return 是否重置成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Modification")
	bool ResetRecordData(int32 RecordDate, bool bAutoSave = true);

	/**
	 * @brief 创建新日期记录
	 * @param RecordDate 记录日期
	 * @param bInheritPrevious 是否继承前一天数据
	 * @param bAutoSave 是否自动保存
	 * @return 是否创建成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Modification")
	bool CreateNewRecord(int32 RecordDate, bool bInheritPrevious = true, bool bAutoSave = true);

	// ==================== 查询接口 ====================

	/**
	 * @brief 获取当前经验值
	 * @param RecordDate 记录日期
	 * @return 当前经验值
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Query")
	int32 GetCurrentExperience(int32 RecordDate) const;

	/**
	 * @brief 获取奖励图标索引
	 * @param RecordDate 记录日期
	 * @return 奖励图标索引
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Query")
	int32 GetRewardIconIndex(int32 RecordDate) const;

	/**
	 * @brief 获取宝箱领取状态
	 * @param RecordDate 记录日期
	 * @param ChestIndex 宝箱索引
	 * @return 是否已领取
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Query")
	int32 GetChestClaimStatus(int32 RecordDate, int32 ChestIndex) const;

	/**
	 * @brief 获取任务完成次数
	 * @param RecordDate 记录日期
	 * @param TaskIndex 任务索引
	 * @return 完成次数
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Query")
	int32 GetTaskCompleteCount(int32 RecordDate, int32 TaskIndex) const;

	/**
	 * @brief 获取任务领取状态
	 * @param RecordDate 记录日期
	 * @param TaskIndex 任务索引
	 * @return 是否已领取
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Query")
	int32 GetTaskClaimStatus(int32 RecordDate, int32 TaskIndex) const;

	/**
	 * @brief 获取限时活动完成次数
	 * @param RecordDate 记录日期
	 * @return 完成次数
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Query")
	int32 GetLimitedActivityCount(int32 RecordDate) const;

	// ==================== 保存接口 ====================

	/**
	 * @brief 保存指定日期的修改
	 * @param RecordDate 记录日期
	 * @return 是否保存成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Save Operations")
	bool SaveRecord(int32 RecordDate);

	/**
	 * @brief 保存所有修改
	 * @return 是否保存成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Save Operations")
	bool SaveAllRecords();

	/**
	 * @brief 加载记录
	 * @param RecordDate 记录日期
	 * @return 是否加载成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Save Operations")
	bool LoadRecord(int32 RecordDate);

private:
	// ==================== 内部数据 ====================
	
	/** 世界上下文对象 */
	UPROPERTY()
	TWeakObjectPtr<UObject> WorldContextObject;
	
	// ==================== 调试辅助函数 ====================
		
	/**
	 * @brief 强制刷新所有页面，重新获取内存数据
	 * @note 用于调试目的，强制所有 UI 组件重新获取最新内存数据
	 */
	void ForceRefreshAllPages();

	/**
	 * @brief 验证初始化状态并记录日志
	 * @param FunctionName 调用函数名
	 * @return 是否验证通过
	 */
	bool ValidateAndLog(const TCHAR* FunctionName) const;

	/**
	 * @brief 验证数组索引是否有效
	 * @param Index 索引值
	 * @param Array 目标数组
	 * @param ArrayName 数组名称（用于日志）
	 * @return 是否有效
	 */
	template<typename T>
	bool ValidateArrayIndex(int32 Index, const TArray<T>& Array, const TCHAR* ArrayName) const
	{
		if (Index < 0 || !Array.IsValidIndex(Index))
		{
			UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: %s索引超出范围: %d"), ArrayName, Index);
			return false;
		}
		return true;
	}

	/**
	 * @brief 验证二进制状态值（0或1）
	 * @param Value 状态值
	 * @param ValueName 值名称（用于日志）
	 * @return 是否有效
	 */
	bool ValidateBinaryState(int32 Value, const TCHAR* ValueName) const;

	/**
	 * @brief 记录修改日志
	 * @param FieldName 字段名称
	 * @param ChangeDesc 变更描述
	 */
	void LogModification(const TCHAR* FieldName, const FString& ChangeDesc) const;
		
	/**
	 * @brief 初始化新记录（不继承）
	 * @param Record 要初始化的记录引用
	 * @param RecordDate 记录日期
	 */
	void InitializeNewRecord(FUpgradeRewardSaveRecord& Record, int32 RecordDate);

	/** 缓存的存档实例 */
	UPROPERTY()
	UDailyLoginSaveGame* CachedSaveGame;

	/** 目标Subsystem引用 */
	UPROPERTY()
	class UUpgradeActivitySubsystem* TargetSubsystem;



	/** 是否已初始化 */
	bool bIsInitialized;
	
	/** 是否有待处理的更改（需要在游戏关闭时保存） */
	bool bHasPendingChanges;

	// ==================== 内部方法 ====================
	
	/**
	 * @brief 获取或创建存档实例
	 * @param RecordDate 记录日期
	 * @return 存档实例
	 */
	UDailyLoginSaveGame* GetOrCreateSaveGame(int32 RecordDate);

	/**
	 * @brief 获取记录指针（查询接口内部使用）
	 * @param RecordDate 记录日期
	 * @return 记录指针，未找到返回nullptr
	 */
	const FUpgradeRewardSaveRecord* GetRecordOrNull(int32 RecordDate) const;
	
	/**
	 * @brief 在游戏关闭时保存所有待处理的更改
	 * @note 这是唯一的实际磁盘保存操作
	 */
	void SavePendingChangesOnShutdown();

public:
	/**
	 * @brief 注册控制台命令
	 */
	void RegisterConsoleCommands();

	/**
	 * @brief 注销控制台命令
	 */
	void UnregisterConsoleCommands();

	/**
	 * @brief 显示每日升级奖励页面（用于调试）
	 */
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void ShowDailyUpgradePage();
	
	/**
	 * @brief 游戏退出时自动保存
	 * @details 在游戏关闭时调用，将所有内存修改保存到磁盘
	 */
	void AutoSaveOnGameExit();
};
