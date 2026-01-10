// 版权声明：在项目设置的描述页面填写您的版权信息

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MetalSlug01GameModeBase.generated.h"

/**
 * 游戏模式基础类 - 定义游戏的核心参数和行为
 * 负责管理游戏会话的生命周期、玩家控制器类型、默认Pawn类型等
 */
UCLASS()
class METALSLUG01_API AMyBaseGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	// 构造函数 - 初始化游戏模式的各种属性
	AMyBaseGameMode();

protected:
	/** 重写初始化函数，在游戏开始前执行必要的逻辑处理 */
	virtual void BeginPlay() override;

private:
	/** 设置默认类映射：通过静态函数在构造函数中动态查找蓝图类
	 * 避免在代码中硬编码资源路径，提高代码灵活性和可维护性
	 */
	void SetupDefaultClasses();
};
