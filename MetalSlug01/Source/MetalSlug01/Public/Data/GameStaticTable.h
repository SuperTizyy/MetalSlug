// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"


// ==========================================
// 1. 动画状态枚举（用于蓝图中切换动画）
// ==========================================

/**
 * 角色动画状态枚举
 * 用途: 在 AnimBP 中根据状态切换动画
 */
UENUM(BlueprintType)
enum class ECharacterAnimationState : uint8
{
	Idle UMETA(DisplayName = "待机"),
	Walking UMETA(DisplayName = "行走"),
	Running UMETA(DisplayName = "奔跑"),
	Jumping UMETA(DisplayName = "跳跃"),
	Falling UMETA(DisplayName = "坠落"),
	Attacking UMETA(DisplayName = "攻击"),
	TakingDamage UMETA(DisplayName = "受伤"),
	Dying UMETA(DisplayName = "死亡"),
	Climbing UMETA(DisplayName = "攀爬"),
	Sliding UMETA(DisplayName = "滑铲"),
	Swimming UMETA(DisplayName = "游泳"),
	Crouching UMETA(DisplayName = "蹲伏"),
	Prone UMETA(DisplayName = "趴下"),
	Aiming UMETA(DisplayName = "瞄准"),
	Reloading UMETA(DisplayName = "装弹")
};


// ==========================================
// 2. 角色类型枚举（用于区分不同角色）
// ==========================================

/**
 * 角色类型枚举
 * 用途: 区分玩家/敌人/NPC/Boss
 */
UENUM(BlueprintType)
enum class ECharacterType : uint8
{
	Player UMETA(DisplayName = "玩家角色"),
	Enemy UMETA(DisplayName = "敌人角色"),
	NPC UMETA(DisplayName = "NPC角色"),
	Boss UMETA(DisplayName = "Boss角色")
};


// ==========================================
// 3. 玩家可控制的角色ID枚举
// ==========================================

/**
 * 玩家可控制的角色 ID 枚举
 * 用途: 蓝图面板中选择不同的角色
 */
UENUM(BlueprintType)
enum class EControlledCharacterID : uint8
{
	Character1 UMETA(DisplayName = "角色1"),
	Character2 UMETA(DisplayName = "角色2"),
	Character3 UMETA(DisplayName = "角色3")
};


// ==========================================
// 4. 移动状态枚举
// ==========================================

/**
 * 角色移动状态枚举
 * 用途: AnimBP 根据状态切换对应动画
 */
UENUM(BlueprintType)
enum class EMovementState : uint8
{
	OnGround UMETA(DisplayName = "地面"),
	InAir UMETA(DisplayName = "空中"),
	Swimming UMETA(DisplayName = "水中"),
	Climbing UMETA(DisplayName = "攀爬"),
	Sliding UMETA(DisplayName = "滑行")
};


// ==========================================
// 5. 玩家索引枚举（用于分屏游戏）
// ==========================================

/**
 * 玩家索引枚举
 * 用途: 分屏游戏中的不同玩家
 */
UENUM(BlueprintType)
enum class EPlayerIndex : uint8
{
	Player1 UMETA(DisplayName = "玩家1"),
	Player2 UMETA(DisplayName = "玩家2"),
	Player3 UMETA(DisplayName = "玩家3"),
	Player4 UMETA(DisplayName = "玩家4")
};


// ==========================================
// 6. 冲刺状态枚举（战士冲刺的不同状态）
// ==========================================

/**
 * 冲刺状态枚举
 * 用途: 跟踪战士冲刺的完整生命周期
 */
UENUM(BlueprintType)
enum class ESprintState : uint8
{
	NotSprinting UMETA(DisplayName = "未冲刺"),  // 未处于冲刺状态
	Sprinting UMETA(DisplayName = "冲刺中"),     // 正在冲刺中
	Cooldown UMETA(DisplayName = "冷却中")       // 冲刺后处于冷却状态
};
