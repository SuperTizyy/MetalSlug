// MetalSlug01. All Rights Reserved.
#pragma once

/**
 * @file SessionResult.h
 * @brief 会话结果结构体 + 房间模式转换工具类
 *
 * 包含内容:
 *   - FRoomSessionResult: 解耦 UI 与 OnlineSubsystem 的结构化房间数据
 *   - FRoomCreationParams: 创房参数结构
 *   - URoomMatchModeUtils: 字符串 ↔ ERoomMatchMode 的单一真理源
 *
 * 大厂原则 — 单一真理源:
 *   - SessionSettings GAME_MODE 字段只能存字符串 (广告层)
 *   - GameState CurrentMatchMode 必须用枚举 (运行时决策层)
 *   - 转换入口 = URoomMatchModeUtils, 不允许散落硬编码
 */

// ==========================================
// 标准库 & UE 引擎头文件
// ==========================================
#include "CoreMinimal.h"
// ERoomMatchMode 枚举(用于 ParseMatchModeFromGameModeString 签名)
#include "Data/Enums/RoomEnums.h"
// UBlueprintFunctionLibrary 基类(UE 静态工具类标准模式)
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SessionResult.generated.h"

/**
 * @class URoomMatchModeUtils
 * @brief 房间比赛模式转换工具 — 字符串 ↔ ERoomMatchMode 的单一真理源
 *
 * 大厂原则 (Single Source of Truth):
 *   - SessionSettings GAME_MODE 字段(广告层)只能存字符串 ("刀战模式" / "生化模式")
 *   - GameState CurrentMatchMode(运行时决策层)必须用 ERoomMatchMode 枚举
 *   - 这两个表示层之间必须有一个**唯一**的转换入口 — 即本工具
 *   - 任何业务方(创房 / 加入 / GameState写入)都必须走本工具,不允许散落硬编码
 *
 * 调用方:
 *   - LANRoomPage::OnCreateSessionComplete 创房前: PendingGameMode 字符串 → ERoomMatchMode
 *   - GameFlowSubsystem::HandleStateEntry(InRoom): 把 TargetRoomMode 写入 GameState
 *   - RoomService::RequestAddAI: 读 GameState->CurrentMatchMode(不再硬编码 Melee)
 *
 * 【零兜底原则】:
 *   - 输入字符串为空 / 未识别 → 返回 ERoomMatchMode::None + Log Error
 *   - 调用方拿到 None → 必须显式拒绝(不允许静默 fallback 到默认)
 *
 * 设计: 继承自 UBlueprintFunctionLibrary (UE 静态工具类标准模式)
 *   - 不需要实例化, 静态方法直接通过 K2 Node 调用
 *   - 自动暴露到 BP 蓝图节点列表
 */
UCLASS()
class METALSLUG01_API URoomMatchModeUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 把 SessionSettings GAME_MODE 字符串(中文 DisplayName)解析为 ERoomMatchMode
	 * @param GameModeString "刀战模式" / "生化模式" / 其他未识别字符串
	 * @return ERoomMatchMode::Melee/Zombie 或 ERoomMatchMode::None(失败, 已 Log Error)
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|Room", meta = (DisplayName = "Parse Match Mode From Game Mode String"))
	static ERoomMatchMode ParseMatchModeFromGameModeString(const FString& GameModeString);

	/**
	 * ERoomMatchMode → 中文 DisplayName(用于 SessionSettings 写回)
	 * @param Mode 模式枚举
	 * @return "刀战模式" / "生化模式" / 空字符串(失败)
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|Room", meta = (DisplayName = "Get Game Mode String From Match Mode"))
	static FString GetGameModeStringFromMatchMode(ERoomMatchMode Mode);
};

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
