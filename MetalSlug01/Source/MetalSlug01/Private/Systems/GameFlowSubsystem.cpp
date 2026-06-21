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

// ============== 生命周期 ==============

void UGameFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 启动期一次性访问每张活动表, 触发 LoadSynchronous 并打印缺失列表
	// 设计: 故意不在这里直接调 GetMissingTables (那样只会检查未访问过的表)
	//       而是逐个 Get 一遍, 确保所有活动 DT 都被加载
	UE_LOG(LogGameFlow, Log, TEXT("[GameFlow] 启动: 预加载活动 DataTable..."));
	const TArray<FName> Missing = FActivityDataTableService::GetMissingTables();
	if (Missing.Num() > 0)
	{
		// 改造: TArray<FName> 不能直接 FString::Join, 改用手动拼接
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
	// 【大厂核心修复 2026.06.28】在 Subsystem::Initialize 末尾直接 BootToLogin
	// ==========================================
	// 背景:
	//   之前依赖 UMetalSlugGameInstance::Init() 末尾调 BootToLogin()
	//   但 PIE 启动时, GameInstance::Init() 可能在 Subsystem::Initialize 之前/之后
	//   表现不一致 → 出现"NewState=0 (PreLogin)"的玩家看不到登录页 bug
	//
	// 大厂方案:
	//   "状态机自己负责自己的状态推进" —— Subsystem 是状态的唯一权威
	//   在自己 Initialize 完成时, 主动把状态从 PreLogin 推到 Login
	//   不依赖任何外部 GameInstance 调用
	//
	// 时序:
	//   1. Subsystem::Initialize() 内部订阅 OnStateChanged (其他 Subsystem 也在 Initialize, 此时它们尚未订阅 → 没关系)
	//   2. 主动 BootToLogin → Broadcast(Login)
	//   3. 此时尚未订阅的 UIViewService 不会收到这次广播 (正常)
	//   4. UIViewService::Initialize() 订阅 OnStateChanged → 此时 CurrentState=Login, 但不会收到 broadcast
	//   5. World BeginPlay 完成 → PostLoadMapWithWorld 触发 → Broadcast(CurrentState=Login) → UIViewService 收到 → ShowPanel(Login) ✅
	//
	// 这个方案的关键:
	//   PostLoadMapWithWorld 回调 (路径 B 延迟同步) 是 UI 显示的真正"触发器"
	//   Subsystem::Initialize 里的 BootToLogin 只是"把状态从 PreLogin 推走"
	//   两者职责清晰, 不冲突
	UE_LOG(LogGameFlow, Log, TEXT("[GameFlow] Subsystem::Initialize 末尾主动触发 BootToLogin"));
	BootToLogin();
}

void UGameFlowSubsystem::Deinitialize()
{
	// 清理活动表缓存 (GC 友好)
	FActivityDataTableService::Shutdown();

	// 【P0】解绑全局 PostLoadMapWithWorld 委托, 防止野指针回调
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
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
	// 【大厂架构 - 测试绕行通道 2026.06.30】
	// 检查 bSkipLoginDirectToLobby 配置项，短路跳过正常登录流程
	// ==========================================
	// 入口位置选在 BootToLogin() 的原因:
	//   - BootToLogin 是状态机的唯一"启动入口"，所有冷启动都经过这里
	//   - 此时 CurrentState == PreLogin，未被任何逻辑污染
	//   - 可以干净地分流: 正常账号走 Login，测试开关走 MainLobby
	//
	// 设计优势:
	//   - 不改动 TransitToState (TransitToState 是通用状态切换器，不应包含业务开关)
	//   - 不改动 HandleStateEntry (HandleStateEntry 是物理地图操作层，职责单一)
	//   - 开关检查内聚在"流程决策"层，符合大厂分层架构原则
	// ==========================================
	const UMetalSlugTestSettings* TestSettings = GetDefault<UMetalSlugTestSettings>();
	if (TestSettings && TestSettings->bSkipLoginDirectToLobby)
	{
		// 1. 调用 AccountSubsystem::MockLoginForTesting() 分配内存中的临时账号
		//    行为: 在 AccountData 中注入一个随机 TestUser_XXXX，CurrentLoggedInUser 指向它
		//    特性: 不写盘，关闭游戏后自动销毁，不污染真实存档
		if (UAccountSubsystem* AccountSub = GetGameInstance()->GetSubsystem<UAccountSubsystem>())
		{
			AccountSub->MockLoginForTesting();
		}
		else
		{
			UE_LOG(LogGameFlow, Error,
				TEXT("[GameFlow][测试绕行] AccountSubsystem 不可用，跳过 MockLogin"));
		}

		// 2. 切到 MainLobby 让 UI 可见（LANRoom 面板，留在 L_Login 常驻地图）
		TransitToState(EMatchState::MainLobby);

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
void UGameFlowSubsystem::TransitToState(EMatchState NewState)
{
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
		// 【架构精进】进入房间态时，物理跳转到指定的对战地图
		if (TargetRoomMapName != NAME_None)
		{
			// 仅当当前不在目标地图时才执行 OpenLevel，避免无谓重载
			if (!CurrentMapName.Contains(TargetRoomMapName.ToString()))
			{
				UGameplayStatics::OpenLevel(this, TargetRoomMapName);
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

		// 关键: 直接 TransitToState, 覆盖 BootToLogin 设的 Login
		// 大厂设计: 用现有状态机入口走, 享受"安全校验 + 广播 + HandleStateEntry"的完整链路
		//
		// 【新架构说明 - 2026.06.28】
		// TransitToState 幂等保护导致的静默跳过, 现在由独立中断通道兜底:
		//   网络失败 → HandleNetworkFailure → OnInterrupted.Broadcast(LANRoom) → UIViewService → ShowPanel
		//   状态机的静默跳过不影响 UI, 因为 UI 通过 OnInterrupted 通道已刷新
		TransitToState(Desired);
		return;
	}

	// ==========================================
	// 路径 B: 【大厂架构 - P0 修复 2026.06.28】延迟同步当前状态
	// ==========================================
	// 场景:
	//   UMetalSlugGameInstance::Init() 早于 World BeginPlay,
	//   → BootToLogin() 调 TransitToState(Login) → Broadcast(Login)
	//   → UIViewService.OnGameFlowStateChanged(Login) → ShowPanel(Login)
	//   → GetLocalPlayerController() 返回 null (World 还没创建)
	//   → ShowPanel 失败 → 用户看不到登录页 💀
	//
	// 修复:
	//   PostLoadMapWithWorld 触发时, World BeginPlay 已完成, PC 已 ready.
	//   此时主动 Broadcast 当前状态, 让 UIViewService 重新拉起 (这次会成功).
	//
	// 大厂设计哲学:
	//   "事件驱动 + 状态机主动补偿" — 状态机知道自己的初始 broadcast 时机太早,
	//   所以在 World ready 后补偿一次, 让所有 UI 重新同步.
	//   这比让 UIViewService 自己实现重试机制更干净 (职责更清晰).
	// ==========================================

	// 取当前状态的描述 (用于日志, 修复 UE 5.6 FormatStringSan)
	const FString CurName = UEnum::GetValueAsString(CurrentState);
	UE_LOG(LogGameFlow, Log,
		TEXT("[GameFlow] PostLoadMapWithWorld: World 已就绪, 延迟同步当前状态 %d (Name=%s) → UI 拉起"),
		(int32)CurrentState, *CurName);

	// 关键: 直接 Broadcast, 跳过 TransitToState 的幂等保护
	// 原因: 此时 CurrentState 已经是 Login (Init 阶段切过来的),
	//       如果用 TransitToState 会被"状态相同"保护拦截
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
