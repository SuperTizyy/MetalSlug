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
	
	// 【新增】：标记玩家是不是刚从房间里退出来
	UPROPERTY(BlueprintReadWrite, Category = "State")
	bool bIsReturningFromRoom = false;
	
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
	
	// 【新增】检查某个账号是否已经在线
	UFUNCTION(BlueprintCallable, Category = "Account")
	bool IsAccountOnline(const FString& Username);

	// 【新增】执行登出操作（解除状态锁）
	UFUNCTION(BlueprintCallable, Category = "Account")
	void Logout();

	// 【新增】重写子系统销毁函数（当玩家点右上角X关闭游戏时自动触发）
	virtual void Deinitialize() override;
	
	// 【新增】获取当前登录的玩家账号名
	UFUNCTION(BlueprintCallable, Category = "Account")
	FString GetCurrentLoggedInUser() const { return CurrentLoggedInUser; }
	
	// ==========================================
	// 战备偏好记忆功能
	// ==========================================
	
	// 获取当前账号上次选中的角色名
	UFUNCTION(BlueprintCallable, Category = "Account")
	FString GetLastSelectedCharacter();

	// 实时保存当前账号选中的角色名到硬盘
	UFUNCTION(BlueprintCallable, Category = "Account")
	void SaveLastSelectedCharacter(const FString& CharacterName);
	
	UFUNCTION(BlueprintCallable, Category = "Account")
	FString GetLastSelectedWeapon(int32 BackpackSlot); // 传入 1 或 2

	UFUNCTION(BlueprintCallable, Category = "Account")
	void SaveLastSelectedWeapon(int32 BackpackSlot, const FString& WeaponRowName);
	
	// ==========================================
	// 开发与测试专用接口
	// ==========================================
	
	//执行伪登录，跳过验证并分配一个临时身份
	UFUNCTION(BlueprintCallable, Category = "Account|Debug")
	void MockLoginForTesting();

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
	
	// 【新增】记录当前这个游戏窗口登录的账号名
	FString CurrentLoggedInUser;
};
