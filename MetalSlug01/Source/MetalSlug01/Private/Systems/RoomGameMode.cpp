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
#include "UI/MyGameHUD.h"

ARoomGameMode::ARoomGameMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// // 告诉引擎使用你的 HUD 类
	// HUDClass = AMyGameHUD::StaticClass();
	
	// 【强行关闭无缝漫游】：在基础局域网测试中，开启它纯属自找麻烦！
	bUseSeamlessTravel = false;
	
	// 【新增初始化】：默认大厅等待，但强行开启跳过测试开关！
	CurrentRoomState = ERoomState::WaitingInRoom;
	bSkipRoomPhaseForTesting = true;
}

void ARoomGameMode::AddPlayerToRoom(const FString& PlayerName)
{
	// 【智能分配算法】：红队人少就去红队，否则去蓝队
	if (RedTeamNames.Num() <= BlueTeamNames.Num())
	{
		RedTeamNames.Add(PlayerName);
	}
	else
	{
		BlueTeamNames.Add(PlayerName);
	}
	// 【新增】：向全服播报绿字提示！
	BroadcastSystemMessage(FString::Printf(TEXT("玩家【%s】加入了房间"), *PlayerName));
	
	// 名单更新了，立刻通知全房间的人刷新 UI！
	BroadcastRoomUpdate();
}

void ARoomGameMode::BroadcastRoomUpdate()
{
	// 1. 【核心修复】：必须在 for 循环外面先声明并获取 HostName！
	FString HostName = TEXT("");
	if (ARoomPlayerController* HostPC = Cast<ARoomPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		HostName = HostPC->MyPlayerName;
	}
	
	// ==========================================
	// 【新增核心逻辑】：把最新的总人数更新到大厅广告牌上！
	// ==========================================
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid())
		{
			// 拿到当前正在运行的房间 (NAME_GameSession)
			FNamedOnlineSession* Session = Sessions->GetNamedSession(NAME_GameSession);
			if (Session)
			{
				// 计算当前房间里的绝对总人数（红队 + 蓝队，包含了真人和 AI）
				int32 CurrentTotalPlayers = RedTeamNames.Num() + BlueTeamNames.Num();
				
				// 覆写那个名为 TOTAL_PLAYERS_WITH_AI 的标签！
				Session->SessionSettings.Set(FName("TOTAL_PLAYERS_WITH_AI"), CurrentTotalPlayers, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
				
				// 提交更新！这一步会让局域网里的其他玩家立刻搜到新的人数！
				Sessions->UpdateSession(NAME_GameSession, Session->SessionSettings, true);
			}
		}
	}

	// 2. 遍历当前世界（房间）里的所有玩家控制器
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get());
		if (PC)
		{
			// 呼叫对讲机的 Client RPC，把服务器的名单和房主名字硬塞给他们
			PC->Client_UpdateRoomUI(RedTeamNames, BlueTeamNames, HostName);
            
			// 【顺带补上】：名单刷新后，紧接着把字典里保存的所有人的准备状态重新刷一遍！
			for (const auto& Pair : PlayerReadyStates)
			{
				PC->Client_UpdatePlayerReadyState(Pair.Key, Pair.Value);
			}
		}
	}
}

void ARoomGameMode::ChangePlayerTeam(const FString& PlayerName, bool bToRedTeam)
{
	// 1. 简单粗暴：先把这个玩家从两个队伍里都踢出去（防止分身）
	RedTeamNames.Remove(PlayerName);
	BlueTeamNames.Remove(PlayerName);

	// 2. 根据他的请求，把他加进对应的队伍
	if (bToRedTeam)
	{
		RedTeamNames.AddUnique(PlayerName); // AddUnique 防止重复添加
	}
	else
	{
		BlueTeamNames.AddUnique(PlayerName);
	}

	// 3. 名单发生变化，立刻广播给全房间的所有人！
	BroadcastRoomUpdate();
}

void ARoomGameMode::RemovePlayerFromRoom(const FString& PlayerName)
{
	// 1. 无脑从两个队伍里把这个名字删掉
	RedTeamNames.Remove(PlayerName);
	BlueTeamNames.Remove(PlayerName);
	
	// 【新增】：向全服播报绿字提示！
	BroadcastSystemMessage(FString::Printf(TEXT("玩家【%s】退出了房间"), *PlayerName));

	// 2. 广播给房间里剩下的人，让他们刷新 UI（这步极其关键，否则别人屏幕上还有你）
	BroadcastRoomUpdate();
	
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
// 【新增】：给 AI 发放唯一身份证并加入名单
// ----------------------------------------------------
void ARoomGameMode::AddAIToRoom(bool bToRedTeam, const FString& CharacterName, int32 Count)
{
	for (int32 i = 0; i < Count; ++i)
	{
		// 核心防同名魔法：给 AI 名字加上自增编号！
		FString UniqueAIName = FString::Printf(TEXT("[AI] %s_%d"), *CharacterName, AINextID);
		
		// 编号自增，确保下一个绝对不重名
		AINextID++;

		// 塞进对应的队伍数组里
		if (bToRedTeam)
		{
			RedTeamNames.AddUnique(UniqueAIName);
		}
		else
		{
			BlueTeamNames.AddUnique(UniqueAIName);
		}
	}

	// 【体验优化】：向全服播报绿字提示，告诉大家房主加了几个 AI！
	FString TeamStr = bToRedTeam ? TEXT("红队") : TEXT("蓝队");
	BroadcastSystemMessage(FString::Printf(TEXT("房主向【%s】部署了 %d 名 AI 士兵 [%s]"), *TeamStr, Count, *CharacterName));

	// 【核心修复】：名字必须和你上面写好的广播函数一模一样！
	BroadcastRoomUpdate();
}

// ----------------------------------------------------
// 【终极广播核心】：强行让所有客户端的 UI 和服务器的数组保持一致！
// ----------------------------------------------------
void ARoomGameMode::BroadcastRoomUIUpdate()
{
	// ==========================================
	// 1. 必须在最前面声明并获取 HostName！
	// ==========================================
	FString HostName = TEXT("");
	if (ARoomPlayerController* HostPC = Cast<ARoomPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		HostName = HostPC->MyPlayerName;
	}

	// ==========================================
	// 2. 然后才能在下面的循环里使用 HostName！
	// ==========================================
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get());
		if (PC)
		{
			// 此时编译器已经认识 HostName 了，绝对不会再报 C2065！
			PC->Client_UpdateRoomUI(RedTeamNames, BlueTeamNames, HostName);
			
			// 紧接着把字典里保存的所有人的准备状态重新刷一遍！
			for (const auto& Pair : PlayerReadyStates)
			{
				PC->Client_UpdatePlayerReadyState(Pair.Key, Pair.Value);
			}
		}
	}
}

void ARoomGameMode::UpdatePlayerReadyState(const FString& PlayerName, bool bIsReady)
{
	// 写入服务器的记忆字典里
	PlayerReadyStates.Add(PlayerName, bIsReady);

	// 全频道广播！通知所有人的 UI 更新这个人！
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
		{
			PC->Client_UpdatePlayerReadyState(PlayerName, bIsReady);
		}
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