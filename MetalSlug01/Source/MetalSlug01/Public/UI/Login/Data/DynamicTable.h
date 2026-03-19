#pragma once

// 引入核心头文件
#include "CoreMinimal.h"
// 必须包含自动生成的反射文件，且必须放在所有的 include 的最下面！
#include "DynamicTable.generated.h"

// ==========================================
// 运行时数据结构 (档案袋)
// ==========================================

/**
 * 玩家账号记录结构体
 * 用于将玩家的单个账号所有相关信息打包在一起
 */
USTRUCT(BlueprintType)
struct FAccountRecord
{
	GENERATED_BODY()

public:
	// 玩家的登录账号（用户名）
	// SaveGame 宏标记这个变量需要被写入硬盘
	UPROPERTY(SaveGame)
	FString Username;

	// 玩家的登录密码（实际商业项目中这里会存加密后的哈希值，我们先存明文）
	UPROPERTY(SaveGame)
	FString Password;
	
	// 结构体的默认构造函数
	FAccountRecord()
	{
		// 默认初始化为空字符串
		Username = TEXT("");
		Password = TEXT("");
	}

	// 带有参数的构造函数，方便我们在注册时一行代码完成打包
	FAccountRecord(FString InUsername, FString InPassword)
	{
		Username = InUsername;
		Password = InPassword;
	}
};
