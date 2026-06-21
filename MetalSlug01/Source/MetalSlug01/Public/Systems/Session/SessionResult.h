// MetalSlug01. All Rights Reserved.
#pragma once

// ==========================================
// 标准库 & UE 引擎头文件
// ==========================================
#include "CoreMinimal.h"
#include "SessionResult.generated.h"

/**
 * @struct FRoomSessionResult
 * @brief 结构化房间数据，用于解耦 UI 与 OnlineSubsystem
 *
 * @section 设计目的
 * - 将 FOnlineSessionSearchResult 解析后的数据封装为此结构体
 * - Widget/Presenter 只操作此结构体，无需了解 OnlineSubsystem 细节
 * - 便于后续扩展（如 JSON 序列化、网络传输）
 *
 * @section 字段说明
 * - RoomName：房间显示名称
 * - HostAccount：房主账号名
 * - GameMode：游戏模式（刀战/生化）
 * - MapName：地图显示名
 * - LevelName：地图资源名（用于 OpenLevel）
 * - CurrentPlayers：当前人数（含 AI）
 * - MaxPlayers：最大人数
 * - bIsInBattle：是否在战斗中
 * - Signature：签名字符串（用于防闪烁比对）
 * - SearchResult：原始搜索结果（仅在加入房间时使用）
 */
USTRUCT(BlueprintType)
struct METALSLUG01_API FRoomSessionResult
{
	GENERATED_BODY()

public:
	// ==================== 基础信息 ====================

	/** 房间显示名称 */
	UPROPERTY(BlueprintReadOnly, Category = "RoomInfo")
	FString RoomName;

	/** 房主账号名 */
	UPROPERTY(BlueprintReadOnly, Category = "RoomInfo")
	FString HostAccount;

	/** 游戏模式（刀战模式/生化模式） */
	UPROPERTY(BlueprintReadOnly, Category = "RoomInfo")
	FString GameMode;

	/** 地图显示名 */
	UPROPERTY(BlueprintReadOnly, Category = "RoomInfo")
	FString MapName;

	/** 地图资源名（用于 OpenLevel） */
	UPROPERTY(BlueprintReadOnly, Category = "RoomInfo")
	FName LevelName;

	// ==================== 人数信息 ====================

	/** 当前人数（含 AI） */
	UPROPERTY(BlueprintReadOnly, Category = "RoomInfo")
	int32 CurrentPlayers = 0;

	/** 最大人数 */
	UPROPERTY(BlueprintReadOnly, Category = "RoomInfo")
	int32 MaxPlayers = 10;

	// ==================== 状态信息 ====================

	/** 是否在战斗中（战斗中禁止加入） */
	UPROPERTY(BlueprintReadOnly, Category = "RoomInfo")
	bool bIsInBattle = false;

	/** 签名字符串（房间名_人数_最大人数_是否在战斗） */
	UPROPERTY(BlueprintReadOnly, Category = "RoomInfo")
	FString Signature;

	// ==================== 内部数据（不暴露给 Blueprint） ====================

	/** 原始搜索结果（仅用于 JoinSession，无需解析） */
	TSharedPtr<FOnlineSessionSearchResult> RawSearchResult;

	// ==================== 构造函数 ====================

	FRoomSessionResult() = default;

	/** 从原始搜索结果解析构造 */
	explicit FRoomSessionResult(const FOnlineSessionSearchResult& InResult);

	/** 生成签名字符串 */
	FString GenerateSignature() const;

	/** 检查是否有效 */
	bool IsValid() const;
};

/**
 * @struct FRoomCreationParams
 * @brief 创房参数结构体
 */
USTRUCT(BlueprintType)
struct METALSLUG01_API FRoomCreationParams
{
	GENERATED_BODY()

public:
	/** 房间名称 */
	UPROPERTY(BlueprintReadWrite, Category = "RoomParams")
	FString RoomName;

	/** 房间密码（暂未实现） */
	UPROPERTY(BlueprintReadWrite, Category = "RoomParams")
	FString Password;

	/** 游戏模式 */
	UPROPERTY(BlueprintReadWrite, Category = "RoomParams")
	FString GameMode;

	/** 地图显示名 */
	UPROPERTY(BlueprintReadWrite, Category = "RoomParams")
	FString MapName;

	/** 地图资源名 */
	UPROPERTY(BlueprintReadWrite, Category = "RoomParams")
	FName LevelName;

	/** 最大人数 */
	UPROPERTY(BlueprintReadWrite, Category = "RoomParams")
	int32 MaxPlayers = 10;

	/** 房主账号 */
	UPROPERTY(BlueprintReadWrite, Category = "RoomParams")
	FString HostAccount;
};
