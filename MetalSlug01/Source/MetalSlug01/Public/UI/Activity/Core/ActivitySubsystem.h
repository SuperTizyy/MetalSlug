//1

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ActivitySubsystem.generated.h"

/**
 * 活动系统全局 Subsystem
 * 职责：
 * 1. 持有所有活动 Track
 * 2. 负责服务器同步
 * 3. 向 UI 提供只读数据
 * GameInstanceSubsystem→ 全游戏唯一→ 切地图不销毁→ 最适合“活动系统”
 */
UCLASS()
class METALSLUG01_API UActivitySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	/**
	 * 初始化 Subsystem
	 * 在 GameInstance 创建时调用
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	
	/**
	 * 反初始化
	 * 游戏退出时调用
	 */
	virtual void Deinitialize() override;
private:
	/**
	 * 当前所有活动 Track
	 * Key = ActivityId
	 */
	UPROPERTY()
	TMap<FName, UObject*> ActivityTracks;
public:
	/**
	 * 注册一个活动 Track
	 * 只允许 Subsystem 内部调用
	 */
	template<typename T>
	T* CreateActivityTrack(FName ActivityId);
	
	/**
	 * 获取活动 Track（只读）
	 */
	template<typename T>
	T* GetActivityTrack(FName ActivityId) const;
};

template<typename T>
T* UActivitySubsystem::CreateActivityTrack(FName ActivityId)
{
	// 防止重复创建
	if (ActivityTracks.Contains(ActivityId))
	{
		return Cast<T>(ActivityTracks[ActivityId]);
	}

	// 创建 Track 对象
	T* NewTrack = NewObject<T>(this);

	// 安全检查
	if (!NewTrack)
	{
		return nullptr;
	}

	// 存入 Map
	ActivityTracks.Add(ActivityId, NewTrack);

	return NewTrack;
}

template<typename T>
T* UActivitySubsystem::GetActivityTrack(FName ActivityId) const
{
	UObject* const* Found = ActivityTracks.Find(ActivityId);
	if (!Found)
	{
		return nullptr;
	}

	return Cast<T>(*Found);
}



