// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MusicProjectSettings.generated.h"

// 前向声明
class USoundBase;

/**
 * @class UMusicProjectSettings
 * @brief 背景音乐项目设置
 *
 * 【大厂架构 - Project Settings Layer】
 *
 * 配置位置：Edit → Project Settings → MetalSlug01 → Music
 *
 * 注意：使用 TSoftObjectPtr 而非 TObjectPtr，因为 Config 不支持直接对象引用
 */
UCLASS(Config = Game, DefaultConfig, Meta = (DisplayName = "Music Settings"))
class METALSLUG01_API UMusicProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// ==========================================
	// 主菜单音乐
	// ==========================================

	/** 主菜单/大厅背景音乐 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Music|MainMenu",
		meta = (DisplayName = "Main Menu Music", AllowedClasses = "SoundBase"))
	TSoftObjectPtr<USoundBase> MainMenuMusic;

	/** 主菜单音乐是否循环播放 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Music|MainMenu",
		meta = (DisplayName = "Loop Main Menu Music"))
	bool MainMenuMusicLoop = true;

	// ==========================================
	// 活动页面音乐
	// ==========================================

	/** 活动页面背景音乐 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Music|Activity",
		meta = (DisplayName = "Activity Music", AllowedClasses = "SoundBase"))
	TSoftObjectPtr<USoundBase> ActivityMusic;

	/** 活动页面音乐是否循环播放 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Music|Activity",
		meta = (DisplayName = "Loop Activity Music"))
	bool ActivityMusicLoop = true;

	// ==========================================
	// 房间音乐
	// ==========================================

	/** 房间背景音乐 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Music|Room",
		meta = (DisplayName = "Room Music", AllowedClasses = "SoundBase"))
	TSoftObjectPtr<USoundBase> RoomMusic;

	/** 房间音乐是否循环播放 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Music|Room",
		meta = (DisplayName = "Loop Room Music"))
	bool RoomMusicLoop = true;

	// ==========================================
	// 战斗音乐
	// ==========================================

	/** 战斗背景音乐 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Music|Battle",
		meta = (DisplayName = "Battle Music", AllowedClasses = "SoundBase"))
	TSoftObjectPtr<USoundBase> BattleMusic;

	/** 战斗音乐是否循环播放 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Music|Battle",
		meta = (DisplayName = "Loop Battle Music"))
	bool BattleMusicLoop = true;

	// ==========================================
	// 全局设置
	// ==========================================

	/** 全局音乐音量 (0.0 - 1.0) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Music|Global",
		meta = (DisplayName = "Master Music Volume", ClampMin = "0.0", ClampMax = "1.0"))
	float MasterMusicVolume = 1.0f;

	/** 音乐淡入淡出时间（秒） */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Music|Global",
		meta = (DisplayName = "Crossfade Duration", ClampMin = "0.0", ClampMax = "5.0"))
	float CrossfadeDuration = 1.0f;
};
