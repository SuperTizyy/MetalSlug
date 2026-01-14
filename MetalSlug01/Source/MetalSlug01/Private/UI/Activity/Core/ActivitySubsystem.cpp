// 1


#include "UI/Activity/Core/ActivitySubsystem.h"

#include "UI/Activity/Track/DailyLogin/DailyLoginTrack.h"
#include "UI/Activity/Track/Treasure/TreasureTrack.h"

void UActivitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ================= 创建 DailyLogin Track =================
	DailyLoginTrack = NewObject<UDailyLoginTrack>(this);
	if (DailyLoginTrack)
	{
		DailyLoginTrack->Initialize(7);
	}

	// ================= 创建 Treasure Track =================
	TreasureTrack = NewObject<UTreasureTrack>(this);
	if (TreasureTrack)
	{
		TreasureTrack->Init();
	}
}

void UActivitySubsystem::Deinitialize()
{
	// Subsystem 销毁时，Track 会随 GC 自动回收
	DailyLoginTrack = nullptr;
	TreasureTrack = nullptr;

	Super::Deinitialize();
}

UDailyLoginTrack* UActivitySubsystem::GetDailyLoginTrack() const
{
	return DailyLoginTrack;
}

UTreasureTrack* UActivitySubsystem::GetTreasureTrack() const
{
	return TreasureTrack;
}
