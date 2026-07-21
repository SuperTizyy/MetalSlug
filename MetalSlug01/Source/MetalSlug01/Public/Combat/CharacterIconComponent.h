// ==========================================
// 版权声明：在项目设置的描述页面填写您的版权信息。
// ==========================================
//
// @file CharacterIconComponent.h
// @brief 角色头像/武器图标刷新子系统 — 从 ABaseCharacter 抽离
//
// 【架构定位 — 2026.07.12 P0 大厂重构】
//   BaseCharacter.cpp 已 3957 行, 单文件维护成本爆炸.
//   本文件把 BaseCharacter 中"角色头像加载 + 武器图标加载 + HUD 推送"逻辑
//   抽出到独立 ActorComponent.
//
// 【职责 (In Scope)】
//   1. RefreshCharacterIcon — 从 PlayerState 读 CharacterID, 查 CharacterDataTable
//   2. Client_RefreshCharacterIcon_Implementation — 客户端接收服务器传来的头像贴图
//   3. RetryRefreshCharacterIcon — HUD 未就绪时的延迟重试 (防日志洪水)
//   4. RefreshWeaponIconOnHUD — 武器图标加载 (服务器查表 → 客户端推 HUD)
//   5. GetCharacterAvatarFromTable — BlueprintPure 查表助手
//
// 【不负责 (Out of Scope)】
//   - HUD Widget 实例化 (UMyGameHUD::TryGet 仍由调用方保证)
//   - DataTable 资产管理 (由 GameMode/配置层持有)
//   - CharacterID 的写入 (由 ARoomPlayerState::SelectedCharacterID 持有, 真理源在 PS)
//   - 死亡/武器切换 (由 CombatDeathComponent / WeaponAttachmentComponent 触发)
//
// 【大厂原则 - 单一真理源】
//   - CharacterID 真理源 = ARoomPlayerState::SelectedCharacterID (Phase 1 已落地)
//   - WeaponID 真理源 = ARoomPlayerState::SelectedWeaponID1 (主武器槽位, 大厅真理源)
//     ※ v52 3 槽位扩展: 武器图标默认显示主武器槽位的武器 (HUD 显示哪个槽位的武器由外部 GameHUD 决定)
//   - Avatar 贴图真理源 = CharacterDataTable 行 (由 GameMode 持有 DataTable 指针)
//   - 本组件不持有 CharacterID 副本, 只缓存上一次请求的 CharacterID 用于 Retry
//
// 【大厂原则 - 零兜底】
//   - CharacterID 为空 → Log Error + return (拒绝 fallback)
//   - DataTable 为空 → Log Error + return (拒绝隐式 nullptr)
//   - 重试上限 20 次 → 超过后 Log Warning + 停止 (防日志洪水)
//
// 【大厂原则 - 职责分离】
//   - 角色图标 ↔ 武器图标: 两条独立链路, 互不干扰
//   - 服务器查表 → Client RPC 推送: 服务器是数据权威, 客户端只是显示端
//
// 【调用链路 (大厂原则 - 集中调度)】
//   PossessedBy → Server 调 RefreshCharacterIcon
//                       ↓ 查 DataTable
//                       ↓ Client_RefreshCharacterIcon (RPC)
//                       ↓ 客户端收到 Avatar → BroadcastCharacterIconReady
//                                              ↓ IsLocallyControlled?
//                                              ↓ CharacterEvents->OnCharacterIconReady.Broadcast
//                                              ↓ GameHUDWidget 订阅 → 更新 UI
//
// 【迁移路径 (Phase 2)】
//   1. 本次只创建 2 个文件 (本文件 + .cpp), 不动 BaseCharacter.h / .cpp
//   2. 后续 batch: BaseCharacter 字段/方法迁移到本组件, BaseCharacter 调委托
//   3. 字段迁移完后再删除 BaseCharacter 中重复字段
//
// 【UE 5.6 注意事项】
//   .generated.h 必须用**裸文件名** (UE 5.6 UHT 字面 OrdinalIgnoreCase 比对, 不处理路径前缀)
// ==========================================
#pragma once

// ==========================================
// 头文件包含区
// ==========================================
#include "CoreMinimal.h"                          // UE 引擎核心最小化
#include "Components/ActorComponent.h"            // UActorComponent 基类

// UE 自动生成的头文件 (必须最后, 且用裸文件名 — UE 5.6 严格模式)
#include "CharacterIconComponent.generated.h"

// 前置声明 (加快编译)
class ABaseCharacter;                            // 拥有者 (Owner)
class UTexture2D;                                // 头像/武器图标贴图
class UDataTable;                                // CharacterDataTable/WeaponDataTable


/**
 * @class UCharacterIconComponent
 * @brief 角色图标子系统 — BaseCharacter 的"图标数据"层
 *
 * 设计模式: ActorComponent 自治子系统
 *   - Owner 暴露: 通过 Owner->GetPlayerState<>() / Owner->GetWorld()->GetAuthGameMode()
 *   - 不跨边界: HUD 推送走 CharacterEvents 事件总线, 不直接持有 GameHUDWidget 引用
 *   - 职责单一: 只管"图标从哪里来 + 怎么推到 UI", 不管 HUD 实例化
 *
 * 使用方法:
 *   1. BaseCharacter 构造函数中 CreateDefaultSubobject<UCharacterIconComponent>
 *   2. RefreshCharacterIcon 转发: CharacterIcon->RefreshCharacterIcon()
 *   3. RefreshWeaponIconOnHUD 转发: CharacterIcon->RefreshWeaponIconOnHUD()
 *   4. GetCharacterAvatarFromTable 转发: CharacterIcon->GetCharacterAvatarFromTable(InCharID)
 *
 * 后续迁移:
 *   - BaseCharacter::RefreshCharacterIcon → 改为转发到本组件
 *   - BaseCharacter::Client_RefreshCharacterIcon_Implementation → 改为转发到本组件
 *   - BaseCharacter::RetryRefreshCharacterIcon → 改为转发到本组件
 *   - BaseCharacter::RefreshWeaponIconOnHUD → 改为转发到本组件
 *   - BaseCharacter::GetCharacterAvatarFromTable → 改为转发到本组件
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UCharacterIconComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// ==========================================
	// 构造函数
	// ==========================================

	/**
	 * 构造函数: 设置默认值
	 * 目的: 不创建子组件 (本组件没有子组件), 仅初始化字段默认值
	 */
	UCharacterIconComponent();

	// 【2026.07.12 P0 重构】Friend 双向授权:
	//   - ABaseCharacter 调本组件 protected 方法 (TryResolveHUDWidget / Client_RefreshCharacterIcon 等)
	friend class ABaseCharacter;

	// ==========================================
	// 公开 API — BaseCharacter 转发入口
	// ==========================================

	/**
	 * 刷新角色头像 — 服务器主入口
	 *
	 * 【2026.07.12 P0 重构】从 BaseCharacter 抽离
	 *
	 * 流程:
	 *   1. 服务器: 从 ARoomPlayerState::SelectedCharacterID 读 CharacterID
	 *   2. 服务器: 调 GameMode->CharacterDataTable.FindRow<FCharacterInfo>(CharID) 拿 Avatar
	 *   3. 服务器: Client_RefreshCharacterIcon(CharID, Avatar) (Client RPC)
	 *   4. 客户端: Client_RefreshCharacterIcon_Implementation
	 *      → 缓存 CharacterID → 调 BroadcastCharacterIconReady
	 *      → CharacterEvents->OnCharacterIconReady.Broadcast (HUD 订阅此事件)
	 *
	 * 大厂原则 - 单一真理源:
	 *   - CharacterID 真理源 = ARoomPlayerState::SelectedCharacterID
	 *   - Avatar 真理源 = CharacterDataTable 行 (服务器查表)
	 *
	 * 大厂原则 - 零兜底:
	 *   - PlayerState 为空 → Log Error + return
	 *   - CharacterID 为空 → Log Error + return
	 *   - DataTable 为空 / 查不到行 → Log Error + return (不返回默认 Avatar)
	 *   - Client RPC 客户端实现里若 HUD 未就绪 → RetryRefreshCharacterIcon 重试
	 *
	 * 调用方:
	 *   - ABaseCharacter::PossessedBy (服务器, 玩家登录后)
	 *   - Server_SpawnAfterLoadout 之类的开局刷新钩子
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Icon")
	void RefreshCharacterIcon();

	/**
	 * 客户端 RPC 实现 — 接收服务器传来的头像数据
	 *
	 * 【2026.07.12 P0 重构】从 BaseCharacter 抽离
	 *
	 * 服务器调 RefreshCharacterIcon → 查表 → Client_RefreshCharacterIcon(CharID, Avatar)
	 * 客户端收到 → 缓存 + 调 CharacterEvents 广播 (HUD 订阅)
	 *
	 * 【重要】这是 Client RPC 的 _Implementation 部分。
	 *          UFUNCTION(Client, Reliable) 修饰符在 BaseCharacter::Client_RefreshCharacterIcon 上,
	 *          BaseCharacter 的转发壳会直接调本方法 (绕过 RPC 序列化).
	 *
	 * 大厂原则 - 零兜底:
	 *   - Avatar 为空 → Log Error + 不广播 (拒绝显示空白)
	 *   - 缓存 CharacterID 用于 HUD 未就绪时的 Retry
	 *
	 * @param InCharacterID  角色 ID (用于缓存 + 验证)
	 * @param Avatar        头像贴图 (服务器查表后传入, 避免客户端查表)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Icon")
	void Client_RefreshCharacterIcon_Implementation(const FString& InCharacterID, UTexture2D* Avatar);

	/**
	 * HUD 未就绪时的延迟重试
	 *
	 * 【2026.07.12 P0 重构】从 BaseCharacter 抽离
	 *
	 * 触发场景: Client_RefreshCharacterIcon_Implementation 收到 Avatar 时
	 *          HUD Widget 还没创建 (NativeConstruct 还没跑), 不能立即推 UI
	 *          → 0.5s 后重试, 直到成功 或 超过 CurrentIconRetryCount 上限
	 *
	 * 大厂原则 - 零兜底:
	 *   - 重试上限 20 次 (10s 后停止) — 防日志洪水
	 *   - 成功推 HUD 后清零计数器
	 *   - 失败超上限 → Log Warning + 停止 (不再 SetTimer)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Icon")
	void RetryRefreshCharacterIcon();

	/**
	 * 刷新武器图标到 HUD
	 *
	 * 【2026.07.12 P0 重构】从 BaseCharacter 抽离
	 *
	 * 流程:
	 *   1. 服务器: 从 ARoomPlayerState::SelectedWeaponID1 读 WeaponID
	 *   2. 服务器: 查 WeaponDataTable 拿 WeaponIcon
	 *   3. 服务器: Multicast 或 Client RPC 推客户端
	 *   4. 客户端: BroadcastWeaponIconReady → CharacterEvents 事件总线
	 *
	 * 注: 武器图标的 RPC 路径在 BaseCharacter 中保持 (武器图标走 Client RPC 即可,
	 *     不像 Multicast_NotifyKill 那么危险), 本方法只在 BaseCharacter 转发壳里被调
	 *
	 * 大厂原则 - 单一真理源:
	 *   - WeaponID 真理源 = ARoomPlayerState::SelectedWeaponID1
	 *
	 * 调用方:
	 *   - ABaseCharacter::PossessedBy (服务器, 玩家登录后)
	 *   - WeaponAttachmentComponent::OnRep_CurrentWeapon 转发 (武器切换后)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Icon")
	void RefreshWeaponIconOnHUD();

	/**
	 * 客户端 RPC 实现 — 接收服务器传来的武器图标数据
	 *
	 * 【v40.1 P0 新增】镜像 Client_RefreshCharacterIcon_Implementation
	 *
	 * 服务器调 RefreshWeaponIconOnHUD → 查表 → Client_RefreshWeaponIcon(WeaponID, Icon)
	 * 客户端收到 → 缓存 + 调 CharacterEvents 广播 (HUD 订阅)
	 *
	 * 【重要】这是 Client RPC 的 _Implementation 部分.
	 *          UFUNCTION(Client, Reliable) 修饰符在 BaseCharacter::Client_RefreshWeaponIcon 上,
	 *          BaseCharacter 的转发壳会直接调本方法 (服务器本地走本地 RPC 实现, 远端客户端走 RPC 序列化)
	 *
	 * 大厂原则 - 零兜底:
	 *   - Icon 为空 → Log Error + 不广播
	 *
	 * @param InWeaponID 武器 ID (用于缓存 + 验证)
	 * @param Icon       武器图标贴图 (服务器查表后传入, 避免客户端查 GameMode)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Icon")
	void Client_RefreshWeaponIcon_Implementation(const FString& InWeaponID, UTexture2D* Icon);

	/**
	 * 从 CharacterDataTable 查指定角色 ID 的头像贴图 (BlueprintPure)
	 *
	 * 【2026.07.12 P0 重构】从 BaseCharacter 抽离
	 *
	 * 注意: 这是服务器侧查表函数 (因为客户端拿不到 GameMode/DataTable).
	 *       服务器查好后通过 Client RPC 把贴图指针传给客户端.
	 *
	 * 大厂原则 - 零兜底:
	 *   - DataTable 为空 → Log Error + return nullptr
	 *   - 查不到行 → Log Error + return nullptr (不返回默认贴图)
	 *
	 * @param CharID 角色 ID (来自 ARoomPlayerState::SelectedCharacterID)
	 * @return 头像贴图指针 (失败返回 nullptr)
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Icon")
	UTexture2D* GetCharacterAvatarFromTable(const FString& CharID) const;

	/**
	 * 从 WeaponDataTable 查指定武器 ID 的图标贴图 (服务器侧)
	 *
	 * 【v40.1 P0 新增】镜像 GetCharacterAvatarFromTable
	 *
	 * 注意: 这是服务器侧查表函数 (因为客户端拿不到 GameMode/DataTable).
	 *       服务器查好后通过 Client RPC 把贴图指针传给客户端.
	 *
	 * 大厂原则 - 零兜底:
	 *   - DataTable 为空 → Log Error + return nullptr
	 *   - 查不到行 → Log Error + return nullptr (不返回默认贴图)
	 *
	 * @param WeaponID 武器 ID (来自 Owner->GetSpawnWeaponID())
	 * @return 武器图标贴图指针 (失败返回 nullptr)
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Icon")
	UTexture2D* GetWeaponIconFromTable(const FString& WeaponID) const;

	// ==========================================
	// UE 生命周期
	// ==========================================
public:
	/**
	 * UE 组件生命周期: 组件被注册时调用 (类似 BeginPlay)
	 * 用途: 缓存 Owner Character 引用
	 */
	virtual void BeginPlay() override;

	/**
	 * UE 组件生命周期: 组件被注销时调用 (类似 EndPlay)
	 * 用途: 清理 CharacterIconRefreshTimerHandle, 清零重试计数器, 清空缓存
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==========================================
	// 字段 — 全部 private (单一真理源 + 大厂封装)
	// ==========================================
private:
	/**
	 * 缓存角色 ID — 延迟刷新时使用 (避免循环触发服务器 RPC)
	 *
	 * 大厂原则:
	 *   - 这是"上一次请求的 CharacterID" 缓存, 不是真理源
	 *   - 真理源在 ARoomPlayerState::SelectedCharacterID
	 *   - Retry 时用本字段判断 "HUD 还没好" 时, 拿哪个 CharacterID 重试
	 */
	UPROPERTY(Transient)
	FString CachedCharacterIDForIcon;

	/**
	 * 缓存的武器 ID (供 Retry/HUD 推送使用)
	 *
	 * 【v40.1 P0 新增】镜像 CachedCharacterIDForIcon
	 *
	 * 大厂原则:
	 *   - 这是"上一次请求的 WeaponID" 缓存, 不是真理源
	 *   - 真理源在 Owner->GetSpawnWeaponID() (Replicated)
	 *   - Retry 时用本字段判断 "HUD 还没好" 时, 拿哪个 WeaponID 重试
	 */
	UPROPERTY(Transient)
	FString CachedWeaponIDForIcon;

	/**
	 * 角色图标重试计数器 — 与 HUD 重试计数器配对, 防止日志洪水
	 *
	 * 大厂原则 - 零兜底:
	 *   - 上限 20 次 (10 秒后停止)
	 *   - 成功推 HUD 后清零
	 *   - 超过上限 → Log Warning + 停止 SetTimer (不静默继续重试)
	 */
	UPROPERTY(Transient)
	int32 CurrentIconRetryCount = 0;

	/**
	 * 角色图标刷新延迟重试定时器句柄
	 *
	 * 大厂原则 - 防御型清理:
	 *   - EndPlay 显式 ClearTimer
	 *   - 成功推送后 ClearTimer + 清计数器
	 */
	FTimerHandle CharacterIconRefreshTimerHandle;

	/**
	 * 重试间隔 (秒) — 默认 0.5s
	 *
	 * 大厂原则 - 单一真理源:
	 *   - 整个工程所有"延迟重试 HUD"都走 0.5s, 避免散落各处
	 *   - 如果未来需要动态调整, 改成 UPROPERTY(EditDefaultsOnly) 让策划在 BP 配
	 */
	static constexpr float IconRefreshRetryIntervalSeconds = 0.5f;

	/**
	 * 重试上限 — 默认 20 次 (10s 后停止)
	 *
	 * 大厂原则 - 零兜底:
	 *   - 超过即停, 防止日志洪水
	 */
	static constexpr int32 MaxIconRefreshRetryCount = 20;

	/**
	 * Owner Character 缓存 (避免每次访问 Owner 时 Cast)
	 *
	 * 大厂原则 - 缓存友好:
	 *   - BeginPlay 缓存一次, 后续访问 O(1)
	 *   - EndPlay 清空 (Owner 销毁时)
	 *
	 * 不需要 UPROPERTY 修饰 — Owner 通过 GetOwner() 保证生命周期
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ABaseCharacter> OwnerCharacter;

	// ==========================================
	// 内部辅助方法 (private)
	// ==========================================
private:
	/**
	 * 通过 Owner Character 路径查找 CharacterDataTable (服务器侧)
	 *
	 * 路径: Owner->GetWorld()->GetAuthGameMode<ARoomGameMode>()->CharacterDataTable
	 *
	 * 大厂原则 - 零兜底:
	 *   - GameMode 为空 → Log Error + return nullptr
	 *   - DataTable 为空 → Log Error + return nullptr
	 *
	 * @return CharacterDataTable 指针 (失败返回 nullptr)
	 */
	UDataTable* ResolveCharacterDataTable() const;

	/**
	 * 通过 Owner Character 路径查找 WeaponDataTable (服务器侧)
	 *
	 * 路径: Owner->GetWorld()->GetAuthGameMode<ARoomGameMode>()->WeaponDataTable
	 *
	 * 大厂原则 - 零兜底:
	 *   - GameMode 为空 → Log Error + return nullptr
	 *   - DataTable 为空 → Log Error + return nullptr
	 *
	 * @return WeaponDataTable 指针 (失败返回 nullptr)
	 */
	UDataTable* ResolveWeaponDataTable() const;

	/**
	 * 通过 Owner Character 路径查找 ARoomPlayerState
	 *
	 * 大厂原则 - 零兜底:
	 *   - PlayerState 为空 → Log Error + return nullptr
	 *   - 类型不匹配 → Log Error + return nullptr
	 *
	 * @return ARoomPlayerState 指针 (失败返回 nullptr)
	 */
	class ARoomPlayerState* ResolvePlayerState() const;

	/**
	 * 广播"角色头像就绪"事件到 CharacterEvents (BaseCharacter 上的事件总线)
	 *
	 * 调用链:
	 *   本方法 → Owner->CharacterEvents->OnCharacterIconReady.Broadcast(CharID, Icon)
	 *   → GameHUDWidget 订阅此事件 → 更新 UI
	 *
	 * 大厂原则 - 依赖倒置:
	 *   - 不直接 Push 到 GameHUDWidget (避免循环依赖)
	 *   - 通过 CharacterEvents 事件总线广播, UI 层订阅
	 *
	 * @param CharID 角色 ID
	 * @param Icon   头像贴图 (nullptr 也广播, 由订阅方决定是否处理)
	 */
	void BroadcastCharacterIconReady(const FString& CharID, UTexture2D* Icon);

	/**
	 * 广播"武器图标就绪"事件到 CharacterEvents
	 *
	 * 同 BroadcastCharacterIconReady, 但事件是武器图标专用
	 *
	 * @param WeaponID 武器 ID
	 * @param Icon     武器图标贴图
	 */
	void BroadcastWeaponIconReady(const FString& WeaponID, UTexture2D* Icon);
};