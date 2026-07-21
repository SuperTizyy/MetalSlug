// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 APlayerState 类（基类）
#include "GameFramework/PlayerState.h"

// 引入房间相关枚举（ERoomState/ERoomMatchMode — ERoomTeam 已于 2026.07.10 删除）
#include "Data/Enums/RoomEnums.h"
#include "GameplayTagContainer.h" // 【2026.07.10 P0 重构】FGameplayTag 阵营

// UE 自动生成的头文件
#include "RoomPlayerState.generated.h"


// ==========================================
// 1. 委托声明
// ==========================================

/**
 * 计分板数据变化时的动态多播委托
 * 用于 UI 监听计分板数据刷新
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnScoreboardDataChanged);

/**
 * 房间玩家状态变化时的动态多播委托
 * 用于 UI 监听队伍/准备状态变化
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomPlayerStateChanged);


/**
 * @class ARoomPlayerState
 * @brief 房间玩家状态类
 *
 * 职责说明:
 * - 在 Server 和 Client 之间自动同步单个玩家的队伍、准备状态以及计分板数据
 * - 暴露"已选角色+武器"信息给服务器 GameMode 使用
 * - 提供计分板（击杀/死亡/助攻/得分）的同步和广播
 *
 * 架构理念:
 * 1. 网络同步: ReplicatedUsing 机制自动同步到所有客户端
 * 2. UI 解耦: 通过委托广播，UI 层只需监听事件即可刷新
 * 3. 服务器权威: 所有数据修改均在 HasAuthority() 校验下执行
 */
UCLASS()
class METALSLUG01_API ARoomPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	/**
	 * 构造函数
	 * 目的: 初始化默认值、开启网络同步
	 */
	ARoomPlayerState();

	// ==========================================
	// 角色/武器选择 Getter（供服务器 GameMode 使用）
	// ==========================================

	/**
	 * 获取当前选中的角色 ID
	 */
	FString GetSelectedCharacterID() const { return SelectedCharacterID; }

	/**
	 * 获取当前选中的 1 号位武器 ID
	 */
	FString GetSelectedWeapon1ID() const { return SelectedWeaponID1; }

	/**
	 * 获取当前选中的 2 号位武器 ID
	 */
	FString GetSelectedWeapon2ID() const { return SelectedWeaponID2; }

	/**
	 * 【2026.07.18 P0】获取当前选中的 3 号位武器 ID (近战武器)
	 *
	 * v52 槽位扩展: 大厅 3 把武器 (主+副+近战) 对应 SelectedWeaponID1/2/3
	 * 战斗 Spawn 时统一读出 3 把应用到玩家
	 */
	FString GetSelectedWeapon3ID() const { return SelectedWeaponID3; }

	// ==========================================
	// 角色/武器选择 Setter
	// ==========================================

	/**
	 * 一次性设置角色+3 把武器（Controller 专用）
	 * 改为本地 Setter（去掉此处的 Server RPC，统一由 Controller 转发）
	 *
	 * 【v52 P0 扩展】参数从 2 把改为 3 把 (主/副/近战)
	 *
	 * @param InCharID     角色 ID
	 * @param InPrimaryID  主武器 ID (Slot 1)
	 * @param InSecondaryID 副武器 ID (Slot 2)
	 * @param InMeleeID    近战武器 ID (Slot 3)
	 */
	void SetPlayerLoadout(const FString& InCharID, const FString& InPrimaryID, const FString& InSecondaryID, const FString& InMeleeID);

	// ==========================================
	// 网络同步注册
	// ==========================================

	/**
	 * 重写此函数，注册需要网络同步的变量
	 * 【核心规范】: 必须重写
	 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==========================================
	// 2. 核心同步数据 (Replicated Data)
	// ==========================================

	/**
	 * 【2026.07.10 P0 重构】玩家当前所属阵营 (FGameplayTag)
	 *
	 * 设计 (大厂原则 - 单一真理源):
	 *   - 取代 ERoomTeam (None/Attack/Defense) — 全部阵营表达统一用 FGameplayTag
	 *   - 有效值: Faction.Offense (攻方) / Faction.Defense (守方)
	 *   - 由 ARoomGameMode::ModeRulesByMode 决定每种模式哪个 Tag 属于攻/守方
	 *   - 初始值: Faction.Defense (玩家默认守方, 平衡设计; 模式启动后由 GameMode 重写)
	 *
	 * 迁移 (旧 BP 引用 ERoomTeam 字段会编译失败 → 显式更新 BP 即可):
	 *   - ERoomTeam::None     → 空 Tag (新流程中不存在, GameMode 必须显式设)
	 *   - ERoomTeam::Attack   → Faction.Offense
	 *   - ERoomTeam::Defense  → Faction.Defense
	 */
	UPROPERTY(ReplicatedUsing = OnRep_FactionTag, BlueprintReadOnly, Category = "Room|State")
	FGameplayTag CurrentFactionTag;

	/**
	 * 玩家准备状态
	 * ReplicatedUsing: 客户端收到新值时会自动触发 OnRep_IsReady
	 */
	UPROPERTY(ReplicatedUsing = OnRep_IsReady, BlueprintReadOnly, Category = "Room|State")
	bool bIsReady;

	/**
	 * 【2026.07.11 v29.6 大厂原则】玩家是否已显式选择阵营
	 *
	 * 设计动机:
	 *   - AddPlayerToRoom 内置 auto-balance 会按"哪边人少"自动分配阵营
	 *   - 但 auto-balance 多次跑 (EnterSkipToHostMode 一次 + DelayedSendPlayerInfo 一次),
	 *     会**反复覆盖**玩家已经在 UI 选过的阵营
	 *   - 玩家点 Btn_JoinDefenseTeam 切队之后, 又被 auto-balance 改回 Offense → 用户感知"切队没生效"
	 *
	 * 大厂原则 (单一真理源 + 玩家意图不可覆盖):
	 *   - 玩家主动切队 (ChangePlayerTeam 成功改阵营) → bHasExplicitlyChosenTeam = true, 并 Replicate
	 *   - auto-balance 只在 bHasExplicitlyChosenTeam == false 时跑
	 *   - 玩家从未主动切过 → 默认 Defense (PS 构造函数已设), auto-balance 可优化初值
	 *   - 玩家主动切过 → auto-balance **不再动**, 玩家意图是真神
	 *
	 * 注: 不可 Reset; 跨 session 不持久化 (P0 这版先 Reload-only, 未来如需 reset 重连场景, 再加 reset API)
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|State")
	bool bHasExplicitlyChosenTeam = false;

	/**
	 * 【2026.07.11 v29.6】服务器专用: 标记玩家已显式选择阵营
	 *
	 * 调用方: ARoomGameMode::ChangePlayerTeam (收到玩家主动切队请求, 改阵营成功后)
	 * 大厂原则: 一旦玩家主动切队, auto-balance 永远不再覆盖
	 */
	void Server_MarkTeamExplicitlyChosen();

	// ==========================================
	// 3. 计分板数据 (Scoreboard Data)
	// ==========================================
public:
	/**
	 * 获取总得分
	 */
	int32 GetScore() const { return RoomScore; }

	/**
	 * 获取击杀数
	 */
	int32 GetKills() const { return RoomKills; }

	/**
	 * 获取死亡数
	 */
	int32 GetDeaths() const { return RoomDeaths; }

	/**
	 * 获取助攻数
	 */
	int32 GetAssists() const { return RoomAssists; }

	/**
	 * 服务器专用: 增加得分（+1 击杀 +20 分）
	 * 同步更新 GameState 中的队伍击杀统计
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void AddKillScore();

	/**
	 * 服务器专用: 增加助攻得分（+1 助攻 +10 分）
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void AddAssistScore();

	/**
	 * 服务器专用: 增加死亡次数
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void AddDeath();

	/**
	 * 服务器专用: 重置计分板数据（每回合开始时调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void ResetScoreboardStats();

protected:
	/**
	 * 计分板数据复制通知回调
	 */
	UFUNCTION()
	void OnRep_ScoreboardData();

	/**
	 * 总得分（网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ScoreboardData, BlueprintReadOnly, Category = "Scoreboard")
	int32 RoomScore;

	/**
	 * 击杀数（网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ScoreboardData, BlueprintReadOnly, Category = "Scoreboard")
	int32 RoomKills;

	/**
	 * 死亡数（网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ScoreboardData, BlueprintReadOnly, Category = "Scoreboard")
	int32 RoomDeaths;

	/**
	 * 助攻数（网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ScoreboardData, BlueprintReadOnly, Category = "Scoreboard")
	int32 RoomAssists;

public:
	/**
	 * 计分板数据变化时的广播事件
	 * UI 监听此委托即可刷新
	 */
	UPROPERTY(BlueprintAssignable, Category = "Scoreboard|Events")
	FOnScoreboardDataChanged OnScoreboardDataChanged;

	// ==========================================
	// 4. 客户端数据刷新回调 (Rep Notifies)
	// ==========================================

	/**
	 * 【2026.07.10 重构】FactionTag 变化时的客户端回调
	 * 替代原 OnRep_Team, OnRep 函数名变更仅为清晰区分 — UI 监听 OnStateChanged 即可
	 */
	UFUNCTION()
	void OnRep_FactionTag();

	/**
	 * 准备状态变化时的客户端回调
	 */
	UFUNCTION()
	void OnRep_IsReady();

	// ==========================================
	// 5. UI 绑定接口
	// ==========================================
	/**
	 * 队伍/准备状态变化广播事件
	 * UI 只需监听此委托即可刷新，实现完美解耦
	 */
	UPROPERTY(BlueprintAssignable, Category = "Room|Events")
	FOnRoomPlayerStateChanged OnStateChanged;

protected:
	// ==========================================
	// 6. 战备选择数据 (Replicated)
	// ==========================================
	/**
	 * 选中的角色 ID（用于显示队友信息）
	 */
	UPROPERTY(Replicated)
	FString SelectedCharacterID;

	/**
	 * 【v52】选中的 1 号位武器 ID = 主武器 (Primary)
	 * 真理源 = 大厅 UI BP1 的 SelectedWeaponID1
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Loadout")
	FString SelectedWeaponID1;

	/**
	 * 【v52】选中的 2 号位武器 ID = 副武器 (Secondary)
	 * 真理源 = 大厅 UI 的 TempSelectedWeaponsByType[Secondary] (运行期, 不存档)
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Loadout")
	FString SelectedWeaponID2;

	/**
	 * 【2026.07.18 P0】选中的 3 号位武器 ID (近战武器)
	 *
	 * 背景:
	 *   旧 v23-v51: 大厅只有 2 把武器槽位 (SelectedWeaponID1/2)
	 *   v52: 扩展为 3 槽位 (主/副/近战), 支持生化模式运行时切换
	 *
	 * 大厂原则 (单一真理源):
	 *   - 3 个槽位都从大厅写入, 战斗 Spawn 时统一读出
	 *   - RoomInsidePage 改造后, 每个背包都装 3 把武器, 玩家切背包时整个 Loadout 替换
	 *
	 * 零兜底 (v51 同原则):
	 *   - 空字符串表示"未选", 拒绝用 NAME_None 当标志位
	 *   - 大厅 UI 必须保证至少 1 把 (主或近战) 有值, 否则不允许开局
	 *
	 * 网络同步: Replicated (跟随 SelectedWeaponID1/2 自动同步)
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Loadout")
	FString SelectedWeaponID3;

private:
	// ==========================================
	// 7. 计分板常量
	// ==========================================
	/**
	 * 击杀得分（每次击杀 +20 分）
	 */
	static constexpr int32 KillScoreValue = 20;

	/**
	 * 助攻得分（每次助攻 +10 分）
	 */
	static constexpr int32 AssistScoreValue = 10;
};
