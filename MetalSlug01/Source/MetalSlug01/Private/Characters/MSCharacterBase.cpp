#include "Characters/MSCharacterBase.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

// 构造函数实现
AMSCharacterBase::AMSCharacterBase()
{
	// 工业级优化：若此对象不需要每帧执行逻辑，可设为 false 以提升 CPU 性能
	PrimaryActorTick.bCanEverTick = true;

	// 1. 创建并配置 Sprite 渲染组件
	// SetupAttachment(RootComponent) 将其挂载到胶囊体下
	SpriteComponent = CreateDefaultSubobject<UPaperFlipbookComponent>(TEXT("SpriteRender"));
	SpriteComponent->SetupAttachment(RootComponent);

	// 2. 核心物理约束：合金弹头是横版游戏，必须锁定 Y 轴偏移
	// 工业级做法：通过代码强制限制移动平面，防止角色因物理碰撞掉入背景深处
	GetCharacterMovement()->bConstrainToPlane = true; 
	GetCharacterMovement()->SetPlaneConstraintNormal(FVector(0.f, 1.f, 0.f)); // 约束在 XZ 平面（法线指向 Y）

	// 3. 配置移动组件
	// 横版游戏通常手动控制镜像（Scale -1 或 1），而非让引擎自动旋转
	GetCharacterMovement()->bOrientRotationToMovement = false; 
	bUseControllerRotationYaw = false; // 禁止角色随视角（鼠标）旋转

	// 4. 属性初始化
	Health = MaxHealth;
}

void AMSCharacterBase::BeginPlay()
{
	Super::BeginPlay();
}

void AMSCharacterBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// 伤害计算逻辑
float AMSCharacterBase::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (Health <= 0.f) return 0.f;

	// 调用基类计算原始伤害
	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    
	// 使用 FMath::Clamp 确保生命值不会跌破 0 或超过最大值
	Health = FMath::Clamp(Health - ActualDamage, 0.f, MaxHealth);

	// 检查死亡状态
	if (Health <= 0.f)
	{
		OnDeath();
	}

	return ActualDamage;
}

// 死亡逻辑实现
void AMSCharacterBase::OnDeath()
{
	// 1. 关闭胶囊体碰撞，防止挡住其他角色或子弹
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    
	// 2. 停止一切移动输入
	GetCharacterMovement()->DisableMovement();

	// 3. 打印日志（工业级调试常用）
	UE_LOG(LogTemp, Log, TEXT("Actor [%s] is dead."), *GetName());
}