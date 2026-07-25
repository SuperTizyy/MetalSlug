// ==========================================
// 击杀音效 Component【v100 大厂架构 — 零等待 / 零兜底】(2026.07.26)
//
// @brief 玩家击杀敌人或母体时,在 Victim 位置播放对应的连杀音效
//
// 【架构定位 — SRP 关注点分离】
//   - ABaseCharacter: 持有本组件,经 Resolver 暴露给其他调用方
//   - 本组件: 只管本地音效播放 — 一次 UGameplayStatics::PlaySoundAtLocation
//   - ARoomGameMode: 单一真理源持有 KillStreakIconDataTable (复用了 HUD 连杀图标的同表)
//   - ABaseCharacter::Multicast_NotifyKill_Implementation: 经 Resolver 同时触发 KillSound
//
// 【为什么新建组件 (不复用 MotherSpawnSoundComponent)】
//   - 资产注入方式不同: Mother 是组件字段(单一资产),KillSound 是 DataTable(7 种音查表)
//   - 接口签名不同: Mother 是无参 PlaySpawnSound(),KillSound 接收 EKillStreakType 决定用哪个音
//   - 职责不同: Mother 是单一"母体生成音效",KillSound 是 N 种"连杀音效"映射
//   - 共享同一条 RPC 入口(Multicast_NotifyKill),避免重复 RPC(项目大厂原则 — 单一入口)
//
// 【为什么复用 FKillStreakIconInfo 加 KillSound 字段(零新建表)】
//   - HUD 连杀图标早已按 EKillStreakType 行驱动(老项目 v41 大厂架构)
//   - 同一个枚举,同一个表,图标+音效一行配置,零重复数据源
//   - 大厂原则 — 单一真理源: 一个业务概念(图标+音效组合)在同一表中
//   - 旧版连杀图标都已配好,只需在 DataTable 加 KillSound 列即可启动新功能
//
// 【为什么不在 OnRep_OnKill 中启动音效】
//   - 旧方案同时调 OnRep 与 Multicast 会双发(项目历史教训)
//   - 大厂原则 — 关注点分离: Multicast 负责"一次性"触发,OnRep 负责状态校验 + 状态事件广播
//
// 【为什么不用 PlaySound2D】
//   - 击杀音效应该带 3D 位置 (玩家能听到 Victim 被击杀的方向感)
//   - PlaySoundAtLocation 自动 3D 衰减,与 MotherSpawnSound 完全对称
//
// 【大厂原则 — 单一入口】
//   - Multicast_NotifyKill_Implementation 是所有"击杀音效"的唯一入口
//   - 不论 bIsKillerPlayer(玩家 vs AI 杀人)、不论 bIsAssist(是否助攻)、不论模式(刀战/生化)
//     所有客户端 + 服务器本地都会播放音效(围观者也能听到)
//   - 业务规则(用户 2026.07.26 明确): "击杀敌人或母体需要播放声音,每个连杀都是不同的声音"
//
// 【零等待 — 与 v99.2 / v99.3 同款架构】
//   - 没有运行期模式校验(IsAllowedByCurrentMatchMode 等)— 兜底 = 反模式
//   - 真理源在 Multicast_NotifyKill(已经过 v40.9 大厂改造:bIsKillerPlayer 守卫)
//   - 客户端 RPC 触发 = 服务器已决定"有击杀发生",事件必然对
//
// 【零兜底 — 严禁】
//   - 禁止: 缺表时用默认 DataTable 兜底
//   - 禁止: 缺资产时用默认 SoundCue 兜底
//   - 禁止: 缺 World 时容错播放
//   - 禁止: 找不到对应 Row 时静默跳过(必须 Log Error 提示配表)
//   - 禁止: bIsAssist=true 时跳过播音(所有人都听到击杀音效,助攻也算)
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Enums/CombatEnums.h"  // EKillStreakType
#include "KillSoundComponent.generated.h"

class UDataTable;
class USoundBase;
class ABaseCharacter;

/**
 * @class UKillSoundComponent
 * @brief 击杀音效组件 — 按 EKillStreakType 查表播放对应 SoundCue
 *
 * 工作流:
 *   1. BaseCharacter 构造函数中 CreateDefaultSubobject<UKillSoundComponent>
 *      (所有角色都挂载;零架构分支,无论玩家 / AI / Mother / 普通角色)
 *   2. GameMode.KillStreakIconDataTable 字段中配好 DT_KillStreakIconInfo(含 KillSound 列)
 *   3. 服务器 ABaseCharacter::Multicast_NotifyKill 触发 → 每台机器本地查表 + 播放一次
 *   4. 组件 PlaySoundAtLocation(在 Victim 当前位置,3D 衰减)
 *
 * 大厂原则 — 单一真理源:
 *   - 组件不复制(音效本地播放,服务器 / 客户端各自独立)
 *   - 触发信号通过 ABaseCharacter::Multicast_NotifyKill 统一分发
 *   - DataTable 真理源在 ARoomGameMode → KillStreakIconDataTable(与 HUD 连杀图标同表)
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UKillSoundComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKillSoundComponent();

	/**
	 * 在 Owner 当前位置播放对应 EKillStreakType 的音效(查 DataTable)
	 *
	 * 触发方: ABaseCharacter::Multicast_NotifyKill_Implementation(单一网络入口)
	 *
	 * 大厂原则 — 零等待:
	 *   - 仅校验: Owner / World / DataTable / Row / Sound
	 *   - 不再运行期校验模式或任何"上游应该做的"事
	 *
	 * 零兜底:
	 *   - 缺表 / 找不到 Row / Sound 为空 → Log Error + 拒绝播放
	 *   - 不允许"找不到时用 OnKill 默认音"等兜底逻辑
	 *
	 * @param InStreakType 连杀类型(Headshot / OneKill / TwoKills / ThreeKills / FourKills / FiveKills)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Kill")
	void PlayKillSound(EKillStreakType InStreakType);

	// ==========================================
	// 配置项(运行时由 ARoomGameMode 注入;零硬编码)
	// ==========================================

	/**
	 * 【v100 大厂架构 — 单一真理源注入入口】
	 *
	 * 由外部代码(ARoomGameMode::InitGameState 或 KillSoundComponent::EnsureKillStreakDataTable)调一次,
	 * 把 GM->KillStreakIconDataTable 同步到组件(走与 v37 WeaponAttachmentDataTable 同款模式)。
	 *
	 * 不在此字段的 BP EditDefaultsOnly 暴露:
	 *   - 真理源在 GM,EditDefaultsOnly 会让 BP 误以为"BP 也能配"
	 *
	 * 必须配指向 DT_KillStreakIconInfo 的 DataTable
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Kill")
	void SetKillStreakIconDataTable(UDataTable* InDataTable);

private:
	/**
	 * Lazy resolve Owner — 每次按需查询,避开 BeginPlay 缓存被 BP archetype 覆写的陷阱
	 *
	 * 大厂原则(同 v40.6 / v99.3 / v100 MotherSpawnSoundComponent):
	 *   - 不缓存 raw pointer: BP archetype 会覆写 C++ 默认字段
	 *   - 每次 GetOwner() + Cast<T>: 真理源 = UE 标准 API
	 *   - Cast 失败 / GetOwner 无效 → Log Error + return nullptr
	 */
	ABaseCharacter* ResolveOwnerCharacter() const;

	/**
	 * 【v100 单一真理源懒注入】EnsureKillStreakDataTable — 确保组件持有 DataTable
	 *
	 * 大厂原则 — 懒注入 (Lazy Injection):
	 *   - 不在 BeginPlay 主动注入(BeginPlay 时序不可靠,见 v40.6 BP archetype 陷阱)
	 *   - 在 PlayKillSound 第一次调用时检查;若未注入,向 GM 拉一次
	 *   - 失败 Log Error + 标记 bHasTriedLazyInject 拒绝重复尝试(避免每帧刷错日志)
	 *
	 * @return true = 组件已持有 DataTable(可正常查表),false = 注入失败(调用方应拒绝播放)
	 */
	bool EnsureKillStreakDataTable();

	/**
	 * 【v100 单一真理源】运行时缓存的连杀 DataTable(由 GM 注入)
	 *
	 * NOT EditDefaultsOnly: 真理源在 GameMode,不在 BP
	 * UPROPERTY: 防 GC + 反射访问
	 *
	 * 关联: DT_KillStreakIconInfo (FKillStreakIconInfo 行结构)
	 *       Headshot / OneKill / TwoKills / ThreeKills / FourKills / FiveKills 6 行
	 */
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> KillStreakIconDataTable;

	/**
	 * 【v100 懒注入幂等守卫】已尝试过 lazy 注入(无论成功失败)
	 *
	 * 大厂原则 - 防御型设计:
	 *   - GM 字段为空 / 类型错时,不要每帧刷 Log Error
	 *   - 一次性失败,标记已尝试,后续 PlayKillSound 直接拒绝(不再查 GM)
	 *   - 如果策划后来修了 GM,可以调 SetKillStreakIconDataTable 重置
	 */
	bool bHasTriedLazyInject = false;
};
