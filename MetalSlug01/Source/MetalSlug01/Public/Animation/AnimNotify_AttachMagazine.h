// ==========================================
// AnimNotify_AttachMagazine — 换弹动画结束时重新安装弹匣
//
// 【v61 新增】
//
// 功能说明:
//   - 在换弹动画快结束时触发 (AnimNotify)
//   - 把弹匣 (MagazineSkeletalMesh) 从角色手部取下
//   - 重新吸附回武器上的原始位置
//
// 架构设计:
//   - DetachMagazine 触发时, 弹匣从武器脱离并吸附到角色左手
//   - AttachMagazine 触发时, 弹匣从角色左手脱离并吸附回武器
//   - 弹匣回武器后, 换弹动画结束
//
// 使用方法:
//   1. 在换弹动画蒙太奇结束前 (最后 5-10 帧) 添加 AnimNotify: AttachMagazine
//   2. 确保 DetachMagazine 已在动画开始处添加
//
// 零兜底:
//   - 武器无 MagazineSkeletalMesh → Log Error + return
// ==========================================

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_AttachMagazine.generated.h"

/**
 * @class UAnimNotify_AttachMagazine
 * @brief 换弹动画结束时重新安装弹匣
 */
UCLASS(const, hidecategories = Object, collapsecategories, meta = (DisplayName = "AttachMagazine"))
class METALSLUG01_API UAnimNotify_AttachMagazine : public UAnimNotify
{
	GENERATED_BODY()

public:
	// 通知名称 (调试时显示在编辑器)
	virtual FString GetNotifyName_Implementation() const override
	{
		return TEXT("AttachMagazine");
	}

	// 核心回调: 动画播放到此处时自动调用
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
