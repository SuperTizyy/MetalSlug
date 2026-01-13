#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PaperFlipbookComponent.h" 
#include "MSCharacterBase.generated.h"

// #include "PaperFlipbookComponent.h"  包含 Paper2D 组件头文件

/**
 * AMSCharacterBase
 * 工业级规范：使用 Abstract 关键字防止此类在编辑器中被直接摆放，强制要求使用其派生类。
 */
UCLASS(Abstract)
class METALSLUG01_API AMSCharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	// 构造函数：初始化组件与默认属性
	AMSCharacterBase();

protected:
	// 游戏开始时的初始化逻辑
	virtual void BeginPlay() override;

	// --- 组件 (Components) ---
    
	// 2D 渲染组件：用于显示角色动画（Flipbook）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Render", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPaperFlipbookComponent> SpriteComponent;

	// --- 战斗属性 (Stats) ---

	// 当前生命值：使用 EditAnywhere 方便在蓝图实例中调试
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Stats")
	float Health;

	// 最大生命值：初始设为 100
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Stats")
	float MaxHealth = 100.f;

	// --- 核心机制 (Core Mechanics) ---

	// 引擎原生伤害接口：处理所有来自外界的 Damage
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

	// 死亡后的逻辑处理（虚函数，允许子类重写特有的死亡表现）
	virtual void OnDeath();

public:	
	// 每帧更新
	virtual void Tick(float DeltaTime) override;

	// 获取生命值百分比：常用于 UI ProgressBar 绑定
	UFUNCTION(BlueprintCallable, Category = "MS|Stats")
	float GetHealthPercent() const { return Health / MaxHealth; }
};