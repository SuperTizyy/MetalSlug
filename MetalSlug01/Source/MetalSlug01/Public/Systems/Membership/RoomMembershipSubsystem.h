// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// ==========================================
// URoomMembershipSubsystem — 房间成员管理子系统
//
// 【2026.07.11 v31 大厂架构重构】从 RoomGameMode 拆出
//
// 设计原则:
//   - 单一职责: 玩家/AI 成员管理 (入队/换队/踢人/房主/准备状态)
//   - 不持 Pawn 生成数据: 归 URoomSpawnSubsystem
//   - 不持仇恨账本: 归 URoomTargetingSubsystem
//   - 不持 SpawnInProgress 重入标志: 归 URoomSpawnSubsystem (真理源单一)
//
// 职责清单:
//   - AddPlayerToRoom / RemovePlayerFromRoom: 玩家入队/退队
//   - ChangePlayerTeam: 玩家换队 (Offense ↔ Defense)
//   - TransferHostTo: 房主转交
//   - UpdatePlayerReadyState / CheckAllPlayersReady: 准备状态
//   - BroadcastChatMessage / BroadcastSystemMessage: 消息广播
//
// 大厂原则 - 职责分层:
//   - 业务操作 (RPC 入口) 留在 RoomGameMode (因为要调 Server RPC)
//   - 业务实现下沉到本 Subsystem (纯逻辑, 不依赖 RPC)
//
// 访问入口:
//   URoomMembershipSubsystem* MemberSys = URoomMembershipSubsystem::Get(this);
// ==========================================

// UE 引擎基础
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"

// Room 共享枚举 (ERoomState / ERoomMatchMode)
#include "Data/Enums/RoomEnums.h"

// 自动生成的反射头 — 必须放在所有 #include 之后, forward declaration 之前
// 大厂注意 (UE 5.6 UHT 严格模式): .generated.h 必须用裸文件名, 不能带目录前缀
//   UHT Parser 中: includeNameString 跟 GeneratedHeaderFileName 做字面 OrdinalIgnoreCase 比对
//   "Systems/Membership/RoomMembershipSubsystem.generated.h" 永远 != "RoomMembershipSubsystem.generated.h"
#include "RoomMembershipSubsystem.generated.h"

// ==========================================
// 前向声明 — 避免在本头中 include 完整定义
// ==========================================
class AController;
class APlayerController;
class ARoomPlayerController;
class ARoomPlayerState;
class ARoomGameState;

UCLASS()
class METALSLUG01_API URoomMembershipSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ==========================================
	// 生命周期
	// ==========================================

	static URoomMembershipSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// ==========================================
	// 玩家管理
	// ==========================================

	/**
	 * @brief 处理新玩家加入房间
	 *
	 * v31 大厂架构 - 显式补回 PS:
	 *   - 测试模式 (URoomService::EnterSkipToHostMode) 绕开了 PostLogin
	 *   - 旧实现会因 PlayerState=null 而静默跳过
	 *   - 新实现: PS 缺失 → 显式 SpawnActor<ARoomPlayerState> + PC->SetPlayerState
	 *
	 * @param RequestingController 玩家控制器
	 * @param PlayerName 玩家展示名
	 * @param PlayerStateClass 玩家状态类 (从 ARoomGameMode::PlayerStateClass 传入, 类型为 TSubclassOf<APlayerState>)
	 *
	 * 大厂原则 (v31.5 零兜底):
	 *   - 接口签名用 TSubclassOf<APlayerState>, 与父类 AGameModeBase::PlayerStateClass 完全匹配
	 *   - 内部运行时校验: 必须 IsChildOf(ARoomPlayerState::StaticClass()), 否则 Log Error + 拒绝
	 *   - 这样调用方不用关心类型推导, GameMode::PlayerStateClass 直接透传即可
	 */
	void AddPlayerToRoom(AController* RequestingController, const FString& PlayerName,
		TSubclassOf<APlayerState> PlayerStateClass);

	/**
	 * @brief 处理玩家离开房间
	 */
	void RemovePlayerFromRoom(AController* RequestingController);

	/**
	 * @brief 玩家主动换队
	 * @param bToAttackTeam true=攻方 (Faction.Offense), false=守方 (Faction.Defense)
	 */
	void ChangePlayerTeam(AController* RequestingController, bool bToAttackTeam);

	/**
	 * @brief 更新玩家准备状态
	 */
	void UpdatePlayerReadyState(AController* RequestingController, bool bIsReady);

	/**
	 * @brief 检查所有玩家是否都已准备
	 */
	bool CheckAllPlayersReady();

	// ==========================================
	// 房主管理
	// ==========================================

	/**
	 * @brief 服务端主动转交房主权限
	 * @param NewHostPlayerName 新房主名 (空 = 随机选下一个)
	 * @return 是否成功
	 */
	bool TransferHostTo(const FString& NewHostPlayerName);

	// ==========================================
	// 消息广播
	// ==========================================

	void BroadcastChatMessage(const FString& SenderName, const FString& Message);
	void BroadcastSystemMessage(const FString& Message);
};