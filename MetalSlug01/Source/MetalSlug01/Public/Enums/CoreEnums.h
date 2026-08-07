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
	Battleing		UMETA(DisplayName = "Battleing"),       // 战斗态：核心对战关卡内
	// 【v216 大厂架构新增】结算页状态 — 跨地图显示
	//   - MulticastEnterSettlement_Implementation 在服务器广播时, 服务器调 RequestStateOnNextLoad(SettlementPage)
	//   - L_Login 加载完后, GameFlowSubsystem 切到 SettlementPage → UIViewService ShowPanel(SettlementPanel)
	//   - UScoreboardWidget::NativeConstruct 时 ConsumeSnapshot → ApplySnapshot → 显示结算 UI
	// 【v216.2 大厂架构重构】去除 RPC 链路
	//   - 历史 (v216): 玩家点 Button_ReturnToLobby → Server_SettlementReturnToLobby → Client_OpenLobbyFromSettlement
	//     → RequestStateOnNextLoad(MainLobby) — 但跨地图后 PC 类型变化导致 Cast 失败 (按钮无反应)
	//   - 新路径 (v216.2): 玩家点 Button_ReturnToLobby → UScoreboardWidget 直接调
	//     UGameFlowSubsystem::RequestStateOnNextLoad(MainLobby) → L_Login 切到主大厅状态
	//   - 两个 RPC 已全部删除 (RoomPlayerController.h): UFUNCTION 声明 + _Validate + _Implementation 全部移除
	//   - 0 网络往返, 跨 PC 类型工作 (任何 PC 都能调 GameFlowSubsystem)
	SettlementPage	UMETA(DisplayName = "Settlement Page") // 结算页状态：跨地图, 在 L_Login 上显示 ScoreboardWidget
};

/**
 * @enum EUIPanel
 * @brief UI 面板枚举（业务层只认这个，不知道具体 Widget 类）
 */
UENUM(BlueprintType)
enum class EUIPanel : uint8
{
	None        	UMETA(DisplayName = "None"),
	Login       	UMETA(DisplayName = "Login"),
	MainMenu    	UMETA(DisplayName = "Main Menu"),
	LANRoom     	UMETA(DisplayName = "LAN Room"),
	RoomInside  	UMETA(DisplayName = "Room Inside"),
	BattleHUD   	UMETA(DisplayName = "Battle HUD"),
	// 【v216 大厂架构新增】结算面板
	//   - EMatchState::SettlementPage 对应的 UI 面板
	//   - 在 L_Login 上由 UIViewService 创建, 通过 UScoreboardWidget::ApplySnapshot 应用跨地图快照
	//   - 历史 (v22-v215.x): 在 WBP_GameHUDWidget 内嵌结算覆盖板, 玩家不能离开房间关卡
	//   - 新 (v216): 在 L_Login 上独立显示, 玩家切图到大厅, 状态机驱动 UI 切换
	SettlementPanel	UMETA(DisplayName = "Settlement Panel") // 结算面板：跨地图, 在 L_Login 上显示
};

// UE 自动生成的头文件（必须放在所有 UENUM/USTRUCT/UCLASS 声明之后，
// 且本身必须被 include，否则 TIsUEnumClass 模板特化不会生效，导致
// 引用了 EMatchState/EUIPanel 的 DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam
// 宏展开失败 — 报错 C2143/C4430/C3861）
#include "CoreEnums.generated.h"
