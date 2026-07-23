// ==========================================
// 版权声明：在项目设置的描述页面填写您的版权信息。
// ==========================================
//
// @file CharacterIconComponent.cpp
// @brief 角色头像/武器图标刷新子系统实现 — 从 ABaseCharacter 抽离
//
// 【2026.07.12 P0 大厂重构】
//   BaseCharacter.cpp 太大(3957 行), 单文件维护成本爆炸.
//   本文件完整实现"角色头像加载 + 武器图标加载 + HUD 推送"逻辑, 与 BaseCharacter 解耦.
//
// 拆分来源:
//   - BaseCharacter.cpp 第 795-835 行 (RefreshCharacterIcon)
//   - BaseCharacter.cpp 第 852-876 行 (Client_RefreshCharacterIcon_Implementation)
//   - BaseCharacter.cpp 第 887-971 行 (RetryRefreshCharacterIcon)
//   - BaseCharacter.cpp 第 981-1006 行 (RefreshWeaponIconOnHUD)
//   - BaseCharacter.cpp 第 1015-1031 行 (GetCharacterAvatarFromTable)
//   - BaseCharacter.cpp 第 3701-3712 行 (BroadcastCharacterIconReady)
//   - BaseCharacter.cpp 第 3759-3766 行 (BroadcastWeaponIconReady)
//
// 【大厂原则 (Phase 2 落地)】
//   - 单一真理源: CharacterID/WeaponID 在 ARoomPlayerState, Avatar 贴图在 CharacterDataTable
//   - 零兜底: 缺 DataTable/PS/查不到行 → Log Error + return (不静默返回默认)
//   - 职责分离: 只管"图标从哪里来 + 怎么推到 UI", HUD 实例化仍由调用方负责
//   - 集中调度: 所有图标刷新都走本组件, BaseCharacter 改为转发壳
// ==========================================

// ==========================================
// 头文件包含区
// ==========================================
// 引入本组件头文件
#include "Combat/CharacterIconComponent.h"

// 引入 Owner Character (用于访问 PlayerState / GameMode / CharacterEvents)
#include "Characters/BaseCharacter.h"

// 引入 PlayerState (查 CharacterID/WeaponID 真理源)
#include "Systems/Core/RoomPlayerState.h"

// 引入 ActorIterator (用于遍历 World 中的 Character)
#include "EngineUtils.h"

// 引入 GameMode (查 CharacterDataTable/WeaponDataTable)
#include "Systems/RoomGameMode.h"

// 引入 DataTable 行结构体 (FCharacterInfo / FWeaponInfo)
#include "Data/Tables/CharacterTableRow.h"
#include "Data/Tables/WeaponTableRow.h"

// 引入 CharacterEvents 事件总线组件 (广播 OnCharacterIconReady/OnWeaponIconReady)
#include "Components/CharacterEvents.h"

// 引入贴图指针类型
#include "Engine/Texture2D.h"

// 引入 WeaponFireComponent (弹药数据真理源 — v84 大厂架构新增)
#include "Components/WeaponFireComponent.h"

// 引入 TimerManager (SetTimer / ClearTimer 用于重试机制)
#include "TimerManager.h"

// 引入 World 相关 (GetWorld / GetAuthGameMode)
#include "Engine/World.h"
#include "Engine/Engine.h"

// 引入 GameModeBase (用于 GetAuthGameMode 类型转换)
#include "GameFramework/GameModeBase.h"


// ==========================================
// 1. 构造函数
// ==========================================

/**
 * UCharacterIconComponent 构造函数
 *
 * 目的: 启用 Tick (本组件不需要 Tick, 但保留扩展位);
 *       初始化字段默认值 (UE 反射默认值)。
 *
 * 设计要点:
 *   - PrimaryComponentTick.bCanEverTick = false (图标刷新由 RPC 触发, 不需要每帧检查)
 *   - 所有字段在头文件已给默认值, 构造函数不必重复赋值
 *   - CachedCharacterIDForIcon = "" (无缓存)
 *   - CurrentIconRetryCount = 0 (无重试历史)
 */
UCharacterIconComponent::UCharacterIconComponent()
{
	// 组件本身不 Tick — 图标刷新由 RPC 触发, 不需要每帧检查
	PrimaryComponentTick.bCanEverTick = false;
}


// ==========================================
// 2. UE 生命周期
// ==========================================

/**
 * UCharacterIconComponent::ResolveOwnerCharacter
 *
 * 【v84 大厂架构】使用 GetTypedOuter 获取 Owner Character
 *
 * 使用 GetTypedOuter 而非 GetOwner() 来获取 Owner:
 *   - GetTypedOuter 查找包含此组件的 Actor，无论它是否被正确标记为 Owner
 *   - 比 GetOwner() 更可靠，避免 CDO 问题
 *
 * @return Owner Character 指针 (失败返回 nullptr 并 Log Error)
 */
ABaseCharacter* UCharacterIconComponent::ResolveOwnerCharacter() const
{
	// 【v84 最终修复】使用 TActorIterator 遍历 World 获取有效的 Owner
	// 
	// 根因分析:
	//   - BeginPlay 中 GetOwner()=BP_SWAT_C_0 (正确)
	//   - 但 ResolvePlayerState 中 GetOwner()=Default__BaseCharacter (CDO)
	//   - 这意味着 this 指针指向的是 CDO 上的组件实例
	//
	// 解决方案:
	//   - 不依赖 this 的 Owner 关系
	//   - 使用 TActorIterator 遍历 World，找到拥有这个组件的 ABaseCharacter
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] ResolveOwnerCharacter: GetWorld() 返回 nullptr."));
		return nullptr;
	}

	// 遍历所有 ABaseCharacter，找到持有这个组件实例的
	for (TActorIterator<ABaseCharacter> It(World); It; ++It)
	{
		ABaseCharacter* Char = *It;
		if (!Char || !Char->IsValidLowLevel())
		{
			continue;
		}

		// 检查这个 Character 是否拥有这个组件（使用地址比较）
		UCharacterIconComponent* Found = Char->FindComponentByClass<UCharacterIconComponent>();
		if (Found == this)
		{
			return Char;
		}
	}

	// 【最终兜底】尝试从 GetOwner() 获取，并验证不是 CDO
	AActor* OwnerActor = GetOwner();
	if (OwnerActor)
	{
		// 检查是否为 CDO (Class Default Object)
		const bool bIsCDO = OwnerActor->GetName().Contains(TEXT("Default__"));
		if (!bIsCDO)
		{
			return Cast<ABaseCharacter>(OwnerActor);
		}
	}

	UE_LOG(LogTemp, Error,
		TEXT("[CharacterIconComponent] ResolveOwnerCharacter: 无法找到有效的 Owner Character. "
			 "this=%p, GetOwner()=%s. "
			 "【根因】组件实例附加到了错误的 Actor (可能是 CDO). "
			 "【修复】检查 BP_SWAT_C Components 面板，确保 CharacterIcon 组件没有被 BP 实例化覆盖."),
		(void*)this,
		OwnerActor ? *OwnerActor->GetName() : TEXT("<null>"));
	return nullptr;
}

/**
 * UCharacterIconComponent::BeginPlay
 *
 * 简单记录 BeginPlay 执行
 */
void UCharacterIconComponent::BeginPlay()
{
	Super::BeginPlay();

	// 简单记录 BeginPlay 执行
	UE_LOG(LogTemp, Log,
		TEXT("[CharacterIconComponent] BeginPlay: Owner=%s"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("<null>"));

	// 【v85 大厂架构新增】订阅激活武器的弹药变化事件
	// 根因: 开火消耗弹药后，弹夹数量必须实时更新到 HUD
	// 解决方案: CharacterIconComponent 订阅 WeaponFireComponent::OnAmmoChanged，
	//           弹药变化时自动广播弹夹信息到 HUD
	ABaseCharacter* OwnerChar = ResolveOwnerCharacter();
	if (!OwnerChar)
	{
		return;
	}

	// 订阅当前武器的弹药变化
	SubscribeToActiveWeaponAmmo();

	// 【v85.3 大厂架构重构】服务器/客户端分流
	//   服务器 BeginPlay 时 CurrentWeapon 已存在 (Server_SpawnAllWeapons 先于 BeginPlay),
	//   服务器立即 BroadcastWeaponAmmoInfo → 触发 RPC 推弹药数据 + 本地缓存/Broadcast
	//   客户端 BeginPlay 时 CurrentWeapon 还是 None (OnRep_CurrentWeapon 还没触发),
	//   客户端不能直接调 BroadcastWeaponAmmoInfo (SERVER ONLY — HasAuthority 检查会拦截),
	//   客户端弹药数据来源 = OnRep_CurrentWeapon 末尾的 SubscribeToActiveWeaponAmmo
	//                    + 服务器 SpawnAllWeapons RPC 推送的 Client_RefreshWeaponAmmo
	if (OwnerChar->HasAuthority())
	{
		BroadcastWeaponAmmoInfo(OwnerChar);
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[CharacterIconComponent] BeginPlay: 客户端路径, 不调 BroadcastWeaponAmmoInfo. "
				 "Owner='%s'. 弹药来源 = Server RPC (Server_SpawnAllWeapons/SwitchToWeaponSlot 末尾) + "
				 "OnRep_CurrentWeapon 重新订阅 + OnWeaponAmmoChanged (开火实时)."),
			*OwnerChar->GetName());
	}
}


// -----------------------------------------------------------------------------
// 【v85 大厂架构新增】订阅当前激活武器的弹药变化事件
// 用于 BeginPlay 初始化，以及武器切换后重新订阅
// -----------------------------------------------------------------------------
void UCharacterIconComponent::SubscribeToActiveWeaponAmmo()
{
	ABaseCharacter* OwnerChar = ResolveOwnerCharacter();
	if (!OwnerChar)
	{
		return;
	}

	// 1. 取消旧武器的订阅
	if (SubscribedWeaponFireComponent.IsValid())
	{
		SubscribedWeaponFireComponent->OnAmmoChanged.RemoveDynamic(this, &UCharacterIconComponent::OnWeaponAmmoChanged);
		SubscribedWeaponFireComponent.Reset();
	}

	// 2. 订阅新武器
	ABaseWeapon* ActiveWeapon = OwnerChar->GetCurrentWeapon();
	if (ActiveWeapon)
	{
		if (UWeaponFireComponent* FireComp = ActiveWeapon->FindComponentByClass<UWeaponFireComponent>())
		{
			FireComp->OnAmmoChanged.AddDynamic(this, &UCharacterIconComponent::OnWeaponAmmoChanged);
			SubscribedWeaponFireComponent = FireComp;
			UE_LOG(LogTemp, Log,
				TEXT("[CharacterIconComponent] SubscribeToActiveWeaponAmmo: 订阅 %s 的 OnAmmoChanged"),
				*ActiveWeapon->GetName());
		}
	}
}


/**
 * UCharacterIconComponent::EndPlay
 *
 * 清理 CharacterIconRefreshTimerHandle + 清零重试计数器 + 清空缓存
 *
 * 大厂原则 - 防御型清理:
 *   - UE 内部虽会自动清理 Timer, 但显式 ClearTimer 是工业级规范
 *   - 防止边缘 case: 组件在 Character 销毁时还有 Timer 排队
 *   - 清空缓存防止 EndPlay 后回调意外触发
 */
void UCharacterIconComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 【v85 大厂架构新增】取消武器弹药变化订阅
	if (SubscribedWeaponFireComponent.IsValid())
	{
		SubscribedWeaponFireComponent->OnAmmoChanged.RemoveDynamic(this, &UCharacterIconComponent::OnWeaponAmmoChanged);
		SubscribedWeaponFireComponent.Reset();
	}

	if (UWorld* World = GetWorld())
	{
		// 显式清理重试 Timer — 防残留
		World->GetTimerManager().ClearTimer(CharacterIconRefreshTimerHandle);
	}

	// 清理组件持有的资源
	CachedCharacterIDForIcon.Empty();
	CurrentIconRetryCount = 0;

	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 3. 公开 API — BaseCharacter 转发入口
// ==========================================

/**
 * RefreshCharacterIcon — 服务器主入口
 *
 * 【2026.07.12 P0 重构】从 BaseCharacter 抽离
 *
 * 完整流程:
 *   1. 服务器: 从 ARoomPlayerState::SelectedCharacterID 读 CharacterID
 *   2. 服务器: 调 ResolveCharacterDataTable 拿 DataTable, FindRow 拿 Avatar
 *   3. 服务器: 调 Client_RefreshCharacterIcon_Implementation (绕过 RPC, 直接传贴图)
 *      注: RPC 修饰符在 BaseCharacter::Client_RefreshCharacterIcon 上, 这里走本地调用
 *          (服务器自己也是一台机器, 直接本地调用即可, 不需要真的走 RPC)
 *   4. 客户端: 缓存 CharacterID + 调 BroadcastCharacterIconReady
 *
 * 大厂原则 - 单一真理源:
 *   - CharacterID 真理源 = ARoomPlayerState::SelectedCharacterID
 *   - Avatar 真理源 = CharacterDataTable 行 (服务器查表)
 *
 * 大厂原则 - 零兜底:
 *   - PlayerState 为空 → Log Error + return
 *   - CharacterID 为空 → Log Error + return
 *   - DataTable 为空 → Log Error + return
 *   - 查不到行 → Log Error + return (不返回默认 Avatar)
 */
void UCharacterIconComponent::RefreshCharacterIcon()
{
	// ============================================================
	// 前置检查: Owner 必须有效 【v84 使用 ResolveOwnerCharacter】
	// ============================================================
	ABaseCharacter* Owner = ResolveOwnerCharacter();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] RefreshCharacterIcon: Owner Character 已失效. "
				 "组件可能在 Owner 销毁后被外部调用, 这是 API 误用."));
		return;
	}

	// ============================================================
	// Step 1: 从 ARoomPlayerState::SelectedCharacterID 读 CharacterID
	// ============================================================
	ARoomPlayerState* RoomPS = ResolvePlayerState();
	if (!RoomPS)
	{
		// ResolvePlayerState 内部已 Log Error, 这里直接 return
		return;
	}

	const FString CharID = RoomPS->GetSelectedCharacterID();

	// ============================================================
	// Step 2: 校验 CharacterID 非空 (大厂原则 - 零兜底)
	// ============================================================
	if (CharID.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] RefreshCharacterIcon: CharID 为空. "
				 "Player '%s' 没有从 PlayerState 读到 SelectedCharacterID. "
				 "根因: ARoomPlayerState 没复制到位 (检查 PS.OnRep 链路) "
				 "或 RoomGameMode.PostLogin 没调 InitPlayerState. "
				 "【v32 零兜底】拒绝显示默认头像, 强制修复配置. 【大厂原则】不静默兼容."),
			*Owner->GetName());
		return;
	}

	// ============================================================
	// Step 3: 服务器查表拿 Avatar
	// ============================================================
	UTexture2D* Avatar = GetCharacterAvatarFromTable(CharID);

	// ============================================================
	// Step 4: 通知所属客户端刷新头像 (RPC 走 Actor 层, 不能直接调 Component.Implementation)
	// ============================================================
	// 【v40.1 P0 修复】必须调 Owner->Client_RefreshCharacterIcon (Actor 层 RPC),
	//                  不能直接调 Client_RefreshCharacterIcon_Implementation (Component 层).
	//   根因: Component 不是 Actor, RPC 序列化必须在 Actor 上. 旧版直接调 Implementation
	//         完全绕过 RPC → 服务器本地能跑 (ListenServer 同进程直接调用 Implementation),
	//         但普通客户端 Pawn 上 Client RPC 收不到 → 头像永远不显示
	//   修复: 调 Owner->Client_RefreshCharacterIcon(CharID, Avatar) 触发真正的 RPC,
	//         服务器自己的 Pawn 走本地 Implementation, 远端客户端的 Pawn 走 RPC 序列化
	//   大厂原则 - 职责分层: RPC 必须在 Actor, Component 只负责业务逻辑
	if (Owner)
	{
		Owner->Client_RefreshCharacterIcon(CharID, Avatar);

		// [v40.1 P0 修复] 服务器同时触发武器图标刷新 (走 RPC 路径)
		//   旧版: RefreshCharacterIcon 只发头像 RPC, RefreshWeaponIconOnHUD 由 Client_RefreshCharacterIcon_Implementation 末尾触发
		//         → 客户端 Implementation 末尾 RefreshWeaponIconOnHUD 走 else 分支 (HasAuthority=false) → Client_RefreshWeaponIcon_Implementation(WeaponID, nullptr)
		//         → Icon=nullptr → 客户端 HUD UpdateWeaponIconFromID 查 AuthGameMode=nullptr → 武器图标永远不显示
		//   新版: 服务器 RefreshCharacterIcon 内显式调 RefreshWeaponIconOnHUD → 服务器 HasAuthority=true → 查表 → Client_RefreshWeaponIcon(WeaponID, Icon) RPC 推完整 Icon
		//         → Client_RefreshCharacterIcon_Implementation 末尾不再调 RefreshWeaponIconOnHUD (避免双重 Refresh, 且永远拿不到 Icon)
		RefreshWeaponIconOnHUD();
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] RefreshCharacterIcon: Owner 失效, 无法触发 Client RPC. "
				 "CharID=%s."),
			*CharID);
	}
}


/**
 * Client_RefreshCharacterIcon_Implementation — 客户端接收头像数据
 *
 * 【2026.07.12 P0 重构】从 BaseCharacter 抽离
 *
 * 服务器调 RefreshCharacterIcon → 查表 → Client_RefreshCharacterIcon(CharID, Avatar)
 * 客户端收到 → 缓存 + 调 CharacterEvents 广播 (HUD 订阅)
 *
 * 注: 这是 RPC _Implementation 部分, 由 BaseCharacter::Client_RefreshCharacterIcon 转发
 *     调用方也可以绕过 RPC 直接调本方法 (服务器本地就是这种用法)
 *
 * 大厂原则 - 零兜底:
 *   - Avatar 为空 → Log Error + 不广播 (拒绝显示空白)
 *   - 缓存 CharacterID 用于 HUD 未就绪时的 Retry
 *
 * @param InCharacterID  角色 ID (用于缓存 + 验证)
 * @param Avatar        头像贴图 (服务器查表后传入, 避免客户端查表)
 */
void UCharacterIconComponent::Client_RefreshCharacterIcon_Implementation(const FString& InCharacterID, UTexture2D* Avatar)
{
	// ============================================================
	// 前置检查: Owner 必须有效 【v84 使用 ResolveOwnerCharacter】
	// ============================================================
	ABaseCharacter* Owner = ResolveOwnerCharacter();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] Client_RefreshCharacterIcon_Implementation: Owner Character 已失效."));
		return;
	}

	// ============================================================
	// 【P0 双保险】: 必须是本机玩家才允许改本机 HUD
	// Client RPC 按 UE 语义只在所属客户端上执行,
	// 加 IsLocallyControlled() 是双保险, 防止未来 UE 语义变化或被意外 Multicast 时污染 HUD
	// ============================================================
	if (!Owner->IsLocallyControlled())
	{
		return;
	}

	// ============================================================
	// 缓存 ID, 延迟刷新时使用
	// ============================================================
	CachedCharacterIDForIcon = InCharacterID;

	// ============================================================
	// Avatar 为空 → Log Error (大厂原则 - 零兜底, 拒绝显示空白)
	// ============================================================
	if (!Avatar)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] Client_RefreshCharacterIcon_Implementation: Avatar 为空. "
				 "CharID='%s', Owner='%s'. "
				 "根因: 服务器 GetCharacterAvatarFromTable 查表失败 (DataTable 空 或 行不存在). "
				 "【大厂原则】拒绝显示空白, 强制修复 CharacterDataTable 配置."),
			*InCharacterID, *Owner->GetName());
		// 不 return — 仍然尝试 Retry, 因为 Avatar 可能在下一次查表时恢复
	}

	// ============================================================
	// HUD 未就绪 → 延迟重试 (保留重试机制, CharacterEvents 组件本身也受 HUD 生命周期影响)
	// ============================================================
	if (!Owner->TryResolveHUDWidget(/*bLogWarning=*/false))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CharacterIconComponent] HUD 未就绪, 延迟刷新, InCharacterID=%s"),
			*InCharacterID);

		UWorld* World = Owner->GetWorld();
		if (World)
		{
			World->GetTimerManager().SetTimer(
				CharacterIconRefreshTimerHandle,
				this,
				&UCharacterIconComponent::RetryRefreshCharacterIcon,
				IconRefreshRetryIntervalSeconds,
				/*bLoop=*/false);
		}
		return;
	}

	// ============================================================
	// 通过 CharacterEvents 广播头像事件 (替代直接 GameHUDWidget Push)
	// ============================================================
	BroadcastCharacterIconReady(InCharacterID, Avatar);

	// [v40.1 P0 修复] 末尾不再调 RefreshWeaponIconOnHUD
	//   旧版: Client_RefreshCharacterIcon_Implementation 末尾调 RefreshWeaponIconOnHUD
	//         → 客户端 HasAuthority=false → Client_RefreshWeaponIcon_Implementation(WeaponID, nullptr)
	//         → 客户端拿不到 Icon → HUD 永远显示空白
	//   新版: RefreshCharacterIcon 内服务器侧显式调 RefreshWeaponIconOnHUD (走 RPC 路径推 Icon)
	//         → 客户端 Implementation 收到 Client_RefreshWeaponIcon RPC 时 Icon 已经传过来
}


/**
 * RetryRefreshCharacterIcon — HUD 未就绪时的延迟重试
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
void UCharacterIconComponent::RetryRefreshCharacterIcon()
{
	// ============================================================
	// 【P0 防御 1】: Character 或 World 已失效 → 停止重试 【v84 使用 ResolveOwnerCharacter】
	// ============================================================
	ABaseCharacter* Owner = ResolveOwnerCharacter();
	UWorld* World = GetWorld();
	if (!Owner || !IsValid(Owner) || !World)
	{
		return;
	}

	// ============================================================
	// 【P0 防御 2】: 仅本机玩家需要刷新 HUD
	// 与 Client_RefreshCharacterIcon_Implementation 保持一致, 防止非本地玩家的
	// 定时器意外跑起来污染本机 HUD
	// ============================================================
	if (!Owner->IsLocallyControlled())
	{
		return;
	}

	// ============================================================
	// 【P0 防御 3】: 最大重试次数限制 (大厂原则 - 防日志洪水)
	// ============================================================
	CurrentIconRetryCount++;
	if (CurrentIconRetryCount >= MaxIconRefreshRetryCount)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] RetryRefreshCharacterIcon 达到最大重试次数 (%d 次), 放弃. "
				 "Owner='%s', CachedID='%s'. "
				 "根因: HUD Widget 持续不可用, 持续 10s. "
				 "检查 GameHUDWidget 是否在 PIE 启动时正确创建."),
			CurrentIconRetryCount, *Owner->GetName(), *CachedCharacterIDForIcon);
		CurrentIconRetryCount = 0;
		World->GetTimerManager().ClearTimer(CharacterIconRefreshTimerHandle);
		return;
	}

	// ============================================================
	// HUD 仍未就绪 → 继续重试
	// ============================================================
	if (!Owner->TryResolveHUDWidget(/*bLogWarning=*/false))
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[CharacterIconComponent] HUD 未就绪, 重试中... (%d/%d), CachedID=%s"),
			CurrentIconRetryCount, MaxIconRefreshRetryCount, *CachedCharacterIDForIcon);

		World->GetTimerManager().SetTimer(
			CharacterIconRefreshTimerHandle,
			this,
			&UCharacterIconComponent::RetryRefreshCharacterIcon,
			IconRefreshRetryIntervalSeconds,
			/*bLoop=*/false);
		return;
	}

	// ============================================================
	// HUD 已就绪 → 直接刷新 (使用缓存的 ID)
	// ============================================================
	UE_LOG(LogTemp, Log,
		TEXT("[CharacterIconComponent] 延迟重试刷新角色图标和武器图标, CachedID=%s (重试 %d 次后成功)"),
		*CachedCharacterIDForIcon, CurrentIconRetryCount);

	CurrentIconRetryCount = 0;  // 成功 → 重置计数器

	// ============================================================
	// 【v32 零兜底改造】单一缓存路径 — 从 CharacterEvents 取缓存的 Avatar
	//
	// 历史 (v22-v31.x) 反模式:
	//   Layer 1: CharacterEvents 缓存
	//   Layer 2 (兜底): 客户端 GetCharacterAvatarFromTable (永远 nullptr, 必然失败)
	//   = 反向承认兜底永远失败仍保留
	//
	// 新架构 (v32 — 单一缓存路径):
	//   - 只走 CharacterEvents 缓存 (服务器权威, RPC 同步给客户端)
	//   - 缓存为空 → Log Error + 跳过广播 (强制修复 Server-Client 同步链)
	// ============================================================
	FString CachedID;
	UTexture2D* CachedAvatar = nullptr;

	// [v40 P0 修复] 必须用 ResolveCharacterEvents() 而非裸字段访问 CharacterEvents
	if (UCharacterEvents* EventsComp = Owner->ResolveCharacterEvents())
	{
		if (EventsComp->GetCachedCharacterIcon(CachedID, CachedAvatar) && CachedAvatar)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[CharacterIconComponent] RetryRefreshCharacterIcon: 从 CharacterEvents 缓存取 Avatar, "
					 "CachedID=%s, Avatar=%s"),
				*CachedID, *CachedAvatar->GetName());
			BroadcastCharacterIconReady(CachedID, CachedAvatar);

			// [v40.1 P0 修复] Retry 不再调 RefreshWeaponIconOnHUD (避免客户端 else 分支问题)
			//   武器图标链路: RefreshCharacterIcon → 服务器 RefreshWeaponIconOnHUD → RPC 推 Icon
			//   Retry 仅补发头像, 武器图标已由服务器 RefreshCharacterIcon 链路完成
			return;
		}
	}

	// ============================================================
	// 【v32 零兜底】缓存为空 → Log Error + 不广播
	// ============================================================
	UE_LOG(LogTemp, Error,
		TEXT("[CharacterIconComponent][RetryRefreshCharacterIcon] Pawn='%s' CharacterEvents 缓存为空! "
			 "根因: 服务器 BroadcastCharacterIconReady 没在客户端到达前触发, "
			 "或 CharacterEvents Component 没挂, 或缓存查找不到 CachedID=%s. "
			 "【v32 零兜底】拒绝客户端查表兜底, 强制修复 Server-Client Avatar 同步链. "
			 "【大厂原则】不静默广播空 Avatar."),
		*Owner->GetName(), *CachedCharacterIDForIcon);
}


/**
 * RefreshWeaponIconOnHUD — 刷新武器图标到 HUD
 *
 * 【v36 显式优于隐式】改读 Owner.SpawnWeaponID (Replicated 真理源), 不再走 GM 缓存兜底
 *
 * 旧版 (v32-v35) 反模式:
 *   - 优先从 GM->GetPlayerSpawnData 缓存读 (绕过 PS 复制时序)
 *   - 这是典型的"显式覆盖"反模式 — 用 GM 缓存兜底, 把配置错掩盖
 *   - WeaponID 为空 → 静默 return (注释自辩"武器图标是 best-effort")
 *
 * 新架构 (v36 — 单一真理源):
 *   - 真理源 = Owner->GetSpawnWeaponID() (已 Replicated, 自动同步)
 *   - 客户端通过 Replicate 自动拿到 SpawnWeaponID (跟 OnRep_CurrentWeapon 同步触发)
 *   - 不读 GM 缓存 (GM 缓存只是临时容错, 不是真理)
 *   - WeaponID 空 → Log Warning (跳过图标), 不静默 (配置错必须可观测)
 *
 * 流程:
 *   1. 服务器: 读 Owner->GetSpawnWeaponID() (Replicated)
 *   2. 服务器: 查 WeaponDataTable 拿 WeaponIcon
 *   3. 服务器: 通过 CharacterEvents 广播武器图标事件
 *
 * 调用方:
 *   - ABaseCharacter::PossessedBy (服务器, 玩家登录后)
 *   - WeaponAttachmentComponent::OnRep_CurrentWeapon 转发 (武器切换后)
 */
void UCharacterIconComponent::RefreshWeaponIconOnHUD()
{
	// ============================================================
	// 前置检查: Owner 必须有效 【v84 使用 ResolveOwnerCharacter】
	// ============================================================
	ABaseCharacter* Owner = ResolveOwnerCharacter();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] RefreshWeaponIconOnHUD: Owner Character 已失效."));
		return;
	}

	// ============================================================
	// 【v36 单一真理源】从 Owner->GetSpawnWeaponID() 读 (已 Replicated)
	// ============================================================
	const FString WeaponID = Owner->GetSpawnWeaponID();

	// ============================================================
	// WeaponID 为空 → Log Warning + return (大厂原则: 配置错必须可观测)
	// ============================================================
	if (WeaponID.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CharacterIconComponent] RefreshWeaponIconOnHUD: Pawn=%s 的 SpawnWeaponID 为空, 跳过武器图标刷新. "
				 "【v36 零兜底】不静默. "
				 "【v54.3 改名提示】2) 关卡预放 AI: 检查 DA_AIBehaviorConfig_*.LevelPlacedWeaponClass 未配. "
				 "【根因排查】1) PlayerState.SelectedWeapon1ID 未写入 2) 关卡预放 AI 的 DA LevelPlacedWeaponClass 未配 3) PossessedBy 时序问题."),
			*Owner->GetName());
		return;
	}

	// ============================================================
	// 【v40.1 P0 修复】服务器查表拿 Icon, 走 Client RPC 推给客户端
	//   根因: 旧版直接 BroadcastWeaponIconReady(WeaponID, nullptr) — 客户端 CharacterEvents
	//         不复制, 客户端 HUD 收到 Icon=nullptr 后走 UpdateWeaponIconFromID 查表
	//         客户端调 GetAuthGameMode() 返回 nullptr → 永远查不到 → 武器图标永远不显示
	//   修复: 服务器查表拿 Icon, 走 Client_RefreshWeaponIcon RPC 推给客户端
	//         客户端 Implementation 收到 Icon 直接广播给本地 HUD, 不需要再查表
	//   大厂原则 - 单一真理源: 武器图标真理源 = WeaponDataTable 行 (服务器查表)
	// ============================================================
	if (Owner->HasAuthority())
	{
		UTexture2D* Icon = GetWeaponIconFromTable(WeaponID);
		// Icon 可能为 nullptr (DataTable 配错), Implementation 内部会 Log Error 报告根因
		Owner->Client_RefreshWeaponIcon(WeaponID, Icon);

		// ============================================================
		// 【v84 大厂架构新增】同时广播武器弹药信息 (与图标同步)
		//   大厂原则 - 单一真理源:
		//     - 弹药真理源 = WeaponFireComponent.CurrentAmmo / MagazineSize / ReserveAmmo
		//     - CharacterIconComponent 读取并封装为文本, 通过 CharacterEvents 推送给 HUD
		//   调用方:
		//     - 服务器 RefreshWeaponIconOnHUD (武器图标刷新时同步刷新弹药)
		// ============================================================
		BroadcastWeaponAmmoInfo(Owner);
	}
	else
	{
		// [v40.1 P0 修复] 客户端分支不再直接调 Implementation 推 nullptr
		//   根因: 客户端没服务器查表能力 → Implementation 收到 nullptr → HUD UpdateWeaponIconFromID 失败
		//   新版: 客户端武器图标完全依赖服务器 RefreshCharacterIcon 链路 RPC 推过来的 Client_RefreshWeaponIcon
		//         如果走到这里说明服务器 RefreshCharacterIcon 没调 RefreshWeaponIconOnHUD
		//         → 这是一个 bug, 应该 Log Error 强制修复
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] RefreshWeaponIconOnHUD: 客户端不应该走这条路. "
				 "Pawn=%s, WeaponID=%s. "
				 "【v40.1 修复】服务器 RefreshCharacterIcon 链路应该已经触发 Client_RefreshWeaponIcon RPC. "
				 "若出现, 检查 RefreshCharacterIcon 末尾是否调了 RefreshWeaponIconOnHUD."),
			*Owner->GetName(), *WeaponID);
	}
}


/**
 * GetCharacterAvatarFromTable — 从 CharacterDataTable 查指定角色 ID 的头像贴图
 *
 * 【2026.07.12 P0 重构】从 BaseCharacter 抽离 (改为 const)
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
UTexture2D* UCharacterIconComponent::GetCharacterAvatarFromTable(const FString& CharID) const
{
	// ============================================================
	// CharID 为空 → 直接返回 nullptr (Log Warning, 不 Log Error)
	// ============================================================
	if (CharID.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CharacterIconComponent] GetCharacterAvatarFromTable: CharID 为空, 直接返回 nullptr."));
		return nullptr;
	}

	// ============================================================
	// 通过 GameMode 获取数据表 (大厂原则 - 零兜底)
	// ============================================================
	UDataTable* CharacterDataTable = ResolveCharacterDataTable();
	if (!CharacterDataTable)
	{
		// ResolveCharacterDataTable 内部已 Log Error
		return nullptr;
	}

	// ============================================================
	// 查表 (使用 FName ContextString 便于调试追踪)
	// ============================================================
	static const FString ContextString(TEXT("CharacterAvatarLookup"));
	FCharacterInfo* Info = CharacterDataTable->FindRow<FCharacterInfo>(FName(*CharID), ContextString);

	// ============================================================
	// 查表结果校验 (大厂原则 - 零兜底)
	// ============================================================
	if (!Info)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] GetCharacterAvatarFromTable: 查不到 CharID='%s' 在 CharacterDataTable. "
				 "根因: DT_CharacterList 没有这个 RowName, 或 RowName 拼写错误. "
				 "【大厂原则】拒绝返回默认贴图, 强制修复 DataTable 配置."),
			*CharID);
		return nullptr;
	}

	if (!Info->AvatarIcon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] GetCharacterAvatarFromTable: CharID='%s' 行的 AvatarIcon 字段为空. "
				 "根因: DT_CharacterList 里这行的 AvatarIcon 没配. "
				 "【大厂原则】拒绝返回 nullptr 兜底, 强制修复 DataTable."),
			*CharID);
		return nullptr;
	}

	return Info->AvatarIcon;
}


// ==========================================
// 4. 内部辅助方法 — private
// ==========================================

/**
 * ResolveCharacterDataTable — 通过 Owner Character 路径查找 CharacterDataTable (服务器侧)
 *
 * 路径: Owner->GetWorld()->GetAuthGameMode<ARoomGameMode>()->CharacterDataTable
 *
 * 大厂原则 - 零兜底:
 *   - Owner 为空 → Log Error + return nullptr
 *   - World 为空 → Log Error + return nullptr
 *   - GameMode 为空 → Log Error + return nullptr
 *   - GameMode 不是 ARoomGameMode → Log Error + return nullptr
 *   - DataTable 为空 → Log Error + return nullptr
 */
UDataTable* UCharacterIconComponent::ResolveCharacterDataTable() const
{
	ABaseCharacter* Owner = ResolveOwnerCharacter();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] ResolveCharacterDataTable: Owner Character 已失效."));
		return nullptr;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] ResolveCharacterDataTable: World 已失效."));
		return nullptr;
	}

	ARoomGameMode* GM = Cast<ARoomGameMode>(World->GetAuthGameMode());
	if (!GM)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] ResolveCharacterDataTable: AuthGameMode 不是 ARoomGameMode. "
			 "Owner='%s'. 根因: 当前 World 的 GameMode 没配 ARoomGameMode 或 GameMode 还没初始化."),
			*Owner->GetName());
		return nullptr;
	}

	if (!GM->CharacterDataTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] ResolveCharacterDataTable: ARoomGameMode->CharacterDataTable 为空. "
			 "根因: BP_RoomGameMode 没配 CharacterDataTable (DT_CharacterList). "
			 "【大厂原则】拒绝返回 nullptr 兜底, 强制修复 BP 配置."),
			*Owner->GetName());
		return nullptr;
	}

	return GM->CharacterDataTable;
}


/**
 * ResolveWeaponDataTable — 通过 Owner Character 路径查找 WeaponDataTable (服务器侧)
 *
 * 路径: Owner->GetWorld()->GetAuthGameMode<ARoomGameMode>()->WeaponDataTable
 *
 * 大厂原则 - 零兜底:
 *   - 同 ResolveCharacterDataTable 的所有校验
 */
UDataTable* UCharacterIconComponent::ResolveWeaponDataTable() const
{
	ABaseCharacter* Owner = ResolveOwnerCharacter();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] ResolveWeaponDataTable: Owner Character 已失效."));
		return nullptr;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] ResolveWeaponDataTable: World 已失效."));
		return nullptr;
	}

	ARoomGameMode* GM = Cast<ARoomGameMode>(World->GetAuthGameMode());
	if (!GM)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] ResolveWeaponDataTable: AuthGameMode 不是 ARoomGameMode. "
			 "Owner='%s'."), *Owner->GetName());
		return nullptr;
	}

	if (!GM->WeaponDataTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] ResolveWeaponDataTable: ARoomGameMode->WeaponDataTable 为空. "
			 "根因: BP_RoomGameMode 没配 WeaponDataTable (DT_WeaponList). "
			 "【大厂原则】拒绝返回 nullptr 兜底, 强制修复 BP 配置."),
			*Owner->GetName());
		return nullptr;
	}

	return GM->WeaponDataTable;
}


/**
 * GetWeaponIconFromTable — 从 WeaponDataTable 查指定 Weapon ID 的武器图标贴图 (服务器侧)
 *
 * 【v40.1 P0 新增】镜像 GetCharacterAvatarFromTable
 *
 * 大厂原则 - 零兜底:
 *   - DataTable 为空 → Log Error + return nullptr
 *   - 查不到行 → Log Error + return nullptr (不返回默认贴图)
 *   - WeaponIcon 字段为空 → Log Error + return nullptr
 *
 * @param WeaponID 武器 ID (来自 Owner->GetSpawnWeaponID())
 * @return 武器图标贴图指针 (失败返回 nullptr)
 */
UTexture2D* UCharacterIconComponent::GetWeaponIconFromTable(const FString& WeaponID) const
{
	if (WeaponID.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CharacterIconComponent] GetWeaponIconFromTable: WeaponID 为空, 直接返回 nullptr."));
		return nullptr;
	}

	UDataTable* WeaponDataTable = ResolveWeaponDataTable();
	if (!WeaponDataTable)
	{
		return nullptr;
	}

	static const FString ContextString(TEXT("WeaponIconLookup"));
	FWeaponInfo* Info = WeaponDataTable->FindRow<FWeaponInfo>(FName(*WeaponID), ContextString);

	if (!Info)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] GetWeaponIconFromTable: 查不到 WeaponID='%s' 在 WeaponDataTable. "
				 "根因: DT_WeaponList 没有这个 RowName, 或 RowName 拼写错误. "
				 "【大厂原则】拒绝返回默认贴图, 强制修复 DataTable 配置."),
			*WeaponID);
		return nullptr;
	}

	if (!Info->WeaponIcon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] GetWeaponIconFromTable: WeaponID='%s' 行的 WeaponIcon 字段为空. "
				 "根因: DT_WeaponList 里这行的 WeaponIcon 没配. "
				 "【大厂原则】拒绝返回 nullptr 兜底, 强制修复 DataTable."),
			*WeaponID);
		return nullptr;
	}

	return Info->WeaponIcon;
}


/**
 * Client_RefreshWeaponIcon_Implementation — 客户端接收武器图标数据
 *
 * 【v40.1 P0 新增】镜像 Client_RefreshCharacterIcon_Implementation
 *
 * 服务器调 RefreshWeaponIconOnHUD → 查表 → Client_RefreshWeaponIcon(WeaponID, Icon)
 * 客户端收到 → 缓存 + 调 CharacterEvents 广播 (HUD 订阅)
 *
 * 注: UFUNCTION(Client, Reliable) 修饰符在 BaseCharacter::Client_RefreshWeaponIcon 上,
 *     BaseCharacter 转发壳直接调本方法 (服务器本地走本地 RPC 实现, 远端客户端走 RPC 序列化)
 *
 * 大厂原则 - 零兜底:
 *   - Icon 为空 → Log Error + 不广播
 *
 * @param InWeaponID 武器 ID (用于缓存 + 验证)
 * @param Icon       武器图标贴图 (服务器查表后传入, 避免客户端查 GameMode)
 */
void UCharacterIconComponent::Client_RefreshWeaponIcon_Implementation(const FString& InWeaponID, UTexture2D* Icon)
{
	ABaseCharacter* Owner = ResolveOwnerCharacter();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] Client_RefreshWeaponIcon_Implementation: Owner Character 已失效."));
		return;
	}

	// 【P0 双保险】: 必须是本机玩家才允许改本机 HUD
	if (!Owner->IsLocallyControlled())
	{
		return;
	}

	// 缓存 WeaponID (用于 Retry 时的 HUD 推送)
	CachedWeaponIDForIcon = InWeaponID;

	if (!Icon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] Client_RefreshWeaponIcon_Implementation: Icon 为空. "
				 "WeaponID='%s', Owner='%s'. "
				 "根因: 服务器 GetWeaponIconFromTable 查表失败 (DataTable 空 或 行不存在 或 WeaponIcon 字段为空). "
				 "【大厂原则】拒绝显示空白, 强制修复 WeaponDataTable 配置."),
			*InWeaponID, *Owner->GetName());
		// 不 return — 仍尝试走 CharacterEvents, 让 HUD 自己决策 (大厂原则 - 不静默)
	}

	// 通知 HUD (走 CharacterEvents 本地事件总线, 因为 CharacterEvents 不复制)
	BroadcastWeaponIconReady(InWeaponID, Icon);
}


/**
 * Client_RefreshWeaponAmmo_Implementation — 客户端接收武器弹药数据
 *
 * 【v85.3 P0 新增】镜像 Client_RefreshWeaponIcon_Implementation
 *
 * 服务器调用链 (v85.3 大厂架构):
 *   Server_SpawnAllWeapons (末尾)
 *     → Owner->Client_RefreshWeaponAmmo(CurrentAmmo, MagazineSize, ReserveAmmo)
 *   Server_SwitchToWeaponSlot (末尾)
 *     → Owner->Client_RefreshWeaponAmmo(CurrentAmmo, MagazineSize, ReserveAmmo)
 *
 * 客户端收到 → 缓存 + 调 CharacterEvents 广播 (HUD 订阅)
 *
 * 【根本修复】为什么需要这个 RPC:
 *   旧版 (v85): 客户端 CharacterIconComponent::BeginPlay 时
 *     - CurrentWeapon 还是 None (OnRep_CurrentWeapon 还没触发)
 *     - BroadcastWeaponAmmoInfo → 走"无武器"分支 → 缓存 "1/1, bIsMelee=true"
 *     - HUD Bind-Snapshot 读到 → 显示 1/1 (用户报错的现象)
 *
 *   新版 (v85.3): 服务器主动 RPC 推弹药数据给客户端
 *     - Client_RefreshWeaponAmmo 收到 CurrentAmmo/MagazineSize/ReserveAmmo
 *     - 缓存 SetCachedWeaponAmmoInfo + 广播 OnWeaponAmmoInfoReady
 *     - HUD 立刻显示 "30/120" 而不是 "1/1"
 *
 * 大厂原则 - 与 v40.1 武器图标 RPC 镜像对称:
 *   - 武器图标真理源 = DT_WeaponInfo 行 (服务器查表) → Client_RefreshWeaponIcon RPC
 *   - 武器弹药真理源 = WeaponFireComponent 字段 (服务器读) → Client_RefreshWeaponAmmo RPC
 *
 * 大厂原则 - 零兜底:
 *   - bIsLocallyControlled=false → 不处理 (其他玩家的弹药不影响本机 HUD)
 *   - Owner/Events 为空 → Log Error + return
 *
 * @param CurrentAmmo   服务器读 FireComp->GetCurrentAmmo()
 * @param MagazineSize  服务器读 FireComp->GetMagazineSize()
 * @param ReserveAmmo   服务器读 FireComp->GetReserveAmmo()
 */
void UCharacterIconComponent::Client_RefreshWeaponAmmo_Implementation(int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo)
{
	ABaseCharacter* Owner = ResolveOwnerCharacter();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] Client_RefreshWeaponAmmo_Implementation: Owner Character 已失效."));
		return;
	}

	// 【P0 双保险】: 必须是本机玩家才允许改本机 HUD (与其他玩家的弹药隔离)
	if (!Owner->IsLocallyControlled())
	{
		return;
	}

	// 零兜底验证 — 服务器送来的弹药数据合理性检查
	if (MagazineSize <= 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] Client_RefreshWeaponAmmo_Implementation: MagazineSize <= 0 — 服务器数据异常. "
				 "CurrentAmmo=%d, MagazineSize=%d, ReserveAmmo=%d, Owner='%s'. "
				 "【排查】服务器 WeaponFireComponent::InitializeFromWeaponConfig 配错了 DT_WeaponInfo.MagazineSize 字段."),
			CurrentAmmo, MagazineSize, ReserveAmmo,
			*Owner->GetName());
		// 仍然广播 — 让 HUD 显示异常数据而非空白 (大厂原则: 显式优于隐式)
	}

	UCharacterEvents* Events = Owner->ResolveCharacterEvents();
	if (!Events)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] Client_RefreshWeaponAmmo_Implementation: ResolveCharacterEvents 失败. "
				 "Owner='%s'. 检查 CharacterEvents 组件是否挂载."),
			*Owner->GetName());
		return;
	}

	// 大厂原则 - 双轨制 (镜像 v40.2 武器图标缓存):
	//   1. 先写缓存 (Snapshot Caching — 用于后续 HUD Bind 时拉快照)
	//   2. 再 Broadcast (事件总线 — 让已订阅的 HUD 实时刷新)
	Events->SetCachedWeaponAmmoInfo(CurrentAmmo, MagazineSize, ReserveAmmo, /* bIsMelee */ false);
	Events->OnWeaponAmmoInfoReady.Broadcast(CurrentAmmo, MagazineSize, ReserveAmmo);

	UE_LOG(LogTemp, Log,
		TEXT("[CharacterIconComponent] Client_RefreshWeaponAmmo_Implementation: 收到弹药数据 %d/%d+%d. "
			 "Owner='%s'"),
		CurrentAmmo, MagazineSize, ReserveAmmo,
		*Owner->GetName());
}


/**
 * ResolvePlayerState — 通过 Owner Character 路径查找 ARoomPlayerState
 *
 * 大厂原则 - 零兜底:
 *   - Owner 为空 → Log Error + return nullptr
 *   - PlayerState 为空 → Log Error + return nullptr
 *   - PlayerState 不是 ARoomPlayerState → Log Error + return nullptr
 */
ARoomPlayerState* UCharacterIconComponent::ResolvePlayerState() const
{
	ABaseCharacter* Owner = ResolveOwnerCharacter();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] ResolvePlayerState: Owner Character 已失效."));
		return nullptr;
	}

	APlayerState* PS = Owner->GetPlayerState();
	if (!PS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] ResolvePlayerState: Owner '%s' 没有 PlayerState. "
			 "根因: PlayerController 没绑 PlayerState, 或 GameMode.PostLogin 没调 InitPlayerState. "
			 "【大厂原则】拒绝返回 nullptr 兜底, 强制修复 Spawn 链路."),
			*Owner->GetName());
		return nullptr;
	}

	ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(PS);
	if (!RoomPS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] ResolvePlayerState: PlayerState 类型不是 ARoomPlayerState. "
			 "实际类型='%s', Owner='%s'. "
			 "根因: GameMode 的 PlayerStateClass 没配成 ARoomPlayerState, BP 错配 fallback 到 APlayerState. "
			 "【大厂原则】拒绝 fallback 兜底, 强制修复 BP 配置."),
			*PS->GetClass()->GetName(), *Owner->GetName());
		return nullptr;
	}

	return RoomPS;
}


/**
 * BroadcastCharacterIconReady — 广播"角色头像就绪"事件到 CharacterEvents
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
void UCharacterIconComponent::BroadcastCharacterIconReady(const FString& CharID, UTexture2D* Icon)
{
	ABaseCharacter* Owner = ResolveOwnerCharacter();
	if (!Owner)
	{
		return;
	}

	// [v40 P0 修复] 必须用 ResolveCharacterEvents() 而非裸字段访问 CharacterEvents
	//   根因: BP archetype 实例化会 nullify CharacterEvents 字段, 但 BP 给所有 Owner->Component 写的代码都失效
	//         结果: 普通玩家客户端 OnCharacterIconReady 从未 Broadcast → HUD 头像缓存永远空
	//   修复: 用 resolver 函数 (FindComponentByClass) 重新解析,即使字段被 nullify 也能获取组件
	UCharacterEvents* Events = Owner->ResolveCharacterEvents();
	if (!Events)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] BroadcastCharacterIconReady: ResolveCharacterEvents 失败. "
				 "【v40 修复】不应该再出现 — 检查 BP CharacterIconComponent 是否挂上."));
		return;
	}

	// 【2026-07-01 P0 修复】"事件 + 缓存"双轨制
	// 先写缓存, 再 Broadcast, 保证订阅者订阅时能从缓存拿到最新值
	Events->SetCachedCharacterIcon(CharID, Icon);
	Events->OnCharacterIconReady.Broadcast(CharID, Icon);
}


/**
 * BroadcastWeaponIconReady — 广播"武器图标就绪"事件到 CharacterEvents
 *
 * 同 BroadcastCharacterIconReady, 但事件是武器图标专用
 *
 * @param WeaponID 武器 ID
 * @param Icon     武器图标贴图
 */
void UCharacterIconComponent::BroadcastWeaponIconReady(const FString& WeaponID, UTexture2D* Icon)
{
	ABaseCharacter* Owner = ResolveOwnerCharacter();
	if (!Owner)
	{
		return;
	}

	// [v40 P0 修复] 必须用 ResolveCharacterEvents() 而非裸字段访问
	UCharacterEvents* Events = Owner->ResolveCharacterEvents();
	if (!Events)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] BroadcastWeaponIconReady: ResolveCharacterEvents 失败. "
				 "【v40 修复】不应该再出现 — 检查 BP CharacterIconComponent 是否挂上."));
		return;
	}

	// 【v40.2 P0 修复】"事件 + 缓存"双轨制 (镜像 BroadcastCharacterIconReady)
	//   根因: 旧版只 Broadcast 不缓存 → 客户端 HUD Bind-Snapshot 时拿不到武器图标 → 永远不显示
	//   修复: 先写缓存, 再 Broadcast, 保证 Bind-Snapshot 能补发
	Events->SetCachedWeaponIcon(WeaponID, Icon);
	Events->OnWeaponIconReady.Broadcast(WeaponID, Icon);
}


/**
 * BroadcastWeaponAmmoInfo — 【v85.3 拆分】服务器本地专用 (镜像 RefreshWeaponIconOnHUD)
 *
 * 服务器调用链 (v85.3 大厂架构重构后):
 *   Server_SpawnAllWeapons (末尾)
 *     → BroadcastWeaponAmmoInfo (本地服务器读取弹药 + Owner->Client_RefreshWeaponAmmo RPC)
 *   Server_SwitchToWeaponSlot (末尾)
 *     → BroadcastWeaponAmmoInfo (本地服务器读取弹药 + Owner->Client_RefreshWeaponAmmo RPC)
 *   RefreshCharacterIcon 链路 → RefreshWeaponIconOnHUD (末尾追加) → BroadcastWeaponAmmoInfo
 *
 * 【v85.3 镜像对称原则 — 与 RefreshWeaponIconOnHUD 完全对称】
 *   旧版 (v85): BroadcastWeaponAmmoInfo 直接读 FireComp + 本地 Broadcast
 *     → 客户端调它时 (因为 CurrentWeapon 还没复制过来), 走"无武器"分支, 缓存"1/1, bIsMelee=true"
 *     → HUD 显示错误弹药数据 (用户报错的现象)
 *
 *   新版 (v85.3): BroadcastWeaponAmmoInfo 仅服务器调用:
 *     → 服务器本地读 FireComp (数据必然有效, 因为服务器 Spawn 时序保证)
 *     → 写 Owner->Client_RefreshWeaponAmmo RPC 推弹药数据给客户端
 *     → 客户端收到 → 缓存 + OnWeaponAmmoInfoReady.Broadcast (HUD 更新)
 *     → 服务器本地也 Broadcast (因为 ListenServer = 同进程, 服务器自己也要更新 HUD)
 *
 * 大厂原则 - 镜像对称 v40.1/v40.2:
 *   武器图标: RefreshWeaponIconOnHUD (服务器) → Owner->Client_RefreshWeaponIcon RPC → 客户端缓存+广播
 *   武器弹药: BroadcastWeaponAmmoInfo (服务器) → Owner->Client_RefreshWeaponAmmo RPC → 客户端缓存+广播
 *
 * 大厂原则 - 客户端隔离:
 *   - 本函数现在是 SERVER ONLY, 调用前检查 HasAuthority()
 *   - 客户端不允许直接读 FireComp + Broadcast (因为 CurrentWeapon 还没复制过来)
 *   - 客户端弹药来源只有两条: 1) Client_RefreshWeaponAmmo RPC 推送 2) OnWeaponAmmoChanged (开火后实时)
 *
 * @param InOwner 角色指针 (由调用方传入, 避免重复 Cast)
 */
void UCharacterIconComponent::BroadcastWeaponAmmoInfo(ABaseCharacter* InOwner)
{
	if (!InOwner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] BroadcastWeaponAmmoInfo: InOwner 为空."));
		return;
	}

	// 【v85.3 大厂原则 - 服务端授权检查】
	//   本函数现在仅服务器调用, 因为:
	//   1. 服务器读 FireComp 数据必然正确 (CurrentWeapon 一定存在)
	//   2. 客户端调它会读到默认值 1/1 (CurrentWeapon 还没复制过来)
	//   3. 必须通过 Owner->Client_RefreshWeaponAmmo RPC 路径推送数据
	if (!InOwner->HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] BroadcastWeaponAmmoInfo: 客户端不应该走这条路. "
				 "InOwner='%s'. "
				 "【v85.3 修复】客户端弹药来源必须是 Client_RefreshWeaponAmmo RPC 或 OnWeaponAmmoChanged, "
				 "不允许直接调用 BroadcastWeaponAmmoInfo."),
			*InOwner->GetName());
		return;
	}

	// 采集弹药数据 (服务端权威)
	int32 CurrentAmmo = 0;
	int32 MagazineSize = 0;
	int32 ReserveAmmo = 0;
	bool bIsMelee = false;

	ABaseWeapon* CurrentWeapon = InOwner->GetCurrentWeapon();
	if (!CurrentWeapon)
	{
		// 服务器理论上一定有 CurrentWeapon (Spawn 时序保证)
		// 客户端如果走这里, 就走 [v85.3] 上面 HasAuthority 检查被拦截
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] BroadcastWeaponAmmoInfo: 服务器无 CurrentWeapon (InOwner='%s'). "
				 "【大厂原则】零兜底 — 服务器 Spawn 时序保证一定有武器, 此分支意味着逻辑错误, "
				 "请检查 Server_SpawnAllWeapons → RequestWeaponSpawn 调用链."),
			*InOwner->GetName());
		return;
	}

	const EWeaponMeshType MeshType = CurrentWeapon->GetMeshType();

	if (MeshType == EWeaponMeshType::Melee)
	{
		// 近战武器: 固定 1/1 (弹药无意义)
		bIsMelee = true;
		CurrentAmmo = 1;
		MagazineSize = 1;
		ReserveAmmo = 0;
	}
	else
	{
		// 枪械: 从 WeaponFireComponent 读取 (字段直接访问)
		if (UWeaponFireComponent* FireComp = CurrentWeapon->WeaponFireComponent)
		{
			CurrentAmmo = FireComp->GetCurrentAmmo();
			MagazineSize = FireComp->GetMagazineSize();
			ReserveAmmo = FireComp->GetReserveAmmo();
		}
		else
		{
			// 零兜底 — 服务器枪械一定有 WeaponFireComponent
			UE_LOG(LogTemp, Error,
				TEXT("[CharacterIconComponent] BroadcastWeaponAmmoInfo: 枪械 '%s' 没有 WeaponFireComponent. "
					 "InOwner='%s'. "
					 "【大厂原则】零兜底 — DT_WeaponInfo.MeshType 配错 (Melee 配到枪械) 或 WeaponFireComponent 未挂载."),
				*CurrentWeapon->GetName(), *InOwner->GetName());
			return;
		}
	}

	// 【v85.3 P0 镜像对称 v40.1】服务器本地也广播 (ListenServer = 同进程, 服务器自己也要更新 HUD)
	//   大厂原则 - 显式优于隐式:
	//     服务器本地调 Client_RefreshWeaponAmmo_Implementation → 缓存 + Broadcast
	//     ListenServer 直接走本地 RPC (UE RPC 行为), 不走网络序列化
	UCharacterEvents* Events = InOwner->ResolveCharacterEvents();
	if (Events)
	{
		// 服务器本地路径: 直接写缓存 + 广播 (镜像 Client_RefreshWeaponAmmo_Implementation)
		Events->SetCachedWeaponAmmoInfo(CurrentAmmo, MagazineSize, ReserveAmmo, bIsMelee);
		Events->OnWeaponAmmoInfoReady.Broadcast(CurrentAmmo, MagazineSize, ReserveAmmo);
	}

	// 【v85.3 P0 核心修复】RPC 推弹药数据给所有客户端 (镜像 Owner->Client_RefreshWeaponIcon)
	if (bIsMelee)
	{
		// 近战武器也推 RPC — 保证所有客户端缓存一致 (HUD Bind-Snapshot 时不会显示 "1/1, bIsMelee=true" 默认值)
		InOwner->Client_RefreshWeaponAmmo(CurrentAmmo, MagazineSize, ReserveAmmo);
	}
	else
	{
		InOwner->Client_RefreshWeaponAmmo(CurrentAmmo, MagazineSize, ReserveAmmo);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[CharacterIconComponent] BroadcastWeaponAmmoInfo: 服务器广播弹药 %d/%d+%d, bIsMelee=%d. "
			 "InOwner='%s', Weapon='%s'"),
		CurrentAmmo, MagazineSize, ReserveAmmo, bIsMelee ? 1 : 0,
		*InOwner->GetName(), *CurrentWeapon->GetName());
}


// -----------------------------------------------------------------------------
// 【v84 大厂架构新增】服务器直接刷新武器图标 (武器切换时调用)
// -----------------------------------------------------------------------------
void UCharacterIconComponent::RefreshWeaponIconOnHUDFromServer(const FString& InWeaponID)
{
	ABaseCharacter* Owner = ResolveOwnerCharacter();
	if (!Owner)
	{
		return;
	}

	// 【v85 大厂架构新增】武器切换时重新订阅新武器的弹药变化
	// 根因: 切枪后必须订阅新武器的 OnAmmoChanged，否则弹夹数量不更新
	SubscribeToActiveWeaponAmmo();

	// 服务器查表获取武器图标
	UTexture2D* WeaponIcon = GetWeaponIconFromTable(InWeaponID);
	if (!WeaponIcon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] RefreshWeaponIconOnHUDFromServer: 武器 '%s' 查不到图标. "
				 "InOwner='%s'. 跳过武器图标刷新."),
			*InWeaponID,
			*Owner->GetName());
		return;
	}

	// 缓存 WeaponID
	CachedWeaponIDForIcon = InWeaponID;

	// 服务器查表获取图标, 通过 RPC 推送到客户端
	if (Owner->HasAuthority())
	{
		Owner->Client_RefreshWeaponIcon(InWeaponID, WeaponIcon);
	}
}


// -----------------------------------------------------------------------------
// 【v85 大厂架构新增】弹药变化回调 — WeaponFireComponent::OnAmmoChanged 订阅
// 【v85.3 大厂重构】直接用 NewAmmo/MagazineSize, 不再调 BroadcastWeaponAmmoInfo (SERVER ONLY)
// -----------------------------------------------------------------------------
void UCharacterIconComponent::OnWeaponAmmoChanged(int32 NewAmmo, int32 MagazineSize)
{
	ABaseCharacter* OwnerChar = ResolveOwnerCharacter();
	if (!OwnerChar)
	{
		return;
	}

	UCharacterEvents* Events = OwnerChar->ResolveCharacterEvents();
	if (!Events)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CharacterIconComponent] OnWeaponAmmoChanged: ResolveCharacterEvents 失败. "
				 "Owner='%s'. 检查 CharacterEvents 组件是否挂载."),
			*OwnerChar->GetName());
		return;
	}

	// 零兜底 — 服务器/客户端都走这条路, 不走 BroadcastWeaponAmmoInfo (那是 SERVER ONLY 的 RPC 推入口)
	//   ReserveAmmo 这里不知道 (OnAmmoChanged 只传 CurrentAmmo/MagazineSize),
	//   保留缓存的 ReserveAmmo, 不重置
	//   如果缓存从未写入 (bCachedWeaponAmmoValid=false), 退化用 0 (不允许 -1 显示在 HUD 上)
	int32 OldReserveAmmo = 0;
	bool bOldIsMelee = false;
	int32 OldCurrentAmmo = 0;
	int32 OldMagazineSize = 0;
	const bool bHasValidCache = Events->GetCachedWeaponAmmoInfo(OldCurrentAmmo, OldMagazineSize, OldReserveAmmo, bOldIsMelee);
	if (!bHasValidCache)
	{
		// 缓存从未初始化 — ReserveAmmo 用 0 而非 -1 (避免 HUD 显示 -1)
		OldReserveAmmo = 0;
		UE_LOG(LogTemp, Verbose,
			TEXT("[CharacterIconComponent] OnWeaponAmmoChanged: CharacterEvents 缓存无效, ReserveAmmo 退化到 0. "
				 "正常情况: Server RPC 先推 Client_RefreshWeaponAmmo 写入缓存, 再触发本回调."));
	}
	Events->SetCachedWeaponAmmoInfo(NewAmmo, MagazineSize, OldReserveAmmo, /* bIsMelee */ false);
	Events->OnWeaponAmmoInfoReady.Broadcast(NewAmmo, MagazineSize, OldReserveAmmo);

	UE_LOG(LogTemp, Verbose,
		TEXT("[CharacterIconComponent] OnWeaponAmmoChanged: %d/%d, ReserveAmmo=%d. "
			 "Owner='%s'"),
		NewAmmo, MagazineSize, OldReserveAmmo,
		*OwnerChar->GetName());
}