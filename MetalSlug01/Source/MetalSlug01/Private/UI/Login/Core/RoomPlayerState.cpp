#include "UI/Login/Core/RoomPlayerState.h"
// 【极其关键】：必须包含此头文件才能使用 DOREPLIFETIME 宏
#include "Net/UnrealNetwork.h" 

ARoomPlayerState::ARoomPlayerState()
{
	// 默认初始化
	CurrentTeam = ERoomTeam::None;
	bIsReady = false;
	
	// 确保 PlayerState 开启网络同步
	bReplicates = true;
}

void ARoomPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 【核心规范】：在这里注册变量。DOREPLIFETIME 会让引擎底层接管这些变量的网络同步
	DOREPLIFETIME(ARoomPlayerState, CurrentTeam);
	DOREPLIFETIME(ARoomPlayerState, bIsReady);
}

// 只有客户端会在变量改变时自动执行这些 OnRep 函数
void ARoomPlayerState::OnRep_Team()
{
	// 队伍发生变化，通知 UI 刷新
	OnStateChanged.Broadcast();
}

void ARoomPlayerState::OnRep_IsReady()
{
	// 准备状态发生变化，通知 UI 刷新
	OnStateChanged.Broadcast();
}