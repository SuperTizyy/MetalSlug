//1

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ActivitySubsystem.generated.h"

class UDailyLoginTrack;
class UTreasureTrack;

/**
 * Activity 子系统
 * 职责：
 * 1. 统一管理所有 Activity Track
 * 2. 负责 Track 的创建、生命周期
 * 3. 为 Page / UI 提供 Track 访问入口
 *
 * 规则：
 * - Subsystem 不做业务判断
 * - Subsystem 不直接操作 UI
 * - Track 的逻辑独立存在
 */
UCLASS()
class METALSLUG01_API UActivitySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ================= 生命周期 =================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

public:
	// ================= Track 访问接口 =================

	/** 获取每日登录 Track */
	UDailyLoginTrack* GetDailyLoginTrack() const;

	/** 获取宝箱 Track */
	UTreasureTrack* GetTreasureTrack() const;

private:
	// ================= Track 持有 =================

	UPROPERTY()
	UDailyLoginTrack* DailyLoginTrack = nullptr;

	UPROPERTY()
	UTreasureTrack* TreasureTrack = nullptr;
};


