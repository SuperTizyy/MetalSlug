// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"
// 引入 UE 原生 USaveGame 类（基类）
#include "GameFramework/SaveGame.h"
// 【关键修复 3】让这个纸箱类认识你的"档案袋"结构体
#include "Account/AccountSaveTypes.h"
// UE 自动生成的头文件
#include "AccountSaveGame.generated.h"


// ==========================================
// 存档管理类 (物流纸箱)
// ==========================================

/**
 * @class UAccountSaveGame
 * @brief 全局账号存档类
 *
 * 职责说明:
 * - 负责装载所有玩家的账号记录
 * - 将其序列化到本地硬盘（通过 UE SaveGame 系统）
 * - 供 UAccountSubsystem 读写
 *
 * 架构理念:
 * 1. 数据驱动: 整个存档是一个 TMap，Key 是账号名，Value 是 FAccountRecord 档案袋
 * 2. 自动序列化: 所有 UPROPERTY(SaveGame) 标记的字段会被引擎自动持久化
 * 3. 简单容器: 本身不包含任何业务逻辑，只负责"装箱/拆箱"
 */
UCLASS()
class METALSLUG01_API UAccountSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// ==========================================
	// 持久化数据
	// ==========================================

	/**
	 * 存储所有玩家账号记录的哈希表
	 * Key (左边) = 账号名（方便极速查找某个账号是否存在）
	 * Value (右边) = 对应的 FAccountRecord 结构体（也就是那个档案袋）
	 */
	UPROPERTY(SaveGame)
	TMap<FString, FAccountRecord> AccountRecords;
};
