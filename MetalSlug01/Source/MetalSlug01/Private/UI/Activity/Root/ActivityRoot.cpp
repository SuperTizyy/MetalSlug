// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Activity/Root/ActivityRoot.h"
#include "UI/Activity/Controller/ActivityController.h"
#include "UI/Activity/Root/ActivityMenuItem.h"
#include "Components/VerticalBox.h"

void UActivityRoot::BuildLeftMenu()
{
	if (!ActivityConfigTable || !LeftMenuBox)
	{
		UE_LOG(LogTemp, Error,
			TEXT("BuildLeftMenu failed: ActivityConfigTable=%p LeftMenuBox=%p"),
			ActivityConfigTable,
			LeftMenuBox);
		return;
	}

	TArray<FActivityConfig*> Rows;
	ActivityConfigTable->GetAllRows(TEXT("BuildLeftMenu"), Rows);

	UE_LOG(LogTemp, Error,
		TEXT("BuildLeftMenu: Rows.Num() = %d"),
		Rows.Num());

	if (Rows.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("BuildLeftMenu: EMPTY TABLE OR STRUCT MISMATCH"));
		return;
	}

	// ✅ UE 正确排序写法（注意是引用）
	Rows.Sort([](const FActivityConfig& A, const FActivityConfig& B)
	{
		return A.SortOrder < B.SortOrder;
	});

	for (FActivityConfig* Row : Rows)
	{
		if (!Row || !MenuItemClass)
		{
			continue;
		}

		UActivityMenuItem* Item =
			CreateWidget<UActivityMenuItem>(GetWorld(), MenuItemClass);

		if (!Item)
		{
			continue;
		}

		Item->Init(Row->PageId, Row->DisplayName);

		Item->OnClicked.AddDynamic(
			Controller,
			&UActivityController::OnMenuClicked
		);

		LeftMenuBox->AddChild(Item);
	}
}




void UActivityRoot::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UE_LOG(LogTemp, Error, TEXT("ActivityRoot::NativeOnInitialized START"));

	if (!ActivityConfigTable)
	{
		UE_LOG(LogTemp, Error,
			TEXT("ActivityConfigTable is NULL in ActivityRoot"));
		return;
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("ActivityConfigTable OK"));
	}

	if (!RightContent || !LeftMenuBox)
	{
		UE_LOG(LogTemp, Error,
			TEXT("UI BindWidget failed: RightContent=%p LeftMenuBox=%p"),
			RightContent,
			LeftMenuBox);
		return;
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("BindWidget OK"));
	}

	Controller = NewObject<UActivityController>(this);
	Controller->Init(ActivityConfigTable, RightContent);

	UE_LOG(LogTemp, Error, TEXT("Controller Init OK"));

	BuildLeftMenu();
}
