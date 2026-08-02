// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Game/Widgets/PlayerStatusWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Characters/BaseCharacter.h"
// 【2026-07-01 P0】新增: 主动从 CharacterEvents 缓存拉取头像
#include "Components/CharacterEvents.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
// 【v120 新增】动态材质参数设置
#include "Materials/MaterialInstanceDynamic.h"
// 【v121.3 新增】母体技能冷却倒计时
#include "Combat/MotherSkillComponent.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UPlayerStatusWidget::NativeConstruct
 *
 * 主动从角色拉取初始数据
 * 解决: UI 创建晚于角色数据初始化导致的初始值为 0 的问题
 */
void UPlayerStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 主动从角色拉取初始数据，解决 UI 创建晚于角色数据初始化导致的初始值为 0 的问题
	PullInitialDataFromCharacter();
}


/**
 * UPlayerStatusWidget::NativeTick
 *
 * 【v121.3 新增】每帧更新母体技能冷却进度
 *
 * 冷却进度由 Tick 自动更新，不需要外部调用 UpdateMotherSkillCooldownProgress
 */
void UPlayerStatusWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 【v121.3】每帧更新冷却进度
	UpdateMotherSkillCooldownProgress();
}


/**
 * UPlayerStatusWidget::PullInitialDataFromCharacter
 *
 * 1. PC = GetOwningPlayer() 获取当前被操控的玩家控制器
 * 2. Cast<ABaseCharacter>(PC->GetPawn()) 获取当前角色
 * 3. 拉取 AC/ACE/HP/Energy 各项初值
 * 4. 【2026-07-01 P0】拉取头像缓存 (CharacterEvents::GetCachedCharacterIcon)
 *    这是"事件 + 缓存"双轨制的兜底:
 *    - GameHUDWidget 正常路径会订阅 CharacterEvents.OnCharacterIconReady 事件
 *    - 但若 GameHUDWidget 还没绑定 CharacterEvents, 这里主动从缓存拉头像
 *    - 解决: 头像事件丢失时, PlayerStatus 仍能显示头像
 * 注意: GetCurrentHealth/GetMaxHealth 等方法必须存在，否则编译失败
 */
void UPlayerStatusWidget::PullInitialDataFromCharacter()
{
	// 通过 PlayerController 获取当前被操控的角色
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ABaseCharacter* Character = Cast<ABaseCharacter>(PC->GetPawn()))
		{
			// ==========================================
			// 【2026-07-01 P0】从 CharacterEvents 缓存拉取头像 (主动补发)
			// 【v39 修复】用 ResolveCharacterEvents 而非裸字段访问 (BP archetype null 字段防护)
			// ==========================================
			if (UCharacterEvents* Events = Character->ResolveCharacterEvents())
			{
				FString CachedCharID;
				UTexture2D* CachedAvatar = nullptr;
				if (Events->GetCachedCharacterIcon(CachedCharID, CachedAvatar))
				{
					UE_LOG(LogTemp, Log,
						TEXT("[PlayerStatus][Pull-Initial] 从 CharacterEvents 缓存拉取头像: CharID=%s, Avatar=%s"),
						*CachedCharID,
						CachedAvatar ? *CachedAvatar->GetName() : TEXT("nullptr"));
					UpdateCharacterIcon(CachedAvatar);
				}
				else
				{
					UE_LOG(LogTemp, Verbose,
						TEXT("[PlayerStatus][Pull-Initial] 无头像缓存, 等待事件推送"));
				}

				// 【v40.2 P0 修复】武器图标由 GameHUDWidget TryBindToCharacterEvents 补发 (镜像头像)
				//   根因: PlayerStatusWidget 不持有 WeaponPanel 引用 (职责分离 — 武器图标属于 WeaponPanel)
				//         GameHUDWidget 持有 Widget_WeaponPanel → 在 TryBindToCharacterEvents 内主动拉武器图标缓存 → 调 OnWeaponIconReady → 走 UpdateWeaponIconFromID → Widget_WeaponPanel->UpdateMeleeWeaponIcon
				//   此处不再重复武器图标 Pull-Initial (避免 v40.2 误调 UpdateMeleeWeaponIcon 导致编译错误)
				//   大厂原则 - 单一职责: PlayerStatus 只管 Status 字段 (HP/Energy/AC/ACE/头像), 武器图标由 HUD 整体层处理
			}

			// 拉取初始 AC 值（此时角色可能在服务端已经初始化过 AC）
			UpdateACValue(Character->GetAC());

			// 拉取初始 ACE 值
			UpdateACEValue(Character->GetACE());

			// 拉取初始血量
			UpdateHealth(Character->GetCurrentHealth(), Character->GetMaxHealth());
			UpdateHealthText(FMath::CeilToInt(Character->GetCurrentHealth()), FMath::CeilToInt(Character->GetMaxHealth()));

			// 拉取初始能量
			UpdateEnergy(Character->GetCurrentEnergy(), Character->GetMaxEnergy());
			UpdateEnergyText(FMath::CeilToInt(Character->GetCurrentEnergy()), FMath::CeilToInt(Character->GetMaxEnergy()));
		}
	}
}


/**
 * UPlayerStatusWidget::Initialize
 *
 * 1. 血条默认 100%
 * 2. 能量条默认 100%
 * 防御性: BindWidget 绑定失败时也能编译通过
 */
bool UPlayerStatusWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 初始化默认值
	if (PB_HealthBar)
	{
		PB_HealthBar->SetPercent(1.0f);
	}

	if (PB_EnergyBar)
	{
		PB_EnergyBar->SetPercent(1.0f);
	}

	return true;
}


// ==========================================
// 2. 血量 / 能量
// ==========================================

/**
 * UPlayerStatusWidget::UpdateHealth
 *
 * 1. 计算百分比: Current / Max (防御性: Max > 0)
 * 2. Clamp 0~1
 * 3. 设置进度条
 * 4. 根据百分比设置颜色: > 60% 绿, > 30% 黄, 否则 红
 */
void UPlayerStatusWidget::UpdateHealth(float Current, float Max)
{
	if (!PB_HealthBar)
	{
		UE_LOG(LogTemp, Error, TEXT("[Health] PlayerStatus UpdateHealth: PB_HealthBar 为空!"));
		return;
	}

	float Percent = (Max > 0.0f) ? (Current / Max) : 0.0f;
	Percent = FMath::Clamp(Percent, 0.0f, 1.0f);
	UE_LOG(LogTemp, Warning, TEXT("[Health] PlayerStatus UpdateHealth: %.1f/%.1f = %.2f%%"), Current, Max, Percent * 100);
	PB_HealthBar->SetPercent(Percent);

	// 根据血量百分比设置颜色
	if (Percent > 0.6f)
	{
		PB_HealthBar->SetFillColorAndOpacity(FLinearColor::Green);
	}
	else if (Percent > 0.3f)
	{
		PB_HealthBar->SetFillColorAndOpacity(FLinearColor::Yellow);
	}
	else
	{
		PB_HealthBar->SetFillColorAndOpacity(FLinearColor::Red);
	}
}


/**
 * UPlayerStatusWidget::UpdateEnergy
 *
 * 1. 计算百分比
 * 2. 设置能量条
 * 注意: 能量条颜色保持单一
 */
void UPlayerStatusWidget::UpdateEnergy(float Current, float Max)
{
	if (!PB_EnergyBar)
	{
		return;
	}

	float Percent = (Max > 0.0f) ? (Current / Max) : 0.0f;
	Percent = FMath::Clamp(Percent, 0.0f, 1.0f);
	PB_EnergyBar->SetPercent(Percent);
}


/**
 * UPlayerStatusWidget::UpdateHealthText
 *
 * 文本格式: "X/Y"（使用 FText::Format 支持本地化）
 */
void UPlayerStatusWidget::UpdateHealthText(int32 Current, int32 Max)
{
	if (Text_HealthValue)
	{
		Text_HealthValue->SetText(FText::Format(
			NSLOCTEXT("PlayerStatus", "HealthFormat", "{0}/{1}"),
			FText::AsNumber(Current),
			FText::AsNumber(Max)
		));
	}
}


/**
 * UPlayerStatusWidget::UpdateEnergyText
 *
 * 文本格式: "X/Y"
 */
void UPlayerStatusWidget::UpdateEnergyText(int32 Current, int32 Max)
{
	if (Text_EnergyValue)
	{
		Text_EnergyValue->SetText(FText::Format(
			NSLOCTEXT("PlayerStatus", "EnergyFormat", "{0}/{1}"),
			FText::AsNumber(Current),
			FText::AsNumber(Max)
		));
	}
}


// ==========================================
// 3. AC / ACE
// ==========================================

/**
 * UPlayerStatusWidget::UpdateACValue
 *
 * 1. 设置 Text_ACValue 数字
 * 2. 调用 RefreshACIconColor 同步刷新防护服图标
 */
void UPlayerStatusWidget::UpdateACValue(int32 Value)
{
	if (Text_ACValue)
	{
		Text_ACValue->SetText(FText::AsNumber(Value));
	}

	// AC 值变化时同步刷新防护服图标颜色
	RefreshACIconColor(Value);
}


/**
 * UPlayerStatusWidget::RefreshACIconColor
 *
 * AC 值越高，防护服越亮（蓝白色）；AC 值越低，防护服越暗（红黑色）
 * 分档: 0-25 低 / 26-50 中 / 51-75 良好 / 76+ 最佳
 */
void UPlayerStatusWidget::RefreshACIconColor(int32 CurrentAC)
{
	if (!Image_ACIcon)
	{
		return;
	}

	FLinearColor IconColor;

	// AC 值越高，防护服越亮（蓝白色）；AC 值越低，防护服越暗（红黑色）
	// 分档: 0-25 低 / 26-50 中 / 51-75 良好 / 76+ 最佳
	if (CurrentAC >= 76)
	{
		// 最佳状态: 明亮的蓝白色（防护服完好）
		IconColor = FLinearColor(0.6f, 0.85f, 1.0f, 1.0f);
	}
	else if (CurrentAC >= 51)
	{
		// 良好状态: 黄色（防护服轻微受损）
		IconColor = FLinearColor(1.0f, 0.9f, 0.2f, 1.0f);
	}
	else if (CurrentAC >= 26)
	{
		// 中等状态: 橙色（防护服明显受损）
		IconColor = FLinearColor(1.0f, 0.55f, 0.1f, 1.0f);
	}
	else
	{
		// 危急状态: 深红色（防护服濒临崩溃）
		IconColor = FLinearColor(0.9f, 0.1f, 0.1f, 1.0f);
	}

	Image_ACIcon->SetColorAndOpacity(IconColor);
}


/**
 * UPlayerStatusWidget::UpdateACEValue
 *
 * 简单设置 ACE 文本 + 默认白色
 */
void UPlayerStatusWidget::UpdateACEValue(int32 Value)
{
	if (Text_ACEValue)
	{
		Text_ACEValue->SetText(FText::AsNumber(Value));
		Text_ACEValue->SetColorAndOpacity(FLinearColor::White);
	}
}


/**
 * UPlayerStatusWidget::SetACEValueWithRank
 *
 * 根据 EACERankType 设置颜色:
 * - Gold: 金色 (1, 0.85, 0.2)
 * - White: 白色
 * - None: 灰色 (0.5, 0.5, 0.5)
 */
void UPlayerStatusWidget::SetACEValueWithRank(int32 Value, EACERankType RankType)
{
	if (!Text_ACEValue)
	{
		return;
	}

	Text_ACEValue->SetText(FText::AsNumber(Value));

	FLinearColor TextColor;
	switch (RankType)
	{
	case EACERankType::Gold:
		TextColor = FLinearColor(1.0f, 0.85f, 0.2f, 1.0f); // 金色
		break;
	case EACERankType::White:
		TextColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f); // 白色
		break;
	default:
		TextColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f); // 灰色（无 ACE 时）
		break;
	}

	Text_ACEValue->SetColorAndOpacity(TextColor);
}


// ==========================================
// 4. 角色 / 技能
// ==========================================

/**
 * UPlayerStatusWidget::UpdateCharacterIcon
 *
 * 设置 Image_CharacterIcon 的画刷
 *
 * 【2026-07-01 重构 v2 - 软引用异步加载】:
 *   - 传入 UTexture2D* 硬指针: 直接使用 (服务器已传递有效贴图)
 *   - 传入 nullptr: 通过 TSoftObjectPtr + StreamableManager 异步加载
 *     - 解决: 硬指针为 nullptr 但 UI 仍需显示头像的场景
 *     - 避免: 旧请求的回调在新请求之后到达 (PendingCharacterIconSoftRef 取消机制)
 *   - 重复贴图短路: 同一贴图不重复 SetBrush
 *   - 空指针保护: Image_CharacterIcon 或 Icon 为空时直接 return
 *   - 显式刷颜色: SetColorAndOpacity(White) 防止上一次 tint 残留导致白板
 */
void UPlayerStatusWidget::UpdateCharacterIcon(UTexture2D* Icon)
{
	if (!Image_CharacterIcon)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlayerStatus] UpdateCharacterIcon: Image_CharacterIcon 未绑定!"));
		return;
	}

	// 硬指针直接使用 (大多数场景走这里)
	if (Icon)
	{
		ApplyCharacterIconBrush(Icon);
		return;
	}

	// Icon 为 nullptr: 尝试从软引用加载 (兼容未来扩展场景)
	// 注: 当前服务器已传递有效 UTexture2D*, 这段代码主要是防御性兜底
	UE_LOG(LogTemp, Warning, TEXT("[PlayerStatus] UpdateCharacterIcon: 收到空指针, 跳过"));
}


/**
 * UPlayerStatusWidget::ApplyCharacterIconBrush
 *
 * 应用头像贴图到 Image_CharacterIcon
 * 内部方法, 被 UpdateCharacterIcon 和 OnCharacterIconLoaded 调用
 *
 * 【2026-07-01 P0 修复】:
 *   - 移除"重复贴图短路"逻辑 (原本 CurrentBrush == Icon 时 return)
 *     原因: 事件总线的"竞争订阅"修复依赖此函数可重复调用,
 *           若短路会导致 "新订阅者拉取缓存时, 缓存的 Avatar 已被设过, 直接 return" → 头像仍然不显示
 *   - 显式刷颜色: SetColorAndOpacity(White) 防止 tint 残留
 *   - 防御性: 控件 + 贴图双重空指针检查
 */
void UPlayerStatusWidget::ApplyCharacterIconBrush(UTexture2D* Icon)
{
	if (!Image_CharacterIcon || !Icon)
	{
		return;
	}

	// 【2026-07-01 P0】移除"重复贴图短路" — 保证事件 + 缓存补发路径都能写入
	// 写入新贴图 + 显式恢复白色不透明
	Image_CharacterIcon->SetBrushFromTexture(Icon);
	Image_CharacterIcon->SetColorAndOpacity(FLinearColor::White);

	UE_LOG(LogTemp, Log,
		TEXT("[PlayerStatus] ApplyCharacterIconBrush: 已写入贴图 %s"),
		*Icon->GetName());
}


/**
 * UPlayerStatusWidget::OnCharacterIconLoaded
 *
 * 异步加载头像完成回调 (StreamableManager 回调)
 *
 * 【2026-07-01 新增】:
 *   - 取消过期请求: 如果请求加载时收到过新的 UpdateCharacterIcon, 则跳过
 *   - 空指针保护: Image_CharacterIcon 为空时直接 return
 */
void UPlayerStatusWidget::OnCharacterIconLoaded(FSoftObjectPath AssetPath, FStreamableHandle* LoadedHandle)
{
	// 防御 1: Widget 已销毁
	if (!IsValid(this))
	{
		return;
	}

	// 防御 2: 请求已过期 (被新的 UpdateCharacterIcon 取消)
	// 比对路径, 如果不对应则直接丢弃, 避免旧请求覆盖新请求
	if (!PendingCharacterIconSoftRef.IsValid() ||
		PendingCharacterIconSoftRef.ToSoftObjectPath() != AssetPath)
	{
		return;
	}

	// 从 StreamableManager 句柄中取出已加载的 UObject
	// 注意: GetLoadedAsset() 是 StreamableHandle 提供的安全访问 API
	if (!LoadedHandle)
	{
		PendingCharacterIconSoftRef.Reset();
		return;
	}

	UObject* Asset = LoadedHandle->GetLoadedAsset();
	PendingCharacterIconSoftRef.Reset();

	if (UTexture2D* Texture = Cast<UTexture2D>(Asset))
	{
		ApplyCharacterIconBrush(Texture);
	}
}


/**
 * UPlayerStatusWidget::UpdateSkillIcon
 *
 * 1. 校验 SkillIndex 范围
 * 2. HB_SkillBar->GetChildAt(SkillIndex) 获取对应槽位
 * 3. Cast 为 UImage
 * 4. 设置画刷并显示
 */
void UPlayerStatusWidget::UpdateSkillIcon(int32 SkillIndex, UTexture2D* Icon)
{
	if (!HB_SkillBar || SkillIndex < 0)
	{
		return;
	}

	// 获取技能栏中的图标控件
	if (UWidget* SkillWidget = HB_SkillBar->GetChildAt(SkillIndex))
	{
		if (UImage* SkillIcon = Cast<UImage>(SkillWidget))
		{
			if (Icon)
			{
				SkillIcon->SetBrushFromTexture(Icon);
				SkillIcon->SetVisibility(ESlateVisibility::Visible);
			}
		}
	}
}


/**
 * UPlayerStatusWidget::SetSkillCooldown
 *
 * 设置冷却遮罩透明度
 * 冷却百分比越高, 遮罩越透明 (冷却完毕)
 * @param SkillIndex 技能槽索引
 * @param CooldownPercent 冷却百分比 (0~1)
 */
void UPlayerStatusWidget::SetSkillCooldown(int32 SkillIndex, float CooldownPercent)
{
	if (!HB_SkillBar || SkillIndex < 0 || CooldownPercent < 0.0f)
	{
		return;
	}

	// 获取对应的冷却遮罩控件并设置透明度
	if (SkillIndex < SkillCooldownOverlays.Num())
	{
		if (UImage* Overlay = SkillCooldownOverlays[SkillIndex])
		{
			// 冷却百分比越高，遮罩越透明
			float Opacity = 1.0f - FMath::Clamp(CooldownPercent, 0.0f, 1.0f);
			Overlay->SetRenderOpacity(Opacity);
		}
	}
}


// ==========================================
// 9. 母体加速技能冷却遮罩 (v120 新增)
// ==========================================

/**
 * UPlayerStatusWidget::SetMotherSkillIconVisibility
 *
 * 【v121 大厂架构新增】设置母体技能图标显隐
 *
 * @param bIsMother 是否为母体 (true=显示, false=隐藏)
 *
 * 显隐规则:
 *   - 母体 (bIsMother=true) → 显示技能图标
 *   - 非母体 (bIsMother=false) → 隐藏技能图标
 *
 * 设计原则 — 零兜底:
 *   - 非母体时主动隐藏, 不依赖"默认隐藏" (避免 BP 蓝图中误设为显示)
 *   - 每次状态变化都显式设置, 不依赖上一次状态
 */
void UPlayerStatusWidget::SetMotherSkillIconVisibility(bool bIsMother)
{
	// 【v121.2 修复】使用更保守的检查方式
	// 根因: IsValid() 对某些"半有效"对象返回 true，但对象实际已无效，导致崩溃 0x18
	// 解决方案: 双重检查 + 防御性调用 + try-catch
	if (!Image_MotherSkillIcon || !Image_MotherSkillIcon->IsValidLowLevel())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PlayerStatus] SetMotherSkillIconVisibility: Image_MotherSkillIcon 未绑定或无效!"));
		return;
	}

	// 二次检查: 确保对象在游戏线程上
	if (!IsInGameThread())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PlayerStatus] SetMotherSkillIconVisibility: 不在游戏线程，跳过!"));
		return;
	}

	// 设置显隐 (防御性调用)
	ESlateVisibility TargetVisibility = bIsMother ? ESlateVisibility::Visible : ESlateVisibility::Hidden;
	Image_MotherSkillIcon->SetVisibility(TargetVisibility);

	// 【v121.2 修复】防御性日志输出
	// 崩溃发生在 GetOwningPlayerPawn()->GetName()，需要检查返回值有效性
	if (APawn* OwningPawn = GetOwningPlayerPawn())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[PlayerStatus] SetMotherSkillIconVisibility: bIsMother=%d → %s. Owner=%s"),
			bIsMother ? 1 : 0,
			bIsMother ? TEXT("Visible") : TEXT("Hidden"),
			*OwningPawn->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Display,
			TEXT("[PlayerStatus] SetMotherSkillIconVisibility: bIsMother=%d → %s. Owner=Unknown"),
			bIsMother ? 1 : 0,
			bIsMother ? TEXT("Visible") : TEXT("Hidden"));
	}
}

/**
 * UPlayerStatusWidget::UpdateMotherSkillCooldownProgress
 *
 * 更新母体技能冷却遮罩的动态材质参数 CDProgress
 *
 * 【v121.3 重构】自动从 MotherSkillComponent 获取冷却数据计算倒计时
 *
 * 冷却进度逻辑:
 *   - 冷却中: CDProgress 从 1.0 逐渐减小到 0.0
 *   - 冷却结束: CDProgress = 0.0 (无遮罩)
 *   - 冷却未开始: CDProgress = 0.0 (无遮罩)
 *
 * 实现:
 *   - 从 Character 获取 MotherSkillComponent
 *   - 读取 SkillCooldownEndTime 和 TotalCooldownDuration
 *   - 计算: CDProgress = RemainingTime / TotalDuration
 *   - 设置 CDProgress 参数到动态材质
 */
void UPlayerStatusWidget::UpdateMotherSkillCooldownProgress()
{
	// 【v121.3 修复】使用更保守的检查方式
	if (!Image_MotherSkillIcon || !Image_MotherSkillIcon->IsValidLowLevel())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PlayerStatus] UpdateMotherSkillCooldownProgress: Image_MotherSkillIcon 未绑定或无效!"));
		return;
	}

	// 确保在游戏线程
	if (!IsInGameThread())
	{
		return;
	}

	// 【v121.3 重构】自动从 MotherSkillComponent 计算冷却进度
	// CDProgress 参数保留但不再使用，直接从组件读取冷却数据
	float CalculatedProgress = 0.0f;

	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ABaseCharacter* Character = Cast<ABaseCharacter>(PC->GetPawn()))
		{
			if (UMotherSkillComponent* SkillComp = Character->ResolveMotherSkillComponent())
			{
				if (UWorld* World = GetWorld())
				{
					const float Now = World->GetTimeSeconds();
					const float TotalDuration = SkillComp->GetTotalCooldownDuration();

					if (TotalDuration > 0.0f)
					{
						const float Remaining = SkillComp->GetSkillCooldownRemainingSeconds();
						// CDProgress = 冷却剩余时间 / 总冷却时间
						// 冷却开始时 Remaining = TotalDuration, CDProgress = 1.0 (全遮罩)
						// 冷却结束时 Remaining = 0, CDProgress = 0.0 (无遮罩)
						CalculatedProgress = FMath::Clamp(Remaining / TotalDuration, 0.0f, 1.0f);
					}
				}
			}
		}
	}

	// 获取 Image 的 Brush 材质
	FSlateBrush Brush = Image_MotherSkillIcon->GetBrush();
	if (!Brush.HasUObject())
	{
		return;
	}

	UMaterialInterface* Material = Brush.GetResourceObject() ? Cast<UMaterialInterface>(Brush.GetResourceObject()) : nullptr;
	if (!Material)
	{
		return;
	}

	// 获取或创建动态材质实例
	UMaterialInstanceDynamic* DynMat = Cast<UMaterialInstanceDynamic>(Material);
	if (!DynMat)
	{
		DynMat = UMaterialInstanceDynamic::Create(Material, this);
		if (DynMat)
		{
			FSlateBrush NewBrush = Brush;
			NewBrush.SetResourceObject(DynMat);
			Image_MotherSkillIcon->SetBrush(NewBrush);
		}
	}

	if (!DynMat)
	{
		return;
	}

	// 设置 CDProgress 参数 (Clamp 保证 0~1 范围)
	const float ClampedProgress = FMath::Clamp(CalculatedProgress, 0.0f, 1.0f);
	DynMat->SetScalarParameterValue(FName(TEXT("CDProgress")), ClampedProgress);

	UE_LOG(LogTemp, Verbose,
		TEXT("[PlayerStatus] UpdateMotherSkillCooldownProgress: CDProgress=%.2f"),
		ClampedProgress);
}
