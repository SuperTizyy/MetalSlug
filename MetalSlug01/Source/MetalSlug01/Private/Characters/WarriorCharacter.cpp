// // WarriorCharacter.cpp
//
// #include "Characters/WarriorCharacter.h"
//
// #include "GameFramework/CharacterMovementComponent.h"  // 包含角色移动组件
// #include "Components/InputComponent.h"                 // 包含输入组件
// #include "Data/GameStaticTable.h"
// #include "Engine/World.h"                              // 包含世界管理器
//
// /**
//  * 构造函数 - 初始化战士角色的各项参数
//  */
// AWarriorCharacter::AWarriorCharacter()
// {
// 	// 给 Tick 插上电源并打开开关
// 	PrimaryActorTick.bCanEverTick = true;
// 	PrimaryActorTick.bStartWithTickEnabled = true;
// 	
// 	// 设置冲刺相关默认值
// 	SprintSpeedMultiplier = 3000.0f;                    // 冲刺时速度变为正常速度的3000倍
// 	SprintDuration = 0.3f;                           // 冲刺持续0.3秒
// 	SprintCooldownDuration = 1.0f;                   // 冲刺冷却1秒
// 	SprintState = ESprintState::NotSprinting;        // 初始状态为未冲刺
// 	SprintTimer = 0.0f;                              // 冲刺计时器归零
// 	SprintCooldownTimer = 0.0f;                      // 冲刺冷却计时器归零
// 	NormalMovementSpeed = 600.0f;                    // 默认正常移动速度
//
// 	// 在构造函数中设置默认移动速度
// 	MovementSpeed = NormalMovementSpeed;             // 将基础移动速度设为正常移动速度
// 	
// 	// 禁用角色随控制器的偏航角（Yaw/左右方向）旋转。
// 	// 设置为 false 后，当玩家转动鼠标观察四周时，角色身体不会跟着原地转动。
// 	// 这通常用于第三人称动作游戏，让角色可以独立于相机进行转向。
// 	bUseControllerRotationYaw = false;
// 	// 禁用角色随控制器的俯仰角（Pitch/上下方向）旋转。
// 	// 设置为 false 后，即使玩家抬头或低头看，角色的身体也不会前后倾斜。
// 	// 只有飞行器或潜艇类游戏才可能将此项设为 true。
// 	bUseControllerRotationPitch = false;
// 	// 禁用角色随控制器的翻滚角（Roll/左右侧倾）旋转。
// 	// 设置为 false 后，相机左右倾斜时，角色身体不会跟着向左或向右歪斜。
// 	// 在常规的人形角色逻辑中，这一项几乎永远保持为 false。
// 	bUseControllerRotationRoll = false;
// 	// 让角色自动转向移动方向
// 	GetCharacterMovement()->bOrientRotationToMovement = true;
// 	// 设置转向速度 (数值越大，转身越快)
// 	GetCharacterMovement()->RotationRate = FRotator(0.0f, 2000.0f, 0.0f);
// 	
// }
//
// /**
//  * 游戏开始时调用 - 用于初始化角色状态
//  */
// void AWarriorCharacter::BeginPlay()
// {
// 	Super::BeginPlay();                              // 调用父类的BeginPlay
// 	
// 	// 保存初始移动速度，用于后续恢复
// 	NormalMovementSpeed = MovementSpeed;
// 	
// 	//从ACharacter类中获取角色移动数据，存到自己的变量中
// 	CharacterMovementComp = GetCharacterMovement(); // 必须显式赋值！
// }
//
// /**
//  * 每帧调用 - 用于更新角色状态
//  */
// void AWarriorCharacter::Tick(float DeltaTime)
// {
// 	Super::Tick(DeltaTime);                          // 调用父类的Tick
// 	
// 	// 更新冲刺相关的计时器
// 	UpdateSprintTimer(DeltaTime);                    // 更新冲刺计时器
// 	UpdateSprintCooldownTimer(DeltaTime);            // 更新冲刺冷却计时器
// }
//
// /**
//  * 输入绑定 - 设置玩家输入映射
//  */
// void AWarriorCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
// {
// 	Super::SetupPlayerInputComponent(PlayerInputComponent); // 调用父类的输入设置
//
// 	// 绑定冲刺输入 - 将冲刺按键映射到相应的函数
// 	PlayerInputComponent->BindAction(TEXT("Sprint"), IE_Pressed, this, &AWarriorCharacter::SprintInput);
// }
//
// /**
//  * 冲刺按键按下时调用
//  */
// void AWarriorCharacter::SprintInput()
// {
// 	StartSprint();                                   // 开始冲刺
// }
//
//
// /**
//  * 开始冲刺
//  */
// void AWarriorCharacter::StartSprint()
// {
// 	// 检查是否可以冲刺
// 	if (!CanSprint())
// 	{
// 		return;                                      // 如果不能冲刺则直接返回
// 	}
//
// 	// 开始冲刺 - 更新状态和计时器
// 	UpdateSprintState(ESprintState::Sprinting);      // 将冲刺状态设为冲刺中
// 	SprintTimer = 0.0f;                              // 重置冲刺计时器
//
// 	// 获取玩家最后一次输入的移动方向向量
// 	FVector DashDirection = GetLastMovementInputVector();
// 	
// 	// 如果玩家站着没动按冲刺，默认向角色正前方冲
// 	if (DashDirection.IsNearlyZero())
// 	{
// 		DashDirection = GetActorForwardVector();
// 	}
// 	else
// 	{
// 		// 确保方向向量长度为 1（归一化），防止斜向冲刺速度变快
// 		DashDirection.Normalize(); 
// 	}
// 	
// 	// 计算冲刺力度 (方向 * 巨大的数值)
// 	FVector DashVelocity = DashDirection * SprintSpeedMultiplier;
// 	
// 	// LaunchCharacter 是 UE 处理瞬间击退/冲刺的最完美函数
// 	// 后面两个 true 表示覆盖角色当前的 XY 和 Z 轴原有速度
// 	LaunchCharacter(DashVelocity, true, true);
// 	
// 	// 触发蓝图事件 - 通知蓝图冲刺已开始
// 	OnSprintStarted();
// }
//
// /**
//  * 停止冲刺
//  */
// void AWarriorCharacter::StopSprint()
// {
// 	// 停止冲刺 - 只有在冲刺中时才能停止
// 	if (SprintState == ESprintState::Sprinting)
// 	{
// 		UpdateSprintState(ESprintState::Cooldown);   // 将冲刺状态设为冷却中
// 		SprintCooldownTimer = 0.0f;                  // 重置冲刺冷却计时器
//
// 		// 恢复正常移动速度 - 停止冲刺后恢复原来的移动速度
// 		if (CharacterMovementComp)                   // 检查移动组件是否存在
// 		{
// 			// 强行把角色的物理速度清零，让他瞬间停住（你可以根据手感决定要不要这行）
// 			CharacterMovementComp->Velocity = FVector::ZeroVector;
// 			//CharacterMovementComp->MaxWalkSpeed = MovementSpeed; // 恢复正常速度
// 		}
//
// 		// 触发蓝图事件 - 通知蓝图冲刺已停止
// 		OnSprintStopped();
// 	}
// }
//
// /**
//  * 检查是否可以冲刺
//  * @return 如果可以冲刺返回true，否则返回false
//  */
// bool AWarriorCharacter::CanSprint() const
// {
// 	// 检查冲刺条件：
// 	// 1. 当前不在冲刺中
// 	// 2. 当前不在冷却中
// 	// 3. 移动组件存在
// 	// 4. 角色在地面上
// 	return SprintState != ESprintState::Sprinting && 
// 		   SprintState != ESprintState::Cooldown &&
// 		   CharacterMovementComp && 
// 		   CharacterMovementComp->IsMovingOnGround();
// }
//
// /**
//  * 重载基础移动函数 - 根据冲刺状态调整移动速度
//  * @param Direction 移动方向
//  */
// void AWarriorCharacter::BasicMove(const FVector& Direction)
// {
// 	// 根据冲刺状态调整移动速度
// 	if (SprintState == ESprintState::Sprinting)
// 	{
// 		return;
// 	}
// 	
// 	// 调用父类的移动逻辑 - 执行基本的移动操作
// 	Super::BasicMove(Direction);
// }
//
// /**
//  * 重载设置移动速度 - 同步更新冲刺相关速度
//  * @param NewSpeed 新的移动速度
//  */
// void AWarriorCharacter::SetMovementSpeed(float NewSpeed)
// {
// 	// 调用父类方法 - 先执行父类的设置速度逻辑
// 	Super::SetMovementSpeed(NewSpeed);
//
// 	// 更新正常移动速度 - 保存新的正常移动速度
// 	NormalMovementSpeed = NewSpeed;
// 	
// }
//
// /**
//  * 更新冲刺状态
//  * @param NewState 新的冲刺状态
//  */
// void AWarriorCharacter::UpdateSprintState(ESprintState NewState)
// {
// 	if (SprintState != NewState)                     // 只有当状态真正改变时才执行
// 	{
// 		SprintState = NewState;                      // 更新冲刺状态
//
// 		// 根据新状态触发相应事件
// 		switch (SprintState)
// 		{
// 		case ESprintState::NotSprinting:
// 			// 未冲刺状态无需特殊处理
// 			break;
// 		case ESprintState::Sprinting:
// 			// 冲刺状态已经在StartSprint中处理过
// 			break;
// 		case ESprintState::Cooldown:
// 			// 冲刺冷却开始 - 触发蓝图事件
// 			OnSprintCooldownStarted();
// 			
// 			break;
// 		}
// 	}
// }
//
// /**
//  * 更新冲刺计时器
//  * @param DeltaTime 时间增量
//  */
// void AWarriorCharacter::UpdateSprintTimer(float DeltaTime)
// {
// 	if (SprintState == ESprintState::Sprinting)      // 只在冲刺状态下更新
// 	{
// 		SprintTimer += DeltaTime;                    // 增加冲刺时间
//
// 		// 检查冲刺是否结束 - 如果冲刺时间超过了设定的持续时间
// 		if (SprintTimer >= SprintDuration)
// 		{
// 			StopSprint();                            // 停止冲刺
// 		}
// 	}
// }
//
// /**
//  * 更新冲刺冷却计时器
//  * @param DeltaTime 时间增量
//  */
// void AWarriorCharacter::UpdateSprintCooldownTimer(float DeltaTime)
// {
// 	if (SprintState == ESprintState::Cooldown)       // 只在冷却状态下更新
// 	{
// 		SprintCooldownTimer += DeltaTime;            // 增加冷却时间
//
// 		// 检查冷却是否结束 - 如果冷却时间超过了设定的冷却时间
// 		if (SprintCooldownTimer >= SprintCooldownDuration)
// 		{
// 			UpdateSprintState(ESprintState::NotSprinting); // 将状态改为未冲刺
// 			OnSprintReady();                         // 触发冲刺就绪事件
// 		}
// 	}
// }
//
// /**
//  * 冲刺冷却结束回调函数
//  */
// void AWarriorCharacter::OnSprintCooldownEnd()
// {
// 	// 冲刺冷却结束 - 更新状态并触发就绪事件
// 	UpdateSprintState(ESprintState::NotSprinting);   // 将状态改为未冲刺
// 	OnSprintReady();                                 // 触发冲刺就绪事件
// }