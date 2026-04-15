#include "Systems/RoomGameMode.h"

#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "Systems/RoomPlayerController.h"
#include "Engine/World.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Characters/BaseCharacter.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Login/Data/StaticTable.h"
#include "Weapons/BaseWeapon.h"
#include "Kismet/GameplayStatics.h"
#include "Characters/BaseCharacter.h"
#include "Systems/RoomGameState.h"
#include "UI/MyGameHUD.h"
#include "UI/Login/Core/RoomPlayerState.h"

ARoomGameMode::ARoomGameMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	
	// 【强行关闭无缝漫游】：在基础局域网测试中，开启它纯属自找麻烦！
	bUseSeamlessTravel = false;
	
	// 【新增初始化】：默认大厅等待，但强行开启跳过测试开关！
	CurrentRoomState = ERoomState::WaitingInRoom;
	bSkipRoomPhaseForTesting = true;
	
	// 配置引擎的标准框架类
	GameStateClass = ARoomGameState::StaticClass();
	PlayerStateClass = ARoomPlayerState::StaticClass();
}

void ARoomGameMode::AddPlayerToRoom(AController* RequestingController, const FString& PlayerName)
{
	if (ARoomGameState* GS = GetGameState<ARoomGameState>())
	{
		// 谁第一个进房间（房主建房时），谁的名字就刻在 GameState 上！
		if (GS->HostPlayerName.IsEmpty())
		{
			GS->HostPlayerName = PlayerName;
		}
	}
	
	if (ARoomPlayerState* PS = RequestingController->GetPlayerState<ARoomPlayerState>())
	{
		// 引擎底层会自动同步名字
		PS->SetPlayerName(PlayerName);
		
		// 智能分配算法：向 GameState 查询目前哪边人少？
		if (ARoomGameState* GS = GetGameState<ARoomGameState>())
		{
			int32 RedCount = GS->GetPlayersInTeam(ERoomTeam::Red).Num();
			int32 BlueCount = GS->GetPlayersInTeam(ERoomTeam::Blue).Num();
			
			// 修改队伍，引擎会自动将这个改动广播给全服！
			PS->CurrentTeam = (RedCount <= BlueCount) ? ERoomTeam::Red : ERoomTeam::Blue;
		}
	}
	BroadcastSystemMessage(FString::Printf(TEXT("玩家【%s】加入了房间"), *PlayerName));
}

// ==========================================
// 切换队伍逻辑
// ==========================================
void ARoomGameMode::ChangePlayerTeam(AController* RequestingController, bool bToRedTeam)
{
	// 不再按名字去找，直接获取发请求的那个人的 PlayerState
	if (ARoomPlayerState* PS = RequestingController->GetPlayerState<ARoomPlayerState>())
	{
		// 服务器直接修改它的值
		PS->CurrentTeam = bToRedTeam ? ERoomTeam::Red : ERoomTeam::Blue;
		
		// 只要改了这行代码，UE 引擎底层会自动在下一个 Tick 将这个变量打包，
		// 顺着网线发给房间里所有的客户端，并触发他们本地的 OnRep_Team()！
		// 你再也不用手动写 Broadcast 广播了！
		
		// （可选）：如果你需要在服务端也立刻触发逻辑，可以手动调一次
		// PS->OnRep_Team(); 
	}
}

void ARoomGameMode::RemovePlayerFromRoom(AController* RequestingController)
{
	if (ARoomPlayerState* PS = RequestingController->GetPlayerState<ARoomPlayerState>())
	{
		BroadcastSystemMessage(FString::Printf(TEXT("玩家【%s】退出了房间"), *PS->GetPlayerName()));
	}
	// 架构优势：我们不需要再手动从任何 Array 里 Remove 名字了！
	// 当玩家断开连接时，引擎会自动把他的 PlayerState 从 GameState 中销毁并移除。
}

void ARoomGameMode::BroadcastChatMessage(const FString& SenderName, const FString& Message)
{
	// 1. 鉴定谁是房主？(服务器上排在第一个的本地玩家就是房主)
	FString HostName = TEXT("");
	if (ARoomPlayerController* HostPC = Cast<ARoomPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		HostName = HostPC->MyPlayerName;
	}

	// 对比一下，发消息的这个人是不是房主？
	bool bIsHost = (SenderName == HostName);

	// 2. 拿着大喇叭，给房间里所有的对讲机下达指令！
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
		{
			// 触发所有人的 Client RPC
			PC->Client_ReceiveChatMessage(SenderName, bIsHost, Message, false);
		}
	}
}

void ARoomGameMode::BroadcastSystemMessage(const FString& Message)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
		{
			// 系统消息：没有发送人，bIsHost为false，最末尾的 bIsSystemMsg 为 true！
			PC->Client_ReceiveChatMessage(TEXT(""), false, Message, true);
		}
	}
}

// ----------------------------------------------------
// 给 AI 发放唯一身份证并加入名单
// ----------------------------------------------------
void ARoomGameMode::AddAIToRoom(bool bToRedTeam, const FString& CharacterName, int32 Count)
{
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, TEXT("注意：AI 添加逻辑将在 PlayerState 彻底接管后重构！"));
	// 真正的工业级做法是：直接在这里 Spawn 一个带有 PlayerState 的 AIController。
}

// 切换准备逻辑
void ARoomGameMode::UpdatePlayerReadyState(AController* RequestingController, bool bIsReady)
{
	if (ARoomPlayerState* PS = RequestingController->GetPlayerState<ARoomPlayerState>())
	{
		// 服务器直接修改它的值，引擎会自动同步给全网！
		PS->bIsReady = bIsReady;
	}
}

// ----------------------------------------------------
// 【新增】：处理玩家请求生成（测试沙盒核心）
// ----------------------------------------------------
void ARoomGameMode::HandlePlayerRequestSpawn(APlayerController* PC, FString CharRowName, FString WeaponRowName)
{
	if (!PC) return;

	// 1. 状态机拦截：如果不是测试模式，且还在等大家选人，就不给发角色！
	if (!bSkipRoomPhaseForTesting && CurrentRoomState == ERoomState::WaitingInRoom)
	{
		return; // 乖乖在 UI 里待着
	}

	// 2. 如果跳过测试，或者已经是开战状态了，准备发枪！
	TSubclassOf<ABaseCharacter> ClassToSpawn = TestCharacterClass;
	TSubclassOf<ABaseWeapon> WeaponToSpawn = TestWeaponClass;

	// TODO（未来接UI时）：这里用 CharRowName 去查 DataTable 获取真实的 ClassToSpawn

	// 防崩容错
	if (!ClassToSpawn) 
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("RoomGameMode报错：没配置要生成的角色类！请在蓝图 TestCharacterClass 里配置！"));
		return;
	}

	// 3. 执行真正的生成
	SpawnAndEquip(PC, ClassToSpawn, WeaponToSpawn);
}

void ARoomGameMode::SpawnAndEquip(APlayerController* PC, TSubclassOf<ABaseCharacter> CharClass, TSubclassOf<ABaseWeapon> WeaponClass)
{
	// 在地图里找个出生点 (PlayerStart)
	AActor* SpawnPoint = UGameplayStatics::GetActorOfClass(GetWorld(), APlayerStart::StaticClass());
	FTransform SpawnTransform = SpawnPoint ? SpawnPoint->GetTransform() : FTransform(FVector(0, 0, 100));

	// 如果玩家当前控制着大厅的隐形摄像头，先销毁旧身体
	if (PC->GetPawn()) 
	{
		PC->GetPawn()->Destroy();
	}

	// 召唤全新的 3D 角色！
	ABaseCharacter* NewChar = GetWorld()->SpawnActor<ABaseCharacter>(CharClass, SpawnTransform);
	if (NewChar)
	{
		// 灵魂附体！
		PC->Possess(NewChar); 

		// 给他发刀！
		if (WeaponClass)
		{
			NewChar->EquipWeapon(WeaponClass);
		}
	}
}

ABaseCharacter* ARoomGameMode::RequestTargetForAI(ABaseCharacter* RequestingAI)
{
	if (!RequestingAI) return nullptr;

	// 1. 获取场上所有的存活敌人（假设你有个获取敌对玩家的函数）
	TArray<ABaseCharacter*> AllEnemies = GetAllAliveEnemiesFor(RequestingAI);
	if (AllEnemies.Num() == 0) return nullptr;
	
	// 2. 根据战绩/分数对敌人进行降序排序 (把第一名排在最前面)
	AllEnemies.Sort([](const ABaseCharacter& A, const ABaseCharacter& B) {
		// 假设你的 PlayerState 里有 GetScore() 或者 GetKills()
		return A.GetPlayerState()->GetScore() > B.GetPlayerState()->GetScore();
	});

	// ==========================================
	// 🌊 第一轮：优先找完全落单的 (包揽孤狼)
	// ==========================================
	for (ABaseCharacter* Enemy : AllEnemies)
	{
		if (GetAttackerCount(Enemy) == 0)
		{
			// 找到一个没人盯的！分配给他！
			AIHuntingMap.Add(RequestingAI, Enemy); // Add 会自动覆盖同一个 AI 之前的记录
			return Enemy;
		}
	}

	// ==========================================
	// 🔥 第二轮：如果没有落单的，执行“仇恨均摊”！
	// ==========================================
	int32 MinAttackers = 999999;
	ABaseCharacter* BestTarget = nullptr;

	for (ABaseCharacter* Enemy : AllEnemies)
	{
		int32 CurrentAttackers = GetAttackerCount(Enemy);
		
		// 【算法神来之笔】：注意这里是严格小于 (<)
		// 因为 AllEnemies 已经是按排名从高到低排好了。
		// 当出现平局时 (比如第一名有1个AI，第二名也有1个AI)，
		// 由于第一名先被遍历，MinAttackers 变成了 1，
		// 轮到第二名时，1 < 1 为假，所以依然会保留第一名！
		if (CurrentAttackers < MinAttackers)
		{
			MinAttackers = CurrentAttackers;
			BestTarget = Enemy;
		}
	}

	if (BestTarget)
	{
		// 均摊分配成功！
		AIHuntingMap.Add(RequestingAI, BestTarget);
		return BestTarget;
	}

	return nullptr;
}

void ARoomGameMode::ReleaseTarget(ABaseCharacter* RequestingAI)
{
	// 当 AI 死了，或者想放弃目标时，直接从账本里把这个 AI 抹除
	if (IsValid(RequestingAI) && AIHuntingMap.Contains(RequestingAI))
	{
		AIHuntingMap.Remove(RequestingAI);
	}
}

TArray<ABaseCharacter*> ARoomGameMode::GetAllAliveEnemiesFor(ABaseCharacter* RequestingAI)
{
	TArray<ABaseCharacter*> AliveEnemies;
	if (!RequestingAI) return AliveEnemies;

	TArray<AActor*> AllCharacters;
	// 瞬间获取当前地图里所有的 ABaseCharacter
	UGameplayStatics::GetAllActorsOfClass(this, ABaseCharacter::StaticClass(), AllCharacters);

	for (AActor* Actor : AllCharacters)
	{
		ABaseCharacter* Char = Cast<ABaseCharacter>(Actor);
		
		// 1. 不能是空指针
		// 2. 不能是正在请求的 AI 自己 (自己不能追杀自己)
		// 3. 目标必须是活着的
		if (Char && Char != RequestingAI && !Char->GetIsDead())
		{
			// TODO: 等你以后做了队伍系统，这里还要加一句 && Char->TeamID != RequestingAI->TeamID
			// 团队竞技目前暂时把除了自己以外的所有活人都当成敌人
			AliveEnemies.Add(Char);
		}
	}

	return AliveEnemies;
}

// 统计函数实现
int32 ARoomGameMode::GetAttackerCount(ABaseCharacter* TargetEnemy)
{
	int32 Count = 0;
	// 遍历整个猎人账本
	for (const auto& Pair : AIHuntingMap)
	{
		// 如果猎人还没死，并且他的目标正是我们要查的人
		if (IsValid(Pair.Key) && !Pair.Key->IsDead() && Pair.Value == TargetEnemy)
		{
			Count++;
		}
	}
	return Count;
}


// 检查是否所有人准备就绪
bool ARoomGameMode::CheckAllPlayersReady()
{
	// 直接问全局的 GameState
	if (ARoomGameState* GS = GetGameState<ARoomGameState>())
	{
		for (APlayerState* GenericPS : GS->PlayerArray)
		{
			if (ARoomPlayerState* PS = Cast<ARoomPlayerState>(GenericPS))
			{
				// 假设未分配队伍的人不算在内，且房主(Host)默认随时Ready
				// (这里可以根据你的具体业务逻辑微调)
				if (PS->CurrentTeam != ERoomTeam::None && !PS->bIsReady)
				{
					return false; // 有人没准备！
				}
			}
		}
	}
	return true;
}