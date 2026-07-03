// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class MetalSlug01 : ModuleRules
{
	public MetalSlug01(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { 
			"Core", "CoreUObject", "Engine", "InputCore", "UMG", "Paper2D", 
			"EnhancedInput", "GameplayTags", "Json", "JsonUtilities","Slate", 
			"SlateCore","OnlineSubsystem", "OnlineSubsystemNull", "OnlineSubsystemUtils","DeveloperSettings","AIModule",
			// 【大厂 P0 修复 2026.06.28】ENetworkFailure::Type 来自 NetCore 模块
			// 用于 GameFlowSubsystem 的 HandleNetworkFailure 回调参数类型
			"NetCore",
			// 【P0 链接修复 2026.07.03】UNavigationSystemV1 / ARecastNavMesh 来自 NavigationSystem 模块
			// 用于 BaseAIController 启动期 NavMesh 可用性探测
			"NavigationSystem" });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
