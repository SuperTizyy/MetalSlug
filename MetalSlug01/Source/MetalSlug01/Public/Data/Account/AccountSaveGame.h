// ==========================================
// 头文件包含说明
// ==========================================
#pragma once
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"
// 引入 UE 原生 USaveGame 类（基类）
#include "GameFramework/SaveGame.h"
// 让这个纸箱类认识你的"档案袋"结构体
#include "Data/Account/AccountSaveTypes.h"
// UE 自动生成的头文件
#include "AccountSaveGame.generated.h"


// ==========================================
// 存档管理类 (物流纸箱)
// ==========================================

/**
 * @class UAccountSaveGame
 * @brief 本地账号存档（纯数据容器）
 *
 * 【大厂架构原则：Storage 是哑数据层】
 * - 只负责"装箱/拆箱"，不持有任何业务逻辑、不做任何校验。
 * - 读写入口统一在 ULocalAccountStore（仓储层）。
 *
 * 【关键修复】本次重构的两大变化：
 * 1) 移除 bIsOnline 字段（已迁移到 Session 层）。
 * 2) 新增 SlotSchemaVersion，用于未来存档格式升级时做兼容迁移。
 */
UCLASS()
class METALSLUG01_API UAccountSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/**
	 * 存档 schema 版本号
	 * 用途: 后续对 FAccountRecord 字段做不兼容变更时，可读取该值决定走迁移分支。
	 */
	UPROPERTY(SaveGame)
	int32 SlotSchemaVersion = 1;

	/**
	 * 存储本客户端注册过的所有账号记录
	 * Key (左边) = 账号名（方便极速查找某个账号是否存在）
	 * Value (右边) = 对应的 FAccountRecord 结构体（也就是那个档案袋）
	 */
	UPROPERTY(SaveGame)
	TMap<FString, FAccountRecord> AccountRecords;
};