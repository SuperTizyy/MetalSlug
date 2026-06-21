#pragma once

// 包含核心最小化组件
#include "CoreMinimal.h"
// 包含开发者设置模块的基类
#include "Engine/DeveloperSettings.h"
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
 * - bSkipLoginDirectToLobby: 跳过登录直通大厅（测试用）
 * - bAutoCreateTestRoom: 自动创建测试房间（第一个窗口）或自动加入已有房间
 * - AutoTestRoomName: 测试房间的固定名称
 * - AutoTestRoomSearchTimeout: 搜索房间最大等待时间
 */
UCLASS(Config=Game, defaultconfig, meta=(DisplayName="MetalSlug Debug Settings"))
class METALSLUG01_API UMetalSlugTestSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMetalSlugTestSettings()
	{
		bSkipLoginDirectToLobby = false;
		bAutoCreateTestRoom = false;
		AutoTestRoomName = TEXT("AutoTestRoom");
		AutoTestRoomSearchTimeoutSeconds = 30.0f;
		AutoTestRoomMapName = TEXT("L_Room");
		AutoTestRoomMaxPlayers = 4;
	}

	// ==========================================
	// 登录跳过配置
	// ==========================================

	// 测试开关：勾选后，启动登录地图时将直接跳过账号登录，并分配随机身份直通联机大厅
	UPROPERTY(Config, EditAnywhere, Category = "UI Flow Testing", meta = (ToolTip="开启后无视正常流程，自动分配伪装账号并进入大厅"))
	bool bSkipLoginDirectToLobby;

	// ==========================================
	// 自动测试房间配置（bSkipLoginDirectToLobby=true 时生效）
	// ==========================================

	// 开启后：第一个客户端自动创建测试房间，其余客户端自动搜索并加入
	UPROPERTY(Config, EditAnywhere, Category = "UI Flow Testing|Auto Test Room",
		meta = (ToolTip="开启后，第一个客户端自动创建测试房间，其余自动加入"))
	bool bAutoCreateTestRoom;

	// 测试房间的固定名称（所有客户端搜索同一名称的房间）
	UPROPERTY(Config, EditAnywhere, Category = "UI Flow Testing|Auto Test Room",
		meta = (ToolTip="自动测试房间的名称，所有客户端搜索此名称的房间"))
	FString AutoTestRoomName;

	// 搜索房间的最大等待时间（秒），超时后非首个客户端放弃搜索
	UPROPERTY(Config, EditAnywhere, Category = "UI Flow Testing|Auto Test Room",
		meta = (ToolTip="非首个客户端搜索房间的最大等待时间，超时后放弃"))
	float AutoTestRoomSearchTimeoutSeconds;

	// 自动测试房间加载的地图名称
	UPROPERTY(Config, EditAnywhere, Category = "UI Flow Testing|Auto Test Room",
		meta = (ToolTip="自动测试房间使用的地图资产名"))
	FString AutoTestRoomMapName;

	// 自动测试房间的最大玩家数
	UPROPERTY(Config, EditAnywhere, Category = "UI Flow Testing|Auto Test Room",
		meta = (ClampMin = "2", ClampMax = "10", ToolTip="自动测试房间的最大人数"))
	int32 AutoTestRoomMaxPlayers;
};