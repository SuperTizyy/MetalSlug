// ==========================================
// 角色状态事件总线 实现
// 关联头文件: CharacterEvents.h
// ==========================================
#include "Components/CharacterEvents.h"
#include "Net/UnrealNetwork.h"
#include "Engine/Texture2D.h"


// ==========================================
// 1. 构造函数
// ==========================================
/**
 * UCharacterEvents 构造函数
 *
 * 初始化默认值:
 *   - PrimaryComponentTick.bCanEverTick = false (事件总线无 Tick 需求)
 *   - SetIsReplicatedByDefault(false) (事件在本地触发, 不参与复制)
 */
UCharacterEvents::UCharacterEvents()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}


// ==========================================
// 2. 复制配置: RemoteRole=Skip (事件在本地触发, 不需要复制)
// 解释:
//   - HealthComponent / EnergyComponent 的 OnRep 已在客户端触发 Broadcast
//   - AC/ACE 变化通过 OnRep 在客户端触发 Broadcast
//   - 头像/武器加载只在本地客户端执行, 不需要网络复制
//   - 因此 CharacterEvents 本身不需要复制, 节省带宽
// ==========================================
void UCharacterEvents::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// DOREPLIFETIME_CONDITION 留空: UPROPERTY 中无 Replicated 字段,
	// 所有事件字段均为 BlueprintAssignable (非复制), 符合设计预期
}


// ==========================================
// 3. 快照访问器 (订阅者主动拉取)
// ==========================================

/**
 * UCharacterEvents::GetCachedCharacterIcon
 *
 * 订阅者用这个方法在订阅成功后立刻拉取"最近一次广播的头像",
 * 解决事件总线模式的"竞争订阅"问题:
 *   - Broadcast 时同步写入缓存
 *   - 新订阅者订阅成功后主动调用本方法, 把缓存值"补发"给自己
 * @param OutCharID 角色 ID (空字符串 = 尚无广播)
 * @param OutAvatar 头像贴图 (可能为 nullptr, 表示查表失败)
 * @return true=有缓存数据 (即使 Avatar 为空), false=从未广播过
 */
bool UCharacterEvents::GetCachedCharacterIcon(FString& OutCharID, UTexture2D*& OutAvatar) const
{
	// 缓存有效性: 至少有一次广播(CharID 非空)
	// 即使 Avatar 为空 (查表失败), 也算有效 — 订阅者可以决定是否显示占位图
	if (CachedCharacterID.IsEmpty())
	{
		OutCharID = TEXT("");
		OutAvatar = nullptr;
		return false;
	}

	OutCharID = CachedCharacterID;
	OutAvatar = CachedAvatarIcon;
	return true;
}


/**
 * UCharacterEvents::GetCachedACValue
 *
 * @return 缓存的 AC 值 (-1 = 尚无任何 AC 广播)
 */
int32 UCharacterEvents::GetCachedACValue() const
{
	return CachedACValue;
}


/**
 * UCharacterEvents::GetCachedACEState
 *
 * @param OutACE   ACE 数值 (无缓存时输出 -1)
 * @param OutRank  排名枚举 (无缓存时输出 None)
 */
void UCharacterEvents::GetCachedACEState(int32& OutACE, EACERankType& OutRank) const
{
	if (bCachedACEValid)
	{
		OutACE = CachedACEValue;
		OutRank = CachedACERank;
	}
	else
	{
		OutACE = -1;
		OutRank = EACERankType::None;
	}
}


// ==========================================
// 4. 快照写入接口 (由 BaseCharacter 的 BroadcastXxx 调用)
// 设计原则: "Broadcast 即写缓存", 保证缓存始终是"最后一次广播的最终一致性快照"
// ==========================================

/**
 * UCharacterEvents::SetCachedCharacterIcon
 *
 * @param InCharID 角色 ID (空字符串表示清空缓存, 几乎不会发生)
 * @param InAvatar 头像贴图 (允许 nullptr — 表示查表失败, 仍然写缓存)
 */
void UCharacterEvents::SetCachedCharacterIcon(const FString& InCharID, UTexture2D* InAvatar)
{
	CachedCharacterID = InCharID;
	CachedAvatarIcon = InAvatar;
}


// ==========================================
// 【v40.2 P0 新增】武器图标快照 (镜像 CachedCharacterID/CachedAvatarIcon)
// ==========================================

/**
 * UCharacterEvents::GetCachedWeaponIcon
 *
 * 【v40.2 P0 修复】镜像 GetCachedCharacterIcon
 *
 * 订阅者 (GameHUDWidget) 订阅成功后立刻拉取"最近一次广播的武器图标",
 * 解决事件总线模式的"竞争订阅"问题:
 *   - 服务器 RefreshCharacterIcon → RefreshWeaponIconOnHUD → RPC → Client_RefreshWeaponIcon_Implementation → BroadcastWeaponIconReady
 *   - 如果 Broadcast 时 HUD 还没订阅, 事件丢失 → HUD 永远没武器图标
 *   - 现在 Bind-Snapshot 也会主动调 GetCachedWeaponIcon → 模拟补发
 *
 * @param OutWeaponID 武器 ID (空 = 从未广播)
 * @param OutIcon     武器图标贴图 (可能为 nullptr, 表示查表失败)
 * @return true=有缓存 (即使 Icon 为空), false=从未广播过
 */
bool UCharacterEvents::GetCachedWeaponIcon(FString& OutWeaponID, UTexture2D*& OutIcon) const
{
	if (!bCachedWeaponIconValid)
	{
		OutWeaponID = TEXT("");
		OutIcon = nullptr;
		return false;
	}

	OutWeaponID = CachedWeaponID;
	OutIcon = CachedWeaponIcon;
	return true;
}


/**
 * UCharacterEvents::SetCachedWeaponIcon
 *
 * @param InWeaponID 武器 ID
 * @param InIcon     武器图标贴图 (允许 nullptr — 表示查表失败, 仍然写缓存)
 */
void UCharacterEvents::SetCachedWeaponIcon(const FString& InWeaponID, UTexture2D* InIcon)
{
	CachedWeaponID = InWeaponID;
	CachedWeaponIcon = InIcon;
	bCachedWeaponIconValid = true;
}


/**
 * UCharacterEvents::SetCachedACValue
 */
void UCharacterEvents::SetCachedACValue(int32 InAC)
{
	CachedACValue = InAC;
}


/**
 * UCharacterEvents::SetCachedACEState
 *
 * 即使 ACE=0 也算有效 (玩家开局 ACE 就是 0, 这是真实状态)
 * bCachedACEValid 一旦置 true 就不再重置 — 后续 ACE 变化正常写缓存
 */
void UCharacterEvents::SetCachedACEState(int32 InACE, EACERankType InRank)
{
	CachedACEValue = InACE;
	CachedACERank = InRank;
	bCachedACEValid = true;
}
