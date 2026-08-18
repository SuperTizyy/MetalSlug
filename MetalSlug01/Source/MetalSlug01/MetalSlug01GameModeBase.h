// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// MetalSlug01GameModeBase 头文件 (项目最基础 GameMode)
// ==========================================
//
// 文件作用:
//   1. 声明 AMyGameModeBase 类 — 继承 AGameModeBase
//   2. 声明构造函数 (用于配置默认 Pawn/HUD/PlayerController)
//   3. 这是关卡中所有游戏规则、玩家生成、UI 挂载的"上帝"控制器
//
// 职责说明:
//   - 继承自 UE 原生的 AGameModeBase,作为本项目的默认游戏模式入口
//   - 在构造函数中绑定默认的 Pawn（玩家角色蓝图）和 HUD（抬头显示器）
//   - 战斗地图会使用更专门的 ARoomGameMode,本类主要用于登录/菜单地图
//
// 设计理念 (大厂原则 - 极简化基类):
//   1. 极简化设计: 不包含任何游戏逻辑,仅作为"占位"基类
//   2. 易于扩展: 任何关卡都可以直接继承本类并按需覆写关键参数
//   3. 默认值合理: 构造函数中已配置好 Pawn 与 HUD, 拖到关卡即可使用
// ==========================================

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件（FString/TArray/基础宏）
#include "CoreMinimal.h"

// 引入 UE 原生 AGameModeBase 类（基类）
#include "GameFramework/GameModeBase.h"

// UE 自动生成的头文件（必须放在最后一行）
// 作用: 启用反射、序列化、蓝图集成等 UE 反射系统功能
#include "MetalSlug01GameModeBase.generated.h"

/**
 * @class AMyGameModeBase
 * @brief 项目最基础的 GameMode 类
 *
 * 职责说明:
 * - 继承自 UE 原生的 AGameModeBase，作为本项目的默认游戏模式入口
 * - 在构造函数中绑定默认的 Pawn（玩家角色蓝图）和 HUD（抬头显示器）
 * - 是关卡中所有游戏规则、玩家生成、UI 挂载的"上帝"控制器
 * - 战斗地图会使用更专门的 ARoomGameMode，本类主要用于登录/菜单地图
 *
 * 设计理念:
 * 1. 极简化设计: 不包含任何游戏逻辑，仅作为"占位"基类
 * 2. 易于扩展: 任何关卡都可以直接继承本类并按需覆写关键参数
 * 3. 默认值合理: 构造函数中已配置好 Pawn 与 HUD，拖到关卡即可使用
 *
 * 【关键字段说明】
 * - 无自定义 UPROPERTY: 基类 GameMode 的 DefaultPawnClass/HUDClass/PlayerControllerClass 已够用
 * - 战斗逻辑 (MatchState/Spawn/Respawn) 在 ARoomGameMode 子类中实现
 */
UCLASS()
class METALSLUG01_API AMyGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	/**
	 * 构造函数 — 在游戏模式被加载时自动调用
	 *
	 * @brief  配置默认的玩家角色蓝图和 HUD 类
	 * @note   触发时机: 游戏进入关卡、GameMode 被引擎实例化时由 UE 自动调用
	 * @note   约束: 构造函数中只能使用 ConstructorHelpers 或 StaticClass 等静态方式加载资源
	 *         不能调用 SpawnActor 等运行时函数,因为此时 World 尚未完全初始化
	 */
	AMyGameModeBase();
};
