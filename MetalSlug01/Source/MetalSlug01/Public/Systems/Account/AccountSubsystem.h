// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// 包含虚幻引擎核心最小化组件
#include "CoreMinimal.h"
// 包含游戏实例子系统的基类头文件
#include "Subsystems/GameInstanceSubsystem.h"
// 【关键引入】包含数据表头文件，让子系统能认识 FAccountRecord 结构体
#include "Data/Account/AccountSaveTypes.h"
// 自动生成的反射头文件，必须放在最后一行
#include "AccountSubsystem.generated.h"

// 前向声明 SaveGame 容器类，避免头文件循环包含
class UAccountSaveGame;


/**
 * @class UAccountSubsystem
 * @brief 账号管理全局子系统
 *
 * 职责说明:
 * - 在内存中高速处理玩家的注册、登录请求
 * - 在适当时机写入硬盘（SaveGame 系统）
 * - 提供"战备偏好记忆"功能（记住玩家上次选的角色/武器）
 * - 提供"双开同账号互踢"防护
 *
 * 架构理念:
 * 1. 全局唯一: 继承自 UGameInstanceSubsystem，整个游戏只有一个实例
 * 2. 内存字典优先: 所有高频查找都在内存中进行，绝不卡顿
 * 3. 关键状态双重保险: 改状态时先 LoadDataFromDisk() 拿最新数据
 * 4. 即时存盘: 登录/登出立刻 SaveDataToDisk()
 */
UCLASS()
class METALSLUG01_API UAccountSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * 标记玩家是否刚从房间里退出来
	 * 用途: LoginPage 决定是否要跳过大厅
	 */
	UPROPERTY(BlueprintReadWrite, Category = "State")
	bool bIsReturningFromRoom = false;

	/**
	 * 重写子系统的初始化生命周期函数
	 * 时机: 游戏刚启动时自动触发
	 * 目的: 从硬盘加载账号数据
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ==========================================
	// 1. 核心业务接口 (供 LoginPage 等 UI 调用)
	// ==========================================

	/**
	 * 尝试执行登录逻辑
	 * 流程: 先 LoadDataFromDisk() 拿最新 -> 检查账号密码 -> 检查是否在线
	 * @param Username 玩家输入的账号
	 * @param Password 玩家输入的密码
	 * @return 匹配且未在线返回 true，否则返回 false
	 */
	UFUNCTION(BlueprintCallable, Category = "Account")
	bool TryLogin(const FString& Username, const FString& Password);

	/**
	 * 尝试执行注册逻辑
	 * @param Username 想要注册的账号
	 * @param Password 想要注册的密码
	 * @return 账号不存在返回 true（成功注册），已存在返回 false
	 */
	UFUNCTION(BlueprintCallable, Category = "Account")
	bool TryRegister(const FString& Username, const FString& Password);

	/**
	 * 检查某个账号是否已经在线
	 * @param Username 要检查的账号
	 * @return 是否在线
	 */
	UFUNCTION(BlueprintCallable, Category = "Account")
	bool IsAccountOnline(const FString& Username);

	/**
	 * 执行登出操作（解除状态锁）
	 */
	UFUNCTION(BlueprintCallable, Category = "Account")
	void Logout();

	/**
	 * 重写子系统销毁函数
	 * 时机: 当玩家点右上角X关闭游戏时自动触发
	 * 目的: 自动 Logout 防止账号永久卡死
	 */
	virtual void Deinitialize() override;

	/**
	 * 获取当前登录的玩家账号名
	 */
	UFUNCTION(BlueprintCallable, Category = "Account")
	FString GetCurrentLoggedInUser() const { return CurrentLoggedInUser; }

	// ==========================================
	// 2. 战备偏好记忆功能
	// ==========================================

	/**
	 * 获取当前账号上次选中的角色名
	 */
	UFUNCTION(BlueprintCallable, Category = "Account")
	FString GetLastSelectedCharacter();

	/**
	 * 实时保存当前账号选中的角色名到硬盘
	 */
	UFUNCTION(BlueprintCallable, Category = "Account")
	void SaveLastSelectedCharacter(const FString& CharacterName);

	/**
	 * 获取当前账号上次选中的武器名
	 * @param BackpackSlot 背包槽位（1 或 2）
	 */
	UFUNCTION(BlueprintCallable, Category = "Account")
	FString GetLastSelectedWeapon(int32 BackpackSlot);

	/**
	 * 实时保存当前账号选中的武器名到硬盘
	 * @param BackpackSlot 背包槽位（1 或 2）
	 * @param WeaponRowName 武器行名
	 */
	UFUNCTION(BlueprintCallable, Category = "Account")
	void SaveLastSelectedWeapon(int32 BackpackSlot, const FString& WeaponRowName);

	// ==========================================
	// 3. 开发与测试专用接口
	// ==========================================

	/**
	 * 执行伪登录，跳过验证并分配一个临时身份
	 * 用途: 跳过登录页面直接测试
	 * 注意: 故意不 SaveDataToDisk()，临时账号只存在于本次内存
	 */
	UFUNCTION(BlueprintCallable, Category = "Account|Debug")
	void MockLoginForTesting();

	/**
	 * 获取玩家档案的安全只读指针
	 * @param AccountName 账号名
	 * @return FAccountRecord 指针（找不到返回 nullptr）
	 */
	const FAccountRecord* GetAccountRecord(const FString& AccountName) const;

private:
	// ==========================================
	// 4. 内部数据与读写操作
	// ==========================================

	/**
	 * 核心内存数据: 存储所有账号的哈希表
	 * Key = 账号字符串，Value = FAccountRecord 档案袋
	 * 所有的高频查找都在这个内存变量中进行，绝不卡顿
	 */
	TMap<FString, FAccountRecord> AccountData;

	/**
	 * 物理硬盘上 .sav 存档文件的名称
	 */
	const FString SaveSlotName = TEXT("LocalAccountDataSlot");

	/**
	 * 私有底层工具: 从硬盘上的 .sav 文件中读取数据，并填充到内存的 AccountData 中
	 */
	void LoadDataFromDisk();

	/**
	 * 私有底层工具: 将当前内存中的 AccountData 数据，物理写入到硬盘的 .sav 文件中
	 */
	void SaveDataToDisk();

	/**
	 * 记录当前这个游戏窗口登录的账号名
	 */
	FString CurrentLoggedInUser;
};
