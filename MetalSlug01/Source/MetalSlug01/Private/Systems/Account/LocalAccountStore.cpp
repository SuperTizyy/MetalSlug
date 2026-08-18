// ==========================================
// 头文件包含区
// ==========================================
// 引入 LocalAccountStore 类声明
#include "Systems/Account/LocalAccountStore.h"
// UE 提供的 SaveGame 静态工具
#include "Kismet/GameplayStatics.h"
// 配置文件读写
#include "Misc/ConfigCacheIni.h"
// GUID 生成
#include "Misc/Guid.h"
// 路径工具
#include "Misc/Paths.h"
// 文件 IO
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"


// ==========================================
// 1. 子系统生命周期
// ==========================================

void ULocalAccountStore::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 1) 计算本进程专属槽位（读/写本地 identity 配置）
	EnsureLocalClientIdentity();

	// 2) 初始化内存缓存（不存在则凭空制造一个空容器）
	Cache = nullptr;
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		Cache = Cast<UAccountSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	}
	if (!Cache)
	{
		Cache = Cast<UAccountSaveGame>(UGameplayStatics::CreateSaveGameObject(UAccountSaveGame::StaticClass()));
	}

	UE_LOG(LogTemp, Log, TEXT("[AccountStore] Initialize slot=%s records=%d"),
		*SlotName, Cache ? Cache->AccountRecords.Num() : 0);
}

/**
 * @brief 子系统销毁时的兜底落盘 — 确保内存修改持久化
 *
 * 如果 Cache 存在未写盘的改动, 强制 FlushToDisk 一次
 * 防止 PIE 突然关闭 / 客户端异常退出导致偏好丢失
 */
void ULocalAccountStore::Deinitialize()
{
	// 子系统销毁时, 如果内存里有未落盘改动, 兜底写一次
	if (Cache)
	{
		FlushToDisk();
	}
	Super::Deinitialize();
}


// ==========================================
// 2. IO 操作
// ==========================================

bool ULocalAccountStore::FlushToDisk()
{
	if (!Cache)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AccountStore] FlushToDisk 失败: Cache 为空"));
		return false;
	}

	const bool bOk = UGameplayStatics::SaveGameToSlot(Cache, SlotName, 0);
	if (!bOk)
	{
		UE_LOG(LogTemp, Error, TEXT("[AccountStore] FlushToDisk 失败 slot=%s"), *SlotName);
	}
	return bOk;
}

bool ULocalAccountStore::ReloadFromDisk()
{
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		// 没有历史存档不算失败, 给一个空容器即可
		Cache = Cast<UAccountSaveGame>(UGameplayStatics::CreateSaveGameObject(UAccountSaveGame::StaticClass()));
		return true;
	}

	UAccountSaveGame* Loaded = Cast<UAccountSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!Loaded)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AccountStore] ReloadFromDisk 解析失败, 保留旧 Cache slot=%s"), *SlotName);
		return false;
	}

	Cache = Loaded;
	return true;
}


// ==========================================
// 3. 本进程身份 (决定槽位名)
// ==========================================

void ULocalAccountStore::EnsureLocalClientIdentity()
{
	// 配置文件路径: <Project>/Saved/Config/LocalClient.identity
	// 注意: 这个文件是单机本地元数据, 不进版本控制, 也不上传云端
	const FString IdentityFilePath = FPaths::ProjectSavedDir() / TEXT("Config/LocalClient.identity");

	FString PersistedGuid;
	if (FPaths::FileExists(IdentityFilePath))
	{
		FFileHelper::LoadFileToString(PersistedGuid, *IdentityFilePath);
		PersistedGuid = PersistedGuid.TrimStartAndEnd();
	}

	// 文件不存在或解析失败 → 重新生成
	// FGuid::Parse 返回 bool 表示字符串是否符合 GUID 格式
	FGuid ParsedGuid;
	if (!FGuid::Parse(PersistedGuid, ParsedGuid))
	{
		PersistedGuid = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		// 写回磁盘, 保证下次启动稳定
		IFileManager& FileMgr = IFileManager::Get();
		FileMgr.MakeDirectory(*FPaths::GetPath(IdentityFilePath), /*Tree*/ true);
		FFileHelper::SaveStringToFile(PersistedGuid, *IdentityFilePath);
	}

	// 用 GUID 的后 8 位作为槽位后缀, 避免槽位名过长且保持唯一
	const FString ShortGuid = PersistedGuid.Right(8).ToLower();
	SlotName = FString::Printf(TEXT("LocalAccountDataSlot_%s"), *ShortGuid);
}