// 包含当前子系统的头文件
#include "UI/Login/Core/AccountSubsystem.h"
// 包含虚幻引擎提供的静态工具函数类（用于执行 LoadGame 和 SaveGame）
#include "Kismet/GameplayStatics.h"
// 【关键引入】必须包含我们用来“装箱”的 SaveGame 类头文件
#include "UI/Login/Core/AccountSaveGame.h"

// 当游戏实例启动时，引擎会自动调用这个初始化函数
void UAccountSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// 先调用父类的初始化逻辑
	Super::Initialize(Collection);

	// 游戏一启动，立刻调用读取函数，把硬盘里的旧账号全部“搬进”内存里
	LoadDataFromDisk();
}

// 供 UI 调用的登录验证函数
bool UAccountSubsystem::TryLogin(const FString& Username, const FString& Password)
{
	// 在内存哈希表（AccountData）中，极速查找是否包含玩家输入的账号
	if (AccountData.Contains(Username))
	{
		// 如果账号存在，把对应的结构体（档案袋）拿出来，比对里面的 Password 字段
		if (AccountData[Username].Password == Password)
		{
			// 密码匹配，允许登录
			return true; 
		}
	}
	
	// 如果账号不存在，或者密码不匹配，统统返回 false 拒绝登录
	return false; 
}

// 供 UI 调用的注册新号函数
bool UAccountSubsystem::TryRegister(const FString& Username, const FString& Password)
{
	// 先在内存里查一下，防止玩家注册一个已经被别人注册过的名字
	if (AccountData.Contains(Username))
	{
		// 账号已被占用，拒绝注册
		return false; 
	}

	// 【核心打包过程】使用你在 DynamicTable.h 里写的有参构造函数，把账号密码装进档案袋
	FAccountRecord NewRecord(Username, Password);

	// 把这个新的档案袋，以账号为 Key，存进内存字典里
	AccountData.Add(Username, NewRecord);

	// 既然有了新账号，必须立刻调用保存函数，把最新的内存状态固化到硬盘里，防止玩家突然断电
	SaveDataToDisk();

	// 注册成功并保存完毕
	return true; 
}

// 底层工具：读取硬盘
void UAccountSubsystem::LoadDataFromDisk()
{
	// 向操作系统询问，硬盘的指定路径下有没有我们的存档文件
	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
	{
		// 如果有文件，就把它读出来，并强制转换为我们的顺丰纸箱类型（UAccountSaveGame）
		UAccountSaveGame* LoadedGame = Cast<UAccountSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
		
		// 确保纸箱没破（读取成功）
		if (LoadedGame)
		{
			// 把纸箱里面装的历史字典，直接赋值覆盖给我们内存里的大账本
			AccountData = LoadedGame->AccountRecords;
		}
	}
}

// 底层工具：写入硬盘
void UAccountSubsystem::SaveDataToDisk()
{
	// 先声明一个纸箱指针
	UAccountSaveGame* SaveGameInstance = nullptr;

	// 先尝试找一下以前有没有用过的旧纸箱（避免覆盖掉里面可能存在的其他数据）
	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
	{
		// 找到旧纸箱，拿过来用
		SaveGameInstance = Cast<UAccountSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	}
	else
	{
		// 如果是玩家第一次玩游戏，硬盘上连文件都没有，我们就用引擎函数凭空制造一个新纸箱
		SaveGameInstance = Cast<UAccountSaveGame>(UGameplayStatics::CreateSaveGameObject(UAccountSaveGame::StaticClass()));
	}

	// 确保我们现在手里确实有一个可以用的纸箱
	if (SaveGameInstance)
	{
		// 把内存里最新的大账本（AccountData），放进纸箱的 AccountRecords 变量里
		SaveGameInstance->AccountRecords = this->AccountData;
		
		// 呼叫引擎底层快递员，把这个纸箱物理写入到电脑硬盘里！
		UGameplayStatics::SaveGameToSlot(SaveGameInstance, SaveSlotName, 0);
	}
}