#pragma once

// 包含核心最小化组件
#include "CoreMinimal.h"
// 包含开发者设置模块的基类
#include "Engine/DeveloperSettings.h"
// 反射头文件
#include "MetalSlugTestSettings.generated.h"

/**
 * 工业级规范：全局开发与测试配置中心
 * 可以在编辑器的 Project Settings (项目设置) 中直接修改，无需在蓝图中连线
 */
// Config=Game 表示配置将保存在 DefaultGame.ini 中
// defaultconfig 告诉引擎这是一个默认加载的配置
// meta=(DisplayName) 是它在编辑器 Project Settings 侧边栏里显示的名称
UCLASS(Config=Game, defaultconfig, meta=(DisplayName="MetalSlug Debug Settings"))
class METALSLUG01_API UMetalSlugTestSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 构造函数：初始化默认值，确保线上包默认关闭作弊功能
	UMetalSlugTestSettings() 
	{ 
		bSkipLoginDirectToLobby = false; 
	}

	// 测试开关：勾选后，启动登录地图时将直接跳过账号登录，并分配随机身份直通联机大厅
	UPROPERTY(Config, EditAnywhere, Category = "UI Flow Testing", meta = (ToolTip="开启后无视正常流程，自动分配伪装账号并进入大厅"))
	bool bSkipLoginDirectToLobby;
};