// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// 包含核心最小化组件
#include "CoreMinimal.h"
// 包含开发者设置模块的基类
#include "Engine/DeveloperSettings.h"
// 引入房间模式枚举 (ERoomMatchMode)
#include "Data/Enums/RoomEnums.h"
// 反射头文件
#include "MetalSlugTestSettings.generated.h"

/**
 * @file MetalSlugTestSettings.h
 * @brief 全局开发与测试配置中心
 * @details 可以在编辑器的 Project Settings (项目设置) 中直接修改，无需在蓝图中连线
 *
 * 设计要点:
 * 1. 继承 UDeveloperSettings: 自动注册到 Project Settings 侧边栏
 * 2. Config=Game + defaultconfig: 配置存于 DefaultGame.ini, 启动加载
 * 3. 所有 UPROPERTY 标记 Config: 编辑器修改自动写入 .ini
 * 4. 生产环境默认值关闭: 避免线上包携带开启的作弊功能
 *
 * 当前配置项:
 * - bSkipLoginDirectToLobby: 跳过登录自动当测试房主 (调试用, 2026.07.03 P0 重构)
 */
UCLASS(Config=Game, defaultconfig, meta=(DisplayName="MetalSlug Debug Settings"))
class METALSLUG01_API UMetalSlugTestSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMetalSlugTestSettings()
	{
		bSkipLoginDirectToLobby = false;
	}

	// ==========================================
	// 登录跳过配置
	// ==========================================

	// ==========================================
	// 【大厂 P0 修复 2026.07.03】测试房主模式开关
	// ==========================================
	// 勾选后行为:
	//   1. GameFlow 启动时调用 AccountSubsystem::MockLoginForTesting() 注入测试身份
	//      → CurrentLoggedInUser = "TestUser_XXXX" (随机, 多开不重名)
	//   2. 调用 RoomService::EnterSkipToHostMode() 显式标房主
	//      → bIsHost = true + 广播 OnHostChanged(true)
	//      → RoomInsidePage 立即以"房主"形态显示按钮 (StartGame / OpenAI)
	//   3. 根据启动地图类型分发:
	//      - L_Login  (主菜单地图) → TransitToState(MainLobby) → 显示 LANRoomPage
	//      - 战斗地图              → OnInterrupted.Broadcast(RoomInside) → 显示 RoomInsidePage
	//
	// 与旧版本的差异:
	//   - 旧: 直接 TransitToState(MainLobby), 假设启动地图是 L_Login
	//        战斗地图启动时会被"自愈"链路推回 InRoom, 房主按钮不显示 (BUG)
	//   - 新: 显式分两步走, 战斗地图启动也能正常显示房主 UI
	//
	// 安全:
	//   - 生产环境默认 false
	//   - 不会写盘 (MockLogin 临时身份)
	//   - 关闭游戏后自动销毁
	// ==========================================
	UPROPERTY(Config, EditAnywhere, Category = "UI Flow Testing", meta = (ToolTip="开启后跳过登录, 自动以测试房主身份进入房间页 (战斗地图启动也能正确显示房主 UI)"))
	bool bSkipLoginDirectToLobby;

	// ==========================================
	// 【v54.5.1 新增】bSkipLoginDirectToLobby=true 时自动创建的房间地图
	// ==========================================
	// 说明: 当 bSkipLoginDirectToLobby=true 时, 自动用本地图启动房间
	// 默认: Japanese_Temple_Demo
	UPROPERTY(Config, EditAnywhere, Category = "Skip Login Room Config",
		meta = (ToolTip="bSkipLoginDirectToLobby=true 时自动进入的房间地图"))
	FString DebugSkipBattleMapName = TEXT("Japanese_Temple_Demo");

	// ==========================================
	// 【v54.5.1 新增】bSkipLoginDirectToLobby=true 时自动创建的房间游戏模式
	// ==========================================
	// 说明: 当 bSkipLoginDirectToLobby=true 时, 自动设置本游戏模式
	// Melee=刀战模式, Zombie=生化模式
	// 注意: UPROPERTY 不支持 enum class, 用 uint8 存储, 提供访问器转换
	UPROPERTY(Config, EditAnywhere, Category = "Skip Login Room Config",
		meta = (ToolTip="bSkipLoginDirectToLobby=true 时自动设置的游戏模式 (0=None, 1=Melee, 2=Zombie)"))
	uint8 DebugSkipRoomMode = static_cast<uint8>(ERoomMatchMode::Melee);

	/** 获取 DebugSkipRoomMode 对应的枚举值 */
	ERoomMatchMode GetDebugSkipRoomMode() const
	{
		return static_cast<ERoomMatchMode>(DebugSkipRoomMode);
	}
};
