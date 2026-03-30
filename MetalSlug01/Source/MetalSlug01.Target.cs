using UnrealBuildTool;
using System.Collections.Generic;


public class MetalSlug01Target : TargetRules
{
	public MetalSlug01Target(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;

		ExtraModuleNames.AddRange( new string[] { "MetalSlug01" } );
		
		// --- 增加以下代码 ---
		// 开启 Unity Build，将小文件合并大文件，减少 CPU 频繁切换任务的开销
		bUseUnityBuild = true;
		// 开启自适应 Unity Build
		bAdaptiveUnityDisablesOptimizations = true;
		// 如果内存够快，可以关闭磁盘缓存加速
		 bUsePCHFiles = true;
	}
}
