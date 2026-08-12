// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Enums/CoreEnums.h"
#include "MusicManagerSubsystem.generated.h"

// 前向声明
class USoundBase;
class UAudioComponent;
class UGameFlowSubsystem;

/**
 * @enum EMusicType
 * @brief 背景音乐类型枚举
 */
UENUM(BlueprintType)
enum class EMusicType : uint8
{
	None     UMETA(DisplayName = "None"),
	MainMenu UMETA(DisplayName = "Main Menu"),
	Activity UMETA(DisplayName = "Activity"),
	Room     UMETA(DisplayName = "Room"),
	Battle   UMETA(DisplayName = "Battle")
};

/**
 * @struct FMusicConfig
 * @brief 单个音乐配置
 */
USTRUCT(BlueprintType)
struct FMusicConfig
{
	GENERATED_BODY()

	/** 音乐资产 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	TObjectPtr<USoundBase> Sound = nullptr;

	/** 循环播放标志 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music", meta = (ExposeOnSpawn = "true"))
	bool bLoop = true;

	/** 音量乘数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music", meta = (ExposeOnSpawn = "true"))
	float VolumeMultiplier = 1.0f;

	/** 音高乘数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music", meta = (ExposeOnSpawn = "true"))
	float PitchMultiplier = 1.0f;
};

/**
 * @class UMusicManagerSubsystem
 * @brief 背景音乐管理系统（GameInstance 级别）
 *
 * 职责:
 * - 管理 4 种场景的背景音乐
 * - 通过 Blueprint 事件驱动切换音乐
 *
 * 架构:
 * - 全局唯一: GameInstance 子系统
 * - 单一播放: 同时只播放一个背景音乐
 */
UCLASS()
class METALSLUG01_API UMusicManagerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ==========================================
	// 静态方法
	// ==========================================

	/** 获取 MusicManagerSubsystem 实例（Blueprint 方便调用） */
	UFUNCTION(BlueprintPure, Category = "Music", meta = (WorldContext = "WorldContextObject"))
	static UMusicManagerSubsystem* Get(UObject* WorldContextObject)
	{
		if (!WorldContextObject) return nullptr;
		if (UWorld* World = WorldContextObject->GetWorld())
		{
			if (UGameInstance* GI = World->GetGameInstance())
			{
				return GI->GetSubsystem<UMusicManagerSubsystem>();
			}
		}
		return nullptr;
	}

	// ==========================================
	// Subsystem 生命周期
	// ==========================================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ==========================================
	// 公共 API
	// ==========================================

	/** 手动切换到指定音乐类型 */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void PlayMusic(EMusicType MusicType);

	/** 停止所有背景音乐 */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void StopAllMusic(float FadeOutDuration = 0.0f);

	/** 获取当前播放的音乐类型 */
	UFUNCTION(BlueprintPure, Category = "Music")
	EMusicType GetCurrentMusicType() const { return CurrentMusicType; }

	/** 检查指定类型的音乐是否正在播放 */
	UFUNCTION(BlueprintPure, Category = "Music")
	bool IsMusicPlaying(EMusicType MusicType) const;

	/** 当游戏状态变化时调用此方法（自动订阅 GameFlowSubsystem） */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void OnGameFlowStateChanged(EMatchState NewState);

	/** 【v228 新增】当 UI 面板切换时调用此方法（自动订阅 UIViewService::OnPanelChanged） */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void OnUIViewPanelChanged(EUIPanel OldPanel, EUIPanel NewPanel);

	/** 【v228 新增】切换到活动页面音乐（供 GameMenuPage 等直接调用） */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void PlayActivityMusic();

	/** 【v228 新增】恢复之前的音乐（活动页面关闭时调用） */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void RestorePreviousMusic();

	// ==========================================
	// 配置
	// ==========================================
	// 注意: 配置现在通过 UMusicProjectSettings (Edit → Project Settings → MetalSlug01 → Music) 管理
	// 废弃的 UMusicManagerConfig DataAsset 不再使用

private:
	/** 加载配置 */
	void LoadMusicConfigFromAsset();

	/** 【v228 新增】尝试绑定 UIViewService（延迟调用） */
	void TryBindToUIViewService();

	/** 根据状态映射到音乐类型 */
	EMusicType MapStateToMusicType(uint8 MatchState) const;

	/** 播放指定类型的音乐 (内部实现，包含停止旧音乐的逻辑) */
	void PlayMusicInternal(EMusicType MusicType);

	// ==========================================
	// 数据成员
	// ==========================================

	/** 当前播放的音乐类型 */
	UPROPERTY(Transient)
	EMusicType CurrentMusicType = EMusicType::None;

	/** 当前播放的 AudioComponent 槽位A */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> TrackA;

	/** 当前播放的 AudioComponent 槽位B */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> TrackB;

	/** 当前激活的槽位 (true=TrackA, false=TrackB) */
	UPROPERTY(Transient)
	bool bTrackAIsActive = true;

	/** 各类型音乐配置 */
	UPROPERTY(Transient)
	TMap<EMusicType, FMusicConfig> RuntimeMusicConfigs;

	/** 【v228 新增】是否处于活动页面模式 */
	UPROPERTY(Transient)
	bool bIsInActivityMode = false;

	/** 【v228 新增】活动页面之前的音乐类型（用于恢复） */
	UPROPERTY(Transient)
	EMusicType PreviousMusicType = EMusicType::None;

	/** 跨关卡持久化（SpawnSound2D 参数） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music|AudioComponent",
		meta = (AllowPrivateAccess = "true"))
	bool bPersistAcrossLevelTransition = true;

	/** 淡出完成后自动销毁（SpawnSound2D 参数） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music|AudioComponent",
		meta = (AllowPrivateAccess = "true"))
	bool bAutoDestroyOnFadeComplete = false;
};
