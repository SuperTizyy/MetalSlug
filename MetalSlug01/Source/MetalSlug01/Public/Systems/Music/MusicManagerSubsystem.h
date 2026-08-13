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
 *
 * 设计原则 (大厂 v260):
 *   - 公共 API (PlayMusic) 不暴露 ZombieBattle
 *   - 业务方只通过 EnterBattleMode() 入口触发, 内部根据 ERoomMatchMode 路由
 *   - ZombieBattle 仅作为 Subsystem 内部 TMap key + PlayMusicInternal 调试日志标签使用
 *   - BP 业务蓝图不应直接调用 PlayMusic(ZombieBattle), 走 EnterBattleMode 才符合零兑底要求
 */
UENUM(BlueprintType)
enum class EMusicType : uint8
{
	None        UMETA(DisplayName = "None"),
	MainMenu    UMETA(DisplayName = "Main Menu"),
	Activity    UMETA(DisplayName = "Activity"),
	Room        UMETA(DisplayName = "Room"),
	Battle      UMETA(DisplayName = "Battle (Melee)"),
	// 【v260】生化战斗音乐 - 仅 Zombie 模式触发 (EnterBattleMode 内部路由)
	ZombieBattle UMETA(DisplayName = "Battle (Zombie) - 仅内部路由使用")
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
	// 【v260 大厂架构新增】生化模式战斗音乐专用入口
	// ==========================================
	//
	// 设计动机 (用户 2026.08.14):
	//   - 刀战模式战斗态: 不播 BGM (StopAllMusic)
	//   - 生化模式战斗态: 播专属音乐 (ZombieBattleMusic 配置)
	//
	// 调用原则:
	//   - 外部触发链路 (GameFlow / UIViewService) 进入 Battleing/BattleHUD 状态时统一调此 API
	//   - 由本 API 内部根据 ERoomMatchMode 路由:
	//       Zombie → PlayMusic(EMusicType::ZombieBattle)
	//       Melee  → StopAllMusic(零兑底, 真的不播任何)
	//       None / Other → 不动作 (Log Error, 进入安全状态)
	//
	// 为什么不扩展 EMusicType 到公共 API:
	//   - EMusicType 是 BlueprintType, 暴露给 BP 后会被业务方锁死
	//   - 模式路由是子系统内部决策, 业务方只需要"进入 Battleing"这个意图
	//   - 0 改动调用方代码 (GameFlow / UIView 现有触发链路不变)
	//
	// 配置缺失 (零兑底):
	//   - ZombieBattleMusic 未配置 → Log Error, 静默停止一切 BGM
	//   - 不会 fallback 到 BattleMusic (避免误导用户以为生效了)
	//
	// ==========================================

	/** 【v260】根据当前房间模式自动路由战斗 BGM, 业务方统一入口 */
	UFUNCTION(BlueprintCallable, Category = "Music")
	void EnterBattleMode();

	/** 【v260】查询当前战斗 BGM 是否正在播放（包括 ZombieBattle） */
	UFUNCTION(BlueprintPure, Category = "Music")
	bool IsBattleMusicPlaying() const;

	// ==========================================
	// 【v260 大厂架构新增】内部模式路由代码（移到 public 区）
	// ==========================================
	//
	// 为什么放在 public 而不是 private:
	//   MSVC 在 .cpp 实现嵌套 private enum class 成员函数时, 返回类型 ERoomMatchCode
	//   在 UMusicManagerSubsystem:: 域外解析失败, 触发 C2143/C2556/C2371 雪崩.
	//   放 public 后, 嵌套类型可见, 编译器跨作用域查找正常.
	//
	// 为什么仍然安全 (未污染 BP API):
	//   - 不是 UENUM (BlueprintType 没有), BP 看不到
	//   - 不是 UFUNCTION, BP 调用不到
	//   - 只用于 .cpp 内部 switch 路由, 不暴露给外部
	// ==========================================

public:
	enum class ERoomMatchCode : uint8
	{
		Error,
		Melee,
		Zombie
	};

	/** 【v260】从 GameState 读取模式并转换为内部路由码, 出错返回 Error 分支 */
	ERoomMatchCode ResolveCurrentMatchMode() const;

private:
	// ==========================================
	// 配置
	// ==========================================
	// 注意: 配置现在通过 UMusicProjectSettings (Edit → Project Settings → MetalSlug01 → Music) 管理
	// 废弃的 UMusicManagerConfig DataAsset 不再使用

	/** 加载配置 */
	void LoadMusicConfigFromAsset();

	/** 【v228 新增】尝试绑定 UIViewService（延迟调用） */
	void TryBindToUIViewService();

	/** 根据状态映射到音乐类型 */
	EMusicType MapStateToMusicType(uint8 MatchState) const;

	/** 播放指定类型的音乐 (内部实现，包含停止旧音乐的逻辑) */
	void PlayMusicInternal(EMusicType MusicType);

	/** 【v260】播放生化战斗 BGM (EnterBattleMode 内部调用) — 接受空配置返回 false */
	bool PlayZombieBattleMusicInternal();

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
