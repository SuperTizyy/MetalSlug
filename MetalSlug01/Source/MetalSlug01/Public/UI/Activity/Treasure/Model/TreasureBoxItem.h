// 9

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TreasureOptionItem.h"
#include "UI/Activity/Treasure/Types/TreasureBoxTypes.h"
#include "TreasureBoxItem.generated.h"

/**
 * 单个宝箱 Item
 * 职责：
 * 1. 管理宝箱生命周期状态
 * 2. 管理选项选择逻辑
 * 3. 不发奖励、不接 UI
 */
UCLASS(BlueprintType)
class METALSLUG01_API UTreasureBoxItem : public UObject
{
	GENERATED_BODY()

public:
	// 宝箱索引（例如 Day8 宝箱）
	UPROPERTY(BlueprintReadOnly)
	int32 BoxIndex = 0;

	UPROPERTY(BlueprintReadOnly)
	bool bUnlocked = false;
	
	UPROPERTY(BlueprintReadOnly)
	bool bClaimed = false;
	
	// 宝箱当前状态
	UPROPERTY(BlueprintReadOnly)
	ETreasureBoxState State = ETreasureBoxState::Locked;

	// 宝箱内所有选项
	UPROPERTY(BlueprintReadOnly)
	TArray<UTreasureOptionItem*> Options;

	// 当前选中的选项（仅一个）
	UPROPERTY(BlueprintReadOnly)
	UTreasureOptionItem* SelectedOption = nullptr;

public:
	// 初始化宝箱
	void InitializeBox(int32 InBoxIndex);

public:
	// UI 调用的领取请求
	void RequestClaim();
	
	// 解锁宝箱
	void Unlock();

	// 选择某个选项
	bool SelectOption(UTreasureOptionItem* Option);

	// 确认领取
	bool Claim();
};
