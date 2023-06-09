// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "PropertyEditorPrivatePCH.h"
#include "SPropertyMenuAssetPicker.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "DelegateFilter.h"
#include "ContentBrowserModule.h"
#include "PropertyEditorAssetConstants.h"
#include "EditorStyleSet.h"
#include "SlateIconFinder.h"

#define LOCTEXT_NAMESPACE "PropertyEditor"

void SPropertyMenuAssetPicker::Construct( const FArguments& InArgs )
{
	CurrentObject = InArgs._InitialObject;
	bAllowClear = InArgs._AllowClear;
	AllowedClasses = InArgs._AllowedClasses;
	NewAssetFactories = InArgs._NewAssetFactories;
	OnShouldFilterAsset = InArgs._OnShouldFilterAsset;
	OnSet = InArgs._OnSet;
	OnClose = InArgs._OnClose;

	FMenuBuilder MenuBuilder(true, NULL);

	if (NewAssetFactories.Num() > 0)
	{
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("CreateNewAsset", "Create New Asset"));
		{
			for (UFactory* Factory : NewAssetFactories)
			{
				TWeakObjectPtr<UFactory> FactoryPtr(Factory);

				MenuBuilder.AddMenuEntry(
					Factory->GetDisplayName(),
					Factory->GetToolTip(),
					FSlateIconFinder::FindIconForClass(Factory->GetSupportedClass()),
					FUIAction(FExecuteAction::CreateSP(this, &SPropertyMenuAssetPicker::OnCreateNewAssetSelected, FactoryPtr))
					);
			}
		}
		MenuBuilder.EndSection();
	}

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("CurrentAssetOperationsHeader", "Current Asset"));
	{
		if( CurrentObject.IsValid() )
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("EditAsset", "Edit"), 
				LOCTEXT("EditAsset_Tooltip", "Edit this asset"),
				FSlateIcon(),
				FUIAction( FExecuteAction::CreateSP( this, &SPropertyMenuAssetPicker::OnEdit ) ) );
		}

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CopyAsset", "Copy"),
			LOCTEXT("CopyAsset_Tooltip", "Copies the asset to the clipboard"),
			FSlateIcon(),
			FUIAction( FExecuteAction::CreateSP( this, &SPropertyMenuAssetPicker::OnCopy ) )
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("PasteAsset", "Paste"),
			LOCTEXT("PasteAsset_Tooltip", "Pastes an asset from the clipboard to this field"),
			FSlateIcon(),
			FUIAction( 
				FExecuteAction::CreateSP( this, &SPropertyMenuAssetPicker::OnPaste ),
				FCanExecuteAction::CreateSP( this, &SPropertyMenuAssetPicker::CanPaste ) )
		);

		if( bAllowClear )
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ClearAsset", "Clear"),
				LOCTEXT("ClearAsset_ToolTip", "Clears the asset set on this field"),
				FSlateIcon(),
				FUIAction( FExecuteAction::CreateSP( this, &SPropertyMenuAssetPicker::OnClear ) )
				);
		}
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("BrowseHeader", "Browse"));
	{
		TSharedPtr<SWidget> MenuContent;

		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

		FAssetPickerConfig AssetPickerConfig;
		for(int32 i = 0; i < AllowedClasses.Num(); ++i)
		{
			AssetPickerConfig.Filter.ClassNames.Add( AllowedClasses[i]->GetFName() );
		}
		// Allow child classes
		AssetPickerConfig.Filter.bRecursiveClasses = true;
		// Set a delegate for setting the asset from the picker
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SPropertyMenuAssetPicker::OnAssetSelected);
		// Use the list view by default
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
		// The initial selection should be the current value
		AssetPickerConfig.InitialAssetSelection = CurrentObject;
		// We'll do clearing ourselves
		AssetPickerConfig.bAllowNullSelection = false;
		// Focus search box
		AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
		// Apply custom filter
		AssetPickerConfig.OnShouldFilterAsset = OnShouldFilterAsset;
		// Don't allow dragging
		AssetPickerConfig.bAllowDragging = false;
		// Save the settings into a special section for asset pickers for properties
		AssetPickerConfig.SaveSettingsName = TEXT("AssetPropertyPicker");

		MenuContent =
			SNew(SBox)
			.WidthOverride(PropertyEditorAssetConstants::ContentBrowserWindowSize.X)
			.HeightOverride(PropertyEditorAssetConstants::ContentBrowserWindowSize.Y)
			[
				ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
			];

		MenuBuilder.AddWidget(MenuContent.ToSharedRef(), FText::GetEmpty(), true);
	}
	MenuBuilder.EndSection();

	ChildSlot
	[
		MenuBuilder.MakeWidget()
	];
}

void SPropertyMenuAssetPicker::OnEdit()
{
	if( CurrentObject.IsValid() )
	{
		UObject* Asset = CurrentObject.GetAsset();
		if ( Asset )
		{
			GEditor->EditObject( Asset );
		}
	}
	OnClose.ExecuteIfBound();
}

void SPropertyMenuAssetPicker::OnCopy()
{
	if( CurrentObject.IsValid() )
	{
		FPlatformMisc::ClipboardCopy(*CurrentObject.GetExportTextName());
	}
	OnClose.ExecuteIfBound();
}

void SPropertyMenuAssetPicker::OnPaste()
{
	FString DestPath;
	FPlatformMisc::ClipboardPaste(DestPath);

	if(DestPath == TEXT("None"))
	{
		SetValue(NULL);
	}
	else
	{
		UObject* Object = LoadObject<UObject>(NULL, *DestPath);
		bool PassesAllowedClassesFilter = true;
		if (Object && AllowedClasses.Num())
		{
			PassesAllowedClassesFilter = false;
			for(int32 i = 0; i < AllowedClasses.Num(); ++i)
			{
				if( Object->IsA(AllowedClasses[i]) )
				{
					PassesAllowedClassesFilter = true;
					break;
				}
			}
		}
		if( Object && PassesAllowedClassesFilter )
		{
			FAssetData ObjectAssetData(Object);

			// Check against custom asset filter
			if (!OnShouldFilterAsset.IsBound()
				|| !OnShouldFilterAsset.Execute(ObjectAssetData))
			{
				SetValue(ObjectAssetData);
			}
		}
	}
	OnClose.ExecuteIfBound();
}

bool SPropertyMenuAssetPicker::CanPaste()
{
	FString ClipboardText;
	FPlatformMisc::ClipboardPaste(ClipboardText);

	FString Class;
	FString PossibleObjectPath = ClipboardText;
	if( ClipboardText.Split( TEXT("'"), &Class, &PossibleObjectPath ) )
	{
		// Remove the last item
		PossibleObjectPath = PossibleObjectPath.LeftChop( 1 );
	}

	bool bCanPaste = false;

	if( PossibleObjectPath == TEXT("None") )
	{
		bCanPaste = true;
	}
	else
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		bCanPaste = PossibleObjectPath.Len() < NAME_SIZE && AssetRegistryModule.Get().GetAssetByObjectPath( *PossibleObjectPath ).IsValid();
	}

	return bCanPaste;
}

void SPropertyMenuAssetPicker::OnClear()
{
	SetValue(NULL);
	OnClose.ExecuteIfBound();
}

void SPropertyMenuAssetPicker::OnAssetSelected( const FAssetData& AssetData )
{
	SetValue(AssetData);
	OnClose.ExecuteIfBound();
}

void SPropertyMenuAssetPicker::SetValue( const FAssetData& AssetData )
{
	OnSet.ExecuteIfBound(AssetData);
}

void SPropertyMenuAssetPicker::OnCreateNewAssetSelected(TWeakObjectPtr<UFactory> FactoryPtr)
{
	if (FactoryPtr.IsValid())
	{
		UFactory* FactoryInstance = DuplicateObject<UFactory>(FactoryPtr.Get(), GetTransientPackage());
		FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
		UObject* NewAsset = AssetToolsModule.Get().CreateAsset(FactoryInstance->GetSupportedClass(), FactoryInstance);
		if (NewAsset != nullptr)
		{
			SetValue(NewAsset);
		}
	}
}

#undef LOCTEXT_NAMESPACE