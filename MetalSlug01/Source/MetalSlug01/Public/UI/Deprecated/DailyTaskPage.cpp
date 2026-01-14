/*// 版权声明：在项目设置的描述页面填写您的版权信息


#include "UI/Activity/Pages/DailyTask/DailyTaskPage.h"
#include "Components/ListView.h"
#include "UI/Activity/Pages/DailyTask/DailyTaskItemObject.h"

// OnPageShow_Implementation - 页面显示时的实现
// 构造演示数据并刷新列表
void UDailyTaskPage::OnPageShow_Implementation()
{
	Super::OnPageShow_Implementation();

	// 构造演示数据
	BuildMockData();

	// 刷新列表
	RefreshList();
}

// BuildMockData - 构造演示数据
// 创建用于演示的测试任务数据
void UDailyTaskPage::BuildMockData()
{
	// 清空现有的任务数据
	TaskDataList.Empty();

	// 创建第一个任务
	FDailyTaskData Task1;
	Task1.TaskId = 1;
	Task1.Title = FText::FromString(TEXT("完成 1 次对局"));
	Task1.CurrentValue = 1;
	Task1.TargetValue = 1;
	Task1.bCompleted = true;
	Task1.bRewardClaimed = false;

	// 创建第二个任务
	FDailyTaskData Task2;
	Task2.TaskId = 2;
	Task2.Title = FText::FromString(TEXT("累计在线 10 分钟"));
	Task2.CurrentValue = 3;
	Task2.TargetValue = 10;
	Task2.bCompleted = false;
	Task2.bRewardClaimed = false;

	// 将任务数据添加到列表
	TaskDataList.Add(Task1);
	TaskDataList.Add(Task2);
}

// RefreshList - 刷新列表
// 根据当前任务数据更新列表显示
void UDailyTaskPage::RefreshList()
{
	// 检查列表控件是否存在
	if (!TaskListView)
	{
		return;
	}

	// 清空列表中的现有项
	TaskListView->ClearListItems();

	// 遍历任务数据列表，为每个任务创建列表项
	for (const FDailyTaskData& Task : TaskDataList)
	{
		// 创建任务项对象
		UDailyTaskItemObject* Item =
			NewObject<UDailyTaskItemObject>(this);

		// 设置任务数据
		Item->TaskData = Task;

		// 将任务项添加到列表中
		TaskListView->AddItem(Item);
	}
}*/