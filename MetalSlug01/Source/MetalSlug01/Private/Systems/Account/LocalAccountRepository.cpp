// ========================================================================
// LocalAccountRepository.cpp — L1 仓储层 - 账号 CRUD 实现文件
// ========================================================================
//
// 文件功能总览:
//   - 实现 Initialize/Deinitialize(声明 Store 依赖)
//   - 实现账号 CRUD: VerifyCredentials / Register / FindRecord
//   - 实现战备偏好: Get/SaveLastSelectedCharacter / Weapon (含自愈式建档)
//   - 实现私有 helper: GetStore (懒加载防御) / Persist (兜底保存)
//
// 大厂原则:
//   - 自愈式 Save:Record 不存在 → 自动创建(用于 MockLogin 临时账号)
//   - 单次持久化:Persist() 内部调 Store->FlushToDisk(),Repository 不在内存里冗余存 AccountData
//   - 业务校验:用户名非空 + 密码非空 + 账号已存在 → 拒绝
// ========================================================================

// ==========================================
// 头文件包含区
// ==========================================
#include "Systems/Account/LocalAccountRepository.h"
#include "Systems/Account/LocalAccountStore.h"
#include "Engine/GameInstance.h"
#include "Data/Account/AccountSaveGame.h"


// ==========================================
// 1. 生命周期
// ==========================================

void ULocalAccountRepository::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 强制保证 Store 在 Repository 之前初始化 (子系统依赖)
	Collection.InitializeDependency(ULocalAccountStore::StaticClass());

	UE_LOG(LogTemp, Log, TEXT("[AccountRepository] Initialize 完成"));
}

void ULocalAccountRepository::Deinitialize()
{
	Super::Deinitialize();
}


// ==========================================
// 2. 静态访问器
// ==========================================

ULocalAccountRepository* ULocalAccountRepository::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = WorldContextObject->GetWorld();
	if (!World) return nullptr;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return nullptr;
	return GI->GetSubsystem<ULocalAccountRepository>();
}


// ==========================================
// 3. 账号 CRUD
// ==========================================

bool ULocalAccountRepository::VerifyCredentials(const FString& Username, const FString& Password) const
{
	if (Username.IsEmpty() || Password.IsEmpty()) return false;

	ULocalAccountStore* Store = GetStore();
	if (!Store || !Store->GetCache()) return false;

	const TMap<FString, FAccountRecord>& Records = Store->GetCache()->AccountRecords;
	const FAccountRecord* Record = Records.Find(Username);
	if (!Record) return false;
	return Record->Password == Password;
}

EAccountRegisterResult ULocalAccountRepository::Register(const FString& Username, const FString& Password)
{
	if (Username.IsEmpty() || Password.IsEmpty())
	{
		return EAccountRegisterResult::InvalidInput;
	}

	ULocalAccountStore* Store = GetStore();
	if (!Store || !Store->GetCache())
	{
		return EAccountRegisterResult::InternalError;
	}

	TMap<FString, FAccountRecord>& Records = Store->GetCache()->AccountRecords;
	if (Records.Contains(Username))
	{
		return EAccountRegisterResult::AlreadyExists;
	}

	Records.Add(Username, FAccountRecord(Username, Password));
	Persist();
	return EAccountRegisterResult::Success;
}

const FAccountRecord* ULocalAccountRepository::FindRecord(const FString& Username) const
{
	ULocalAccountStore* Store = GetStore();
	if (!Store || !Store->GetCache()) return nullptr;
	return Store->GetCache()->AccountRecords.Find(Username);
}


// ==========================================
// 4. 战备偏好
// ==========================================

FString ULocalAccountRepository::GetLastSelectedCharacter(const FString& Username) const
{
	if (const FAccountRecord* Record = FindRecord(Username))
	{
		return Record->LastSelectedCharacter;
	}
	return TEXT("");
}

/**
 * @brief 保存玩家最后选择的角色名(自愈式建档)
 * @param Username 账号名(非空)
 * @param CharacterName 角色 ID(对应 DT_CharacterInfo)
 *
 * 如果 Record 不存在会自动创建临时空 Record, 用于 MockLogin 等临时账号场景
 * 自愈策略确保 MockName (如 TestUser_XXXX) 也能正常保存偏好
 */
void ULocalAccountRepository::SaveLastSelectedCharacter(const FString& Username, const FString& CharacterName)
{
	ULocalAccountStore* Store = GetStore();
	if (!Store || !Store->GetCache() || Username.IsEmpty()) return;

	TMap<FString, FAccountRecord>& Records = Store->GetCache()->AccountRecords;

	// 【自愈式】Record 不存在则自动创建 (用于 MockLogin 等临时账号场景)
	// 设计动机:
	//   - 业务调用方不再需要关心账号是否预先注册
	//   - "TestUser_XXXX" 这种内存临时账号也能正常保存偏好
	//   - 真实注册场景仍走 Register() 路径 (带密码)
	FAccountRecord* Record = Records.Find(Username);
	if (!Record)
	{
		Record = &Records.Add(Username, FAccountRecord(Username, TEXT("")));
		UE_LOG(LogTemp, Log,
			TEXT("[AccountRepository] SaveLastSelectedCharacter: 自动创建临时 Record (User='%s')"),
			*Username);
	}

	Record->LastSelectedCharacter = CharacterName;
	Persist();
}

/**
 * @brief 读取玩家指定背包槽位最后选择的武器 ID
 * @param Username 账号名
 * @param BackpackSlot 背包槽位(1 或 2)
 * @return 武器 RowName,Record 不存在或槽位非法返回空字符串
 */
FString ULocalAccountRepository::GetLastSelectedWeapon(const FString& Username, int32 BackpackSlot) const
{
	if (const FAccountRecord* Record = FindRecord(Username))
	{
		return BackpackSlot == 1 ? Record->LastSelectedWeapon1 : Record->LastSelectedWeapon2;
	}
	return TEXT("");
}

/**
 * @brief 保存玩家指定背包槽位的武器 ID(自愈式建档)
 * @param Username 账号名(非空)
 * @param BackpackSlot 背包槽位(1=主武器/2=副武器, 其他值被忽略)
 * @param WeaponRowName DT_WeaponInfo 表中的 RowName
 *
 * 自愈式建档:Record 不存在则自动创建临时 Record, 保证 MockLogin 等场景能正常持久化偏好
 * 修复关键路径: RoomInsidePage::NativeConstruct → InitDefaultWeapons 依赖此写入
 */
void ULocalAccountRepository::SaveLastSelectedWeapon(const FString& Username, int32 BackpackSlot, const FString& WeaponRowName)
{
	ULocalAccountStore* Store = GetStore();
	if (!Store || !Store->GetCache() || Username.IsEmpty()) return;

	TMap<FString, FAccountRecord>& Records = Store->GetCache()->AccountRecords;

	// 【自愈式】Record 不存在则自动创建 (用于 MockLogin 等临时账号场景)
	// 设计动机:
	//   - 与 SaveLastSelectedCharacter 保持一致的自愈策略
	//   - 让 "TestUser_XXXX" 等内存账号也能正常持久化武器偏好
	//   - 关键修复: RoomInsidePage::NativeConstruct 的 InitDefaultWeapons 路径
	//     依赖 SaveLastSelectedWeapon 写入默认值, 如果 Record 不存在就静默 return,
	//     会导致 PC->Server_SelectLoadout 传空武器, 最终 GM 兜底失败 (Knife 不在 DT 里)
	FAccountRecord* Record = Records.Find(Username);
	if (!Record)
	{
		Record = &Records.Add(Username, FAccountRecord(Username, TEXT("")));
		UE_LOG(LogTemp, Log,
			TEXT("[AccountRepository] SaveLastSelectedWeapon: 自动创建临时 Record (User='%s', Slot=%d, Weapon='%s')"),
			*Username, BackpackSlot, *WeaponRowName);
	}

	if (BackpackSlot == 1) Record->LastSelectedWeapon1 = WeaponRowName;
	else if (BackpackSlot == 2) Record->LastSelectedWeapon2 = WeaponRowName;

	Persist();
}


// ==========================================
// 5. 私有工具
// ==========================================

ULocalAccountStore* ULocalAccountRepository::GetStore() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<ULocalAccountStore>();
	}
	return nullptr;
}

/**
 * @brief 兜底持久化入口 — 将内存中的 Cache 写回磁盘
 *
 * Store 不可用时静默跳过(子系统销毁时序阶段不强制落盘)
 * 业务层 Repository::Register/SaveLastSelectedXxx 末尾统一调用此方法
 */
void ULocalAccountRepository::Persist()
{
	if (ULocalAccountStore* Store = GetStore())
	{
		Store->FlushToDisk();
	}
}