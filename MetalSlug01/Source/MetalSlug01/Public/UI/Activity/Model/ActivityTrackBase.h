// 2

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ActivityTrackBase.generated.h"

/**
 * 所有活动 Track 的基类
 * 职责：
 * 1. 保存活动 ID
 * 2. 管理 Track 生命周期
 * 3. 提供统一的刷新 / 重建接口
 */

/*Abstract：不允许直接实例化
BlueprintType：UI 可读*/
UCLASS(Abstract, BlueprintType) 
class METALSLUG01_API UActivityTrackBase : public UObject
{
	GENERATED_BODY()
public:
	/**
	 * 活动唯一 ID
	 * 由服务器或配置表定义
	 */
	UPROPERTY(BlueprintReadOnly)
	FName ActivityId;
	/**
	 * 当前活动是否已激活
	 * 例如活动过期 / 未开启
	 */
	UPROPERTY(BlueprintReadOnly)
	bool bIsActive = false;
public:
	/**
	 * Track 初始化
	 * 由 ActivitySubsystem 调用
	 */
	virtual void InitializeTrack();
	/**
	 * 刷新 Track 状态
	 * 服务器数据同步后调用
	 */
	virtual void RefreshTrack();
	/**
	 * Track 重建（断线 / 重登）
	 */
	virtual void RebuildTrack();
	/**
	 * Track 是否可展示给玩家
	 */
	virtual bool CanShow() const;
};

