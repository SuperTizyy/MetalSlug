// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 UAnimNotify 类（基类）
#include "Animation/AnimNotifies/AnimNotify.h"

// UE 自动生成的头文件
#include "AnimNotify_Footstep.generated.h"

/**
 * @class UAnimNotify_Footstep
 * @brief 脚步声动画通知 (AnimNotify)
 *
 * 职责说明:
 * - 在角色动画的关键帧（左脚落地、右脚落地）插入此通知
 * - 动画播放到这些帧时自动触发脚步声音效播放
 *
 * 架构理念:
 * 1. 与动画完全同步: 左脚落地触发一次，右脚落地触发一次
 * 2. 性能开销极低: 不依赖 Tick 逻辑
 * 3. 灵活配置: 可在蓝图动画编辑器中灵活配置触发时机
 *
 * 使用方法:
 * 在角色的行走/奔跑 Animation Montage 或 Animation Blueprint 的动画轨迹上，
 * 在需要触发脚步声的关键帧处右键 -> Add Notify -> Footstep 即可。
 */
UCLASS(const, hidecategories = Object, collapsecategories, meta = (DisplayName = "Footstep"))
class METALSLUG01_API UAnimNotify_Footstep : public UAnimNotify
{
	GENERATED_BODY()

public:
	// ==========================================
	// 蓝图可配置参数
	// ==========================================

	/**
	 * 脚步声音效资源
	 * 用途: 在蓝图编辑器中拖拽赋值
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep")
	TObjectPtr<USoundBase> FootstepSound;

	/**
	 * 脚步声音量缩放系数（0.0 静音 ~ 2.0 加倍）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float VolumeMultiplier = 1.0f;

	/**
	 * 音高随机范围
	 * 例如 0.1 表示在 [0.9, 1.1] 之间随机偏移
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PitchVariation = 0.1f;

public:
	// ==========================================
	// 核心回调
	// ==========================================

	/**
	 * AnimNotify 基类核心回调
	 * 动画播放到此处时自动调用（UE5 新签名）
	 */
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	/**
	 * 返回通知的名称（调试时可出现在日志中）
	 */
	virtual FString GetNotifyName_Implementation() const override;

protected:
	// ==========================================
	// 内部实现
	// ==========================================

	/**
	 * 实际播放脚步声的内部实现
	 * BlueprintNativeEvent 要求对应 _Implementation 函数签名必须完全一致（含 const）
	 */
	UFUNCTION(BlueprintNativeEvent)
	void OnFootstep(const USkeletalMeshComponent* MeshComp, FVector Location) const;

	/**
	 * 获取播放位置
	 * 用途: 默认返回 foot_l/foot_r 的中点
	 * 蓝图子类可重载以实现 IK 脚部位置追踪
	 */
	UFUNCTION(BlueprintCallable, Category = "Footstep")
	FVector GetFootstepLocation(const USkeletalMeshComponent* MeshComp) const;
};
