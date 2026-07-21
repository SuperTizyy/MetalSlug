// ==========================================
// AnimNotify_DetachMagazine — 换弹动画开始时脱卸弹夹
//
// 【v61 新增】
//
// 功能说明:
//   - 在换弹动画开始时触发 (AnimNotify)
//   - 把武器的弹匣 (MagazineSkeletalMesh) 从武器上 detach
//   - 吸附到角色手部的 MagazineSocket_L 插槽上
//
// 架构设计:
//   - 弹匣在武器上时: 隐藏在枪身内 (模型设计)
//   - 换弹动画时: 弹匣脱离枪身, 吸附到角色左手
//   - 动画快结束时: AnimNotify_AttachMagazine 把弹匣吸附回武器
//
// 使用方法:
//   1. 在换弹动画蒙太奇开始处 (第 0-5 帧) 添加 AnimNotify: DetachMagazine
//   2. 确保武器 BP 的 MagazineSkeletalMesh 字段已配置
//   3. 确保角色 BP 的 MagazineSocket_L 插槽已添加
//
// 零兜底:
//   - 武器无 MagazineSkeletalMesh → Log Error + return
//   - 角色无 MagazineSocket_L 插槽 → Log Error + return
// ==========================================

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_DetachMagazine.generated.h"

/**
 * @class UAnimNotify_DetachMagazine
 * @brief 换弹动画开始时脱卸弹夹
 */
UCLASS(const, hidecategories = Object, collapsecategories, meta = (DisplayName = "DetachMagazine"))
class METALSLUG01_API UAnimNotify_DetachMagazine : public UAnimNotify
{
	GENERATED_BODY()

public:
	// 通知名称 (调试时显示在编辑器)
	virtual FString GetNotifyName_Implementation() const override
	{
		return TEXT("DetachMagazine");
	}

	// 核心回调: 动画播放到此处时自动调用
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
