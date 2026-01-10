#include "UI/Activity/Root/ActivityMenuItem.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

// NativeOnInitialized - 控件初始化时调用
// 绑定按钮点击事件到处理函数
void UActivityMenuItem::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (ClickButton)
	{
		// 将按钮的点击事件绑定到HandleClicked处理函数
		ClickButton->OnClicked.AddDynamic(
			this,
			&UActivityMenuItem::HandleClicked
		);
	}
}

// Init - 初始化菜单项
// 设置菜单项的页面ID和显示名称
void UActivityMenuItem::Init(FName InPageId, const FText& InDisplayName)
{
	// 保存页面ID
	PageId = InPageId;

	if (TitleText)
	{
		// 设置菜单项显示名称
		TitleText->SetText(InDisplayName);
	}
}

// HandleClicked - 处理菜单项点击事件
// 触发OnClicked事件并将页面ID传递出去
void UActivityMenuItem::HandleClicked()
{
	// 输出日志记录点击事件
	UE_LOG(LogTemp, Error,
		TEXT("MenuItem Clicked: %s"),
		*PageId.ToString());

	// 触发点击事件，传递页面ID
	OnClicked.Broadcast(PageId);
}