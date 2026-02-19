/**
 * @file UniversalDataTableModifier.h
 * @brief 万能动态表修改器 - 支持运行时动态修改DataTable数据
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 * 
 * @details 提供统一的DataTable动态修改接口，支持：
 *          1. 内存中临时修改（会话内有效）
 *          2. 持久化修改（通过SaveGame系统）
 *          3. 事务性操作支持
 *          4. 修改历史记录和回滚功能
 *          5. 多表关联修改支持
 * 
 * @note 遵循UE工业级编码规范，提供完整的中文注释
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Engine/DataTable.h"
#include "Templates/SubclassOf.h"
#include "UniversalDataTableModifier.generated.h"

// ==================== 前置声明 ====================

class UDataTable;

// ==================== 数据结构定义 ====================

/**
 * @brief 表修改操作类型枚举
 * @details 定义支持的各种修改操作类型
 */
UENUM(BlueprintType)
enum class ETableModificationType : uint8
{
	AddRow      UMETA(DisplayName = "添加行"),
	UpdateRow   UMETA(DisplayName = "更新行"),
	DeleteRow   UMETA(DisplayName = "删除行"),
	BatchModify UMETA(DisplayName = "批量修改")
};

/**
 * @brief 修改操作状态枚举
 * @details 跟踪每个修改操作的执行状态
 */
UENUM(BlueprintType)
enum class EModificationStatus : uint8
{
	Pending     UMETA(DisplayName = "待执行"),
	Executing   UMETA(DisplayName = "执行中"),
	Completed   UMETA(DisplayName = "已完成"),
	Failed      UMETA(DisplayName = "执行失败"),
	RolledBack  UMETA(DisplayName = "已回滚")
};

/**
 * @brief 表修改记录结构
 * @details 存储单次修改操作的详细信息
 */
USTRUCT(BlueprintType)
struct FTableModificationRecord
{
	GENERATED_BODY()

public:
	/** 修改唯一标识符 */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	FGuid ModificationId;

	/** 修改类型 */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	ETableModificationType ModificationType;

	/** 目标表路径 */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	FString TargetTablePath;

	/** 目标行名称 */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	FName TargetRowName;

	/** 修改前的数据快照（JSON格式） */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	FString OriginalDataSnapshot;

	/** 修改后的数据快照（JSON格式） */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	FString ModifiedDataSnapshot;

	/** 修改时间戳 */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	FDateTime ModificationTime;

	/** 操作状态 */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	EModificationStatus Status;

	/** 错误信息（如有） */
	UPROPERTY(BlueprintReadOnly, Category = "Modification")
	FString ErrorMessage;

	/** 是否需要持久化 */
	UPROPERTY(BlueprintReadOnly, Category = "Persistence")
	bool bRequiresPersistence;

	/**
	 * @brief 默认构造函数
	 */
	FTableModificationRecord();

	/**
	 * @brief 构造函数
	 * @param InType 修改类型
	 * @param InTablePath 表路径
	 * @param InRowName 行名称
	 * @param InRequiresPersistence 是否需要持久化
	 */
	FTableModificationRecord(
		ETableModificationType InType,
		const FString& InTablePath,
		FName InRowName,
		bool InRequiresPersistence = false
	);
};

/**
 * @brief 动态表修改配置结构
 * @details 控制修改器的行为和策略
 */
USTRUCT(BlueprintType)
struct FDataTableModificationConfig
{
	GENERATED_BODY()

public:
	/** 是否启用内存修改模式 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
	bool bEnableMemoryModification;

	/** 是否启用持久化修改模式 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
	bool bEnablePersistentModification;

	/** 最大修改历史记录数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
	int32 MaxHistoryRecords;

	/** 是否自动保存修改 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
	bool bAutoSaveChanges;

	/** 自动保存间隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
	float AutoSaveInterval;

	/** 是否启用事务支持 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
	bool bEnableTransactionSupport;

	/** 事务超时时间（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
	float TransactionTimeout;

	/**
	 * @brief 构造函数 - 初始化默认配置
	 */
	FDataTableModificationConfig();
};

/**
 * @brief 动态表修改存档结构
 * @details 用于持久化存储修改过的表数据
 */
USTRUCT(BlueprintType)
struct FModifiedTableData
{
	GENERATED_BODY()

public:
	/** 表路径 */
	UPROPERTY()
	FString TablePath;

	/** 修改过的行数据集合（行名 -> JSON数据） */
	UPROPERTY()
	TMap<FName, FString> ModifiedRows;

	/** 删除的行名称集合 */
	UPROPERTY()
	TArray<FName> DeletedRows;

	/** 添加的新行数据集合（行名 -> JSON数据） */
	UPROPERTY()
	TMap<FName, FString> AddedRows;

	/**
	 * @brief 默认构造函数
	 */
	FModifiedTableData() = default;
};

/**
 * @brief 动态表修改存档类
 * @details 用于保存所有修改过的表数据
 */
UCLASS()
class METALSLUG01_API UDataTableModificationSave : public USaveGame
{
	GENERATED_BODY()

public:
	/** 修改过的所有表数据 */
	UPROPERTY()
	TMap<FString, FModifiedTableData> ModifiedTables;

	/** 修改历史记录 */
	UPROPERTY()
	TArray<FTableModificationRecord> ModificationHistory;

	/** 存档版本 */
	UPROPERTY()
	int32 SaveVersion;

	/**
	 * @brief 构造函数
	 */
	UDataTableModificationSave();
};

// ==================== 主要类定义 ====================

/**
 * @brief 万能动态表修改器 - 核心管理类
 * @details 提供统一的DataTable动态修改接口和服务
 * 
 * @note 设计特点：
 *       1. 支持内存和持久化双重修改模式
 *       2. 提供事务性操作支持
 *       3. 内置修改历史和回滚机制
 *       4. 支持多表关联修改
 *       5. 线程安全的操作队列
 */
UCLASS(BlueprintType, Blueprintable)
class METALSLUG01_API UUniversalDataTableModifier : public UObject
{
	GENERATED_BODY()

public:
	// ==================== 生命周期管理 ====================

	/**
	 * @brief 初始化修改器
	 * @param InConfig 修改配置
	 * @param WorldContext 世界上下文
	 * @return 是否初始化成功
	 */
	UFUNCTION(BlueprintCallable, Category = "DataTable Modifier")
	bool InitializeModifier(
		const FDataTableModificationConfig& InConfig,
		UObject* WorldContext
	);

	/**
	 * @brief 销毁修改器
	 * @note 清理资源，保存未提交的修改
	 */
	UFUNCTION(BlueprintCallable, Category = "DataTable Modifier")
	void DestroyModifier();

	// ==================== 表操作接口 ====================

	/**
	 * @brief 加载DataTable
	 * @param TablePath 表路径
	 * @return DataTable引用
	 * @note 支持延迟加载和缓存机制
	 */
	UFUNCTION(BlueprintCallable, Category = "DataTable Operations")
	UDataTable* LoadDataTable(const FString& TablePath);

	/**
	 * @brief 获取表行数据（支持修改后数据）
	 * @param TablePath 表路径
	 * @param RowName 行名称
	 * @return 行数据指针
	 */
	UFUNCTION(BlueprintCallable, Category = "DataTable Operations")
	uint8* GetModifiedRow(const FString& TablePath, FName RowName);

	/**
	 * @brief 获取表所有行（支持修改后数据）
	 * @param TablePath 表路径
	 * @param OutRows 输出行数据数组
	 * @return 是否成功
	 */
	UFUNCTION(BlueprintCallable, Category = "DataTable Operations")
	bool GetAllModifiedRows(const FString& TablePath, TArray<uint8*>& OutRows);

	// ==================== 修改操作接口 ====================

	/**
	 * @brief 开始事务
	 * @param TransactionName 事务名称
	 * @return 事务ID
	 */
	UFUNCTION(BlueprintCallable, Category = "Modification Operations")
	FGuid BeginTransaction(const FString& TransactionName);

	/**
	 * @brief 提交事务
	 * @param TransactionId 事务ID
	 * @return 是否提交成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Modification Operations")
	bool CommitTransaction(FGuid TransactionId);

	/**
	 * @brief 回滚事务
	 * @param TransactionId 事务ID
	 * @return 是否回滚成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Modification Operations")
	bool RollbackTransaction(FGuid TransactionId);

	/**
	 * @brief 添加表行
	 * @param TablePath 表路径
	 * @param RowName 行名称
	 * @param RowData 行数据
	 * @param bPersistent 是否持久化
	 * @return 修改记录ID
	 */
	UFUNCTION(BlueprintCallable, Category = "Modification Operations")
	FGuid AddTableRow(
		const FString& TablePath,
		FName RowName,
		const uint8* RowData,
		bool bPersistent = false
	);

	/**
	 * @brief 更新表行
	 * @param TablePath 表路径
	 * @param RowName 行名称
	 * @param UpdatedData 更新后的数据
	 * @param bPersistent 是否持久化
	 * @return 修改记录ID
	 */
	UFUNCTION(BlueprintCallable, Category = "Modification Operations")
	FGuid UpdateTableRow(
		const FString& TablePath,
		FName RowName,
		const uint8* UpdatedData,
		bool bPersistent = false
	);

	/**
	 * @brief 删除表行
	 * @param TablePath 表路径
	 * @param RowName 行名称
	 * @param bPersistent 是否持久化
	 * @return 修改记录ID
	 */
	UFUNCTION(BlueprintCallable, Category = "Modification Operations")
	FGuid DeleteTableRow(
		const FString& TablePath,
		FName RowName,
		bool bPersistent = false
	);

	/**
	 * @brief 批量修改表数据
	 * @param TablePath 表路径
	 * @param Modifications 修改操作数组
	 * @param bPersistent 是否持久化
	 * @return 事务ID
	 */
	UFUNCTION(BlueprintCallable, Category = "Modification Operations")
	FGuid BatchModifyTable(
		const FString& TablePath,
		const TArray<FTableModificationRecord>& Modifications,
		bool bPersistent = false
	);

	// ==================== 查询和管理接口 ====================

	/**
	 * @brief 获取修改历史
	 * @param MaxRecords 最大记录数
	 * @return 修改记录数组
	 */
	UFUNCTION(BlueprintCallable, Category = "Query Operations")
	TArray<FTableModificationRecord> GetModificationHistory(int32 MaxRecords = 100);

	/**
	 * @brief 回滚指定修改
	 * @param ModificationId 修改ID
	 * @return 是否回滚成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Query Operations")
	bool RevertModification(FGuid ModificationId);

	/**
	 * @brief 获取表的修改状态
	 * @param TablePath 表路径
	 * @return 是否被修改过
	 */
	UFUNCTION(BlueprintCallable, Category = "Query Operations")
	bool IsTableModified(const FString& TablePath);

	/**
	 * @brief 获取所有被修改的表
	 * @return 表路径数组
	 */
	UFUNCTION(BlueprintCallable, Category = "Query Operations")
	TArray<FString> GetModifiedTables();

	// ==================== 持久化接口 ====================

	/**
	 * @brief 保存所有修改
	 * @return 是否保存成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Persistence Operations")
	bool SaveAllModifications();

	/**
	 * @brief 加载已保存的修改
	 * @return 是否加载成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Persistence Operations")
	bool LoadSavedModifications();

	/**
	 * @brief 清除所有修改记录
	 * @param bIncludePersistent 是否包括持久化数据
	 * @return 是否清除成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Persistence Operations")
	bool ClearAllModifications(bool bIncludePersistent = false);

	// ==================== 工具方法 ====================

	/**
	 * @brief 将结构体序列化为JSON字符串
	 * @param StructPtr 结构体指针
	 * @param StructType 结构体类型
	 * @return JSON字符串
	 */
	static FString SerializeStructToJson(const uint8* StructPtr, UStruct* StructType);

	/**
	 * @brief 从JSON字符串反序列化结构体
	 * @param JsonString JSON字符串
	 * @param StructPtr 结构体指针
	 * @param StructType 结构体类型
	 * @return 是否反序列化成功
	 */
	static bool DeserializeStructFromJson(const FString& JsonString, uint8* StructPtr, UStruct* StructType);

	/**
	 * @brief 创建结构体实例
	 * @param StructType 结构体类型
	 * @return 结构体实例指针
	 */
	static uint8* CreateStructInstance(UStruct* StructType);

	/**
	 * @brief 销毁结构体实例
	 * @param StructPtr 结构体指针
	 * @param StructType 结构体类型
	 */
	static void DestroyStructInstance(uint8* StructPtr, UStruct* StructType);

protected:
	// ==================== 内部数据结构 ====================

	/** 修改配置 */
	UPROPERTY()
	FDataTableModificationConfig Config;

	/** 世界上下文 */
	UPROPERTY()
	TWeakObjectPtr<UObject> WorldContextObject;

	/** 加载的DataTable缓存 */
	UPROPERTY()
	TMap<FString, TObjectPtr<UDataTable>> LoadedTables;

	/** 内存中修改的数据缓存 */
	UPROPERTY()
	TMap<FString, TMap<FName, uint8*>> ModifiedTableData;

	/** 修改历史记录 */
	UPROPERTY()
	TArray<FTableModificationRecord> ModificationHistory;

	/** 活跃事务映射 */
	UPROPERTY()
	TMap<FGuid, TArray<FGuid>> ActiveTransactions;

	/** 修改器初始化状态 */
	UPROPERTY()
	bool bIsInitialized;

	/** 自动保存定时器句柄 */
	FTimerHandle AutoSaveTimerHandle;

	// ==================== 内部方法 ====================

	/**
	 * @brief 验证修改操作的有效性
	 * @param TablePath 表路径
	 * @param RowName 行名称
	 * @param ModificationType 修改类型
	 * @param OutError 错误信息输出
	 * @return 是否有效
	 */
	bool ValidateModification(
		const FString& TablePath,
		FName RowName,
		ETableModificationType ModificationType,
		FString& OutError
	);

	/**
	 * @brief 执行修改操作
	 * @param Record 修改记录
	 * @return 是否执行成功
	 */
	bool ExecuteModification(FTableModificationRecord& Record);

	/**
	 * @brief 回滚修改操作
	 * @param Record 修改记录
	 * @return 是否回滚成功
	 */
	bool RollbackModification(const FTableModificationRecord& Record);

	/**
	 * @brief 获取表的行结构类型
	 * @param Table DataTable引用
	 * @return 行结构类型
	 */
	UStruct* GetTableRowStruct(UDataTable* Table) const;

	/**
	 * @brief 自动保存定时器回调
	 */
	void OnAutoSaveTimer();

	/**
	 * @brief 清理过期的历史记录
	 */
	void CleanupOldHistory();

public:
	/**
	 * @brief 构造函数
	 */
	UUniversalDataTableModifier();

	/**
	 * @brief 析构函数
	 */
	virtual ~UUniversalDataTableModifier();
};