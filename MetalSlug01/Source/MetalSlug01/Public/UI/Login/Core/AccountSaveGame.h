// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

// 【关键修复 3】让这个纸箱类认识你的“档案袋”结构体
#include "UI/Login/Data/DynamicTable.h"
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "AccountSaveGame.generated.h"

// ==========================================
// 存档管理类 (物流纸箱)
// ==========================================

/**
 * 全局账号存档类
 * 负责装载所有玩家的账号记录，并将其序列化到本地硬盘
 */
UCLASS()
class METALSLUG01_API UAccountSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// 存储所有玩家账号记录的哈希表
	// Key (左边) 是账号名(Username)，方便我们极速查找某个账号是否存在
	// Value (右边) 是对应的 FAccountRecord 结构体（也就是那个档案袋）
	UPROPERTY(SaveGame)
	TMap<FString, FAccountRecord> AccountRecords;
};
