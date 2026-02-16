#include "UI/Activity/Pages/DailyLogin/DailyLoginCheatWidget.h"

#include "Components/EditableText.h"
#include "Components/Button.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "UI/Activity/Data/DailyLoginSave.h"
#include "UI/Activity/Pages/DailyLogin/DailyLoginPage.h"

void UDailyLoginCheatWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 1. 获取 Subsystem
	UActivitySubsystem* ActivitySub = GetGameInstance()->GetSubsystem<UActivitySubsystem>();
	if (ActivitySub)
	{
		// 2. 从 Subsystem 中获取当前的真实进度
		FPlayerLoginRecord& Record = ActivitySub->GetOrInitPlayerRecord(101);
        
		// 3. 将这个进度同步到你的输入框或显示文本上
		// 注意：Progress=N表示第1到第N天可领取
		if (DayInput)
		{
			FString CurrentDayStr = FString::FromInt(Record.Progress);
			DayInput->SetText(FText::FromString(CurrentDayStr));
		}
        
		UE_LOG(LogTemp, Log, TEXT("CheatWidget: 成功同步当前存档天数: Progress=%d (第1-%d天可领取)"), 
			Record.Progress, Record.Progress);
	}
	
	// 4. 绑定按钮点击事件
	if (ApplyCheatBtn)
	{
		ApplyCheatBtn->OnClicked.AddDynamic(this, &UDailyLoginCheatWidget::OnApplyClicked);
	}
}

void UDailyLoginCheatWidget::OnApplyClicked()
{
	
	if (!DayInput) return;

	UActivitySubsystem* ActivitySub = GetGameInstance()->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySub) return;

	// 1. 获取输入并限制范围
	int32 NewDay = FCString::Atoi(*DayInput->GetText().ToString());
	NewDay = FMath::Clamp(NewDay, 1, 7);

	// 2. 直接修改底层数据
	// 这个函数内部应该包含：修改 Record.Progress + SaveToDisk() + OnActivityDataChanged.Broadcast()
	ActivitySub->Cheat_JumpToDay(101, NewDay);

	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("成功跳转！输入%d表示第1-%d天可领取"), NewDay, NewDay));
}

void UDailyLoginCheatWidget::NotifyMainPageRefresh(int32 NewDay)
{
	// 工业级项目中，通常通过通知或单例管理数据，这里直接查找 Widget 测试
	TArray<UUserWidget*> FoundWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetWorld(), FoundWidgets, UDailyLoginPage::StaticClass(), false);

	for (UUserWidget* Widget : FoundWidgets)
	{
		UDailyLoginPage* MainPage = Cast<UDailyLoginPage>(Widget);
		if (MainPage)
		{
			// 假设你在 DailyLoginPage 中已经定义了设置当前天数并刷新的方法
			// MainPage->SetTestCurrentDay(NewDay); 
			MainPage->Cheat_SetDayAndRefresh(NewDay); // 重新生成 1-7 天并更新固定第 8 天
		}
	}
}