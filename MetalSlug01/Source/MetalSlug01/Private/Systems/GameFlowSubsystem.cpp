// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本子系统的头文件
#include "Systems/GameFlowSubsystem.h"

// 引入 UGameplayStatics 类（提供 OpenLevel 等静态函数）
// 作用: 用于执行关卡切换、玩家查询等通用静态操作
#include "Kismet/GameplayStatics.h"

// 引入 UWorld 类头文件
// 作用: 用于获取当前世界对象、当前地图名等运行时信息
#include "Engine/World.h"

// 引入活动 DataTable 集中加载服务 (启动期一次性检查所有表)
#include "Data/FActivityDataTableService.h"

// 引入统一日志通道
#include "Logs/MetalSlugLogChannels.h"

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
}

void UGameFlowSubsystem::Deinitialize()
{
	// 清理活动表缓存 (GC 友好)
	FActivityDataTableService::Shutdown();
	Super::Deinitialize();
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
		// 大厅态（局域网房间）：不跳转地图，留在 L_Login
		// 管家通过广播让 LoginPlayerController 切换 UI（LANRoomPage）
		// 如果玩家刚从战斗地图退房出来，需要先切回 L_Login
		if (!CurrentMapName.Contains(TEXT("L_Login")))
		{
			// 【架构规范】: 从多人联机关卡退回单机主菜单，必须加 "?offline" 参数！
			// 作用: 强制 UE 引擎清理底层的 NetDriver 网络连接，防止端口被死锁占用
			UGameplayStatics::OpenLevel(this, FName("L_Login"), true, TEXT("?offline"));
		}
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

	case EMatchState::PostBattle:
		// 结算阶段，留在当前地图弹出战绩面板
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
