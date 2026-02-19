/**
 * @file DailyLoginSaveModifier.h
 * @brief 每日登录存档动态修改器
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 * 
 * @details 专门用于运行时修改DailyLoginSaveGame中的动态数据
 *          支持修改玩家进度、领取状态等，并自动保存到.sav文件
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "UI/Activity/Data/DailyLoginSave.h"
#include "Kismet/GameplayStatics.h"
#include "DailyLoginSaveModifier.generated.h"

/**
 * @brief 每日登录存档修改记录
 * @details 记录单次修改操作的详细信息
 */
USTRUCT(BlueprintType)
struct FDailyLoginModificationRecord
{
	GENERATED_BODY()

public:
	/** 修改唯一标识符 */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	FGuid ModificationId;

	/** 活动ID */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	int32 ActivityID;

	/** 修改字段名称 */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	FString FieldName;

	/** 修改前的值 */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	FString OriginalValue;

	/** 修改后的值 */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	FString ModifiedValue;

	/** 修改时间 */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	FDateTime ModificationTime;

	/** 是否已保存 */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	bool bIsSaved;

	/**
	 * @brief 默认构造函数
	 */
	FDailyLoginModificationRecord()
		: ActivityID(0), ModificationTime(FDateTime::Now()), bIsSaved(false)
	{
		ModificationId = FGuid::NewGuid();
	}
};

/**
 * @brief 每日登录存档动态修改器
 * @details 提供运行时修改DailyLoginSaveGame数据的功能
 */
UCLASS()
class METALSLUG01_API UDailyLoginSaveModifier : public UObject
{
	GENERATED_BODY()

public:
	// ==================== 生命周期管理 ====================

	/**
	 * @brief 初始化修改器
	 * @param WorldContext 世界上下文
	 * @return 是否初始化成功
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyLogin Save Modifier")
	bool InitializeModifier(UObject* WorldContext);

	/**
	 * @brief 销毁修改器
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyLogin Save Modifier")
	void DestroyModifier();

	/**
	 * @brief 构造函数
	 */
	UDailyLoginSaveModifier();

	// ==================== 数据修改接口 ====================

	/**
	 * @brief 修改玩家进度
	 * @param ActivityID 活动ID
	 * @param NewProgress 新进度值
	 * @param bAutoSave 是否自动保存
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Modification")
	bool ModifyPlayerProgress(int32 ActivityID, int32 NewProgress, bool bAutoSave = true);

	/**
	 * @brief 修改已领取天数
	 * @param ActivityID 活动ID
	 * @param DayIndex 天数索引
	 * @param bClaimed 是否已领取
	 * @param bAutoSave 是否自动保存
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Modification")
	bool ModifyDayClaimedStatus(int32 ActivityID, int32 DayIndex, bool bClaimed, bool bAutoSave = true);

	/**
	 * @brief 批量修改已领取天数
	 * @param ActivityID 活动ID
	 * @param ClaimedDays 已领取的天数数组
	 * @param bAutoSave 是否自动保存
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Modification")
	bool ModifyClaimedDays(int32 ActivityID, const TArray<int32>& ClaimedDays, bool bAutoSave = true);

	/**
	 * @brief 修改当前领取次数
	 * @param ActivityID 活动ID
	 * @param NewCount 新领取次数
	 * @param bAutoSave 是否自动保存
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Modification")
	bool ModifyCurrentClaimCount(int32 ActivityID, int32 NewCount, bool bAutoSave = true);

	/**
	 * @brief 重置玩家记录
	 * @param ActivityID 活动ID
	 * @param bAutoSave 是否自动保存
	 * @return 是否重置成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Modification")
	bool ResetPlayerRecord(int32 ActivityID, bool bAutoSave = true);

	// ==================== 查询接口 ====================

	/**
	 * @brief 获取玩家当前进度
	 * @param ActivityID 活动ID
	 * @return 当前进度
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Query")
	int32 GetPlayerProgress(int32 ActivityID) const;

	/**
	 * @brief 获取已领取的天数列表
	 * @param ActivityID 活动ID
	 * @return 已领取天数数组
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Query")
	TArray<int32> GetClaimedDays(int32 ActivityID) const;

	/**
	 * @brief 检查某天是否已领取
	 * @param ActivityID 活动ID
	 * @param DayIndex 天数索引
	 * @return 是否已领取
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Query")
	bool IsDayClaimed(int32 ActivityID, int32 DayIndex) const;

	/**
	 * @brief 获取当前领取次数
	 * @param ActivityID 活动ID
	 * @return 当前领取次数
	 */
	UFUNCTION(BlueprintCallable, Category = "Data Query")
	int32 GetCurrentClaimCount(int32 ActivityID) const;

	// ==================== 历史记录接口 ====================

	/**
	 * @brief 获取修改历史记录
	 * @param MaxRecords 最大记录数
	 * @return 修改记录数组
	 */
	UFUNCTION(BlueprintCallable, Category = "History")
	TArray<FDailyLoginModificationRecord> GetModificationHistory(int32 MaxRecords = 50) const;

	/**
	 * @brief 清除修改历史记录
	 */
	UFUNCTION(BlueprintCallable, Category = "History")
	void ClearModificationHistory();

	// ==================== 保存接口 ====================

	/**
	 * @brief 保存指定活动的修改
	 * @param ActivityID 活动ID
	 * @return 是否保存成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Save Operations")
	bool SaveActivityRecord(int32 ActivityID);

	/**
	 * @brief 保存所有修改
	 * @return 是否保存成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Save Operations")
	bool SaveAllRecords();

	/**
	 * @brief 加载活动记录
	 * @param ActivityID 活动ID
	 * @return 是否加载成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Save Operations")
	bool LoadActivityRecord(int32 ActivityID);

private:
	// ==================== 内部数据 ====================

	/** 世界上下文对象 */
	UPROPERTY()
	TWeakObjectPtr<UObject> WorldContextObject;

	/** 缓存的存档实例 */
	UPROPERTY()
	UDailyLoginSaveGame* CachedSaveGame;

	/** 修改历史记录 */
	UPROPERTY()
	TArray<FDailyLoginModificationRecord> ModificationHistory;

	/** 是否已初始化 */
	bool bIsInitialized;

	// ==================== 内部方法 ====================

	/**
	 * @brief 获取或创建存档实例
	 * @param ActivityID 活动ID
	 * @return 存档实例
	 */
	UDailyLoginSaveGame* GetOrCreateSaveGame(int32 ActivityID);

	/**
	 * @brief 添加修改记录
	 * @param ActivityID 活动ID
	 * @param FieldName 字段名
	 * @param OriginalValue 原始值
	 * @param ModifiedValue 修改值
	 */
	void AddModificationRecord(int32 ActivityID, const FString& FieldName, const FString& OriginalValue, const FString& ModifiedValue);

	/**
	 * @brief 清理旧的历史记录
	 */
	void CleanupOldHistory();
};