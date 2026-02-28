
void UUpgradeActivitySaveModifier::ForceRefreshAllPages()
{
	if (!TargetSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ ForceRefreshAllPages: TargetSubsystem为空"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("🔄 FORCE_PAGE_REFRESH_START"));
	UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem地址: %p"), TargetSubsystem);
	UE_LOG(LogTemp, Log, TEXT("📊 当前数据状态:"));
	UE_LOG(LogTemp, Log, TEXT("   RecordDate: %d"), TargetSubsystem->GetRecord().GetDayNumber());
	UE_LOG(LogTemp, Log, TEXT("   CurrentExperience: %d"), TargetSubsystem->GetRecord().CurrentExperience);
	UE_LOG(LogTemp, Log, TEXT("   RewardIconIndex: %d"), TargetSubsystem->GetRecord().RewardIconIndex);
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
	
	// 触发全局刷新事件，强制所有页面重新获取内存数据
	UE_LOG(LogTemp, Log, TEXT("🔊 触发OnGlobalRefresh.Broadcast() - 强制页面刷新"));
	TargetSubsystem->OnGlobalRefresh.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("✅ OnGlobalRefresh.Broadcast()执行完成"));
	
	// 触发奖励图标变更事件
	UE_LOG(LogTemp, Log, TEXT("🔊 触发OnRewardIconIndexChanged.Broadcast()"));
	TargetSubsystem->OnRewardIconIndexChanged.Broadcast(TargetSubsystem->GetCurrentRewardIconIndex());
	UE_LOG(LogTemp, Log, TEXT("✅ OnRewardIconIndexChanged.Broadcast()执行完成"));
	
	UE_LOG(LogTemp, Log, TEXT("\n🎯 页面刷新完成 - 所有UI组件已重新获取最新内存数据"));
}

void UUpgradeActivitySaveModifier::AutoSaveOnGameExit()
{
	if (!TargetSubsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ AutoSaveOnGameExit: TargetSubsystem为空，无需保存"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("💾 AUTO_SAVE_ON_GAME_EXIT_START"));
	UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem地址: %p"), TargetSubsystem);
	UE_LOG(LogTemp, Log, TEXT("📊 执行游戏退出自动保存"));
	UE_LOG(LogTemp, Log, TEXT("⏰ 保存时间: %s"), *FDateTime::Now().ToString());
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
	
	// 保存当前内存数据到磁盘
	TargetSubsystem->SaveStatus();
	
	UE_LOG(LogTemp, Log, TEXT("✅ 游戏退出自动保存完成 - 所有内存修改已持久化到磁盘"));
}
