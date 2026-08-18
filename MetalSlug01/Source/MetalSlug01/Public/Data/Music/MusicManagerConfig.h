// 版权声明：在项目设置的描述页面填写您的版权信息。

// ========================================================================
// MusicManagerConfig.h — 背景音乐配置数据资产 头文件
// ========================================================================
//
// 文件功能总览:
//   - 声明 UMusicManagerConfig 类(继承 UDataAsset),用于在 UE 编辑器中配置
//     4 种场景的背景音乐资产(主菜单 / 活动页 / 房间 / 战斗)
//   - 每个音乐条目 = USoundBase 资产 + 是否循环播放
//
// 大厂架构角色:
//   - 单一真理源: 背景音乐配置全部由 DA_MusicManagerConfig.uasset 提供
//   - 数据驱动: 严禁在 Subsystem 中硬编码资产路径
//   - 由 UMusicManagerSubsystem 在运行时读取并 Play
//
// 编辑器配置路径:
//   Content Browser → Right Click → Miscellaneous → Data Asset → UMusicManagerConfig
// ========================================================================

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"           // UE 引擎核心最小化头文件
#include "Engine/DataAsset.h"       // UDataAsset 基类
#include "Sound/SoundBase.h"        // USoundBase(兼容 SoundCue / SoundWave / MetaSoundSource)
#include "MusicManagerConfig.generated.h"  // UE 自动生成的反射代码头

// 前向声明
class USoundBase;

/**
 * @class UMusicManagerConfig
 * @brief 背景音乐配置数据资产
 *
 * 【大厂架构 - L2 Data Asset Layer】
 *
 * 职责说明:
 * - 在 UE 编辑器中配置 4 种场景的背景音乐资产
 * - 作为 UMusicManagerSubsystem 的配置来源
 *
 * 使用方法:
 * 1. 在 Content Browser 中创建 DataAsset: Right Click → Miscellaneous → Data Asset
 * 2. 选择 UMusicManagerConfig 作为类
 * 3. 在 Details 面板中配置各类型音乐
 * 4. 在 World Settings → Game Instance Override 中引用此配置
 *
 * 【大厂原则 - 单一真理源】
 * - 音乐资产配置 = DA_MusicManagerConfig
 * - 不允许在 Subsystem 中硬编码资产路径
 */
UCLASS()
class METALSLUG01_API UMusicManagerConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// ==========================================
	// 主菜单音乐
	// ==========================================

	/** 主菜单/大厅背景音乐 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music|MainMenu")
	TObjectPtr<USoundBase> MainMenuMusic;

	/** 主菜单音乐是否循环播放 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music|MainMenu", meta = (EditCondition = "MainMenuMusic"))
	bool MainMenuMusicLoop = true;

	// ==========================================
	// 活动页面音乐
	// ==========================================

	/** 活动页面背景音乐 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music|Activity")
	TObjectPtr<USoundBase> ActivityMusic;

	/** 活动页面音乐是否循环播放 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music|Activity", meta = (EditCondition = "ActivityMusic"))
	bool ActivityMusicLoop = true;

	// ==========================================
	// 房间音乐
	// ==========================================

	/** 房间背景音乐 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music|Room")
	TObjectPtr<USoundBase> RoomMusic;

	/** 房间音乐是否循环播放 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music|Room", meta = (EditCondition = "RoomMusic"))
	bool RoomMusicLoop = true;

	// ==========================================
	// 战斗音乐
	// ==========================================

	/** 战斗背景音乐 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music|Battle")
	TObjectPtr<USoundBase> BattleMusic;

	/** 战斗音乐是否循环播放 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music|Battle", meta = (EditCondition = "BattleMusic"))
	bool BattleMusicLoop = true;
};
