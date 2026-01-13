// 1


#include "UI/Activity/Core/ActivitySubsystem.h"

void UActivitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 这里暂时不创建具体 Track
	// 等后续接服务器或配置表
}

void UActivitySubsystem::Deinitialize()
{
	// 清空所有 Track
	ActivityTracks.Empty();

	Super::Deinitialize();
}
