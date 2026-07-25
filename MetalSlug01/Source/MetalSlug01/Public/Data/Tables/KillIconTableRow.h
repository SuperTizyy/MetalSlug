// ==========================================
// 击杀图标/连杀图标表行
// 关联: DT_KillIconInfo / DT_KillStreakIconInfo
// 替代: 原 StaticTable.h 中的 FKillIconInfo / FKillStreakIconInfo 段
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "../Enums/CombatEnums.h" // 依赖 EKillMethod / EKillStreakType
#include "KillIconTableRow.generated.h"

class UTexture2D;
class USoundBase;

/**
 * @struct FKillIconInfo
 * @brief 击杀图标信息表行
 * 用途: 关联 DT_KillIconInfo, 让 HUD 根据击杀方式显示对应图标
 */
USTRUCT(BlueprintType)
struct FKillIconInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 击杀方式枚举 (查找键) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Icon Data")
	EKillMethod KillMethod;

	/** 击杀图标 (供 HUD 击杀信息显示) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Icon Data")
	TObjectPtr<UTexture2D> KillIcon;
};

/**
 * @struct FKillStreakIconInfo
 * @brief 连杀图标 + 音效信息表行
 * 用途: 关联 DT_KillStreakIconInfo, 让 HUD 根据连杀数显示对应图标 + 播放对应音效
 *
 * v100 大厂架构新增 KillSound 字段:
 *   - 旧版只管图标, 音效缺失
 *   - 新版: 同表同枚举, 一对一映射图标+音效 (数据驱动, BP 策划一行配置)
 *   - 不新建 DataTable (零重复架构)
 *   - 不新建枚举 (复用 EKillStreakType)
 */
USTRUCT(BlueprintType)
struct FKillStreakIconInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 连杀类型枚举 (查找键) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Streak Icon Data")
	EKillStreakType StreakType;

	/** 连杀图标 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Streak Icon Data")
	TObjectPtr<UTexture2D> StreakIcon;

	/**
	 * 【v100 大厂架构新增】击杀音效
	 *
	 * 业务规则 (用户 2026.07.26 明确):
	 *   - 每个连杀都是不同声音
	 *   - 爆头 / 一杀 ~ 五杀 / 五杀后 各自独立音效
	 *   - 数据驱动: BP 策划在 DT_KillStreakIconInfo 里按 EKillStreakType 行名配 Sound
	 *
	 * 必填 (零兜底):
	 *   - UKillSoundComponent::PlayKillSound 收到空指针 → Log Error + 拒绝播放
	 *   - 不允许用默认 SoundCue 兜底 (配置错必须显式)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Streak Icon Data")
	TObjectPtr<USoundBase> KillSound;
};
