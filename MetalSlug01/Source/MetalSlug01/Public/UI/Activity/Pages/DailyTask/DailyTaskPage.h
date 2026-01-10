// 版权声明：在项目设置的描述页面填写您的版权信息

#pragma once

#include "CoreMinimal.h"
#include "UI/Activity/Pages/ActivityPageBase.h"
#include "DailyTaskData.h"
#include "DailyTaskPage.generated.h"

class UListView;

/**
 * 每日任务页面
 * 管理每日任务的显示和交互，负责数据获取和列表刷新
 */
UCLASS()
class METALSLUG01_API UDailyTaskPage : public UActivityPageBase
{
	GENERATED_BODY()

public:

	/** 页面显示时刷新数据
	 *  重写基类方法，在页面显示时更新任务列表
	 */
	virtual void OnPageShow_Implementation() override;

protected:

	/** 任务列表 - 用于显示每日任务的列表控件 */
	UPROPERTY(meta = (BindWidget))
	UListView* TaskListView;

private:

	/** 当前任务数据 - 存储当前显示的任务数据列表 */
	UPROPERTY()
	TArray<FDailyTaskData> TaskDataList;

	/** 构造假数据（Demo 用）
	 *  创建演示用的测试数据
	 */
	void BuildMockData();

	/** 刷新列表
	 *  根据当前数据更新列表显示
	 */
	void RefreshList();
};
