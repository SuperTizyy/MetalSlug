// 版权声明：在项目设置的描述页面填写您的版权信息。

/**
 * @file MusicManagerSubsystem.cpp
 * @brief 背景音乐管理子系统实现 — AB 槽位切换 + EnterBattleMode 路由
 */
// ==========================================
// 头文件包含区
// ==========================================
#include "Systems/Music/MusicManagerSubsystem.h"
#include "Systems/GameFlowSubsystem.h"
#include "Services/UIViewService.h"
#include "Settings/MusicProjectSettings.h"
#include "Enums/CoreEnums.h"
#include "Data/Enums/RoomEnums.h" // 【v260】ERoomMatchMode
#include "Systems/RoomGameState.h" // 【v260】CurrentMatchMode 读取
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h" // 【v260】GetWorld()

// ==========================================
// 1. Subsystem 生命周期
// ==========================================

void UMusicManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 从 DataAsset 加载音乐配置
	LoadMusicConfigFromAsset();

	// 自动订阅 GameFlowSubsystem 状态变化
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* Flow = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			// 使用动态多播委托，必须使用 AddDynamic 绑定
			Flow->OnStateChanged.AddDynamic(this, &UMusicManagerSubsystem::OnGameFlowStateChanged);
			UE_LOG(LogTemp, Log, TEXT("[MusicManager] Initialize: 已自动绑定 GameFlowSubsystem"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[MusicManager] Initialize: GameFlowSubsystem 未就绪"));
		}

		// 【v228 新增】订阅 UIViewService 面板切换事件
		// 注意: UIViewService 可能尚未初始化，使用下一帧延迟绑定
		FTimerHandle TimerHandle;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &UMusicManagerSubsystem::TryBindToUIViewService, 0.1f, false);
	}

	UE_LOG(LogTemp, Log, TEXT("[MusicManager] Initialize: 完成"));
}

void UMusicManagerSubsystem::Deinitialize()
{
	// 停止当前音乐
	StopAllMusic(0.0f);

	Super::Deinitialize();
}

// ==========================================
// 2. 公共 API
// ==========================================

void UMusicManagerSubsystem::PlayMusic(EMusicType MusicType)
{
	if (MusicType == EMusicType::None)
	{
		StopAllMusic(0.0f);
		return;
	}

	// 检查配置（从运行时缓存读取）
	FMusicConfig* Config = RuntimeMusicConfigs.Find(MusicType);
	if (!Config)
	{
		// 【v229.2修复】如果目标音乐未配置，停止当前播放的音乐
		UE_LOG(LogTemp, Warning,
			TEXT("[MusicManager] PlayMusic: MusicType=%s 未配置，停止当前音乐"),
			*UEnum::GetValueAsName(MusicType).ToString());
		StopAllMusic(0.1f); // 短暂淡出
		return;
	}

	if (!Config->Sound)
	{
		// 【v229.2修复】如果 Sound 资产未配置，停止当前播放的音乐
		UE_LOG(LogTemp, Warning,
			TEXT("[MusicManager] PlayMusic: MusicType=%s 的 Sound 资产未配置，停止当前音乐"),
			*UEnum::GetValueAsName(MusicType).ToString());
		StopAllMusic(0.1f); // 短暂淡出
		return;
	}

	// 播放音乐（立即切换）
	PlayMusicInternal(MusicType);
}

void UMusicManagerSubsystem::StopAllMusic(float FadeOutDuration)
{
	// 【双槽位 v228】停止两个槽位的音乐
	UAudioComponent* ActiveSlot = bTrackAIsActive ? TrackA : TrackB;
	UAudioComponent* InactiveSlot = bTrackAIsActive ? TrackB : TrackA;

	// 【大厂方案】使用 FadeOut 确保音频完全停止
	if (ActiveSlot && ActiveSlot->GetPlayState() == EAudioComponentPlayState::Playing)
	{
		ActiveSlot->FadeOut(0.1f, 0.0f);
		UE_LOG(LogTemp, Log,
			TEXT("[MusicManager] StopAllMusic: 已停止 TrackA MusicType=%s"),
			*UEnum::GetValueAsName(CurrentMusicType).ToString());
	}

	if (InactiveSlot && InactiveSlot->GetPlayState() == EAudioComponentPlayState::Playing)
	{
		InactiveSlot->FadeOut(0.1f, 0.0f);
	}

	CurrentMusicType = EMusicType::None;
}

/**
 * @brief 判断指定类型音乐是否正在播放
 * @param MusicType 目标音乐类型
 * @return true=类型匹配 + 当前激活轨道正在播放
 *
 * 仅检查当前激活轨道(TrackA 或 TrackB)的 IsPlaying 状态
 * 单轨检查 = 实现简单 + 性能好 (无需遍历所有轨道)
 */
bool UMusicManagerSubsystem::IsMusicPlaying(EMusicType MusicType) const
{
	UAudioComponent* ActiveSlot = bTrackAIsActive ? TrackA : TrackB;
	if (MusicType == CurrentMusicType && ActiveSlot)
	{
		return ActiveSlot->IsPlaying();
	}
	return false;
}

/**
 * @brief 比赛状态变更时的音乐路由回调(由 GameFlowSubsystem 订阅触发)
 * @param NewState 新的比赛状态
 *
 * 路由策略 (大厂原则 - v260 战斗态特殊路由):
 * - Battleing / SettlementPage → EnterBattleMode(模式感知, 刀战不播 BGM / 生化播 ZombieBattle)
 * - 目标音乐 = None → StopAllMusic
 * - 其他 → PlayMusic(TargetMusicType)
 *
 * 优先级链: 战斗态路由 > None 停止 > 默认 PlayMusic
 */
void UMusicManagerSubsystem::OnGameFlowStateChanged(EMatchState NewState)
{
	uint8 StateValue = static_cast<uint8>(NewState);
	EMusicType TargetMusicType = MapStateToMusicType(StateValue);

	UE_LOG(LogTemp, Log,
		TEXT("[MusicManager] OnGameFlowStateChanged: State=%d (%s) -> TargetMusicType=%s, CurrentMusicType=%s"),
		NewState, *UEnum::GetValueAsName(NewState).ToString(),
		*UEnum::GetValueAsName(TargetMusicType).ToString(),
		*UEnum::GetValueAsName(CurrentMusicType).ToString());

	// 【v260 大厂架构】战斗态走特殊路由 — 根据 Mode 决定播哪个
	// Battleing / SettlementPage 都通过 EnterBattleMode 走模式感知路径
	// 其他状态 (MainMenu/InRoom/...) 仍走原有 PlayMusic 路径
	if (NewState == EMatchState::Battleing || NewState == EMatchState::SettlementPage)
	{
		EnterBattleMode();
		return;
	}

	if (TargetMusicType == EMusicType::None)
	{
		StopAllMusic(0.0f);
		UE_LOG(LogTemp, Log,
			TEXT("[MusicManager] OnGameFlowStateChanged: State=%d -> 停止所有音乐"),
			NewState);
	}
	else
	{
		PlayMusic(TargetMusicType);
		UE_LOG(LogTemp, Log,
			TEXT("[MusicManager] OnGameFlowStateChanged: State=%d -> PlayMusic(%s)"),
			NewState, *UEnum::GetValueAsName(TargetMusicType).ToString());
	}
}

// ==========================================
// 3. 内部方法
// ==========================================

void UMusicManagerSubsystem::LoadMusicConfigFromAsset()
{
	RuntimeMusicConfigs.Empty();

	// 从 Project Settings 加载配置
	const UMusicProjectSettings* Settings = GetDefault<UMusicProjectSettings>();
	if (!Settings)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MusicManager] LoadMusicConfigFromAsset: UMusicProjectSettings 未找到!"));
		return;
	}

	// 加载主菜单音乐
	// 【v229修复】直接 LoadSynchronous 而非 IsValid 检查
	if (USoundBase* Sound = Settings->MainMenuMusic.LoadSynchronous())
	{
		FMusicConfig MC;
		MC.Sound = Sound;
		MC.bLoop = Settings->MainMenuMusicLoop;
		MC.VolumeMultiplier = Settings->MasterMusicVolume;
		MC.PitchMultiplier = 1.0f;
		RuntimeMusicConfigs.Add(EMusicType::MainMenu, MC);
		UE_LOG(LogTemp, Log, TEXT("[MusicManager] LoadMusicConfigFromAsset: MainMenu=%s"),
			*Sound->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MusicManager] LoadMusicConfigFromAsset: MainMenu 未配置! 请在 Project Settings → Music 中配置"));
	}

	// 加载活动页面音乐
	// 【v229修复】直接 LoadSynchronous 而非 IsValid 检查，解决配置正确但 IsValid 返回 false 的问题
	if (USoundBase* Sound = Settings->ActivityMusic.LoadSynchronous())
	{
		FMusicConfig MC;
		MC.Sound = Sound;
		MC.bLoop = Settings->ActivityMusicLoop;
		MC.VolumeMultiplier = Settings->MasterMusicVolume;
		MC.PitchMultiplier = 1.0f;
		RuntimeMusicConfigs.Add(EMusicType::Activity, MC);
		UE_LOG(LogTemp, Log, TEXT("[MusicManager] LoadMusicConfigFromAsset: Activity=%s"),
			*Sound->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MusicManager] LoadMusicConfigFromAsset: Activity 未配置! 请在 Project Settings → Music 中配置"));
	}

	// 加载房间音乐
	// 【v229修复】直接 LoadSynchronous 而非 IsValid 检查
	if (USoundBase* Sound = Settings->RoomMusic.LoadSynchronous())
	{
		FMusicConfig MC;
		MC.Sound = Sound;
		MC.bLoop = Settings->RoomMusicLoop;
		MC.VolumeMultiplier = Settings->MasterMusicVolume;
		MC.PitchMultiplier = 1.0f;
		RuntimeMusicConfigs.Add(EMusicType::Room, MC);
		UE_LOG(LogTemp, Log, TEXT("[MusicManager] LoadMusicConfigFromAsset: Room=%s"),
			*Sound->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MusicManager] LoadMusicConfigFromAsset: Room 未配置"));
	}

	// 加载战斗音乐
	// 【v229修复】直接 LoadSynchronous 而非 IsValid 检查
	if (USoundBase* Sound = Settings->BattleMusic.LoadSynchronous())
	{
		FMusicConfig MC;
		MC.Sound = Sound;
		MC.bLoop = Settings->BattleMusicLoop;
		MC.VolumeMultiplier = Settings->MasterMusicVolume;
		MC.PitchMultiplier = 1.0f;
		RuntimeMusicConfigs.Add(EMusicType::Battle, MC);
		UE_LOG(LogTemp, Log, TEXT("[MusicManager] LoadMusicConfigFromAsset: Battle=%s"),
			*Sound->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MusicManager] LoadMusicConfigFromAsset: Battle 未配置"));
	}

	// 【v260】加载生化模式战斗音乐 - 独立槽位, 不与 Battle 复用
	// 零兑底: 未配置时不创建键, EnterBattleMode 查不到直接 Log Error + 静默停止
	if (USoundBase* Sound = Settings->ZombieBattleMusic.LoadSynchronous())
	{
		FMusicConfig MC;
		MC.Sound = Sound;
		MC.bLoop = Settings->ZombieBattleMusicLoop;
		MC.VolumeMultiplier = Settings->MasterMusicVolume;
		MC.PitchMultiplier = 1.0f;
		RuntimeMusicConfigs.Add(EMusicType::ZombieBattle, MC);
		UE_LOG(LogTemp, Log, TEXT("[MusicManager] LoadMusicConfigFromAsset: ZombieBattle=%s"),
			*Sound->GetName());
	}
	else
	{
		// 用 Error 而非 Warning: 进入生化战斗时会激活此键, 缺失是配置错误必须曝露
		UE_LOG(LogTemp, Error,
			TEXT("[MusicManager] LoadMusicConfigFromAsset: ZombieBattle 未配置! "
			     "请在 Project Settings → Music → Zombie → Zombie Battle Music 配置. "
			     "生化模式进入战斗将不会播放任何 BGM (零兑底原则)."));
	}

	if (RuntimeMusicConfigs.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MusicManager] LoadMusicConfigFromAsset: 所有音乐都未配置! 请在 Edit → Project Settings → Music 中配置"));
	}
}

void UMusicManagerSubsystem::TryBindToUIViewService()
{
	// 【v228 新增】延迟绑定 UIViewService（确保 UIViewService 已初始化）
	if (UUIViewService* ViewService = GetGameInstance()->GetSubsystem<UUIViewService>())
	{
		ViewService->OnPanelChanged.AddDynamic(this, &UMusicManagerSubsystem::OnUIViewPanelChanged);
		UE_LOG(LogTemp, Log, TEXT("[MusicManager] TryBindToUIViewService: 已绑定 UIViewService"));
	}
	else
	{
		// UIViewService 还未初始化，再等一帧
		FTimerHandle TimerHandle;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &UMusicManagerSubsystem::TryBindToUIViewService, 0.1f, false);
	}
}

EMusicType UMusicManagerSubsystem::MapStateToMusicType(uint8 MatchState) const
{
	// EMatchState 的数值顺序：PreLogin=0, Login=1, MainMenu=2, MainLobby=3, InRoom=4, Battleing=5, SettlementPage=6
	// 【v260 大厂架构】Battleing / SettlementPage 不在此函数内映射 EMusicType
	//   - 这两个状态由 OnGameFlowStateChanged 拦截后走 EnterBattleMode() 模式感知路径
	//   - 本函数只负责"非战斗态"的简单映射, 避免重复路由 (零重复原则)
	switch (MatchState)
	{
	case 0: // PreLogin
	case 1: // Login
	case 2: // MainMenu
	case 3: // MainLobby
		return EMusicType::MainMenu;

	case 4: // InRoom
		return EMusicType::Room;

	case 5: // Battleing
	case 6: // SettlementPage
		// 【v260】调用方已经拦截, 这里不该被调用. 返回 None 作为安全动作.
		UE_LOG(LogTemp, Error,
			TEXT("[MusicManager] MapStateToMusicType: Battle 状态 (State=%d) 应走 EnterBattleMode "
			     "而非 MapStateToMusicType. 检查 OnGameFlowStateChanged 调用顺序."),
			MatchState);
		return EMusicType::None;

	default:
		UE_LOG(LogTemp, Warning,
			TEXT("[MusicManager] MapStateToMusicType: 未知状态 State=%d"),
			MatchState);
		return EMusicType::None;
	}
}

/**
 * @brief UI 面板切换时的音乐路由回调(由 UIViewService 订阅触发)
 * @param OldPanel 旧面板
 * @param NewPanel 新面板
 *
 * 面板 → 音乐类型映射:
 * - ActivityPanel → Activity
 * - RoomInside / LANRoom → Room
 * - MainMenu → MainMenu
 * - BattleHUD / SettlementPanel → EnterBattleMode(模式感知)
 * - 其他面板不切换
 */
void UMusicManagerSubsystem::OnUIViewPanelChanged(EUIPanel OldPanel, EUIPanel NewPanel)
{
	// 【v228 新增】UI 面板切换时自动切换音乐（仅处理通过 UIViewService 的情况）
	EMusicType MusicType = EMusicType::None;

	switch (NewPanel)
	{
	case EUIPanel::ActivityPanel:
		MusicType = EMusicType::Activity;
		break;

	case EUIPanel::RoomInside:
	case EUIPanel::LANRoom:
		MusicType = EMusicType::Room;
		break;

	case EUIPanel::MainMenu:
		MusicType = EMusicType::MainMenu;
		break;

	case EUIPanel::BattleHUD:
	case EUIPanel::SettlementPanel:
		// 【v260】战斗面板走模式感知路由 (跟 OnGameFlowStateChanged 行为一致)
		// 双订阅路径 (状态 + 面板) 都需要进入正确的生化 / 刀战分支
		EnterBattleMode();
		return;

	default:
		// 其他面板不切换音乐
		break;
	}

	if (MusicType != EMusicType::None)
	{
		PlayMusic(MusicType);
	}
}

/**
 * @brief 进入活动页面专属音乐模式(供 GameMenuPage 等直接调用)
 *
 * 行为:
 * - 已在活动模式 → no-op
 * - 否则保存当前音乐到 PreviousMusicType + 切到 Activity
 *
 * 对称调用: RestorePreviousMusic() 用于活动页面关闭时恢复
 * 大厂原则 - 状态栈: 保存前态以便对称恢复
 */
void UMusicManagerSubsystem::PlayActivityMusic()
{
	// 【v228 新增】切换到活动页面音乐（供 GameMenuPage 等直接调用）
	if (bIsInActivityMode)
	{
		// 已经在活动页面音乐模式
		return;
	}

	// 保存当前音乐类型，以便关闭活动页面时恢复
	PreviousMusicType = CurrentMusicType;
	bIsInActivityMode = true;

	// 切换到活动音乐
	PlayMusic(EMusicType::Activity);

	UE_LOG(LogTemp, Log,
		TEXT("[MusicManager] PlayActivityMusic: 保存之前的音乐=%s, 切换到 Activity"),
		*UEnum::GetValueAsName(PreviousMusicType).ToString());
}

void UMusicManagerSubsystem::RestorePreviousMusic()
{
	// 【v228 新增】恢复之前的音乐（活动页面关闭时调用）
	if (!bIsInActivityMode)
	{
		// 不在活动页面模式，无需恢复
		return;
	}

	bIsInActivityMode = false;

	// 恢复之前的音乐
	if (PreviousMusicType != EMusicType::None)
	{
		PlayMusic(PreviousMusicType);
		UE_LOG(LogTemp, Log,
			TEXT("[MusicManager] RestorePreviousMusic: 恢复音乐=%s"),
			*UEnum::GetValueAsName(PreviousMusicType).ToString());
	}
	else
	{
		// 没有之前的音乐，播放主菜单音乐作为默认
		PlayMusic(EMusicType::MainMenu);
		UE_LOG(LogTemp, Log,
			TEXT("[MusicManager] RestorePreviousMusic: 无历史音乐，播放 MainMenu"));
	}
}

// ==========================================
// 【双槽位 AudioComponent 重构 v228】
// 参考 UE 官方论坛大厂方案：https://forums.unrealengine.com/t/play-one-track-per-event-without-sound-overlap/2662673
// 核心：使用两个预先创建的 AudioComponent 槽位，切换时先 FadeOut 旧槽位，再 FadeIn 新槽位
// ==========================================

void UMusicManagerSubsystem::PlayMusicInternal(EMusicType MusicType)
{
	// ==========================================
	// 步骤1: 获取配置
	// ==========================================
	FMusicConfig* Config = RuntimeMusicConfigs.Find(MusicType);
	if (!Config)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MusicManager] PlayMusicInternal: MusicType=%s 在 RuntimeMusicConfigs 中未配置!"),
			*UEnum::GetValueAsName(MusicType).ToString());
		return;
	}

	if (!Config->Sound)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MusicManager] PlayMusicInternal: MusicType=%s 的 Sound 资产未配置!"),
			*UEnum::GetValueAsName(MusicType).ToString());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MusicManager] PlayMusicInternal: World 无效!"));
		return;
	}

	// ==========================================
	// 步骤2: 确定当前激活的槽位和新的槽位
	// ==========================================
	UAudioComponent* ActiveSlot = bTrackAIsActive ? TrackA : TrackB;
	UAudioComponent* InactiveSlot = bTrackAIsActive ? TrackB : TrackA;

	UE_LOG(LogTemp, Log,
		TEXT("[MusicManager] PlayMusicInternal: MusicType=%s, ActiveSlot=%s, InactiveSlot=%s, bTrackAIsActive=%d"),
		*UEnum::GetValueAsName(MusicType).ToString(),
		ActiveSlot ? *ActiveSlot->GetName() : TEXT("NULL"),
		InactiveSlot ? *InactiveSlot->GetName() : TEXT("NULL"),
		bTrackAIsActive);

	// 如果新音乐和当前音乐相同，不做处理
	if (MusicType == CurrentMusicType && ActiveSlot && ActiveSlot->IsPlaying())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[MusicManager] PlayMusicInternal: MusicType=%s 已在播放，跳过"),
			*UEnum::GetValueAsName(MusicType).ToString());
		return;
	}

	// ==========================================
	// 步骤3: 【关键】停止旧音乐
	// ==========================================
	// 【大厂方案】使用 FadeOut 确保旧音乐完全停止
	if (ActiveSlot && ActiveSlot->GetPlayState() == EAudioComponentPlayState::Playing)
	{
		ActiveSlot->FadeOut(0.1f, 0.0f); // 100ms 淡出
		UE_LOG(LogTemp, Log,
			TEXT("[MusicManager] PlayMusicInternal: 停止旧音乐=%s (FadeOut 100ms)"),
			*UEnum::GetValueAsName(CurrentMusicType).ToString());
	}

	// ==========================================
	// 步骤4: 准备新槽位
	// ==========================================
	// 【大厂方案】使用 SpawnSound2D 创建持久的 2D 音频组件
	// SpawnSound2D 会自动创建正确注册到 World 的 AudioComponent
	if (!InactiveSlot)
	{
		// SpawnSound2D 参数:
		// WorldContext - 用于获取 World
		// Sound - 要播放的声音
		// VolumeMultiplier - 音量
		// PitchMultiplier - 音调
		// StartTime - 起始时间
		// ConcurrencySettings - 并发设置
		// bPersistAcrossLevelTransition - 跨关卡持久化
		// bAutoDestroy - 播放完成后自动销毁
		InactiveSlot = UGameplayStatics::SpawnSound2D(
			World,
			Config->Sound,
			Config->VolumeMultiplier,
			Config->PitchMultiplier,
			0.0f,  // StartTime
			nullptr,  // ConcurrencySettings
			bPersistAcrossLevelTransition,  // 跨关卡持久化
			bAutoDestroyOnFadeComplete  // 自动销毁
		);

		if (!InactiveSlot)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[MusicManager] PlayMusicInternal: SpawnSound2D 创建失败!"));
			return;
		}

		// 设置为 UI 声音（不随游戏暂停而暂停）
		InactiveSlot->bIsUISound = true;

		// 保存到对应槽位
		if (!bTrackAIsActive)
		{
			TrackA = InactiveSlot;
		}
		else
		{
			TrackB = InactiveSlot;
		}

		UE_LOG(LogTemp, Log,
			TEXT("[MusicManager] PlayMusicInternal: SpawnSound2D 创建 AudioComponent 成功"));
	}
	else
	{
		// 复用已有组件 - 先停止
		InactiveSlot->Stop();

		// 设置新音乐
		InactiveSlot->SetSound(Config->Sound);
		InactiveSlot->VolumeMultiplier = Config->VolumeMultiplier;
		InactiveSlot->PitchMultiplier = Config->PitchMultiplier;
		InactiveSlot->bIsUISound = true;
	}

	// ==========================================
	// 步骤5: 播放新音乐
	// ==========================================
	InactiveSlot->Play();

	// 更新槽位状态
	bTrackAIsActive = !bTrackAIsActive;
	CurrentMusicType = MusicType;

	UE_LOG(LogTemp, Log,
		TEXT("[MusicManager] PlayMusicInternal: 播放新音乐 MusicType=%s, Sound=%s, ActiveSlot=%s"),
		*UEnum::GetValueAsName(MusicType).ToString(),
		*Config->Sound->GetName(),
		bTrackAIsActive ? TEXT("TrackA") : TEXT("TrackB"));
}

// ==========================================
// 【v260 大厂架构】生化战斗 BGM 路由实现
//
// 设计动机 (用户 2026.08.14):
//   刀战战斗不播 BGM, 生化战斗播专属 ZombieBattleMusic.
//   通过 EnterBattleMode 入口根据 ERoomMatchMode 自动路由.
//
// 零兑底原则贯彻:
//   - ZombieBattleMusic 未配置 → Log Error, 静默停止 (不 fallback BattleMusic)
//   - Mode=ERoomMatchMode::None / 异常 → Log Error, 静默不动作
//   - GameState 未就绪 (Level 加载中) → Log Error, 静默不动作
// ==========================================

UMusicManagerSubsystem::ERoomMatchCode UMusicManagerSubsystem::ResolveCurrentMatchMode() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		// 主动查询时（如 EnterBattleMode）World 不应为 null
		// 如果拿到 null，说明调用时机过早（Subsystem 初始化但 World 未 ready）
		UE_LOG(LogTemp, Error,
			TEXT("[MusicManager] ResolveCurrentMatchMode: World 为 null! "
			     "EnterBattleMode 在错误的时机被调用. 检查 GameFlowStateChanged 触发顺序."));
		return UMusicManagerSubsystem::ERoomMatchCode::Error;
	}

	ARoomGameState* GS = World->GetGameState<ARoomGameState>();
	if (!GS)
	{
		// 当前 World 没有 RoomGameState. 例如: 在 MainMenu 调用 EnterBattleMode.
		// 这是错误调用方, 强制 Log Error 拒绝路由.
		UE_LOG(LogTemp, Error,
			TEXT("[MusicManager] ResolveCurrentMatchMode: GameState 不是 ARoomGameState! "
			     "Mode 必须从 ARoomGameState::CurrentMatchMode 读取, 不允许读其他地方."));
		return UMusicManagerSubsystem::ERoomMatchCode::Error;
	}

	const ERoomMatchMode Mode = GS->CurrentMatchMode;
	UE_LOG(LogTemp, Log,
		TEXT("[MusicManager] ResolveCurrentMatchMode: CurrentMatchMode=%s"),
		*UEnum::GetValueAsName(Mode).ToString());

	switch (Mode)
	{
	case ERoomMatchMode::Melee:  return UMusicManagerSubsystem::ERoomMatchCode::Melee;
	case ERoomMatchMode::Zombie: return UMusicManagerSubsystem::ERoomMatchCode::Zombie;
	case ERoomMatchMode::None:
	default:
		UE_LOG(LogTemp, Error,
			TEXT("[MusicManager] ResolveCurrentMatchMode: 当前模式=%s. "
			     "这是未配置的非法状态, EnterBattleMode 拒绝路由. "
			     "请联系策划检查 GameFlow::SetTargetRoomMode + RoomGameState 初始化."),
			*UEnum::GetValueAsName(Mode).ToString());
		return UMusicManagerSubsystem::ERoomMatchCode::Error;
	}
}

/**
 * @brief 战斗模式音乐路由入口(根据 GameMode 决定具体音乐策略)
 *
 * 路由决策 (大厂原则 - v260 模式感知):
 * - Melee(刀战): 不播 BGM, StopAllMusic(零兜底, 不 fallback 到其他音乐)
 * - Zombie(生化): PlayZombieBattleMusicInternal, 失败 → Log Warning 但不静默
 * - None / Error: 拒绝路由, Log Error(强制修复 GameFlow 初始化)
 *
 * 单一调度: 所有进入战斗态路径(OnGameFlowStateChanged / OnUIViewPanelChanged)统一收敛到此函数
 */
void UMusicManagerSubsystem::EnterBattleMode()
{
	UE_LOG(LogTemp, Log,
		TEXT("[MusicManager] EnterBattleMode: 进入战斗态路由决策. CurrentMusicType=%s"),
		*UEnum::GetValueAsName(CurrentMusicType).ToString());

	// 【v260】完全限定 ERoomMatchCode (MSVC 在 cpp 域外对嵌套 enum class 不做隐式查找)
	const UMusicManagerSubsystem::ERoomMatchCode Code = ResolveCurrentMatchMode();
	switch (Code)
	{
	case UMusicManagerSubsystem::ERoomMatchCode::Melee:
	{
		// 刀战不播 BGM - 静默停止所有音乐
		// 零兑底: 不 fallback 到任何其他音乐 (即使有 MainMenu/Room 配置)
		UE_LOG(LogTemp, Log,
			TEXT("[MusicManager] EnterBattleMode: 刀战模式, 不播 BGM. StopAllMusic."));
		StopAllMusic(0.1f);
		return;
	}

	case UMusicManagerSubsystem::ERoomMatchCode::Zombie:
	{
		// 生化播专属音乐
		UE_LOG(LogTemp, Log,
			TEXT("[MusicManager] EnterBattleMode: 生化模式, 尝试播 ZombieBattle."));
		const bool bPlayed = PlayZombieBattleMusicInternal();
		if (bPlayed)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[MusicManager] EnterBattleMode: 生化 BGM 启动成功."));
		}
		else
		{
			// PlayZombieBattleMusicInternal 已经 Log Error 了 (未配置)
			// 这里补一刀 Error Log 让调用链可追溯
			UE_LOG(LogTemp, Error,
				TEXT("[MusicManager] EnterBattleMode: 生化 BGM 启动失败 (配置缺失). "
				     "生化模式进入战斗将完全静音. 请检查 Project Settings → Music → ZombieBattle."));
			StopAllMusic(0.1f); // 即使播放失败也确保没有残留音乐
		}
		return;
	}

	case UMusicManagerSubsystem::ERoomMatchCode::Error:
	default:
		// ResolveCurrentMatchMode 已经 Log Error. 这里不重复.
		// 拒绝路由, 静默停止避免播放错音乐.
		UE_LOG(LogTemp, Log,
			TEXT("[MusicManager] EnterBattleMode: 模式解析失败, StopAllMusic 作为安全动作."));
		StopAllMusic(0.1f);
		return;
	}
}

bool UMusicManagerSubsystem::IsBattleMusicPlaying() const
{
	if (CurrentMusicType == EMusicType::Battle || CurrentMusicType == EMusicType::ZombieBattle)
	{
		const UAudioComponent* ActiveSlot = bTrackAIsActive ? TrackA : TrackB;
		return ActiveSlot && ActiveSlot->IsPlaying();
	}
	return false;
}

bool UMusicManagerSubsystem::PlayZombieBattleMusicInternal()
{
	// 直接调复用 PlayMusicInternal - TMap 已包含 ZombieBattle 配置 (如已配)
	// 零兑底: PlayMusicInternal 找不到配置会 Log Error 返回, 不会 fallback
	PlayMusicInternal(EMusicType::ZombieBattle);

	// PlayMusicInternal 找不到配置时不修改 CurrentMusicType (仍保持上一首)
	// 检查是否真的成功切换
	if (CurrentMusicType == EMusicType::ZombieBattle)
	{
		const UAudioComponent* ActiveSlot = bTrackAIsActive ? TrackA : TrackB;
		return ActiveSlot && ActiveSlot->IsPlaying();
	}
	return false;
}
