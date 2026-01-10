#include "UI/Activity/Root/ActivityMenuItem.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void UActivityMenuItem::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (ClickButton)
	{
		ClickButton->OnClicked.AddDynamic(
			this,
			&UActivityMenuItem::HandleClicked
		);
	}
}

void UActivityMenuItem::Init(FName InPageId, const FText& InDisplayName)
{
	PageId = InPageId;

	if (TitleText)
	{
		TitleText->SetText(InDisplayName);
	}
}

void UActivityMenuItem::HandleClicked()
{
	UE_LOG(LogTemp, Error,
		TEXT("MenuItem Clicked: %s"),
		*PageId.ToString());

	OnClicked.Broadcast(PageId);
}
