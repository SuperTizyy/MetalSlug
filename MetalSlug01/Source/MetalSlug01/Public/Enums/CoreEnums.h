// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

#include "CoreMinimal.h"

/**
 * @enum EMatchState
 * @brief 全局游戏链路状态机
 *
 * 严格定义玩家在游戏生命周期中可能处于的每一个核心阶段。
 * 任何"我下一步该显示什么 UI / 进入哪个关卡"的逻辑都必须以本枚举作为唯一切换依据。
 */
UENUM(BlueprintType)
enum class EMatchState : uint8
{
	PreLogin		UMETA(DisplayName = "Pre-Login"),       // 预加载/Logo 阶段（目前预留，用于启动 Logo 展示）
	Login			UMETA(DisplayName = "Login"),           // 登录态：在此展示账号输入界面
	MainMenu		UMETA(DisplayName = "Main Menu"),       // 主菜单态：单人/多人模式选择（不跳转地图，留在 L_Login）
	MainLobby		UMETA(DisplayName = "Main Lobby"),      // 大厅态：局域网房间列表/匹配（不跳转地图，留在 L_Login）
	InRoom			UMETA(DisplayName = "In Room"),         // 房间态：局域网房间等待对战
	Battleing		UMETA(DisplayName = "Battleing")        // 战斗态：核心对战关卡内
};

/**
 * @enum EUIPanel
 * @brief UI 面板枚举（业务层只认这个，不知道具体 Widget 类）
 */
UENUM(BlueprintType)
enum class EUIPanel : uint8
{
	None        UMETA(DisplayName = "None"),
	Login       UMETA(DisplayName = "Login"),
	MainMenu    UMETA(DisplayName = "Main Menu"),
	LANRoom     UMETA(DisplayName = "LAN Room"),
	RoomInside  UMETA(DisplayName = "Room Inside"),
	BattleHUD   UMETA(DisplayName = "Battle HUD")
};

// UE 自动生成的头文件（必须放在所有 UENUM/USTRUCT/UCLASS 声明之后，
// 且本身必须被 include，否则 TIsUEnumClass 模板特化不会生效，导致
// 引用了 EMatchState/EUIPanel 的 DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam
// 宏展开失败 — 报错 C2143/C4430/C3861）
#include "CoreEnums.generated.h"
