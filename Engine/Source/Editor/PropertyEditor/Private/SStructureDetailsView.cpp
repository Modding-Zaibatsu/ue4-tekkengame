// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "PropertyEditorPrivatePCH.h"
#include "PropertyEditorHelpers.h"
#include "ObjectPropertyNode.h"
#include "PropertyEditor.h"
#include "PropertyDetailsUtilities.h"
#include "SPropertyEditor.h"
#include "ScopedTransaction.h"
#include "SResetToDefaultPropertyEditor.h"
#include "SStructureDetailsView.h"
#include "StructurePropertyNode.h"
#include "SColorPicker.h"
#include "SSearchBox.h"


#define LOCTEXT_NAMESPACE "SStructureDetailsView"


SStructureDetailsView::~SStructureDetailsView()
{
	auto RootNodeLocal = GetRootNode();

	if (RootNodeLocal.IsValid())
	{
		SaveExpandedItems(RootNodeLocal.ToSharedRef());
	}
}

UStruct* SStructureDetailsView::GetBaseScriptStruct() const
{
	const UStruct* Struct = StructData.IsValid() ? StructData->GetStruct() : NULL;
	return const_cast<UStruct*>(Struct);
}

void SStructureDetailsView::Construct(const FArguments& InArgs)
{
	DetailsViewArgs = InArgs._DetailsViewArgs;
	
	CustomName = InArgs._CustomName;

	// Create the root property now
	RootNode = MakeShareable(new FStructurePropertyNode);
		
	PropertyUtilities = MakeShareable( new FPropertyDetailsUtilities( *this ) );
	
	ColumnSizeData.LeftColumnWidth = TAttribute<float>(this, &SStructureDetailsView::OnGetLeftColumnWidth);
	ColumnSizeData.RightColumnWidth = TAttribute<float>(this, &SStructureDetailsView::OnGetRightColumnWidth);
	ColumnSizeData.OnWidthChanged = SSplitter::FOnSlotResized::CreateSP(this, &SStructureDetailsView::OnSetColumnWidth);

	TSharedRef<SScrollBar> ExternalScrollbar = 
		SNew(SScrollBar)
		.AlwaysShowScrollbar( true );

		FMenuBuilder DetailViewOptions( true, NULL );

		FUIAction ShowOnlyModifiedAction( 
			FExecuteAction::CreateSP(this, &SStructureDetailsView::OnShowOnlyModifiedClicked),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &SStructureDetailsView::IsShowOnlyModifiedChecked)
		);

		if (DetailsViewArgs.bShowModifiedPropertiesOption)
		{
			DetailViewOptions.AddMenuEntry( 
				LOCTEXT("ShowOnlyModified", "Show Only Modified Properties"),
				LOCTEXT("ShowOnlyModified_ToolTip", "Displays only properties which have been changed from their default"),
				FSlateIcon(),
				ShowOnlyModifiedAction,
				NAME_None,
				EUserInterfaceActionType::ToggleButton 
			);
		}

		FUIAction ShowAllAdvancedAction( 
			FExecuteAction::CreateSP(this, &SStructureDetailsView::OnShowAllAdvancedClicked),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &SStructureDetailsView::IsShowAllAdvancedChecked)
		);

		DetailViewOptions.AddMenuEntry(
			LOCTEXT("ShowAllAdvanced", "Show All Advanced Details"),
			LOCTEXT("ShowAllAdvanced_ToolTip", "Shows all advanced detail sections in each category"),
			FSlateIcon(),
			ShowAllAdvancedAction,
			NAME_None,
			EUserInterfaceActionType::ToggleButton 
			);

		DetailViewOptions.AddMenuEntry(
			LOCTEXT("CollapseAll", "Collapse All Categories"),
			LOCTEXT("CollapseAll_ToolTip", "Collapses all root level categories"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SStructureDetailsView::SetRootExpansionStates, /*bExpanded=*/false, /*bRecurse=*/false)));
		DetailViewOptions.AddMenuEntry(
			LOCTEXT("ExpandAll", "Expand All Categories"),
			LOCTEXT("ExpandAll_ToolTip", "Expands all root level categories"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SStructureDetailsView::SetRootExpansionStates, /*bExpanded=*/true, /*bRecurse=*/false)));

	TSharedRef<SHorizontalBox> FilterBoxRow = SNew( SHorizontalBox )
		.Visibility(this, &SStructureDetailsView::GetFilterBoxVisibility)
		+SHorizontalBox::Slot()
		.FillWidth( 1 )
		.VAlign( VAlign_Center )
		[
			// Create the search box
			SAssignNew( SearchBox, SSearchBox )
			.OnTextChanged(this, &SStructureDetailsView::OnFilterTextChanged)
		];

	if (DetailsViewArgs.bShowOptions)
	{
		FilterBoxRow->AddSlot()
			.HAlign(HAlign_Right)
			.AutoWidth()
			[
				SNew( SComboButton )
				.ContentPadding(0)
				.ForegroundColor( FSlateColor::UseForeground() )
				.ButtonStyle( FEditorStyle::Get(), "ToggleButton" )
				.MenuContent()
				[
					DetailViewOptions.MakeWidget()
				]
				.ButtonContent()
				[
					SNew(SImage)
					.Image( FEditorStyle::GetBrush("GenericViewButton") )
				]
			];
	}

	SAssignNew(DetailTree, SDetailTree)
		.Visibility(this, &SStructureDetailsView::GetTreeVisibility)
		.TreeItemsSource(&RootTreeNodes)
		.OnGetChildren(this, &SStructureDetailsView::OnGetChildrenForDetailTree)
		.OnSetExpansionRecursive(this, &SStructureDetailsView::SetNodeExpansionStateRecursive)
		.OnGenerateRow(this, &SStructureDetailsView::OnGenerateRowForDetailTree)
		.OnExpansionChanged(this, &SStructureDetailsView::OnItemExpansionChanged)
		.SelectionMode(ESelectionMode::None)
		.ExternalScrollbar(ExternalScrollbar);

	ChildSlot
	[
		SNew( SBox )
		.Visibility(this, &SStructureDetailsView::GetPropertyEditingVisibility)
		[
			SNew( SVerticalBox )
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding( 0.0f, 0.0f, 0.0f, 2.0f )
			[
				FilterBoxRow
			]
			+ SVerticalBox::Slot()
			.FillHeight(1)
			.Padding(0)
			[
				SNew( SHorizontalBox )
				+ SHorizontalBox::Slot()
				[
					DetailTree.ToSharedRef()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew( SBox )
					.WidthOverride( 16.0f )
					[
						ExternalScrollbar
					]
				]
			]
		]
	];
}

void SStructureDetailsView::SetStructureData(TSharedPtr<FStructOnScope> InStructData)
{
	//PRE SET
	SaveExpandedItems( RootNode.ToSharedRef() );
	RootNode->SetStructure(NULL);
	RootNodesPendingKill.Add(RootNode);
	RootNode = MakeShareable(new FStructurePropertyNode);

	//SET
	StructData = InStructData;
	RootNode->SetStructure(StructData);
	if (!StructData.IsValid())
	{
		bIsLocked = false;
	}
	
	//POST SET
	DestroyColorPicker();
	ColorPropertyNode = NULL;

	FPropertyNodeInitParams InitParams;
	InitParams.ParentNode = NULL;
	InitParams.Property = NULL;
	InitParams.ArrayOffset = 0;
	InitParams.ArrayIndex = INDEX_NONE;
	InitParams.bAllowChildren = true;
	InitParams.bForceHiddenPropertyVisibility = FPropertySettings::Get().ShowHiddenProperties();
	InitParams.bCreateCategoryNodes = false;

	RootNode->InitNode(InitParams);
	RootNode->SetDisplayNameOverride(CustomName);

	RestoreExpandedItems(RootNode.ToSharedRef());

	UpdatePropertyMap();

	UpdateFilteredDetails();
}

void SStructureDetailsView::ForceRefresh()
{
	SetStructureData(StructData);
}

const TArray< TWeakObjectPtr<UObject> >& SStructureDetailsView::GetSelectedObjects() const
{
	static const TArray< TWeakObjectPtr<UObject> > DummyRef;
	return DummyRef;
}

const TArray< TWeakObjectPtr<AActor> >& SStructureDetailsView::GetSelectedActors() const
{
	static const TArray< TWeakObjectPtr<AActor> > DummyRef;
	return DummyRef;
}

const FSelectedActorInfo& SStructureDetailsView::GetSelectedActorInfo() const
{
	static const FSelectedActorInfo DummyRef;
	return DummyRef;
}

bool SStructureDetailsView::IsConnected() const
{
	return StructData.IsValid() && StructData->IsValid() && RootNode.IsValid() && RootNode->IsValid();
}

TSharedPtr<class FComplexPropertyNode> SStructureDetailsView::GetRootNode()
{
	return RootNode;
}

void SStructureDetailsView::CustomUpdatePropertyMap()
{
	DetailLayout->DefaultCategory(NAME_None).SetDisplayName(NAME_None, CustomName);
}

EVisibility SStructureDetailsView::GetPropertyEditingVisibility() const
{
	return StructData.IsValid() && StructData->IsValid() && RootNode.IsValid() && RootNode->IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

void SStructureDetailsView::RegisterInstancedCustomPropertyLayout(UStruct* Class, FOnGetDetailCustomizationInstance DetailLayoutDelegate)
{
	RegisterInstancedCustomPropertyLayoutInternal(Class, DetailLayoutDelegate);
}

void SStructureDetailsView::UnregisterInstancedCustomPropertyLayout(UStruct* Class)
{
	UnregisterInstancedCustomPropertyLayoutInternal(Class);
}

#undef LOCTEXT_NAMESPACE
