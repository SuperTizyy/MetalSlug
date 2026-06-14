// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// 引入核心头文件
#include "CoreMinimal.h"
// 必须包含自动生成的反射文件，且必须放在所有的 include 的最下面！
#include "DynamicTable.generated.h"


// ==========================================
// 运行时数据结构 (档案袋)
// ==========================================

/**
 * @struct FAccountRecord
 * @brief 玩家账号记录结构体
 *
 * 职责说明:
 * - 将玩家的单个账号所有相关信息打包
 * - 由 UAccountSubsystem 持有
 * - 配合 USaveGame 写入硬盘
 *
 * 架构理念:
 * 1. USTRUCT(BlueprintType): 允许蓝图使用
 * 2. SaveGame 标记: 只有标记的字段才会被序列化到硬盘
 * 3. 默认构造: 所有字段默认初始化
 * 4. 重载构造: 支持一行代码打包（注册时）
 *
 * 安全提示:
 * - Password 实际商业项目应存哈希值（这里明文仅供学习）
 * - bIsOnline 是软锁, 真正的多端登入还需要服务端校验
 */
USTRUCT(BlueprintType)
struct FAccountRecord
{
	GENERATED_BODY()

public:
	/**
	 * 玩家的登录账号（用户名）
	 * SaveGame 宏标记这个变量需要被写入硬盘
	 */
	UPROPERTY(SaveGame)
	FString Username;

	/**
	 * 玩家的登录密码
	 * 实际商业项目中这里会存加密后的哈希值，我们先存明文
	 */
	UPROPERTY(SaveGame)
	FString Password;

	/**
	 * 在线状态锁
	 * 用途: 防止同一个账号被多开登录
	 */
	UPROPERTY(SaveGame)
	bool bIsOnline;

	/**
	 * 【新增】记录该账号最后一次选择的战备角色
	 * 用途: 下次进入游戏时自动恢复上次的角色选择
	 */
	UPROPERTY(SaveGame)
	FString LastSelectedCharacter;

	/**
	 * 【新增】记录该账号的武器背包 1 的武器 ID（RowName）
	 * 用途: 战备武器选择持久化
	 */
	UPROPERTY(SaveGame)
	FString LastSelectedWeapon1;

	/**
	 * 【新增】记录该账号的武器背包 2 的武器 ID（RowName）
	 */
	UPROPERTY(SaveGame)
	FString LastSelectedWeapon2;

	/**
	 * 默认构造函数
	 * 初始化所有字段为安全默认值
	 */
	FAccountRecord()
	{
		// 默认初始化为空字符串
		Username = TEXT("");
		Password = TEXT("");
		bIsOnline = false; // 默认不在线
		LastSelectedCharacter = TEXT("");
		LastSelectedWeapon1 = TEXT("");
		LastSelectedWeapon2 = TEXT("");
	}

	/**
	 * 带有参数的构造函数
	 * 用途: 注册时一行代码完成打包
	 * @param InUsername 账号
	 * @param InPassword 密码
	 */
	FAccountRecord(FString InUsername, FString InPassword)
	{
		Username = InUsername;
		Password = InPassword;
		bIsOnline = false; // 注册时默认不在线
		LastSelectedCharacter = TEXT("");
		LastSelectedWeapon1 = TEXT("");
		LastSelectedWeapon2 = TEXT("");
	}
};
