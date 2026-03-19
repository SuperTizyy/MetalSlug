#pragma once

// 包含虚幻引擎核心最小化组件
#include "CoreMinimal.h"
// 包含游戏实例子系统的基类头文件
#include "Subsystems/GameInstanceSubsystem.h"
// 【关键引入】包含你刚刚建好的数据表头文件，这样子系统就能认识 FAccountRecord 结构体了
#include "UI/Login/Data/DynamicTable.h"
// 自动生成的反射头文件，必须放在最后一行
#include "AccountSubsystem.generated.h"

// 前向声明我们要用的 SaveGame 容器类，避免在头文件中引入多余代码
class UAccountSaveGame;

/**
 * 账号管理全局子系统
 * 负责在内存中高速处理玩家的注册、登录请求，并在适当时机写入硬盘
 */
UCLASS()
class METALSLUG01_API UAccountSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 重写子系统的初始化生命周期函数，游戏刚启动时自动触发
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ==========================================
	// 核心业务接口 (供 LoginPage 等 UI 调用)
	// ==========================================

	// 尝试执行登录逻辑
	// 传入玩家输入的账号和密码，如果匹配返回 true，否则返回 false
	UFUNCTION(BlueprintCallable, Category = "Account")
	bool TryLogin(const FString& Username, const FString& Password);

	// 尝试执行注册逻辑
	// 传入玩家想要注册的账号和密码，如果账号已存在返回 false，成功注册返回 true
	UFUNCTION(BlueprintCallable, Category = "Account")
	bool TryRegister(const FString& Username, const FString& Password);

private:
	// ==========================================
	// 内部数据与读写操作
	// ==========================================

	// 【核心内存数据】存储所有账号的哈希表
	// Key 是账号字符串，Value 是你写的 FAccountRecord 结构体（档案袋）
	// 所有的高频查找都在这个内存变量中进行，绝不卡顿
	TMap<FString, FAccountRecord> AccountData;

	// 定义物理硬盘上 .sav 存档文件的名称
	const FString SaveSlotName = TEXT("LocalAccountDataSlot");

	// 私有底层工具：从硬盘上的 .sav 文件中读取数据，并填充到内存的 AccountData 中
	void LoadDataFromDisk();

	// 私有底层工具：将当前内存中的 AccountData 数据，物理写入到硬盘的 .sav 文件中
	void SaveDataToDisk();
};
