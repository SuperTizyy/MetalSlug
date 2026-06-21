// ==========================================
// URoomStateService.h
// ==========================================
// 房间状态查询门面（View 唯一调用入口）
// 职责: 为 View 层屏蔽 RoomGameState/RoomPlayerState 的细节
// 架构: L2 Service Layer（CQRS - 读取端，与 URoomService 写入端互补）
// ==========================================

#pragma once

// UE 引擎核心最小化头文件
#include "CoreMinimal.h"
// 引入 UGameInstanceSubsystem 头文件
#include "Subsystems/GameInstanceSubsystem.h"
// 引入房间相关枚举（ERoomTeam/ERoomMatchMode 等）
#include "Data/Enums/RoomEnums.h"
// 自动生成的反射头文件
#include "RoomStateService.generated.h"

class ARoomGameState;
class ARoomPlayerState;

/**
 * @struct FPlayerSnapshot
 * @brief 玩家状态快照（供 UI 显示，View 不感知 PlayerState）
 *
 * 设计目的:
 * - View 只读 POJO 数据，避免暴露 ARoomPlayerState 引用造成 View 误改
 * - 解耦 PlayerState 的 UPROPERTY 字段（随时可能重构）
 */
USTRUCT(BlueprintType)
struct FPlayerSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    FString PlayerName;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    ERoomTeam Team = ERoomTeam::None;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    bool bIsReady = false;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    bool bIsHost = false;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 Score = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 Kills = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 Deaths = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 Assists = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    FString SelectedCharacterID;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    FString SelectedWeaponID1;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    FString SelectedWeaponID2;
};

/**
 * @struct FMatchSnapshot
 * @brief 比赛状态快照（供 UI 显示）
 */
USTRUCT(BlueprintType)
struct FMatchSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    ERoomMatchMode MatchMode = ERoomMatchMode::Melee;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 RemainingSeconds = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 CurrentRound = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 AttackerTotalKills = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 DefenderTotalKills = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 AttackerWins = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 DefenderWins = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    FString HostPlayerName;
};

/**
 * @class URoomStateService
 * @brief 房间状态查询门面（GameInstance 子系统）
 *
 * 【大厂标准架构：CQRS 读取端】
 * - URoomService: 写入端（业务编排/RPC 路由）
 * - URoomStateService: 读取端（聚合快照/隐藏 PlayerState/GameState 细节）
 *
 * 【为什么需要它】
 * - View（RoomInsidePage）当前直接 Cast PlayerState/GameState，违反"View 不感知数据层"
 * - 替换为：View 调 RoomStateService.GetMatchSnapshot() / GetPlayerSnapshots()
 * - 未来若引入 ViewModel 中转，本类就是 ViewModel 的数据源
 *
 * 【大厂对应】
 * - Riot: GameDataService
 * - Lyra: ULyraPlayerStateExtensions
 */
UCLASS()
class METALSLUG01_API URoomStateService : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /**
     * @brief 静态访问器
     */
    UFUNCTION(BlueprintCallable, Category = "RoomStateService", meta = (WorldContext = "WorldContextObject"))
    static URoomStateService* Get(const UObject* WorldContextObject);

    // ==========================================
    // 比赛级查询
    // ==========================================

    /**
     * @brief 获取当前比赛的整体快照
     * @return FMatchSnapshot 比赛数据（不在线/无 GameState 时返回默认值）
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    FMatchSnapshot GetMatchSnapshot() const;

    /**
     * @brief 当前是否处于 InRoom/Battleing 状态（即有 RoomGameState）
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    bool IsInRoom() const;

    /**
     * @brief 获取倒计时剩余秒数（封装 GameState::GetMatchRemainingSeconds）
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    int32 GetMatchRemainingSeconds() const;

    // ==========================================
    // 玩家级查询
    // ==========================================

    /**
     * @brief 获取攻方所有玩家的快照列表
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    TArray<FPlayerSnapshot> GetAttackTeamSnapshots() const;

    /**
     * @brief 获取守方所有玩家的快照列表
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    TArray<FPlayerSnapshot> GetDefenseTeamSnapshots() const;

    /**
     * @brief 获取本地玩家的快照（用于 UI 高亮自己的状态）
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    FPlayerSnapshot GetLocalPlayerSnapshot() const;

    /**
     * @brief 本地玩家是否已准备
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    bool IsLocalPlayerReady() const;

    /**
     * @brief 本地玩家是否房主
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    bool IsLocalPlayerHost() const;

    /**
     * @brief 本地玩家当前所属队伍
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    ERoomTeam GetLocalPlayerTeam() const;

    // ==========================================
    // 队伍统计查询
    // ==========================================

    /**
     * @brief 攻方已准备人数
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    int32 GetAttackReadyCount() const;

    /**
     * @brief 守方已准备人数
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    int32 GetDefenseReadyCount() const;

private:
    /**
     * @brief 内部辅助: 获取当前 World 的 RoomGameState
     */
    ARoomGameState* GetRoomGameState() const;

    /**
     * @brief 内部辅助: 获取当前本地玩家的 PlayerState
     */
    ARoomPlayerState* GetLocalPlayerState() const;

    /**
     * @brief 内部辅助: 把一个 ARoomPlayerState 转成 FPlayerSnapshot
     */
    static FPlayerSnapshot BuildSnapshot(ARoomPlayerState* PS, bool bIsHost);
};