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
#include "Systems/GameFlowSubsystem.h"
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
	
	//必须从底层硬编码绑定默认的 HUD 类，确保 MyGameHUD 会伴随玩家生出。
	HUDClass = AMyGameHUD::StaticClass();
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
	ARoomGameState* GS = GetGameState<ARoomGameState>();
	if (!GS) return false;

	// 【防呆设计】：如果房间里没有任何人（理论上不可能），直接拦截
	if (GS->PlayerArray.Num() == 0) return false;

	// 【底层溯源】：在 Listen Server 架构下，服务器的 FirstPlayerController 必定是本机的房主！
	APlayerController* HostPC = Cast<APlayerController>(GetWorld()->GetFirstPlayerController());

	for (APlayerState* GenericPS : GS->PlayerArray)
	{
		if (ARoomPlayerState* PS = Cast<ARoomPlayerState>(GenericPS))
		{
			// 【架构精进】：通过比对 Controller 引用，精准鉴别当前遍历的 PlayerState 是不是房主的
			bool bIsHost = (PS->GetPlayerController() == HostPC);

			// 🌟 核心修复 1 & 2：房主拥有特权，豁免准备状态校验！
			// 因为房主自己主宰游戏什么时候开始，不需要点击准备。
			if (bIsHost)
			{
				continue; // 直接跳过房主的校验，检查下一个人
			}

			// 对于非房主的普通玩家，严格校验其准备状态
			if (PS->CurrentTeam != ERoomTeam::None && !PS->bIsReady)
			{
				// 工业规范：输出日志，方便后期在后台查出是哪个玩家卡住了进程
				UE_LOG(LogTemp, Warning, TEXT("[RoomGameMode] 拦截开局：普通玩家 【%s】 尚未准备！"), *PS->GetPlayerName());
				return false; // 只要有一个人没准备，立刻阻断开局！
			}
		}
	}

	// 走到这里意味着：
	// 情景 A：房间里只有房主 1 个人，循环直接 continue 结束 -> 返回 true 允许开局（解决 Bug 1）
	// 情景 B：房间里有多人，且除房主外的所有人都 bIsReady == true -> 返回 true 允许开局（解决 Bug 2）
	
	UE_LOG(LogTemp, Log, TEXT("[RoomGameMode] 准备状态全部校验通过，准许开局！"));
	return true;
}

// ==========================================
// 1. 接收请求，记录数据，然后命令引擎开始原生生成流程
// ==========================================
void ARoomGameMode::HandlePlayerRequestSpawn(AController* PlayerToSpawn, const FString& CharRowName, const FString& WeaponRowName)
{
	if (!PlayerToSpawn) return;

	// 将玩家的选择持久化存储在 PlayerState 中，防止死亡后丢失
	if (ARoomPlayerState* PS = PlayerToSpawn->GetPlayerState<ARoomPlayerState>())
	{
		PS->SelectedCharacterRowName = CharRowName;
		PS->SelectedWeaponRowName = WeaponRowName;
	}

	// 【大厂规范】：交出控制权！调用 UE 原生生成流程
	// 这行代码会自动触发底层的 FindPlayerStart -> SpawnDefaultPawnFor -> Possess
	RestartPlayer(PlayerToSpawn);
}

// ==========================================
// 2. 引擎底层在生成 Actor 前，会来问我们：“应该生成什么类？”
// ==========================================
UClass* ARoomGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	ARoomPlayerState* PS = InController->GetPlayerState<ARoomPlayerState>();
	if (PS && CharacterDataTable) 
	{
		// 1. 获取玩家选择的角色ID
		FString CharID = PS->GetSelectedCharacterID();
		
		// 2. 查表获取配置行
		static const FString ContextString(TEXT("CharacterSpawnContext"));
		// 【注意】这里必须使用 FCharacterInfo，因为你表里的结构体叫这个名字
		FCharacterInfo* Info = CharacterDataTable->FindRow<FCharacterInfo>(FName(*CharID), ContextString);
		
		// 如果是软引用，需要在生成前执行一下同步加载（LoadSynchronous）将路径转换为真实的类
		if (Info && !Info->CharacterBlueprint.IsNull())
		{
			// 将软指针加载为硬指针类返回给引擎去 Spawn
			return Info->CharacterBlueprint.LoadSynchronous(); 
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoomGameMode] 未找到 ID 为 %s 的角色，或 CharacterBlueprint 为空，使用默认 Pawn"), *CharID);
		}
	}

	// 如果查表失败或没拿到数据，执行父类默认的生成逻辑，防止服务器崩溃
	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

//3. 生成完毕后，查表并派发武器
void ARoomGameMode::RestartPlayer(AController* NewPlayer)
{
	// 先让底层完成生成和附身
	Super::RestartPlayer(NewPlayer);

	// 此时角色已经安全降生在地图上
	if (NewPlayer && NewPlayer->GetPawn())
	{
		if (ARoomPlayerState* PS = NewPlayer->GetPlayerState<ARoomPlayerState>())
		{
			if (!PS->SelectedWeaponRowName.IsEmpty() && WeaponDataTable)
			{
				// 【精准定位】：使用您项目真实的 FWeaponInfo 进行查表
				FName RowName = FName(*PS->SelectedWeaponRowName);
				FString ContextString = TEXT("Weapon Spawn Context");
				
				FWeaponInfo* WeaponInfo = WeaponDataTable->FindRow<FWeaponInfo>(RowName, ContextString);
				if (WeaponInfo && WeaponInfo->WeaponBlueprint)
				{
					// 【实战武器生成】：获取当前附身的角色
					ACharacter* MyChar = Cast<ACharacter>(NewPlayer->GetPawn());
					if (MyChar)
					{
						// 在服务器生成真实的 3D 武器实例
						FActorSpawnParameters SpawnParams;
						SpawnParams.Owner = MyChar;
						SpawnParams.Instigator = MyChar;
						
						ABaseWeapon* SpawnedWeapon = GetWorld()->SpawnActor<ABaseWeapon>(WeaponInfo->WeaponBlueprint, MyChar->GetActorLocation(), MyChar->GetActorRotation(), SpawnParams);
						
						if (SpawnedWeapon)
						{
							/* * 工业级提醒：这里调用您角色身上实际的装备武器函数。
							 * 假设您的 Character 里有类似 EquipWeapon 的接口，可以这样调用：
							 * MyChar->EquipWeapon(SpawnedWeapon); 
							 */
							if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("成功为玩家装备武器: %s"), *WeaponInfo->WeaponName.ToString()));
						}
					}
				}
			}
		}
	}
}


void ARoomGameMode::RequestStartGame(AController* RequestingController)
{
	// 工业级防呆：任何涉及指针的操作必须先做安全校验
	if (!IsValid(RequestingController))
	{
		return;
	}

	// 1. 【身份鉴权】：判断发起请求的人是否为房主
	// 在 Listen Server (局域网) 架构中，World 的 FirstPlayerController 就是本机的房主
	AController* HostController = GetWorld()->GetFirstPlayerController();
	if (RequestingController != HostController)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoomGameMode] 拒绝开局请求：该玩家不是房主！"));
		return; 
	}

	// 2. 【测试开关短路拦截】：如果开启了无视大厅直接测试，强制开局
	if (bSkipRoomPhaseForTesting)
	{
		UE_LOG(LogTemp, Log, TEXT("[RoomGameMode] 测试模式开启，无视准备状态，强制开局！"));
		PerformGameStart();
		return;
	}

	// 3. 【业务逻辑校验】：检查是否全员准备就绪
	if (!CheckAllPlayersReady())
	{
		// 校验失败：向全房间广播系统红字/绿字提示
		BroadcastSystemMessage(TEXT("无法开始游戏：还有玩家未准备就绪！"));
		return;
	}

	// 4. 校验全部通过，移交权限给核心执行器
	PerformGameStart();
}

void ARoomGameMode::PerformGameStart()
{
	// 1. 更新房间状态
	CurrentRoomState = ERoomState::BattleInProgress;

	// 2. 下发 Client RPC 给所有客户端，让他们立刻切 UI（进入阶段三）
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
		{
			// 通知客户端立刻隐藏房间UI，打开准星血条HUD
			PC->Client_TransitToMatchState(EMatchState::Battleing);
		}
	}

	// 3. 【核心更新】：开启服务器倒计时，延迟生成角色，给客户端 UI 切换留出时间
	BroadcastSystemMessage(FString::Printf(TEXT("游戏将在 %.1f 秒后开始..."), MatchStartDelay));
	
	GetWorld()->GetTimerManager().SetTimer(
		MatchStartTimerHandle,
		this,
		&ARoomGameMode::SpawnAllPlayersIntoBattle,
		MatchStartDelay,
		false // 只执行一次
	);
}

void ARoomGameMode::SpawnAllPlayersIntoBattle()
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameMode] 倒计时结束，开始降生玩家物理实体！"));

	// 遍历当前的 GameState 中所有成功建立连接的 PlayerState
	if (ARoomGameState* GS = GetGameState<ARoomGameState>())
	{
		for (APlayerState* GenericPS : GS->PlayerArray)
		{
			if (ARoomPlayerState* PS = Cast<ARoomPlayerState>(GenericPS))
			{
				// 获取这个 PlayerState 对应的 Controller
				if (AController* PlayerController = Cast<AController>(PS->GetOwner()))
				{
					// 【防呆设计】：如果玩家忘了选角色，给一个默认值
					FString FinalChar = PS->SelectedCharacterRowName.IsEmpty() ? TEXT("Warrior") : PS->SelectedCharacterRowName;
					FString FinalWeapon = PS->SelectedWeaponRowName.IsEmpty() ? TEXT("Knife") : PS->SelectedWeaponRowName;

					// 调用您已经写好的 HandlePlayerRequestSpawn 进行生成！
					// 这个函数会触发底层 RestartPlayer -> GetDefaultPawnClassForController -> SpawnActor
					HandlePlayerRequestSpawn(PlayerController, FinalChar, FinalWeapon);
				}
			}
		}
	}
	
	BroadcastSystemMessage(TEXT("战斗开始！"));
}