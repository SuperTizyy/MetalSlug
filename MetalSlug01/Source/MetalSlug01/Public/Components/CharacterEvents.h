// ==========================================
// 角色状态事件总线 【2026-07-01 新增 - 架构重构 v2】
// 位置: Public/Components/CharacterEvents.h
// 用途: 定义 ABaseCharacter 向 UI 层广播的所有状态事件
//
// 架构说明:
//   - 本类是 ABaseCharacter 的"对外事件出口"
//   - 所有需要响应角色状态的 UI 组件 (GameHUDWidget / PlayerStatusWidget) 订阅这里的事件
//   - BaseCharacter 本身不再持有 UGameHUDWidget 引用, 彻底消除 Core→UI 的直接依赖
//   - 依赖方向: BaseCharacter → CharacterEvents → GameHUDWidget (单向)
//
// 对比旧架构:
//   旧: BaseCharacter → GameHUDWidget (直接 Push, 循环依赖风险)
//   新: BaseCharacter → CharacterEvents ← GameHUDWidget (事件订阅, 解耦)
//
// 7 个事件:
//   1. OnCharacterIconReady     - 角色头像贴图已加载完毕
//   2. OnHealthChangedDelegate   - 血量变化 (值 + 最大值)
//   3. OnEnergyChangedDelegate   - 能量变化 (值 + 最大值)
//   4. OnACValueChanged          - AC 防护服值变化
//   5. OnACEValueChanged         - ACE 击杀数变化
//   6. OnACEWithRankChanged      - ACE 击杀数 + 排名颜色变化
//   7. OnWeaponIconReady         - 武器图标已加载完毕
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/CombatEnums.h" // EACERankType (ACE 排名颜色枚举)
#include "Components/HealthComponent.h" // FOnInvincibilityChanged (无敌期事件)
#include "CharacterEvents.generated.h"

// 前向声明 (减少 include 依赖)
class UTexture2D;
class UImage;

// ----------------------------------------
// 1. 角色头像事件
// ----------------------------------------
/**
 * 角色头像贴图已加载完毕
 * @param CharacterID 角色 ID (用于异步加载回调匹配)
 * @param Icon        头像贴图 (可能为空, 表示查表失败)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCharacterIconReady, const FString&, CharacterID, UTexture2D*, Icon);

// ----------------------------------------
// 2. 血量变化事件
// ----------------------------------------
/**
 * 血量变化广播
 * @param Current 当前血量
 * @param Max     最大血量
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChangedDelegate, float, Current, float, Max);

// ----------------------------------------
// 3. 能量变化事件
// ----------------------------------------
/**
 * 能量变化广播
 * @param Current 当前能量
 * @param Max     最大能量
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnergyChangedDelegate, float, Current, float, Max);

// ----------------------------------------
// 4. AC 防护服值变化事件
// ----------------------------------------
/**
 * AC 值变化广播
 * @param NewAC 新的 AC 值
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnACValueChanged, int32, NewAC);

// ----------------------------------------
// 5. ACE 击杀数变化事件
// ----------------------------------------
/**
 * ACE 值变化广播 (无排名信息, 使用默认白色)
 * @param NewACE 新的 ACE 值
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnACEValueChanged, int32, NewACE);

// ----------------------------------------
// 6. ACE + 排名颜色变化事件
// ----------------------------------------
/**
 * ACE 值 + 排名颜色变化广播
 * @param NewACE    新的 ACE 值
 * @param RankType  排名类型 (Gold / White / None)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnACEWithRankChanged, int32, NewACE, EACERankType, RankType);

// ----------------------------------------
// 7. 武器图标加载完毕事件
// ----------------------------------------
/**
 * 武器图标已加载完毕
 * @param WeaponID 武器 ID
 * @param Icon     武器图标贴图 (可能为空)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponIconReady, const FString&, WeaponID, UTexture2D*, Icon);

// ----------------------------------------
// 8. 武器弹药信息事件 【v84 大厂架构新增】
// ----------------------------------------
/**
 * 武器弹药信息就绪 — 用于武器面板 TextBlock 显示弹药数量
 *
 * 格式规则:
 *   - 近战武器 (EWeaponMeshType::Melee): "1/1" (固定值, 弹药无意义)
 *   - 枪械 (Primary/Secondary): "弹匣弹药/弹匣容量 + 备用弹药"
 *
 * 大厂原则 - 单一真理源:
 *   - 真理源在 WeaponFireComponent.GetCurrentAmmo() / GetMagazineSize() / GetReserveAmmo()
 *   - CharacterIconComponent 读取并封装为文本, 通过本事件推送给 HUD
 *   - WeaponPanelWidget 只负责 UI 显示, 不参与数据计算
 *
 * @param CurrentMag   当前弹匣弹药
 * @param MagazineSize 弹匣容量
 * @param ReserveAmmo  备用弹药总数
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWeaponAmmoInfoReady, int32, CurrentMag, int32, MagazineSize, int32, ReserveAmmo);

// ----------------------------------------
// 9. 武器面板显隐状态事件 【v105 新增 - 母体无武器弹药显示】
// ----------------------------------------
/**
 * 武器面板显隐状态变化 — 用于母体等无武器角色隐藏武器弹药 UI
 *
 * 业务规则 (用户 2026.07.27 明确):
 *   - 母体没有武器, 不应该显示弹药 UI (Text_WeaponAmmo / Image_MeleeWeapon)
 *   - 变成母体时隐藏武器面板, 变回人类时显示武器面板
 *
 * 大厂原则 - 单一真理源:
 *   - 真理源在 CharacterIconComponent (调用 SetWeaponPanelVisibility)
 *   - CharacterEvents 作为事件总线, 转发状态变化给 HUD
 *
 * @param bIsVisible true=显示武器面板, false=隐藏武器面板
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnWeaponPanelVisibilityChanged, bool);


// ==========================================
// UCharacterEvents - 角色状态事件总线
// ==========================================
/**
 * @class UCharacterEvents
 * @brief 角色状态事件总线 (Component 形式挂载在 ABaseCharacter 上)
 *
 * 设计决策:
 *   - 做成 Component 而非纯 static 类: 可以在蓝图中可视化配置, 便于调试
 *   - 每个 ABaseCharacter 实例持有一个 UCharacterEvents 实例, 生命周期与角色绑定
 *   - 不复制到远程客户端: RemoteRole=Skip, 因为事件由 BaseCharacter 自身在客户端触发
 *
 * 使用方式:
 *   1. GameHUDWidget::NativeConstruct 中订阅事件 (参考 TryBindToGameState 的重试模式)
 *   2. BaseCharacter 在 BeginPlay 时广播 OnCharacterReady 等事件
 *   3. BaseCharacter 在各处调用 Broadcast() 发出事件
 *
 * 注意: RemoteRole=Skip, 因为:
 *   - HealthComponent / EnergyComponent 已在服务器端 Broadcast
 *   - AC/ACE 变化在 OnRep 中 Broadcast
 *   - 头像/武器加载只在本地客户端执行
 *
 * ==========================================
 * 【2026-07-01 P0 增强】快照/缓存层 (Snapshot Cache)
 * ==========================================
 * 设计动机:
 *   事件总线模式有个经典坑——"竞争订阅"问题:
 *     - BaseCharacter::PossessedBy 时调用 Client_RefreshCharacterIcon
 *     - Client_RefreshCharacterIcon_Implementation 在客户端执行, Broadcast CharacterEvents
 *     - 但此时 GameHUDWidget 还没创建, 没有订阅者
 *     - 当 GameHUDWidget::NativeConstruct 终于订阅时, 那次 Broadcast 已经过去, 永远丢失
 *   旧架构用"RetryRefreshHUD" 绕开 (BaseCharacter 直接访问 GameHUDWidget, 不需要订阅)
 *   新架构必须用"事件 + 缓存"双轨制:
 *     1. Broadcast 时同步写缓存
 *     2. 新订阅者订阅成功后, 主动读缓存, 主动触发一次 "补发"
 *   这样无论何时订阅, 都能拿到最新状态
 *
 * 缓存范围 (2026-07-01 现状, 后续可扩展):
 *   - 头像: CachedCharacterID + CachedAvatarIcon (问题 1 修复)
 *   - AC:    CachedACValue (问题 2 修复: 与头像同模式)
 *   - ACE:   CachedACEValue + CachedACERank
 *   - HP/Energy: 不缓存 (实时性高, 但被订阅频率低, 影响小, 后续需要时再加)
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UCharacterEvents : public UActorComponent
{
	GENERATED_BODY()

public:
	UCharacterEvents();

	// ==========================================
	// 7 个公开事件 (供 GameHUDWidget 等 UI 组件订阅)
	// ==========================================

	/** 角色头像贴图已加载完毕 */
	UPROPERTY(BlueprintAssignable, Category = "CharacterEvents|Icon")
	FOnCharacterIconReady OnCharacterIconReady;

	/** 血量变化 */
	UPROPERTY(BlueprintAssignable, Category = "CharacterEvents|Health")
	FOnHealthChangedDelegate OnHealthChangedDelegate;

	/** 能量变化 */
	UPROPERTY(BlueprintAssignable, Category = "CharacterEvents|Energy")
	FOnEnergyChangedDelegate OnEnergyChangedDelegate;

	/** AC 防护服值变化 */
	UPROPERTY(BlueprintAssignable, Category = "CharacterEvents|AC")
	FOnACValueChanged OnACValueChanged;

	/** ACE 击杀数变化 */
	UPROPERTY(BlueprintAssignable, Category = "CharacterEvents|ACE")
	FOnACEValueChanged OnACEValueChanged;

	/** ACE 值 + 排名颜色变化 */
	UPROPERTY(BlueprintAssignable, Category = "CharacterEvents|ACE")
	FOnACEWithRankChanged OnACEWithRankChanged;

	/** 武器图标已加载完毕 */
	UPROPERTY(BlueprintAssignable, Category = "CharacterEvents|Weapon")
	FOnWeaponIconReady OnWeaponIconReady;

	/** 无敌期状态变化 【2026.07.14 新增 - 显示复活进度条用】 */
	UPROPERTY(BlueprintAssignable, Category = "CharacterEvents|Invincibility")
	FOnInvincibilityChanged OnInvincibilityChanged;

	/** 武器弹药信息就绪 【v84 大厂架构新增 - 武器面板 TextBlock 显示弹药】 */
	UPROPERTY(BlueprintAssignable, Category = "CharacterEvents|Weapon")
	FOnWeaponAmmoInfoReady OnWeaponAmmoInfoReady;

	/** 武器面板显隐状态变化 【v105 新增 - 母体无武器时隐藏弹药 UI】 */
	FOnWeaponPanelVisibilityChanged OnWeaponPanelVisibilityChanged;

	// ==========================================
	// 【2026-07-01 P0 新增】快照访问器 (订阅者主动拉取)
	// ==========================================

	/**
	 * 获取当前缓存的头像 + 角色 ID
	 * @param OutCharID 角色 ID
	 * @param OutAvatar 头像贴图 (可能为 nullptr)
	 * @return true=有缓存数据, false=尚未有任何 Broadcast
	 */
	UFUNCTION(BlueprintCallable, Category = "CharacterEvents|Snapshot")
	bool GetCachedCharacterIcon(FString& OutCharID, UTexture2D*& OutAvatar) const;

	/**
	 * 获取当前缓存的 AC 值
	 * @return 缓存的 AC 值 (无缓存时返回 -1)
	 */
	UFUNCTION(BlueprintCallable, Category = "CharacterEvents|Snapshot")
	int32 GetCachedACValue() const;

	/**
	 * 获取当前缓存的 ACE 值 + 排名
	 * @param OutACE   ACE 数值
	 * @param OutRank  排名枚举 (无缓存时返回 None)
	 */
	UFUNCTION(BlueprintCallable, Category = "CharacterEvents|Snapshot")
	void GetCachedACEState(int32& OutACE, EACERankType& OutRank) const;

	// ==========================================
	// 【2026-07-01 P0 新增】快照写入接口 (由 BroadcastXxx 调用)
	// ==========================================

	/** 写入头像快照 */
	void SetCachedCharacterIcon(const FString& InCharID, UTexture2D* InAvatar);
	/** 写入 AC 快照 */
	void SetCachedACValue(int32 InAC);
	/** 写入 ACE + 排名快照 */
	void SetCachedACEState(int32 InACE, EACERankType InRank);

	/**
	 * 【v40.2 P0 新增】写入武器图标快照
	 *
	 * 大厂原则 - "事件 + 缓存"双轨制:
	 *   - 与头像缓存完全对称 (镜像 SetCachedCharacterIcon)
	 *   - Bind-Snapshot 补发场景: HUD 订阅成功后, 主动拉取快照 → 调用 OnWeaponIconReady 模拟补发
	 *   - 解决了"武器图标 RPC 比 HUD 订阅早触发 → HUD 永远丢失武器图标"问题
	 */
	UFUNCTION(BlueprintCallable, Category = "CharacterEvents|Snapshot")
	void SetCachedWeaponIcon(const FString& InWeaponID, UTexture2D* InIcon);

	/**
	 * 【v40.2 P0 新增】获取当前缓存的武器图标
	 * @param OutWeaponID 武器 ID (空 = 尚无广播)
	 * @param OutIcon     武器图标贴图 (可能为 nullptr, 表示查表失败)
	 * @return true=有缓存, false=尚未有任何武器图标广播
	 */
	UFUNCTION(BlueprintCallable, Category = "CharacterEvents|Snapshot")
	bool GetCachedWeaponIcon(FString& OutWeaponID, UTexture2D*& OutIcon) const;

	/**
	 * 【v85.2 大厂架构新增】写入武器弹药快照
	 *
	 * 大厂原则 - "事件 + 缓存"双轨制:
	 *   - 与武器图标缓存完全对称
	 *   - Bind-Snapshot 补发场景: HUD 订阅成功后, 主动拉取弹药快照
	 */
	UFUNCTION(BlueprintCallable, Category = "CharacterEvents|Snapshot")
	void SetCachedWeaponAmmoInfo(int32 InCurrentAmmo, int32 InMagazineSize, int32 InReserveAmmo, bool bInIsMelee);

	/**
	 * 【v85.2 大厂架构新增】获取当前缓存的武器弹药信息
	 * @return true=有缓存, false=尚未有任何弹药广播
	 */
	UFUNCTION(BlueprintCallable, Category = "CharacterEvents|Snapshot")
	bool GetCachedWeaponAmmoInfo(int32& OutCurrentAmmo, int32& OutMagazineSize, int32& OutReserveAmmo, bool& OutbIsMelee) const;

	/**
	 * 【v105 新增】广播武器面板显隐状态变化 — 用于母体等无武器角色隐藏武器弹药 UI
	 *
	 * 业务规则 (用户 2026.07.27 明确):
	 *   - 母体没有武器, 不应该显示弹药 UI
	 *   - 变成母体时隐藏武器面板, 变回人类时显示武器面板
	 *
	 * @param bIsVisible true=显示武器面板, false=隐藏武器面板
	 */
	UFUNCTION(BlueprintCallable, Category = "CharacterEvents|Weapon")
	void SetWeaponPanelVisibility(bool bIsVisible);

protected:
	// ==========================================
	// 内部: 复制配置 (RemoteRole=Skip, 事件在本地触发)
	// ==========================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==========================================
	// 【2026-07-01 P0 新增】快照存储 (UPROPERTY 防止 GC, 确保订阅者拿到后引用有效)
	// ==========================================

	/** 缓存: 最近一次头像广播的 角色 ID (空字符串 = 尚无广播) */
	UPROPERTY()
	FString CachedCharacterID;

	/** 缓存: 最近一次头像广播的 头像贴图 (可能为 nullptr, 表示查表失败) */
	UPROPERTY()
	TObjectPtr<UTexture2D> CachedAvatarIcon = nullptr;

	/** 缓存: 最近一次 AC 广播 (-1 表示尚无广播) */
	UPROPERTY()
	int32 CachedACValue = -1;

	/** 缓存: 最近一次 ACE 广播 (初始为 -1 + None 表示尚无广播) */
	UPROPERTY()
	int32 CachedACEValue = -1;

	/** 缓存: 最近一次 ACE 排名广播 */
	UPROPERTY()
	EACERankType CachedACERank = EACERankType::None;

	/** 缓存: 是否已有任何 ACE 广播 (用于区分 "ACE=0" 与 "尚未广播") */
	UPROPERTY()
	bool bCachedACEValid = false;

	/**
	 * 【v40.2 P0 新增】武器图标快照缓存 (镜像 CachedCharacterID/CachedAvatarIcon)
	 *
	 * 大厂原则 - 单一真理源:
	 *   - 这只是 HUD Bind-Snapshot 用的"快照", 不是武器图标的真理源
	 *   - 真理源在 Owner->GetSpawnWeaponID() (Replicated 字段) + Owner 持有的 ABaseWeapon 实际图标资源
	 */
	UPROPERTY()
	FString CachedWeaponID = FString();

	/** 缓存: 最近一次武器图标广播的 武器图标贴图 (nullptr = 查表失败) */
	UPROPERTY()
	TObjectPtr<UTexture2D> CachedWeaponIcon = nullptr;

	/** 缓存: 是否已有任何武器图标广播 (用于区分 "尚无" 与 "空字符串") */
	UPROPERTY()
	bool bCachedWeaponIconValid = false;

	/** 缓存: 最近一次武器弹药广播 - 当前弹匣弹药数 */
	UPROPERTY()
	int32 CachedCurrentAmmo = -1;

	/** 缓存: 最近一次武器弹药广播 - 弹匣容量 */
	UPROPERTY()
	int32 CachedMagazineSize = -1;

	/** 缓存: 最近一次武器弹药广播 - 备用弹药数 */
	UPROPERTY()
	int32 CachedReserveAmmo = -1;

	/** 缓存: 最近一次武器弹药广播 - 是否为近战武器 */
	UPROPERTY()
	bool bCachedIsMelee = false;

	/** 缓存: 是否已有任何武器弹药广播 */
	UPROPERTY()
	bool bCachedWeaponAmmoValid = false;
};
