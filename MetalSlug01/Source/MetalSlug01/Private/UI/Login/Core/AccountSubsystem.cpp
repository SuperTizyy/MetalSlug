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
	// 【关键！】因为你是双开测试，另一个窗口可能刚刚改了存档。
	// 所以每次尝试登录前，必须先从硬盘重新读取一次最新数据！
	LoadDataFromDisk();

	if (AccountData.Contains(Username))
	{
		if (AccountData[Username].Password == Password)
		{
			// 如果密码对上了，检查是不是已经在线了
			if (AccountData[Username].bIsOnline)
			{
				return false; // 拦截！账号已在其他地方登录
			}

			// ==========================================
			// 登录成功，开始“上锁”
			// ==========================================
			AccountData[Username].bIsOnline = true; // 修改内存状态
			CurrentLoggedInUser = Username;         // 记住当前窗口的登录人
			SaveDataToDisk();                       // 立刻物理写死到硬盘，通知其他窗口！
			return true; 
		}
	}
	
	// 如果账号不存在，或者密码不匹配，统统返回 false 拒绝登录
	return false; 
}

// 检查是否在线的专门接口
bool UAccountSubsystem::IsAccountOnline(const FString& Username)
{
	// 查岗前先读硬盘，获取另一个窗口写进去的最新状态
	LoadDataFromDisk();
	if (AccountData.Contains(Username))
	{
		return AccountData[Username].bIsOnline;
	}
	return false;
}

// 登出并解锁
void UAccountSubsystem::Logout()
{
	// 1. 如果当前压根没人登录，直接当无事发生，安全返回
	if (CurrentLoggedInUser.IsEmpty())
	{
		return;
	}

	// 2. 【核心修复】：必须先从硬盘拿最新数据覆盖内存！然后再去字典里找！
	LoadDataFromDisk(); 

	// 3. 拿着最新的字典查岗，如果有这个人，才去改状态
	if (AccountData.Contains(CurrentLoggedInUser))
	{
		AccountData[CurrentLoggedInUser].bIsOnline = false; // 解除在线状态锁
		SaveDataToDisk(); // 写入硬盘
	}

	// 4. 无论如何，本地窗口的登录记录必须被清空
	CurrentLoggedInUser.Empty();
}

// 游戏关闭时的安全兜底
void UAccountSubsystem::Deinitialize()
{
	// 玩家直接强退游戏时，自动触发登出解锁，防止账号永久卡死
	Logout();
	Super::Deinitialize();
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

// 获取上次选中的角色名
FString UAccountSubsystem::GetLastSelectedCharacter()
{
	// 确保当前有玩家登录，并且大账本里有他
	if (!CurrentLoggedInUser.IsEmpty() && AccountData.Contains(CurrentLoggedInUser))
	{
		return AccountData[CurrentLoggedInUser].LastSelectedCharacter;
	}
	return TEXT(""); // 查不到就返回空字符串
}

// 保存选中的角色名
void UAccountSubsystem::SaveLastSelectedCharacter(const FString& CharacterName)
{
	if (!CurrentLoggedInUser.IsEmpty() && AccountData.Contains(CurrentLoggedInUser))
	{
		// 1. 修改内存里的记录
		AccountData[CurrentLoggedInUser].LastSelectedCharacter = CharacterName;
		
		// 2. 立刻呼叫快递员，把最新状态物理写入本地硬盘！(防止游戏闪退导致没存上)
		SaveDataToDisk(); 
	}
}

FString UAccountSubsystem::GetLastSelectedWeapon(int32 BackpackSlot)
{
	if (!CurrentLoggedInUser.IsEmpty() && AccountData.Contains(CurrentLoggedInUser))
	{
		return BackpackSlot == 1 ? AccountData[CurrentLoggedInUser].LastSelectedWeapon1 : AccountData[CurrentLoggedInUser].LastSelectedWeapon2;
	}
	return TEXT(""); 
}

void UAccountSubsystem::SaveLastSelectedWeapon(int32 BackpackSlot, const FString& WeaponRowName)
{
	if (!CurrentLoggedInUser.IsEmpty() && AccountData.Contains(CurrentLoggedInUser))
	{
		if (BackpackSlot == 1) AccountData[CurrentLoggedInUser].LastSelectedWeapon1 = WeaponRowName;
		else if (BackpackSlot == 2) AccountData[CurrentLoggedInUser].LastSelectedWeapon2 = WeaponRowName;
		
		SaveDataToDisk(); // 立刻存盘
	}
}