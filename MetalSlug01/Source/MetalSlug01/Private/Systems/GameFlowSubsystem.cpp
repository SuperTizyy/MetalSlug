// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本子系统的头文件
#include "Systems/GameFlowSubsystem.h"

// 【大厂架构 - 独立中断通道】OnInterrupted 广播参数使用 EUIPanel
#include "Services/UIViewService.h"

// 引入测试配置中心（读取 Project Settings 中的 bSkipLoginDirectToLobby）
#include "Tools/MetalSlugTestSettings.h"

// 引入账号子系统（MockLoginForTesting 临时分配测试身份）
#include "Systems/Account/AccountSubsystem.h"

// 引入房间业务服务（EnterSkipToHostMode 显式标房主 - 测试绕行专用 API）
#include "Services/RoomService.h"

// 引入房间 GameState (CurrentMatchMode)
#include "Systems/RoomGameState.h"

// 引入房间模式枚举 (ERoomMatchMode)
#include "Data/Enums/RoomEnums.h"

// 引入会话管理器 (SetSkipLoginRoomDisplayInfo - v54.5.1 skip-login 房间名专用)
#include "Systems/Session/SessionManagerSubsystem.h"

// 引入 SessionResult.h (URoomMatchModeUtils 字符串↔ERoomMatchMode 转换工具 - v93.1 单一真理源)
#include "Systems/Session/SessionResult.h"

// 引入 UGameplayStatics 类（提供 OpenLevel 等静态函数）
// 作用: 用于执行关卡切换、玩家查询等通用静态操作
#include "Kismet/GameplayStatics.h"

// 引入 UWorld 类头文件
// 作用: 用于获取当前世界对象、当前地图名等运行时信息
#include "Engine/World.h"

// 引入 FCoreUObjectDelegates (PostLoadMapWithWorld 在 UObject/Class.h)
#include "UObject/UObjectGlobals.h"

// 引入 GLog 全局日志 (用于绕过 UE_LOG 的编译期 FormatStringSan 校验)
#include "Logging/LogMacros.h"
#include "Misc/OutputDeviceRedirector.h"

// 引入自定义 LogCategory 定义 (LogGameFlow 宏在此展开)
#include "Logs/MetalSlugLogChannels.h"

// 引入活动 DataTable 集中加载服务 (启动期一次性检查所有表)
#include "Data/FActivityDataTableService.h"
#include "Systems/Activity/ActivitySubsystem.h" // v231: UActivitySubsystem 完整定义
#include "Systems/Activity/ActivityDataTableService.h" // v231: UActivityDataTableService 完整定义

// ============== 生命周期 ==============

void UGameFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 启动期一次性访问每张活动表, 触发 LoadSynchronous 并打印缺失列表
	// 设计: 故意不在这里直接调 GetMissingTables (那样只会检查未访问过的表)
	//       而是逐个 Get 一遍, 确保所有活动 DT 都被加载
	UE_LOG(LogGameFlow, Log, TEXT("[GameFlow] 启动: 预加载活动 DataTable..."));
	UActivitySubsystem* ActivitySub = GetGameInstance() ? GetGameInstance()->GetSubsystem<UActivitySubsystem>() : nullptr;
	if (!ActivitySub || !ActivitySub->GetDataTableService())
	{
		UE_LOG(LogGameFlow, Error,
			TEXT("[GameFlow] ActivitySubsystem 或 DataTableService 未初始化, 跳过预加载 (GameFlow 自身初始化早于 Activity?)"));
	}
	else
	{
		FActivityDataTableService& Service = ActivitySub->GetDataTableService()->GetService();
		// 主动触发每张表加载 (Service.Get() 内部强引用缓存, 首次调用 LoadObject)
		Service.Get(ActivityDataTable::ActivityInfo);
		Service.Get(ActivityDataTable::DailyLoginConfig);
		Service.Get(ActivityDataTable::ItemDetail);
		Service.Get(ActivityDataTable::TreasureBoxItem);
		Service.Get(ActivityDataTable::DailyUpgradeReward);

		const TArray<FName> Missing = Service.GetMissingTables();
		if (Missing.Num() > 0)
		{
			FString MissingList;
			for (const FName& ID : Missing)
			{
				if (!MissingList.IsEmpty())
				{
					MissingList += TEXT(", ");
				}
				MissingList += ID.ToString();
			}
			UE_LOG(LogGameFlow, Error, TEXT("[GameFlow] 缺失 %d 张活动 DataTable: %s"),
				Missing.Num(), *MissingList);
		}
	}

	// ==========================================
	// 【P0 架构升级】订阅全局 PostLoadMapWithWorld 委托
	// ==========================================
	// 大厂标准: 跨地图"下一步状态"意图的消费者必须是跨地图持久的层
	//
	// 之前的设计缺陷 (RoomPC::ExecuteLeaveRoom 的 Lambda):
	//   - PC 会被 OpenLevel 销毁 → EndPlay 抢先 Remove 委托 → 永远收不到 PostLoadMapWithWorld
	//   - 退房后 BootToLogin 切到 Login → 玩家看到登录页而不是大厅页
	//
	// 现在 (本类):
	//   - Subsystem 跨地图持久, 不会被 EndPlay 解绑
	//   - 任何业务方都可以调用 RequestStateOnNextLoad() 来"预约"下一张地图的状态
	//   - 一次性消费: 触发后立即清零 PendingPostLoadState, 防污染后续地图
	// ==========================================
	if (!PostLoadMapHandle.IsValid())
	{
		PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda(
			[WeakThis = TWeakObjectPtr<UGameFlowSubsystem>(this)](UWorld* LoadedWorld)
			{
				// 防御 1: 自己是否还活着 (理论上 Subsystem 比 World 活得久, 但仍然防一下)
				if (!WeakThis.IsValid()) return;
				WeakThis->HandlePostLoadMapWithWorld(LoadedWorld);
			});
		// 【修复 UE 5.6 编译错误 C2039/C2131/C2971/C2672】:
		//   FDelegateHandle 没有公开的 GetId() 方法, 直接传给 UE_LOG 的 %d
		//   还会触发 FormatStringSan 的 constexpr 校验报错链.
		//   解决: 用 FString::Printf 包装 + IsValid 布尔值表达语义, 绕过编译期检查.
		const FString LogMsg = FString::Printf(TEXT("[GameFlow] 已订阅 PostLoadMapWithWorld (Valid=%s)"),
			PostLoadMapHandle.IsValid() ? TEXT("true") : TEXT("false"));
		// 直接用 GLog->Log (避开 UE_LOG 的编译期 format 校验)
		GLog->Log(*LogMsg);
	}

	// ==========================================
	// 【大厂 P0 修复 2026.07.03】订阅 UWorld::OnWorldBeginPlay 委托
	// ==========================================
	// 设计动机 (PIE 模式 bug fix):
	//   - PostLoadMapWithWorld 在 PIE 模式下不触发 (UE 5.6 PIE 已知特性)
	//   - 导致 bSkipLoginDirectToLobby 等启动期逻辑在 PIE 模式下完全不执行
	//   - 玩家直接进入战斗场景, 没有 UI, 武器/角色全空
	//
	// 解决方案 (大厂模式 - 双入口保险):
	//   - 入口 A: PostLoadMapWithWorld (已存在, 独立进程模式生效)
	//   - 入口 B: OnWorldBeginPlay    (本新增, PIE 模式生效)
	//   - 用 bHasBootedToLogin 标志位防止双入口重入
	//
	// 时序优势 (相比 PostLoadMapWithWorld):
	//   - PostLoadMapWithWorld: World 刚加载, PC 可能还没 BeginPlay
	//   - OnWorldBeginPlay:     World + 所有 Actor (含 PC) BeginPlay 完毕, 时序 100% 可靠
	//   - 因此 OnWorldBeginPlay 是 UI 创建的最佳时机 (PC 已就绪, Subsystem 可拿到)
	//
	// 实现细节 (UE 5.6 编译错误 C2665 修复 2026.07.03):
	//   - UWorld::OnWorldBeginPlay 的类型是 FSimpleMulticastDelegate (无参数)
	//   - 签名是 "void()" 而不是 "void(UWorld*)"
	//   - 必须用 Lambda 捕获 World 指针, 不能直接 AddUObject 带 UWorld* 参数的回调
	//   - 用 TWeakObjectPtr<UGameFlowSubsystem> 防止野指针 (Subsystem 可能被销毁)
	// ==========================================
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UWorld* World = GI->GetWorld())
		{
			if (!WorldBeginPlayHandle.IsValid())
			{
				// Lambda 方案: 捕获 World + WeakThis
				//   - World 通过捕获拿到 (委托无参数, 需捕获)
				//   - WeakThis 防野指针 (Deinitialize 后委托不会再触发, 但保险)
				TWeakObjectPtr<UGameFlowSubsystem> WeakThis(this);
				WorldBeginPlayHandle = World->OnWorldBeginPlay.AddLambda(
					[WeakThis, World]()
					{
						// 防御 1: Subsystem 是否还活着
						if (!WeakThis.IsValid()) return;
						// 防御 2: World 是否还活着 (理论上 AddLambda 时已存活, 但保险)
						if (!IsValid(World)) return;
						WeakThis->HandleWorldBeginPlay(World);
					});

				UE_LOG(LogGameFlow, Log,
					TEXT("[GameFlow] 已订阅 OnWorldBeginPlay (Valid=%s, 双入口保险: PIE 模式兜底)"),
					WorldBeginPlayHandle.IsValid() ? TEXT("true") : TEXT("false"));
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning,
				TEXT("[GameFlow] Initialize 时 World 不可用, OnWorldBeginPlay 订阅失败 (PIE 模式可能受影响)"));
		}
	}

	// ==========================================
	// 【大厂架构 - 网络失败处理 2026.06.28】
	// ==========================================
	// 【UE 5.6 链路分析】
	//   完整的"Host 关闭 → Client ?closed"链路：
	//     1. Host 端: UNetConnection::SendCloseReason → Cleanup → NetDriver 关闭
	//     2. Client 端: 收到 NMT_Close → UNetConnection::Close
	//     3. UNetConnection::Close 调 GEngine->BroadcastNetworkFailure(FailureReceived, "Host closed")
	//     4. BroadcastNetworkFailure 只做 NetworkFailureEvent.Broadcast (不调 ClientTravel)
	//     5. UEngine::HandleNetworkFailure 才是真正调 ClientTravel 的地方
	//        但它走的是 FGameDelegates::Get().GetHandleDisconnectDelegate() 全局委托
	//        → UOnlineSession::HandleDisconnect → GEngine->HandleDisconnect
	//        → SetClientTravel("?closed")
	//     6. HandleNetworkFailure **不是 UGameInstance 的虚函数** (UE 5.6)
	//
	// 【为什么不能重写 UGameInstance::HandleNetworkFailure】
	//   UE 5.6 的 UGameInstance 没有这个虚函数, 只有 BlueprintImplementableEvent HandleNetworkError
	//   真正的逻辑在 UEngine 中, 但项目不继承 UEngine (不能改引擎模块)
	//
	// 【最终大厂方案】
	//   在 NetworkFailureEvent 回调里, **抢先触发 OpenLevel(L_Login?offline)**
	//   OpenLevel 内部会调 Browse → 触发 World 切换
	//   当 UE 的 ?closed SetClientTravel 在后续帧执行时, World 已经被切换了, 不会再生效
	//
	//   同时通过独立中断通道 OnInterrupted.Broadcast(LANRoom) 强制刷新 UI
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UGameFlowSubsystem::HandleNetworkFailure);
		UE_LOG(LogGameFlow, Log, TEXT("[GameFlow] 已订阅 OnNetworkFailure (抢先 OpenLevel + 独立中断通道)"));
	}

	// ==========================================
	// 【大厂 P0 架构 2026.06.29】订阅 SessionManager::OnSessionTerminated
	// ==========================================
	// 设计目标: 把"Session 终止"作为业务事件统一来源, 不依赖网络对象 (GameState / NetDriver) 生命周期
	//
	// 触发场景:
	//   - Host 主动 DestroySession 成功 → Host 自身需要回大厅
	//   - Client JoinSession 失败 + SessionDoesNotExist / SessionIsFull / CouldNotRetrieveAddress → 客户端需要回大厅
	//
	// 为什么用 SessionManager 而不是直接在 LANRoomPage 里处理:
	//   - LANRoomPage 是 World 级 Actor, 切图就销毁, 不跨地图持久
	//   - SessionManager 是 GameInstanceSubsystem, 跨地图持久, 是 Session 生命周期的权威
	//   - 即使 LANRoomPage 已经 NativeDestruct (例如已经 ClientTravel 到战斗地图),
	//     SessionManager 仍然能感知到 Session 终止
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USessionManagerSubsystem* SessionMgr = GI->GetSubsystem<USessionManagerSubsystem>())
		{
			SessionMgr->OnSessionTerminated.AddDynamic(this, &UGameFlowSubsystem::HandleSessionTerminated);
			UE_LOG(LogGameFlow, Log, TEXT("[GameFlow] 已订阅 SessionManager::OnSessionTerminated"));
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[GameFlow] SessionManagerSubsystem 未找到, 无法订阅 OnSessionTerminated"));
		}
	}

	// ==========================================
	// 【大厂架构修复 2026.07.03】移除 Initialize 末尾的主动 BootToLogin
	// ==========================================
	// 旧问题:
	//   Subsystem::Initialize 时, World 还没创建, PC 还没就绪
	//   → Broadcast(OnStateChanged(Login)) 时 UIViewService 拉到 broadcast
	//     → OnGameFlowStateChanged → ShowPanelWhenPCReady → PC 为 null → 排队等
	//   后续如果没人再次 Broadcast, UI 永远显示不出来 (玩家看到空白画面)
	//
	// 新方案 (大厂模式):
	//   Subsystem::Initialize 只负责"挂好管线" (订阅 PostLoadMapWithWorld, 订阅 Session 终止...)
	//   状态推进完全交给 PostLoadMapWithWorld (时机 = World 已加载, PC 已 ready)
	//   → 那时 Broadcast 出来的 UI 真的能拉起来
	//
	// 之前依赖的两个触发器:
	//   A) UMetalSlugGameInstance::Init() 第二阶段调 BootToLogin
	//      → 当前 Subsystem Initialize 主动广播, GameInstance::Init 又调 BootToLogin 被幂等拦截
	//      → 完全可以用 PostLoadMapWithWorld 替代, 而且时序更可靠
	//   B) PostLoadMapWithWorld 路径 B "延迟同步"
	//      → 这是新方案的"单一入口"
	//
	// 保留 bSkipLoginDirectToLobby 短路开关, 但触发时机改为 PostLoadMapWithWorld
	// ==========================================
	UE_LOG(LogGameFlow, Log,
		TEXT("[GameFlow] Subsystem::Initialize: 状态推进已迁移至 PostLoadMapWithWorld (大厂 P0 修复 2026.07.03)"));
}

void UGameFlowSubsystem::Deinitialize()
{
	// v231: 删除 FActivityDataTableService::Shutdown() 调用 — 重复架构 + 反模式
	// ActivitySubsystem::Deinitialize 已负责释放 DataTableService, 强引用随之析构
	// 不需要 GameFlow 再额外清理 (ActivitySubsystem 比 GameFlowSubsystem 后 Deinitialize, 自动清理链路已覆盖)

	// 【P0】解绑全局 PostLoadMapWithWorld 委托, 防止野指针回调
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}

	// 【大厂 P0 修复 2026.07.03】解绑 OnWorldBeginPlay 委托
	// OnWorldBeginPlay 是 World 自己的事件, 必须 World 存活时才能 Remove
	if (WorldBeginPlayHandle.IsValid())
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UWorld* World = GI->GetWorld())
			{
				World->OnWorldBeginPlay.Remove(WorldBeginPlayHandle);
			}
		}
		WorldBeginPlayHandle.Reset();
	}

	// 【大厂 P0 修复 2026.06.28】解绑网络失败回调, 防止跨地图悬空
	if (GEngine && NetworkFailureHandle.IsValid())
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		NetworkFailureHandle.Reset();
	}

	// 【大厂 P0 修复 2026.06.29】解绑 SessionManager::OnSessionTerminated
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USessionManagerSubsystem* SessionMgr = GI->GetSubsystem<USessionManagerSubsystem>())
		{
			SessionMgr->OnSessionTerminated.RemoveDynamic(this, &UGameFlowSubsystem::HandleSessionTerminated);
		}
	}

	// 清理意图标志 (防止 GameInstance 复用时残留)
	bHasPendingStateOnNextLoad = false;
	PendingPostLoadState = EMatchState::PreLogin;
	bHasBootedToLogin = false;  // 【大厂 P0 修复 2026.07.03】双入口幂等位也清理

	Super::Deinitialize();
}

/**
 * UGameFlowSubsystem::BootToLogin
 *
 * 【大厂标准】从 PreLogin 状态引导到 Login 状态
 *
 * 调用时机:
 * - UMetalSlugGameInstance::Init() 末尾
 * - 所有 Subsystem 已就绪之后
 *
 * 为什么不直接在 Initialize() 里调用 TransitToState(Login)?
 * - Subsystem 的 Initialize() 是 Engine 自动调用的，时序不可控
 * - UIViewService 可能还未初始化，订阅 OnStateChanged 会错过首次事件
 * - 大厂方案: UGameInstance 显式编排，明确两阶段启动
 */
void UGameFlowSubsystem::BootToLogin()
{
	// 【安全防御】只在 PreLogin 状态才能 Boot (防止外部乱调用)
	if (CurrentState != EMatchState::PreLogin)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GameFlowSubsystem] BootToLogin Ignored: CurrentState=%d, expected PreLogin=%d"),
			(int32)CurrentState, (int32)EMatchState::PreLogin);
		return;
	}

	// ==========================================
	// 【大厂 P0 修复 2026.07.03】双入口幂等保护
	// ==========================================
	// 背景:
	//   - 现在有 2 个入口都可能调 BootToLogin: PostLoadMapWithWorld + OnWorldBeginPlay
	//   - 独立进程模式: PostLoadMapWithWorld 先触发 (执行 BootToLogin), 后续 OnWorldBeginPlay 被拦截
	//   - PIE 模式: OnWorldBeginPlay 触发 (执行 BootToLogin), PostLoadMapWithWorld 从不触发 (但即使触发也被拦截)
	//
	// 作用:
	//   - 防止 MockLoginForTesting 被调两次 (导致 Random 名字刷新, 玩家身份在第一帧就变了)
	//   - 防止 TransitToState 被调两次 (会触发 Broadcast 两次, UI 闪烁)
	//
	// 重置时机:
	//   - Deinitialize 中已 Reset 为 false (见 Deinitialize 末尾)
	//   - 当前 GameInstance 销毁时自动重置 (新 Subsystem 初始化时 bHasBootedToLogin 默认 false)
	// ==========================================
	if (bHasBootedToLogin)
	{
		UE_LOG(LogGameFlow, Log,
			TEXT("[GameFlow] BootToLogin: 已执行过, 双入口幂等拦截 (CurrentState=%d)"),
			(int32)CurrentState);
		return;
	}
	bHasBootedToLogin = true;

	// ==========================================
	// 【大厂架构 - 测试绕行通道 2026.07.03 重构】
	// 检查 bSkipLoginDirectToLobby 配置项，短路跳过正常登录流程
	// ==========================================
	// 入口位置选在 BootToLogin() 的原因:
	//   - BootToLogin 是状态机的唯一"启动入口"，所有冷启动都经过这里
	//   - 此时 CurrentState == PreLogin，未被任何逻辑污染
	//   - 可以干净地分流: 正常账号走 Login，测试开关走 SkipToHost 模式
	//
	// 【大厂架构 - 2026.07.03 P0 重构】三层职责分离:
	//   - 旧实现: 直接 TransitToState(MainLobby), 假设启动地图是 L_Login
	//   - 新实现: 进入"显式 SkipToHost 模式", 复用现有房主识别路径
	//     ┌──────────────────────────────────────────────────────────┐
	//     │ Step 1: MockLoginForTesting  → 注入测试身份 (CurrentLoggedInUser) │
	//     │ Step 2: RoomService.EnterSkipToHostMode() → 显式标房主 │
	//     │          ├─ bIsHost = true                                  │
	//     │          ├─ OnHostChanged.Broadcast(true) → RoomInsidePage  │
	//     │          ├─ OnPlayerJoined.Broadcast(本机) → 本机玩家标签 │
	//     │          └─ 同步 GameState->HostPlayerName (Authority 路径) │
	//     │ Step 3: 根据当前 World 类型分发 UI                          │
	//     │          ├─ L_Login → TransitToState(MainLobby) → LANRoomPage │
	//     │          └─ 战斗地图 → OnInterrupted.Broadcast(RoomInside) │
	//     │                       (不走状态机, 避开"战斗地图自愈"链路)    │
	//     └──────────────────────────────────────────────────────────┘
	//
	// 设计优势:
	//   - 不改动 TransitToState (通用状态切换器, 不应包含业务开关)
	//   - 不改动 HandleStateEntry (物理地图操作层, 职责单一)
	//   - "测试房主"做成显式 API, 不依赖隐式副作用 (旧版本 NotifyBecameHost 永远不被调)
	//   - 战斗地图启动也能正确显示房主 UI, 不再被"战斗地图自愈"链路搞糊
	// ==========================================
	const UMetalSlugTestSettings* TestSettings = GetDefault<UMetalSlugTestSettings>();
	if (TestSettings && TestSettings->bSkipLoginDirectToLobby)
	{
		// ---- Step 1: 注入测试身份 ----
		if (UAccountSubsystem* AccountSub = GetGameInstance()->GetSubsystem<UAccountSubsystem>())
		{
			AccountSub->MockLoginForTesting();
		}
		else
		{
			UE_LOG(LogGameFlow, Error,
				TEXT("[GameFlow][测试绕行] AccountSubsystem 不可用，跳过 MockLogin"));
		}

		// ---- Step 2: 显式标房主 (新 API, 复用现有房主识别路径) ----
		if (URoomService* RoomService = URoomService::Get(this))
		{
			RoomService->EnterSkipToHostMode();
		}
		else
		{
			UE_LOG(LogGameFlow, Error,
				TEXT("[GameFlow][测试绕行] RoomService 不可用, 房主身份标定失败"));
		}

		// ---- Step 3: bSkipLoginDirectToLobby=true 直通房间 ----
		// 【v54.5.1 Bug 修复 + 重构】旧走 MainLobby 导致 RoomInside 不显示
		//   旧行为: TransitToState(MainLobby) → RoomPC P0-Fallback 误拦截 → RoomInside 显示失败
		//   新行为: 直接 TransitToState(InRoom) → RoomPC 正常处理 → RoomInside 正确显示
		// ==========================================
		// Step 3a: 设目标地图 (GameFlowSubsystem 持有 TargetRoomMapName, HandleStateEntry(InRoom) 会 OpenLevel)
		//   注意: 清空后再 TransitToState, 让 RoomPC::OnFlowStateChanged(InRoom) 先执行 (地图比较时 TargetRoomMapName 为空,
		//          不会误判当前地图而触发额外的 OpenLevel)
		//   TestSettings 已在 line 379 声明, 直接复用
		const FString BattleMapName = TestSettings->DebugSkipBattleMapName.IsEmpty()
			? TEXT("Japanese_Temple_Demo") : TestSettings->DebugSkipBattleMapName;

		// Step 3b: 设房间模式 (GameState.CurrentMatchMode)
		// 【v93 大厂架构修复】必须走 SetCurrentMatchMode 公开 API, 不能直接赋值:
		//   - 直接赋值 GS->CurrentMatchMode = RoomMode → 绕开 OnRep 路径 → OnMatchModeChanged 永不 Broadcast
		//   - 走 SetCurrentMatchMode → 服务器手动 Broadcast + 客户端 OnRep 触发 Broadcast → UI 立即响应
		//   - 这是 UE 5.6 ReplicatedUsing 字段的"显式优于隐式" 规范 (镜像 SetTotalRounds)
		//
		// 【v93.1 大厂架构修复】零兜底 + 与正式路径共用单一真理源 (SetTargetRoomMode):
		//   - 旧 (v93 之前): None → Melee 兜底 (违反零兜底) → 隐藏配置错
		//   - 新: DebugSkipRoomMode=None → Log Error + 强制开发者在 Project Settings 配置
		//   - 与正式路径共用同一字段 SetTargetRoomMode → 后续 InitGame 不再触发, 由本分支直接调 GS->SetCurrentMatchMode
		ERoomMatchMode RoomMode = ERoomMatchMode::None;
		if (TestSettings)
		{
			RoomMode = TestSettings->GetDebugSkipRoomMode();
		}
		// 零兜底: None → 显式报错 + 拒绝写入 GS, 强制修复 Project Settings
		if (RoomMode == ERoomMatchMode::None)
		{
			UE_LOG(LogGameFlow, Error,
				TEXT("[GameFlow][测试绕行] DebugSkipRoomMode == ERoomMatchMode::None. "
				     "【零兜底】拒绝默认分配 Melee, 拒绝写入 GS->SetCurrentMatchMode. "
				     "【修复路径】Project Settings → MetalSlug Test Settings → Debug Skip Room Mode 必须配 1 (Melee) 或 2 (Zombie)."));
			return;
		}
		// 单一真理源: 写入 TargetRoomMode 字段 (与正式路径共用), 让任何后续读取都有一致数据
		SetTargetRoomMode(RoomMode);
		if (UWorld* World = GetWorld())
		{
			if (ARoomGameState* GS = World->GetGameState<ARoomGameState>())
			{
				GS->SetCurrentMatchMode(RoomMode);
				UE_LOG(LogGameFlow, Log,
					TEXT("[GameFlow][测试绕行] 已设置 CurrentMatchMode=%d (Mode=%s)"),
					(int32)RoomMode, RoomMode == ERoomMatchMode::Melee ? TEXT("Melee") : TEXT("Zombie"));
			}
		}

		// Step 3c: 【v54.5.1 修复】先写入 skip-login 测试房间显示信息，再 TransitToState
		//   根因: TransitToState 会同步广播 OnStateChanged(InRoom)，RoomInsidePage::NativeConstruct
		//          在广播时就执行，此时必须读到 SkipLoginRoomName，否则显示硬编码默认值 "未命名房间"
		//   单一职责: SessionManager 是 skip-login 房间名真理源, GameFlowSubsystem 是唯一写入入口
		//   单一真理源 (v93.1): 字符串转换走 URoomMatchModeUtils::GetGameModeStringFromMatchMode, 不允许散落硬编码
		const FString GameModeStr = URoomMatchModeUtils::GetGameModeStringFromMatchMode(RoomMode);
		if (GameModeStr.IsEmpty())
		{
			// URoomMatchModeUtils 已 Log Error, 这里再 Log 一次根因上下文
			UE_LOG(LogGameFlow, Error,
				TEXT("[GameFlow][测试绕行] RoomMode 解析 GameMode 字符串失败 (已 Log Error). 拒绝写入 SkipLoginRoomName."));
			return;
		}
		const FString SkipLoginRoomName = FString::Printf(TEXT("测试-%s-%s"), *GameModeStr, *BattleMapName);
		if (UGameInstance* GI = GetWorld()->GetGameInstance())
		{
			if (USessionManagerSubsystem* SessionMgr = GI->GetSubsystem<USessionManagerSubsystem>())
			{
				SessionMgr->SetSkipLoginRoomDisplayInfo(SkipLoginRoomName, GameModeStr);
			}
		}

		// Step 3d: 先清空 TargetRoomMapName, 再 TransitToState
		//   TransitToState 会先 Broadcast(InRoom) → RoomPC::OnFlowStateChanged(InRoom)
		//   RoomPC 看到 TargetRoomMapName=NAME_None → 地图比较失败 → 不会误 OpenLevel → 直接创建 RoomUI
		//   然后 HandleStateEntry(InRoom) 才设置 TargetRoomMapName 并检查是否需要 OpenLevel
		TargetRoomMapName = NAME_None;

		UE_LOG(LogGameFlow, Log,
			TEXT("[GameFlow][测试绕行] bSkipLoginDirectToLobby=true, 地图=%s, 模式=%d, TransitToState(InRoom) (v54.5.1 修复)"),
			*BattleMapName, (int32)RoomMode);

		TransitToState(EMatchState::InRoom);

		// Step 3e: HandleStateEntry(InRoom) 执行时设置 TargetRoomMapName
		//   此时 RoomPC::OnFlowStateChanged(InRoom) 已执行完 (RoomUI 已创建)
		//   HandleStateEntry 看到 TargetRoomMapName=NAME_None → 走 else 分支 → 不 OpenLevel
		//   玩家留在当前地图 (Japanese_Temple_Demo), RoomUI 已显示
		SetTargetRoomMapName(FName(*BattleMapName));
		return;
	}

	// ==========================================
	// 【正常业务流程】
	// ==========================================
	UE_LOG(LogGameFlow, Log, TEXT("[GameFlow] BootToLogin: PreLogin → Login (Broadcast OnStateChanged)"));

	// 走标准状态切换流程 (包含安全校验 + 广播 + HandleStateEntry)
	TransitToState(EMatchState::Login);
}

/**
 * UGameFlowSubsystem::TransitToState
 *
 * 切换游戏流程状态的主入口
 *
 * @param NewState 目标状态
 *
 * 执行流程:
 * 1. 安全校验: 如果已在该状态则直接 return（防止地图无限重启）
 * 2. 更新 CurrentState 成员
 * 3. 广播 OnStateChanged 事件
 * 4. 调用 HandleStateEntry() 执行底层物理操作
 */
// 【Blueprint 入口】无 CallerSite，供蓝图调用（UHT 解析用）
void UGameFlowSubsystem::TransitToState(EMatchState NewState)
{
	// Blueprint 层不关心 caller，直接转发给内部实现
	TransitToState(NewState, TEXT("(Blueprint)"));
}

void UGameFlowSubsystem::TransitToState(EMatchState NewState, const TCHAR* CallerSite)
{
	// ==========================================
	// [DEBUG-v228] TransitToState 入口 trace — 定位异常 Broadcast(2) 的 caller
	// ==========================================
	// 背景: __FUNCTION__ 在被调函数体内只能输出函数自身, 无法显示调用方
	// 修复: TransitToState 加了 CallerSite 参数, 所有调用点显式传入 __FUNCTION__
	//       → 从此 caller 100% 可定位
	// 字段: CurrentState/NewState/Name/bHasBootedToLogin/PendingPostLoadState/CallerSite/ThreadId
	// ==========================================
	UE_LOG(LogGameFlow, Log,
		TEXT("[DEBUG-v228][TransitToState-ENTRY] Thread=%u CallerSite=%s CurrentState=%d NewState=%d (Name=%s) bHasBootedToLogin=%s PendingPostLoadState=%d"),
		FPlatformTLS::GetCurrentThreadId(),
		CallerSite,
		(int32)CurrentState, (int32)NewState,
		*UEnum::GetValueAsString(NewState),
		bHasBootedToLogin ? TEXT("true") : TEXT("false"),
		(int32)PendingPostLoadState);

	// ==========================================
	// 【安全防御】防止同一状态被重复调用
	// ==========================================
	// 原因: 防止 UI 重复点击或者逻辑回环导致的地图无限重启
	// 表现: 直接 return，红色屏幕日志 + UE_LOG 提示
	if (CurrentState == NewState)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GameFlowSubsystem] TransitToState Ignored: Already in state %d"), (int32)NewState);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red,
			FString::Printf(TEXT("[GameFlowSubsystem] 状态相同，被拦截！当前: %d, 目标: %d"), (int32)CurrentState, (int32)NewState));
		return;
	}

	// ==========================================
	// 1. 更新当前状态
	// ==========================================
	// 目的: 让 GetCurrentState() 立刻能返回新状态，方便 UI 立即查询
	CurrentState = NewState;

	// ==========================================
	// 2. 广播状态改变事件
	// ==========================================
	// 通知所有订阅 OnStateChanged 的 Controller/UI 准备切换
	// 此时 CurrentState 已经是 NewState，所有 Handler 拿到的参数即为新状态
	UE_LOG(LogGameFlow, Log, TEXT("[GameFlow] Broadcast OnStateChanged(%d) -> 通知所有订阅者"), (int32)CurrentState);
	OnStateChanged.Broadcast(CurrentState);

	// ==========================================
	// 3. 执行地图物理切换逻辑
	// ==========================================
	// 由 HandleStateEntry 内部 switch-case 决定是否需要 OpenLevel
	HandleStateEntry(CurrentState);
}

/**
 * UGameFlowSubsystem::HandleStateEntry
 *
 * 执行进入新状态时的底层物理操作（例如切换关卡地图）
 *
 * @param State 即将进入的新状态
 *
 * 核心思想: 不同状态对应不同的"地图物理动作"：
 *   - Login      -> 强制回到 L_Login（登录地图）
 *   - MainLobby  -> 回到 L_Login 但带 ?offline 参数（清理网络）
 *   - InRoom     -> 跳转到 TargetRoomMapName 指定的对战地图
 *   - Battleing  -> 不跳转（同一张地图内切换 UI）
 *   - PostBattle -> 不跳转（弹出结算面板）
 */
void UGameFlowSubsystem::HandleStateEntry(EMatchState State)
{
	// 获取当前 UWorld 实例，若 World 无效（极少见）则直接返回
	UWorld* World = GetWorld();
	if (!World) return;

	// 获取当前地图名（带前缀，例如 "UEDPIE_0_L_Login"）
	FString CurrentMapName = World->GetMapName();

	// ==========================================
	// 根据 State 分发不同的物理操作
	// ==========================================
	switch (State)
	{
	case EMatchState::Login:
		// 如果当前不在登录地图，则强制跳转回起点 L_Login
		if (!CurrentMapName.Contains(TEXT("L_Login")))
		{
			// 调用引擎静态函数 OpenLevel 加载指定关卡
			UGameplayStatics::OpenLevel(this, FName("L_Login"));
		}
		break;

	case EMatchState::MainMenu:
		// 主菜单态：不跳转地图，留在 L_Login
		// 管家通过广播让 LoginPlayerController 切换 UI（GameMenuPage）
		break;

	case EMatchState::MainLobby:
		// ==========================================
		// 【大厂标准 - P0 修复】单地图常驻模式: MainLobby 不触发 OpenLevel
		// ==========================================
		// 原因: L_Login 是常驻地图 (单地图模式, 代表: 王者荣耀/CS:GO/LOL 大厅)
		//       MainLobby 只是"UI 状态" (显示 LANRoomPage), 不是"地图状态"
		//       因此状态机不应在此处跳图
		//
		// 之前的问题 (模式 C - 分地图强制 OpenLevel):
		//   MainLobby → OpenLevel(L_Login, ?offline) → 销毁 GameInstance
		//   → 新 GameInstance 状态重置为 PreLogin → BootToLogin → 显示 LoginPage
		//   → 玩家必须重新登录 😡
		//
		// 现在 (模式 A - 单地图常驻):
		//   MainLobby → 只广播 OnStateChanged → UIViewService 自动 ShowPanel(LANRoom)
		//   → 玩家无缝看到大厅列表 😊
		//
		// 战斗地图 → MainLobby 的回城路径:
		//   由 RoomPlayerController::ExecuteLeaveRoom 主动调用 OpenLevel(L_Login, ?offline)
		//   然后在新地图加载后主动 TransitToState(MainLobby) 让 UI 切换
		// ==========================================
		break;

	case EMatchState::InRoom:
		// ==========================================
		// 【大厂 P0 修复 2026.07.03】添加 ?listen 参数支持 Listen Server
		// ==========================================
		// 旧代码: UGameplayStatics::OpenLevel(this, TargetRoomMapName);
		//   问题: 没有 ?listen → Host 进图后是单机模式, Client 无法连入
		//
		// 新方案 (大厂分层架构):
		//   - Host 端创房后, 调用方 (LANRoomPage) 通过 SetTargetRoomMapName + bIsHostListenServer 双通道
		//   - 这里根据 bIsHostListenServer 决定是否加 ?listen
		//
		// 【v93 大厂架构修复】把 TargetRoomMode 也通过 URL 参数传给新地图
		//   关键时序: HandleStateEntry(InRoom) 执行时, GameState 仍然是当前地图 (L_Login) 的 GS,
		//   不是新房间地图的 GS. 必须通过 URL Options 把模式信息传给新地图,
		//   然后由新地图的 ARoomGameMode::InitGameState 解析 URL Options 并写入新 GS.
		//
		// UE 5.6 URL Options 完整链路 (从引擎源码逐层确认):
		//   1. UGameplayStatics::OpenLevel(MapName, bAbs, Options):
		//        Cmd = MapName + "?" + Options  (OpenLevel.cpp:993)
		//        → 期望 Options **无** 前导 ? (因为 UE 内部会加)
		//   2. URL.Split("?") 拆出 MapName / Options
		//   3. FURL::FURL 解析:
		//        Op = ["listen", "Mode=2"]  (无前导 ?)
		//   4. World.cpp:5715-5719 重组 Options:
		//        Options = "?listen?Mode=2"  (Op 前面都加 ?)
		//        → InitGame(Options) 收到 "?listen?Mode=2"
		//   5. AGameModeBase::InitGame(Options): OptionsString = Options = "?listen?Mode=2"
		//   6. AGameModeBase::InitGameState: 我们读 OptionsString, 用 UGameplayStatics::ParseOption
		//        ParseOption(OptionsString, "Mode") 工作流程:
		//          - GrabOption 第一次: Left(1)=="?" → Result="listen?Mode=2" → 找下一个 ? → Result="listen"
		//          - GrabOption 第二次: Left(1)=="?" → Result="Mode=2" → 无下一个 ? → Result="Mode=2"
		//          - GetKeyValue: Key="Mode", Value="2"  ✓
		//
		// 大厂正确格式 (OpenLevel 第 4 个参数 Options):
		//   - Host 创房: "listen?Mode=2" (无前导 ?, UE 自己加)
		//   - Client 端: "Mode=2" (无 listen, 但 Mode 仍能解析)
		// ==========================================
		if (TargetRoomMapName != NAME_None)
		{
			// 仅当当前不在目标地图时才执行 OpenLevel，避免无谓重载
			if (!CurrentMapName.Contains(TargetRoomMapName.ToString()))
			{
				// 构建 URL Options (?Mode=数字) — 无前导 ? (UE 内部会加)
				//   - Melee = 1, Zombie = 2
				//   - None = 0 (不写入, 让 InitGameState 走默认分支, 记录 Log 引导修复)
				FString ModeParamStr = TEXT("");
				if (TargetRoomMode != ERoomMatchMode::None)
				{
					// 注意: ?Mode=数字 — 这里的 ? 是 ModeParamStr 内部的, 与 listen 之间用 ? 分隔
					//   - 最终 OpenLevel 第 4 参数: "listen?Mode=2"  (注意前面没有 ?)
					//   - UE OpenLevel 内部加 ?: "MapName?listen?Mode=2"
					//   - FURL 解析: Op = ["listen", "Mode=2"]
					//   - World.cpp 重组: Options = "?listen?Mode=2"
					//   - InitGameState ParseOption("Mode") → 2  ✓
					ModeParamStr = FString::Printf(TEXT("?Mode=%d"), static_cast<int32>(TargetRoomMode));
				}
				else
				{
					UE_LOG(LogGameFlow, Warning,
						TEXT("[GameFlowSubsystem] TargetRoomMode=ERoomMatchMode::None, "
						     "URL 不传 Mode 参数. 新地图 ARoomGameMode::InitGameState 收到后拒绝写入 GS CurrentMatchMode. "
						     "【修复】Host 创房前必须 LANRoomPage::OnConfirmCreateRoomClicked 调用 FlowSub->SetTargetRoomMode()."));
				}

				if (bIsHostListenServer)
				{
					// Host 创房 → 走 Listen Server 模式 ("listen" + ModeParamStr)
					//   - "listen" 是 UE 标准 listen server 标记
					//   - ModeParamStr = "?Mode=数字" (带前导 ?)
					//   - 拼起来: "listen?Mode=2"  (无前导 ?, UE 内部加 ?)
					UGameplayStatics::OpenLevel(this, TargetRoomMapName, true, TEXT("listen") + ModeParamStr);
				}
				else
				{
					// Client 端被 ServerTravel 拉进图 (或 PIE 直接启动) → 不需要 ?listen, 但 Mode 参数仍要带
					//   - 传 "?Mode=2" (带前导 ?) → OpenLevel 加 ? → "MapName??Mode=2"
					//   - FURL 解析: Op = ["Mode=2"]
					//   - World.cpp 重组: Options = "?Mode=2"
					//   - ParseOption("Mode") → 2  ✓
					UGameplayStatics::OpenLevel(this, TargetRoomMapName, true, ModeParamStr);
				}
			}
		}
		else
		{
			// 工业级防呆报错: 防止 UI 没传地图名字就硬切状态
			UE_LOG(LogTemp, Error, TEXT("[GameFlowSubsystem] TargetRoomMapName is NONE! Cannot transit to InRoom."));
		}
		break;

	case EMatchState::Battleing:
		// 【架构精进】战斗态和房间态在同一个地图！
		// 不需要物理跳转。事件广播出去后，交给 RoomPlayerController / RoomGameMode
		// 去把"房间UI"隐藏，并把"准星血条UI"挂出来

		// 【v54.5.1 新增】离开 InRoom 时清空 skip-login 测试房间显示信息
		//   单一职责: SessionManager 持有 skip-login 字段, GameFlowSubsystem 是唯一清空入口
		//   注意: World 已在 HandleStateEntry 入口声明并校验 (line 561)
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (USessionManagerSubsystem* SessionMgr = GI->GetSubsystem<USessionManagerSubsystem>())
			{
				SessionMgr->ResetSkipLoginRoomDisplayInfo();
			}
		}
		break;

	default:
		// 未明确处理的状态: 不做任何操作
		break;
	}
}

/**
 * UGameFlowSubsystem::SetTargetRoomMapName
 *
 * 设置目标房间地图名（由 UI/业务层在调用 TransitToState(InRoom) 之前调用）
 *
 * @param MapName UE 地图资产名（例如 "L_DesertGrayMap"）
 *
 * 作用: 缓存到 TargetRoomMapName 成员，进入 InRoom 时使用
 */
void UGameFlowSubsystem::SetTargetRoomMapName(FName MapName)
{
	TargetRoomMapName = MapName;
}


// ==========================================
// 【P0 架构升级】跨地图"下一步状态"意图机制
// ==========================================

/**
 * UGameFlowSubsystem::RequestStateOnNextLoad
 *
 * 【大厂标准 - P0 修复】跨地图"下一步状态"意图登记
 *
 * 业务方在调用 OpenLevel 之前调用本接口, 告诉 Subsystem:
 *   "下一张地图加载完后, 请把状态切到 X (而不是默认的 Login)"
 *
 * 典型场景 (退房):
 *   1. 玩家点 LeaveRoom
 *   2. RoomPC::ExecuteLeaveRoom() 调用:
 *        Flow->RequestStateOnNextLoad(EMatchState::MainLobby);
 *        UGameplayStatics::OpenLevel(L_Login, ?offline);
 *   3. 新 GameInstance 上线 → BootToLogin → 显示 LoginPage (1帧)
 *   4. PostLoadMapWithWorld 回调触发 → 看到 PendingPostLoadState=MainLobby
 *      → TransitToState(MainLobby) → 显示 LANRoomPage
 *   5. 玩家无缝看到大厅页 😊
 *
 * @param DesiredState 期望在下一张地图加载完毕后切入的状态
 */
void UGameFlowSubsystem::RequestStateOnNextLoad(EMatchState DesiredState)
{
	// 防呆: 不允许"预约 PreLogin" (无效状态)
	if (DesiredState == EMatchState::PreLogin)
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[GameFlow] RequestStateOnNextLoad: PreLogin 是无效目标, 已忽略"));
		return;
	}

	bHasPendingStateOnNextLoad = true;
	PendingPostLoadState = DesiredState;

	// 【修复 UE 5.6 FormatStringSan】%s 接收函数返回值解引用会触发 C2971
	// 解决: 把表达式结果存到 FString 局部变量, 然后用 *Var 解引用
	const FString StateName = UEnum::GetValueAsString(DesiredState);
	UE_LOG(LogGameFlow, Log, TEXT("[GameFlow] 已预约下一张地图的状态: %d (Name=%s)"),
		(int32)DesiredState, *StateName);
}

/**
 * 【v229 新增】从任意状态跳转到 MainLobby 大厅
 *
 * 业务场景:
 *   - TaskDetailWidget 的"未完成任务"按钮点击 → 跳转 LANRoom
 *   - 在 MainMenu 状态下点击 → 也需要能跳转大厅
 *
 * 流程:
 *   1. TransitToState(MainLobby) → 状态机广播 OnStateChanged
 *   2. UIViewService::OnGameFlowStateChanged → ShowPanel(LANRoom)
 *
 * @note 不做 OpenLevel, MainLobby 是 UI 状态不是地图状态 (L_Login 常驻)
 */
void UGameFlowSubsystem::TransitionToMainLobby()
{
	UE_LOG(LogGameFlow, Log, TEXT("[GameFlow] TransitionToMainLobby: 当前状态=%d, 跳转 MainLobby"),
		(int32)GetCurrentState());

	// TransitToState 会:
	// 1. 更新 CurrentState = MainLobby
	// 2. 广播 OnStateChanged(MainLobby)
	// 3. HandleStateEntry(MainLobby) → break (不 OpenLevel)
	// 4. UIViewService 收到 OnStateChanged → ShowPanel(LANRoom)
	TransitToState(EMatchState::MainLobby, TEXT("TransitionToMainLobby"));
}

/**
 * UGameFlowSubsystem::HandlePostLoadMapWithWorld
 *
 * PostLoadMapWithWorld 全局委托回调 (跨地图持久, 不会因 PC 销毁而丢失)
 *
 * @param LoadedWorld 引擎刚加载好的新 World
 *
 * 行为:
 *   - 如果业务方预约了状态 (bHasPendingStateOnNextLoad=true):
 *     → 强制 TransitToState(PendingPostLoadState)
 *     → 覆盖新 GameInstance 触发的 BootToLogin→Login
 *     → 清零标志, 防止重复消费
 *   - 否则: 走"延迟同步"流程 (大厂核心修复)
 *     → 由于当前状态可能是 Login (GameInstance::Init() 早于 World BeginPlay 触发),
 *       UIViewService::OnGameFlowStateChanged 在 Subsystem 初始化阶段调用 ShowPanel 失败,
 *       那时 GameInstanceSubsystem::GetWorld() 返回 null
 *     → 现在 World ready + PC ready, 主动重新 Broadcast 当前状态, 让 UI 重新拉起
 *     → 这是大厂模式: "World 准备好后, 状态机主动通知 UI 一次"
 */
void UGameFlowSubsystem::HandlePostLoadMapWithWorld(UWorld* LoadedWorld)
{
	// 【P1 Debug】加最详细日志
	UE_LOG(LogGameFlow, Log,
		TEXT("[GameFlow] HandlePostLoadMapWithWorld 触发: LoadedWorld=%s, bHasPendingStateOnNextLoad=%s, CurrentState=%d"),
		LoadedWorld ? TEXT("Valid") : TEXT("NULL"),
		bHasPendingStateOnNextLoad ? TEXT("true") : TEXT("false"),
		(int32)CurrentState);

	// 防御: LoadedWorld 可能为 null (极端情况下)
	if (!LoadedWorld) return;

	// ==========================================
	// 路径 A: 有业务预约 → 切到预约状态
	// ==========================================
	if (bHasPendingStateOnNextLoad)
	{
		// 取预约状态 (拷贝到局部, 防止业务方在回调期间再次修改)
		const EMatchState Desired = PendingPostLoadState;

		// 一次性消费: 先清零, 再切状态 (防御重入)
		bHasPendingStateOnNextLoad = false;
		PendingPostLoadState = EMatchState::PreLogin;

		// 检查目标状态是否合理 (防止枚举值漂移)
		if (Desired == EMatchState::PreLogin)
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[GameFlow] PostLoadMapWithWorld: 预约状态已是 PreLogin, 走默认"));
			// 落到路径 B: 重新 Broadcast 当前状态 (此时已是 Login)
			OnStateChanged.Broadcast(CurrentState);
			return;
		}

		// 【修复 UE 5.6 FormatStringSan】提取到局部 FString 再解引用
		const FString DesiredName = UEnum::GetValueAsString(Desired);
		UE_LOG(LogGameFlow, Log, TEXT("[GameFlow] PostLoadMapWithWorld: 主动切到 %d (Name=%s), 覆盖默认 Login"),
			(int32)Desired, *DesiredName);

		// ==========================================
		// 【大厂 P0 修复 2026.07.03】绕过 TransitToState 的幂等保护
		// ==========================================
		// 旧代码: TransitToState(Desired)
		//   问题: 当 Desired == CurrentState (例如 InRoom) 时, TransitToState 的幂等保护静默 return
		//         → 没有任何 Broadcast(InRoom)
		//         → RoomPC::OnFlowStateChanged(InRoom) 不被调用
		//         → RoomInsidePage 永不创建 → 玩家从 LAN 大厅直接跳到战斗画面
		//
		// 真实场景 (2026.07.03 案例):
		//   1. LANRoomPage::OnCreateSessionComplete 调 TransitToState(InRoom)
		//      → CurrentState=InRoom, Broadcast(InRoom), HandleStateEntry(InRoom) → OpenLevel
		//   2. 进战斗地图, PostLoadMapWithWorld 触发
		//      → bHasPendingStateOnNextLoad=true, Desired=InRoom
		//      → 旧代码: TransitToState(InRoom) → 幂等 return → 没 Broadcast → UI 不显示
		//
		// 新方案 (大厂分层架构 - 状态机幂等保护 vs 跨地图意图消费必须区分):
		//   - 路径 A 是"消费业务方预约的意图", 不应该被幂等保护拦截
		//   - 即使 Desired == CurrentState, 路径 A 也要:
		//     1. 设置 CurrentState (虽然值没变, 但语义上"意图已应用")
		//     2. Broadcast 通知所有订阅者 (UI/PC/HUD)
		//   - 路径 B 已经有同样逻辑 (line 624-625)
		// ==========================================
		CurrentState = Desired;
		UE_LOG(LogGameFlow, Log,
			TEXT("[DEBUG-S7-A][PathA-Broadcast] WorldName=%s Desired=%d (Name=%s) → 强制 Broadcast"),
			LoadedWorld ? *LoadedWorld->GetMapName() : TEXT("NULL"), (int32)Desired, *DesiredName);
		// [DEBUG-v228] PathA 强制广播前 trace
		UE_LOG(LogGameFlow, Log,
			TEXT("[DEBUG-v228][PathA-FORCE-Broadcast] Thread=%u Caller=%s WorldName=%s CurrentState=%d Desired=%d (Name=%s) bHasBootedToLogin=%s"),
			FPlatformTLS::GetCurrentThreadId(),
			ANSI_TO_TCHAR(__FUNCTION__),
			LoadedWorld ? *LoadedWorld->GetMapName() : TEXT("NULL"),
			(int32)CurrentState, (int32)Desired, *DesiredName,
			bHasBootedToLogin ? TEXT("true") : TEXT("false"));
		OnStateChanged.Broadcast(Desired);
		return;
	}

	// ==========================================
	// 路径 B: 【大厂架构 P0 修复 2026.07.03】统一启动入口
	// ==========================================
	// 重构动机:
	//   旧方案在 Subsystem::Initialize 末尾 BootToLogin, 但此时 World 未加载
	//   → Broadcast 时 PC 还不存在, UI 永远拉不起来
	//
	// 新方案 (大厂 Single Shot Pattern):
	//   1. Initialize 只挂管线, 不主动推进状态
	//   2. PostLoadMapWithWorld 触发 = "启动信号" (首次) 或 "World 已重置信号" (后续)
	//   3. 首次 (CurrentState == PreLogin) → 主动 BootToLogin 启动游戏
	//      - 若 bSkipLoginDirectToLobby=true → 切到 MainLobby → 显示 LANRoom
	//      - 若 bSkipLoginDirectToLobby=false → 切到 Login → 显示登录页 (可能触发 OpenLevel 回 L_Login)
	//   4. 后续重置 (CurrentState != PreLogin 且 PendingPostLoadState 有值)
	//      → 已在路径 A 中处理: RequestStateOnNextLoad 强制 TransitToState
	//   5. 任何 PostLoadMapWithWorld (包括跨 OpenLevel 重置):
	//      → 不重置状态, 但 Broadcast 当前状态
	//      → 让 UIViewService 拿到"世界已就绪"信号, 重新拉起 (上次失败的 UI 在这次重试)
	//
	// 优势:
	//   - Single Source of Truth: 启动只发生在 PostLoadMapWithWorld 第一次触发
	//   - 时序保证: World 已加载, PC 已 ready, UI 拉起 100% 成功
	//   - 跨图不丢 UI: 即使 OpenLevel 切图, PostLoadMapWithWorld 重新 Broadcast 让 UI 跟着恢复
	// ==========================================

	const FString WorldName = LoadedWorld ? LoadedWorld->GetMapName() : TEXT("NULL");

	// 【情形 1】首次启动: 当前状态 = PreLogin, 状态机从未推进
	if (CurrentState == EMatchState::PreLogin)
	{
		UE_LOG(LogGameFlow, Log,
			TEXT("[GameFlow] PostLoadMapWithWorld: World 已就绪 (Map=%s, 首次启动), BootToLogin"), *WorldName);
		BootToLogin();

		// BootToLogin 内部会广播 (Login / MainLobby), UIViewService 收到并拉起面板
		// 若 bSkipLoginDirectToLobby=false 触发 OpenLevel(L_Login),
		//   → 新的 PostLoadMapWithWorld 会进入"情形 2"
		return;
	}

	// 【情形 2】跨图 / 重置后: 当前状态 已不是 PreLogin
	//   - 此时 World 已 ready, PC 已就绪, 当前状态合适
	//   - 主动 Broadcast, 让 UIViewService 拉起对应面板
	//   - 这覆盖了: BootToLogin→OpenLevel(L_Login) 后的第二次触发
	const FString CurName = UEnum::GetValueAsString(CurrentState);
	UE_LOG(LogGameFlow, Log,
		TEXT("[GameFlow] PostLoadMapWithWorld: World 已就绪 (Map=%s), 延迟同步当前状态 %d (Name=%s) → UI 拉起"),
		*WorldName, (int32)CurrentState, *CurName);

	// ==========================================
	// 【大厂 P0 修复 2026.07.03】战斗地图落地自愈 (Joiner 容错)
	// ==========================================
	// 场景:
	//   - Joiner 路径漏写 RequestStateOnNextLoad, 或
	//   - RPC 链路异常导致 PendingPostLoadState 被吞
	// → 当前 PostLoadMap 触发时, CurrentState 仍停留在 MainLobby (=3)
	// → Broadcast(MainLobby) 后, RoomPC::OnFlowStateChanged 不创建 RoomInsidePage
	// → 玩家进战斗地图后看到空白 UI, 直到 PostLoadMap 才补救
	//
	// 修复:
	//   检查当前 World 是否是战斗地图 (通过 World 名启发式判断, 避免依赖 GameMode include 循环)
	//   - 是战斗地图 + CurrentState != InRoom → 强制 CurrentState=InRoom + Broadcast(InRoom)
	//   - 路径 A (bHasPendingStateOnNextLoad=true) 已优先处理;
	//     这里处理的是 bHasPendingStateOnNextLoad=false 的"漏预约"边缘场景。
	//
	// 设计原则 (Single Source of Truth):
	//   - 进入战斗地图物理空间 = 必须在 InRoom 状态 (UI 才能正确显示)
	//   - 状态机自愈 (Self-healing) > 依赖业务方人工预约
	// ==========================================
	const bool bIsBattleMapWorld =
		WorldName.Contains(TEXT("Japanese_Temple")) ||
		WorldName.Contains(TEXT("Room")) ||
		WorldName.Contains(TEXT("Battle")) ||
		WorldName.Contains(TEXT("Combat"));
	// [DEBUG-v228] PathB 战斗地图自愈入口 trace
	UE_LOG(LogGameFlow, Log,
		TEXT("[DEBUG-v228][PathB-BattleMapCheck] Thread=%u Caller=%s WorldName=%s bIsBattleMapWorld=%s CurrentState=%d (Name=%s)"),
		FPlatformTLS::GetCurrentThreadId(),
		ANSI_TO_TCHAR(__FUNCTION__),
		*WorldName, bIsBattleMapWorld ? TEXT("true") : TEXT("false"),
		(int32)CurrentState, *CurName);
	if (bIsBattleMapWorld && CurrentState != EMatchState::InRoom)
	{
		const FString CurName2 = UEnum::GetValueAsString(CurrentState);
		UE_LOG(LogGameFlow, Warning,
			TEXT("[GameFlow] PostLoadMapWithWorld: 检测到 World=%s 是战斗地图, 但 CurrentState=%d (%s), 强制修正为 InRoom (Joiner 容错)"),
			*WorldName, (int32)CurrentState, *CurName2);
		CurrentState = EMatchState::InRoom;
		OnStateChanged.Broadcast(EMatchState::InRoom);
		return;
	}

	// 主动 Broadcast (跳过 TransitToState 的幂等保护, 因为状态没变)
	OnStateChanged.Broadcast(CurrentState);
}


// ==========================================
// 【大厂 P0 修复 2026.07.03】双入口保险: OnWorldBeginPlay (PIE 模式 fallback)
// ==========================================

/**
 * UGameFlowSubsystem::HandleWorldBeginPlay
 *
 * PIE 模式专属启动入口, 解决 PostLoadMapWithWorld 在 PIE 模式下不触发的 bug
 *
 * 链路分析 (为什么 PIE 模式 PostLoadMapWithWorld 不触发):
 *   - PostLoadMapWithWorld 是 Editor Mode 下的全局委托
 *   - 在 PIE (Play In Editor) 模式下, 编辑器加载地图走的是另一条路径
 *   - UE 5.6 PIE 模式下, PostLoadMapWithWorld **不触发**
 *   - 这是 UE 已知特性 (Lyra / Fortnite 等都有相同问题, 解决方案是 OnWorldBeginPlay)
 *
 * 时序优势 (为什么 OnWorldBeginPlay 是更好的启动时机):
 *   - PostLoadMapWithWorld: World 刚加载, PC 还没 BeginPlay
 *     → Broadcast 时 UIViewService::ShowPanelWhenPCReady 看到 PC=null → 排队等
 *     → 如果后续没再次 Broadcast, UI 永远拉不起来
 *   - OnWorldBeginPlay: World + 所有 Actor (含 PC) BeginPlay 完毕
 *     → 此时 PC 一定已就绪 → UIViewService 拉起面板 100% 成功
 *
 * 双入口幂等:
 *   - 入口 A (PostLoadMapWithWorld): 独立进程模式生效, PIE 模式不触发
 *   - 入口 B (OnWorldBeginPlay):    PIE 模式生效, 独立进程模式也会触发 (晚于 A)
 *   - 用 bHasBootedToLogin 标志位确保 BootToLogin 只执行一次
 *
 * @param World 触发回调的 World (World 已 BeginPlay)
 */
void UGameFlowSubsystem::HandleWorldBeginPlay(UWorld* World)
{
	// 防御: 自己是否还活着
	if (!IsValid(this)) return;

	UE_LOG(LogGameFlow, Log,
		TEXT("[GameFlow] HandleWorldBeginPlay 触发: World=%s, bHasBootedToLogin=%s, CurrentState=%d"),
		World ? *World->GetMapName() : TEXT("NULL"),
		bHasBootedToLogin ? TEXT("true") : TEXT("false"),
		(int32)CurrentState);

	if (!World) return;

	// ==========================================
	// 路径 1: 跨地图"下一步状态"意图消费 (与 PostLoadMapWithWorld 路径 A 一致)
	// ==========================================
	// 场景: 业务方调 RequestStateOnNextLoad + OpenLevel
	// 旧路径: 由 PostLoadMapWithWorld 在独立进程模式下消费
	// 新路径 (大厂 P0 修复): 在 PIE 模式下, 必须也支持消费同一意图
	//
	// 为什么不复用 HandlePostLoadMapWithWorld 的逻辑?
	//   - 两个回调的 World 时序不同 (PostLoadMap 比 OnWorldBeginPlay 早)
	//   - PostLoadMap 时 PC 未就绪, OnWorldBeginPlay 时 PC 已就绪
	//   - 业务方意图消费要求 100% 可靠, 必须两个入口都能消费
	// ==========================================
	if (bHasPendingStateOnNextLoad)
	{
		const EMatchState Desired = PendingPostLoadState;

		// 一次性消费: 先清零
		bHasPendingStateOnNextLoad = false;
		PendingPostLoadState = EMatchState::PreLogin;

		// [DEBUG-v228] OnWorldBeginPlay 路径 1 入口 trace
		UE_LOG(LogGameFlow, Log,
			TEXT("[DEBUG-v228][OnWorldBeginPlay-Path1] Thread=%u Caller=%s WorldName=%s bHasPendingStateOnNextLoad=true Desired=%d (Name=%s) CurrentState=%d"),
			FPlatformTLS::GetCurrentThreadId(),
			ANSI_TO_TCHAR(__FUNCTION__),
			*World->GetMapName(),
			(int32)Desired, *UEnum::GetValueAsString(Desired),
			(int32)CurrentState);

		if (Desired == EMatchState::PreLogin)
		{
			// 落到 BootToLogin 默认路径
			BootToLogin();
			return;
		}

		const FString DesiredName = UEnum::GetValueAsString(Desired);
		UE_LOG(LogGameFlow, Log,
			TEXT("[GameFlow] OnWorldBeginPlay (PIE 入口 B): 消费预约状态 %d (Name=%s), 覆盖默认 Login"),
			(int32)Desired, *DesiredName);

		// [DEBUG-v228] OnWorldBeginPlay 路径 1 强制 Broadcast 前 trace
		UE_LOG(LogGameFlow, Log,
			TEXT("[DEBUG-v228][OnWorldBeginPlay-Path1-FORCE-Broadcast] Thread=%u Caller=%s CurrentState=%d → Desired=%d (Name=%s)"),
			FPlatformTLS::GetCurrentThreadId(),
			ANSI_TO_TCHAR(__FUNCTION__),
			(int32)CurrentState, (int32)Desired, *DesiredName);

		// 绕过 TransitToState 幂等保护, 强制 Broadcast (同 PostLoadMap 路径 A 逻辑)
		CurrentState = Desired;
		OnStateChanged.Broadcast(Desired);
		return;
	}

	// ==========================================
	// 路径 2: 首次启动 (无业务预约) → 调 BootToLogin 启动游戏
	// ==========================================
	// 这是 PIE 模式 fallback 的核心路径:
	//   - 独立进程模式: PostLoadMapWithWorld 已经触发过, bHasBootedToLogin=true, 下面被拦截
	//   - PIE 模式:      PostLoadMapWithWorld 没触发, bHasBootedToLogin=false, 正常执行
	// ==========================================
	if (CurrentState == EMatchState::PreLogin)
	{
		UE_LOG(LogGameFlow, Log,
			TEXT("[GameFlow] OnWorldBeginPlay (PIE 入口 B): World=%s 已就绪 (PC 也 BeginPlay 完成), BootToLogin (PIE 模式兜底)"),
			*World->GetMapName());
		BootToLogin();
		return;
	}

	// ==========================================
	// 路径 3: 已经 Boot 过 (被 PostLoadMapWithWorld 先抢先) → 同步 Broadcast
	// ==========================================
	// 此时 CurrentState != PreLogin (已 Login / MainLobby / InRoom), bHasBootedToLogin=true
	// 说明 BootToLogin 在入口 A 已经执行过, 这里只做"延迟同步" 让 UI 再拉起一次
	// (冗余但安全, UIViewService 内部有幂等保护)
	const FString CurName = UEnum::GetValueAsString(CurrentState);
	UE_LOG(LogGameFlow, Log,
		TEXT("[GameFlow] OnWorldBeginPlay (PIE 入口 B): BootToLogin 已被入口 A 抢先执行, 延迟同步当前状态 %d (Name=%s)"),
		(int32)CurrentState, *CurName);

	// 【v54.5 Bug 修复】战斗地图自愈逻辑收紧 — 不再误判 Login 状态
	//   旧行为: L_Login 打开后, HandleWorldBeginPlay(L_Login) 走路径 3 → bIsBattleMapWorld 检测
	//          → 误判 L_Login 为战斗地图 (因为 CurMapName=Japanese_Temple 还在旧 World) → 广播 InRoom → Login 页面消失
	//   新行为: 只有 CurrentState 已在 Login 或更高 (MainLobby/InRoom) 时才考虑战斗地图自愈
	//   场景: Joiner 加入战斗地图时, CurrentState=InRoom, 但 World 还是战斗地图 → 正常广播 InRoom
	const FString WorldName = World->GetMapName();
	const bool bIsBattleMapWorld =
		WorldName.Contains(TEXT("Japanese_Temple")) ||
		WorldName.Contains(TEXT("Room")) ||
		WorldName.Contains(TEXT("Battle")) ||
		WorldName.Contains(TEXT("Combat"));

	// [DEBUG-v228] OnWorldBeginPlay 路径 3 入口 trace (WorldName/bIsBattleMapWorld 提前到这里)
	UE_LOG(LogGameFlow, Log,
		TEXT("[DEBUG-v228][OnWorldBeginPlay-Path3-ENTRY] Thread=%u Caller=%s WorldName=%s CurrentState=%d (Name=%s) bIsBattleMapWorld=%s bHasBootedToLogin=%s"),
		FPlatformTLS::GetCurrentThreadId(),
		ANSI_TO_TCHAR(__FUNCTION__),
		*WorldName,
		(int32)CurrentState, *CurName,
		bIsBattleMapWorld ? TEXT("true") : TEXT("false"),
		bHasBootedToLogin ? TEXT("true") : TEXT("false"));
	// 【v54.5 关键修复】排除 Login 状态 — Login 是启动过渡态, 战斗地图自愈不应干预
	if (bIsBattleMapWorld && CurrentState > EMatchState::Login && CurrentState != EMatchState::InRoom)
	{
		UE_LOG(LogGameFlow, Warning,
			TEXT("[GameFlow] OnWorldBeginPlay: 检测到 World=%s 是战斗地图, 但 CurrentState=%d (%s), 强制修正为 InRoom (Joiner 容错)"),
			*WorldName, (int32)CurrentState, *CurName);
		CurrentState = EMatchState::InRoom;
		OnStateChanged.Broadcast(EMatchState::InRoom);
		return;
	}

	// 主动 Broadcast (覆盖 UIViewService 因时序问题没拉起的边缘场景)
	// [DEBUG-v228] OnWorldBeginPlay 路径 3 最终 Broadcast trace
	UE_LOG(LogGameFlow, Log,
		TEXT("[DEBUG-v228][OnWorldBeginPlay-Path3-FINAL-Broadcast] Thread=%u Caller=%s CurrentState=%d (Name=%s)"),
		FPlatformTLS::GetCurrentThreadId(),
		ANSI_TO_TCHAR(__FUNCTION__),
		(int32)CurrentState, *CurName);
	OnStateChanged.Broadcast(CurrentState);
}


void UGameFlowSubsystem::HandleNetworkFailure(UWorld* InWorld, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// ==========================================
	// 【大厂架构 - 抢先 OpenLevel + 独立中断通道】
	// ==========================================
	// 【链路分析】
	//   BroadcastNetworkFailure (UEngine, 13932) 只做 NetworkFailureEvent.Broadcast
	//   真正的 ClientTravel(?closed) 在 UEngine::HandleNetworkFailure 里执行, 通过
	//   FGameDelegates::Get().GetHandleDisconnectDelegate() 触发
	//
	// 【为什么抢先 OpenLevel 能生效】
	//   OpenLevel 内部走 UEngine::Browse, 会立刻触发 World 切换
	//   当 UE 的 ?closed SetClientTravel 后续执行时, World 已经切换完成, ?closed 无效
	//   这是 "时序抢占" 而非 "事件拦截" — 在 UE 5.6 中最可靠的方案
	//
	// 【防重入】
	//   BroadcastNetworkFailure 会被触发多次 (FailureReceived + ConnectionLost)
	//   用 LastNetworkFailureTimestamp 防重入, 1 秒冷却
	static constexpr double CooldownSeconds = 1.0;
	const double Now = FPlatformTime::Seconds();
	if (LastNetworkFailureTimestamp > 0.0 && (Now - LastNetworkFailureTimestamp) < CooldownSeconds)
	{
		UE_LOG(LogGameFlow, Warning,
			TEXT("[GameFlow] HandleNetworkFailure: %.0fms 内忽略重复触发 (FailureType=%d)"),
			(Now - LastNetworkFailureTimestamp) * 1000.0, (int32)FailureType);
		return;
	}
	LastNetworkFailureTimestamp = Now;

	// 检测 HostClosedConnection: FailureReceived + ErrorString 含 "Host closed"
	const bool bIsHostClosed = (FailureType == ENetworkFailure::FailureReceived)
		&& ErrorString.Contains(TEXT("Host closed"));

	if (bIsHostClosed)
	{
		// 【大厂 P0 重构 2026.06.29】转交统一入口 HandleSessionTerminated
		// 不再在这里直接 OpenLevel + 广播, 避免与 SessionManager.OnSessionTerminated 链路重复触发
		UE_LOG(LogGameFlow, Log,
			TEXT("[GameFlow] HandleNetworkFailure: 检测到 HostClosedConnection, 转交 HandleSessionTerminated(MainLobby)"));
		HandleSessionTerminated(EMatchState::MainLobby);
		return;
	}

	// 其他 FailureType (ConnectionLost, ConnectionTimeout 等):
	// 【大厂 P0 重构 2026.06.29】也走 HandleSessionTerminated 统一入口
	UE_LOG(LogGameFlow, Log,
		TEXT("[GameFlow] HandleNetworkFailure: 非 HostClosed (FailureType=%d), 转交 HandleSessionTerminated(MainLobby)"),
		(int32)FailureType);
	HandleSessionTerminated(EMatchState::MainLobby);
}

// ==========================================

void UGameFlowSubsystem::HandleInterrupt(uint8 TargetPanel)
{
	// 【大厂架构 - 独立中断通道】
	// 通用中断入口，强制显示指定面板
	UE_LOG(LogGameFlow, Log,
		TEXT("[GameFlow] HandleInterrupt: 强制中断通道触发 → Panel=%d"),
		(int32)TargetPanel);
	OnInterrupted.Broadcast(static_cast<EUIPanel>(TargetPanel));
}

// ==========================================

void UGameFlowSubsystem::HandleSessionTerminated(EMatchState SuggestedState)
{
	// ==========================================
	// 【大厂 P0 架构 2026.06.29】Session 终止统一处理入口
	// ==========================================
	// 单一事件源: 所有"Session 终止"路径都走这里
	//   - HandleNetworkFailure(HostClosed)         → 引擎断网
	//   - HandleNetworkFailure(Other)              → 其他网络失败
	//   - SessionManager.OnSessionTerminated 触发  → Session 销毁
	//
	// 行为（按大厂标准三步走）:
	//   1. 防重入: 1 秒冷却, 避免 HostClosedConnection 双重回调 + OnSessionTerminated 双重触发
	//   2. 抢先 OpenLevel(L_Login ?offline): 时序抢占 UE 引擎的 ?closed SetClientTravel
	//   3. 触发 OnInterrupted.Broadcast(LANRoom): UIViewService 强制 ShowPanel
	//
	// 【v217 大厂架构修复 — 主动 Leave vs 网络失败 区分】:
	//   - 主动 Leave Room: 玩家点 ReturnToLobby, URoomService::RequestLeaveRoom 已处理 UI 状态
	//     → 这里再 OpenLevel 会导致循环切图(已 L_Login 上再切一次)
	//   - 网络失败 (Host 关闭房间/断网): 这里必须 OpenLevel 抢占 SetClientTravel
	//   - 区分依据: 当前地图是否已经是 L_Login
	//     * 是 → 主动 Leave, 跳过 OpenLevel, 仅触发 OnInterrupted
	//     * 否 → 网络失败, 完整三步走 (OpenLevel + RequestStateOnNextLoad + OnInterrupted)

	static constexpr double CooldownSeconds = 1.0;
	const double Now = FPlatformTime::Seconds();
	if (LastNetworkFailureTimestamp > 0.0 && (Now - LastNetworkFailureTimestamp) < CooldownSeconds)
	{
		UE_LOG(LogGameFlow, Warning,
			TEXT("[GameFlow] HandleSessionTerminated: %.0fms 内忽略重复触发"),
			(Now - LastNetworkFailureTimestamp) * 1000.0);
		return;
	}
	LastNetworkFailureTimestamp = Now;

	// 【v217】检测当前地图 — 已在 L_Login 上时跳过 OpenLevel
	UWorld* World = GetWorld();
	const FString CurrentLevel = World ? World->GetMapName() : TEXT("");
	const bool bIsAlreadyInLobby = CurrentLevel.Contains(TEXT("L_Login"));

	if (bIsAlreadyInLobby)
	{
		// 【v217 主动 Leave 路径】玩家点 ReturnToLobby, URoomService 已切好 UI 状态
		// 这里仅触发 OnInterrupted 确保 UIViewService 也收到切页信号(防 bIsInInterrupted 状态错乱)
		UE_LOG(LogGameFlow, Display,
			TEXT("[GameFlow] 【v217】HandleSessionTerminated: 已在 L_Login 上, 跳过 OpenLevel (主动 Leave 路径)."));

		// 注: URoomService::RequestLeaveRoom 已调 OnInterrupted.Broadcast(LANRoom), 这里
		// 仍再 broadcast 一次 — UIViewService 的 bIsInInterrupted 会忽略重复(bIsInInterrupted 防重入)
		OnInterrupted.Broadcast(EUIPanel::LANRoom);
		return;
	}

	UE_LOG(LogGameFlow, Log,
		TEXT("[GameFlow] HandleSessionTerminated: SuggestedState=%d, 抢先 OpenLevel + 触发 OnInterrupted(LANRoom)"),
		(int32)SuggestedState);

	// 1. 把目标状态持久化到下一张地图 (PostLoadMapWithWorld 会消费)
	RequestStateOnNextLoad(SuggestedState);

	// 2. 抢先 OpenLevel, 时序抢占 ?closed ClientTravel
	//    OpenLevel 内部走 UEngine::Browse, 立刻触发 World 切换
	//    当 UE 后续 ?closed SetClientTravel 执行时, World 已切换, 不再生效
	UGameplayStatics::OpenLevel(this, FName(TEXT("L_Login")), false, TEXT("?offline"));

	// 3. 触发独立中断通道, UIViewService 强制 ShowPanel(LANRoom)
	//    注意: 即使 OpenLevel 后续切图, UIViewService 也是 GameInstanceSubsystem, 跨地图持久
	OnInterrupted.Broadcast(EUIPanel::LANRoom);
}
