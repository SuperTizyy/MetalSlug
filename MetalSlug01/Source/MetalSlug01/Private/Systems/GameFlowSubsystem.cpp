#include "Systems/GameFlowSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

void UGameFlowSubsystem::TransitToState(EMatchState NewState)
{
	// 【安全防御】防止同一状态被重复调用，导致地图无限重启或死循环
	if (CurrentState == NewState)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GameFlowSubsystem] TransitToState Ignored: Already in state %d"), (int32)NewState);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[GameFlowSubsystem] State Transiting: %d -> %d"), (int32)CurrentState, (int32)NewState);
	
	// 更新当前状态
	CurrentState = NewState;

	// 1. 广播状态改变事件（通知 Controller 准备切换 UI）
	OnStateChanged.Broadcast(CurrentState);

	// 2. 执行地图物理切换逻辑
	HandleStateEntry(CurrentState);
}

void UGameFlowSubsystem::HandleStateEntry(EMatchState State)
{
	UWorld* World = GetWorld();
	if (!World) return;

	FString CurrentMapName = World->GetMapName();

	switch (State)
	{
	case EMatchState::Login:
		// 如果当前不在登录地图，则强制跳转回起点
		if (!CurrentMapName.Contains(TEXT("L_Login")))
		{
			UGameplayStatics::OpenLevel(this, FName("L_Login"));
		}
		break;

	case EMatchState::MainLobby:
		// 如果当前不在登录地图 (例如玩家刚从沙漠战斗房间退房出来)，则物理跳转回去
		if (!CurrentMapName.Contains(TEXT("L_Login")))
		{
			// 【架构规范】：从多人联机关卡退回单机主菜单，必须加 "?offline" 参数！
			// 这会强制 UE 引擎清理底层的 NetDriver 网络连接，防止端口被死锁占用。
			UGameplayStatics::OpenLevel(this, FName("L_Login"), true, TEXT("?offline"));
		}
		// 如果已经在 L_Login，那就什么都不做，管家会通过广播让 LoginPlayerController 自己切 UI
		break;

	case EMatchState::InRoom:
		// 【架构精进】进入房间态时，物理跳转到指定的对战地图
		if (TargetRoomMapName != NAME_None)
		{
			if (!CurrentMapName.Contains(TargetRoomMapName.ToString()))
			{
				UGameplayStatics::OpenLevel(this, TargetRoomMapName);
			}
		}
		else
		{
			// 工业级防呆报错：防止 UI 没传地图名字就硬切状态
			UE_LOG(LogTemp, Error, TEXT("[GameFlowSubsystem] TargetRoomMapName is NONE! Cannot transit to InRoom."));
		}
		break;

	case EMatchState::Battleing:
		// 【架构精进】战斗态和房间态在同一个地图！
		// 不需要物理跳转。事件广播出去后，交给 RoomPlayerController / RoomGameMode 
		// 去把“房间UI”隐藏，并把“准星血条UI”挂出来。
		break;

	case EMatchState::PostBattle:
		// 结算阶段，留在当前地图弹出战绩面板
		break;

	default:
		break;
	}
}

void UGameFlowSubsystem::SetTargetRoomMapName(FName MapName)
{
	TargetRoomMapName = MapName;
}