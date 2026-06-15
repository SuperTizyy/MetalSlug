// ==========================================
// 物品信息表行 + 宝箱物品表行
// 关联: DT_ItemDetail / DT_TreasureBoxItem
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Data/Enums/ActivityEnums.h"
#include "ItemTableRow.generated.h"

/**
 * @struct FItemDetailRow
 * @brief 物品详情数据表行
 * 用途: 定义游戏中所有物品的基础属性和详细信息
 * 注意: 作为物品系统的中央配置表，支持各种类型物品的统一管理
 */
USTRUCT(BlueprintType)
struct FItemDetailRow : public FTableRowBase
{
	GENERATED_BODY()

public:
	// ==================== 基础标识 ====================
	
	/** 物品唯一 ID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Identity")
	int32 ItemID;

	/** 物品显示名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Identity")
	FText ItemName;

	/** 物品描述信息 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Identity")
	FText ItemDescription;

	// ==================== 视觉表现 ====================
	
	/** 物品图标资源 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Visual")
	TSoftObjectPtr<UTexture2D> ItemIcon;

	/** 物品功能数值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Visual")
	float ItemFunctionValue;

	// ==================== 快捷访问方法 ====================
	
	/** 获取物品 ID */
	FORCEINLINE int32 GetItemID() const { return ItemID; }

	/** 获取物品名称 */
	FORCEINLINE FText GetItemName() const { return ItemName; }

	/** 默认构造函数 */
	FItemDetailRow() 
		: ItemID(0)
		, ItemFunctionValue(0.0f)
	{};
};

/**
 * @struct FTreasureBoxItemRow
 * @brief 宝箱物品配置数据表行
 * 用途: 定义宝箱中包含的物品及其相关配置
 * 注意: 支持随机物品和固定物品的混合配置
 */
USTRUCT(BlueprintType)
struct FTreasureBoxItemRow : public FTableRowBase
{
	GENERATED_BODY()

public:
	// ==================== 基础索引 ====================
	
	/** 宝箱唯一 ID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TreasureBox|Identity")
	int32 BoxID;

	/** 物品唯一 ID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TreasureBox|Identity")
	int32 ItemID;

	// ==================== 视觉表现 ====================
	
	/** 宝箱图标资源 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TreasureBox|Visual")
	TSoftObjectPtr<UTexture2D> BoxIcon;

	// ==================== 随机配置 ====================
	
	/** 是否为随机物品 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TreasureBox|Random")
	bool bIsRandomItem;

	/** 物品数量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TreasureBox|Quantity")
	int32 ItemCount;

	/** 是否物品数量随机 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TreasureBox|Random")
	bool bIsItemCountRandom;

	/** 随机权重 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TreasureBox|Random")
	int32 RandomWeight;

	/** 默认构造函数 */
	FTreasureBoxItemRow() 
		: BoxID(0)
		, ItemID(0)
		, bIsRandomItem(false)
		, ItemCount(1)
		, bIsItemCountRandom(false)
		, RandomWeight(0)
	{};
};

/**
 * @struct FRedDotData
 * @brief 红点数据结构
 * 用途: 用于存储单个活动项的红点状态信息
 * 注意: 由 RedDotManager 使用，与活动系统紧耦合
 */
USTRUCT(BlueprintType)
struct FRedDotData
{
	GENERATED_BODY()

public:
	/** 红点显示类型 */
	UPROPERTY(BlueprintReadOnly, Category = "RedDot")
	ERedDotType DotType = ERedDotType::None;

	/** 红点显示数值（用于 NumberBadge 类型） */
	UPROPERTY(BlueprintReadOnly, Category = "RedDot")
	int32 DotValue = 0;

	/** 是否显示红点 */
	UPROPERTY(BlueprintReadOnly, Category = "RedDot")
	bool bShouldShow = false;

	/** 红点显示优先级 */
	UPROPERTY(BlueprintReadOnly, Category = "RedDot")
	int32 Priority = 0;

	/** 关联的活动标识符 */
	UPROPERTY(BlueprintReadOnly, Category = "RedDot")
	FName AssociatedActivityId;

	/** 默认构造函数 */
	FRedDotData() = default;

	/** 带参数构造函数 */
	FRedDotData(ERedDotType InType, int32 InValue, bool InShow, int32 InPriority, FName InActivityId)
		: DotType(InType), DotValue(InValue), bShouldShow(InShow), Priority(InPriority), AssociatedActivityId(InActivityId)
	{}
};
