// ==========================================
// 脚步音效 Component
// 目的: 抽离 BaseCharacter 的脚步物理检测与音效播放
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FootstepComponent.generated.h"

class USoundBase;

UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UFootstepComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFootstepComponent();

	/** 触发一次脚步检测 (Trace 地面 → 播放对应材质脚步音效) */
	UFUNCTION(BlueprintCallable, Category = "Footstep")
	void PlayFootstep();

protected:
	/** 脚步射线长度 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep")
	float TraceDistance = 100.0f;

	/** 默认脚步音效 (材质未匹配时使用) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep")
	TObjectPtr<USoundBase> DefaultFootstepSound = nullptr;
};
