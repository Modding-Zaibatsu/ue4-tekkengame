// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "PluginBrowserPrivatePCH.h"
#include "SPluginTile.h"
#include "SPluginBrowser.h"
#include "SPluginTileList.h"
#include "PluginStyle.h"
#include "GameProjectGenerationModule.h"
#include "IDetailsView.h"
#include "SHyperlink.h"
#include "PluginMetadataObject.h"
#include "IProjectManager.h"
#include "PluginBrowserModule.h"
#include "ISourceControlModule.h"
#include "PropertyEditorModule.h"
#include "IUATHelperModule.h"
#include "IMainFrameModule.h"
#include "DesktopPlatformModule.h"

#define LOCTEXT_NAMESPACE "PluginListTile"


void SPluginTile::Construct( const FArguments& Args, const TSharedRef<SPluginTileList> Owner, TSharedRef<IPlugin> InPlugin )
{
	OwnerWeak = Owner;
	Plugin = InPlugin;

	RecreateWidgets();
}

void SPluginTile::RecreateWidgets()
{
	const float PaddingAmount = FPluginStyle::Get()->GetFloat( "PluginTile.Padding" );
	const float ThumbnailImageSize = FPluginStyle::Get()->GetFloat( "PluginTile.ThumbnailImageSize" );

	// @todo plugedit: Also display whether plugin is editor-only, runtime-only, developer or a combination?
	//		-> Maybe a filter for this too?  (show only editor plugins, etc.)
	// @todo plugedit: Indicate whether plugin has content?  Filter to show only content plugins, and vice-versa?

	// @todo plugedit: Maybe we should do the FileExists check ONCE at plugin load time and not at query time

	const FPluginDescriptor& PluginDescriptor = Plugin->GetDescriptor();

	// Plugin thumbnail image
	FString Icon128FilePath = Plugin->GetBaseDir() / TEXT("Resources/Icon128.png");
	if(!FPlatformFileManager::Get().GetPlatformFile().FileExists(*Icon128FilePath))
	{
		Icon128FilePath = IPluginManager::Get().FindPlugin(TEXT("PluginBrowser"))->GetBaseDir() / TEXT("Resources/DefaultIcon128.png");
	}

	const FName BrushName( *Icon128FilePath );
	const FIntPoint Size = FSlateApplication::Get().GetRenderer()->GenerateDynamicImageResource(BrushName);
	if ((Size.X > 0) && (Size.Y > 0))
	{
		PluginIconDynamicImageBrush = MakeShareable(new FSlateDynamicImageBrush(BrushName, FVector2D(Size.X, Size.Y)));
	}

	// create support link
	TSharedPtr<SWidget> SupportWidget;
	{
		if (PluginDescriptor.SupportURL.IsEmpty())
		{
			SupportWidget = SNullWidget::NullWidget;
		}
		else
		{
			FString SupportURL = PluginDescriptor.SupportURL;
			SupportWidget = SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FEditorStyle::GetBrush("Icons.Contact"))
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SHyperlink)
					.Text(LOCTEXT("SupportLink", "Support"))
					.ToolTipText(FText::Format(LOCTEXT("NavigateToSupportURL", "Open the plug-in's online support ({0})"), FText::FromString(SupportURL)))
					.OnNavigate_Lambda([=]() { FPlatformProcess::LaunchURL(*SupportURL, nullptr, nullptr); })
				];
		}
	}

	// create documentation link
	TSharedPtr<SWidget> DocumentationWidget;
	{
		if (PluginDescriptor.DocsURL.IsEmpty())
		{
			DocumentationWidget = SNullWidget::NullWidget;
		}
		else
		{
			FString DocsURL = PluginDescriptor.DocsURL;
			DocumentationWidget = SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image(FEditorStyle::GetBrush("MessageLog.Docs"))
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SHyperlink)
						.Text(LOCTEXT("DocumentationLink", "Documentation"))
						.ToolTipText(FText::Format(LOCTEXT("NavigateToDocumentation", "Open the plug-in's online documentation ({0})"), FText::FromString(DocsURL)))
						.OnNavigate_Lambda([=]() { FPlatformProcess::LaunchURL(*DocsURL, nullptr, nullptr); })
				];
		}
	}

	// create vendor link
	TSharedPtr<SWidget> CreatedByWidget;
	{
		if (PluginDescriptor.CreatedBy.IsEmpty())
		{
			CreatedByWidget = SNullWidget::NullWidget;
		}
		else if (PluginDescriptor.CreatedByURL.IsEmpty())
		{
			CreatedByWidget = SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image(FEditorStyle::GetBrush("ContentBrowser.AssetTreeFolderDeveloper"))
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
						.Text(FText::FromString(PluginDescriptor.CreatedBy))
				];
		}
		else
		{
			FString CreatedByURL = PluginDescriptor.CreatedByURL;
			CreatedByWidget = SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image(FEditorStyle::GetBrush("MessageLog.Url"))
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				[				
					SNew(SHyperlink)
						.Text(FText::FromString(PluginDescriptor.CreatedBy))
						.ToolTipText(FText::Format(LOCTEXT("NavigateToCreatedByURL", "Visit the vendor's web site ({0})"), FText::FromString(CreatedByURL)))
						.OnNavigate_Lambda([=]() { FPlatformProcess::LaunchURL(*CreatedByURL, nullptr, nullptr); })
				];
		}
	}

	ChildSlot
	[
		SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.Padding(PaddingAmount)
			[
				SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(PaddingAmount)
					[
						SNew(SHorizontalBox)

						// Thumbnail image
						+ SHorizontalBox::Slot()
							.Padding(PaddingAmount)
							.AutoWidth()
							[
								SNew(SBox)
									.WidthOverride(ThumbnailImageSize)
									.HeightOverride(ThumbnailImageSize)
									[
										SNew(SImage)
											.Image(PluginIconDynamicImageBrush.IsValid() ? PluginIconDynamicImageBrush.Get() : nullptr)
									]
							]

						+SHorizontalBox::Slot()
							[
								SNew(SVerticalBox)

								+ SVerticalBox::Slot()
									.AutoHeight()
									[
										SNew(SHorizontalBox)

										// Friendly name
										+SHorizontalBox::Slot()
										.Padding(PaddingAmount)
										[
											SNew(STextBlock)
												.Text(FText::FromString(PluginDescriptor.FriendlyName))
												.HighlightText_Raw(&OwnerWeak.Pin()->GetOwner().GetPluginTextFilter(), &FPluginTextFilter::GetRawFilterText)
												.TextStyle(FPluginStyle::Get(), "PluginTile.NameText")
										]

										// Version
										+ SHorizontalBox::Slot()
											.HAlign(HAlign_Right)
											.Padding(PaddingAmount)
											.AutoWidth()
											[
												SNew(SHorizontalBox)

												+ SHorizontalBox::Slot()
													.AutoWidth()
													.Padding(0.0f, 0.0f, 0.0f, 1.0f) // Lower padding to align font with version number base
													[
														SNew(SHorizontalBox)

														// beta version icon
														+ SHorizontalBox::Slot()
															.AutoWidth()
															.VAlign(VAlign_Bottom)
															.Padding(0.0f, 0.0f, 3.0f, 2.0f)
															[
																SNew(SImage)
																	.Visibility(PluginDescriptor.bIsBetaVersion ? EVisibility::Visible : EVisibility::Collapsed)
																	.Image(FPluginStyle::Get()->GetBrush("PluginTile.BetaWarning"))
															]

														// version label
														+ SHorizontalBox::Slot()
															.AutoWidth()
															.VAlign(VAlign_Bottom)
															[
																SNew(STextBlock)
																	.Text(PluginDescriptor.bIsBetaVersion ? LOCTEXT("PluginBetaVersionLabel", "BETA Version ") : LOCTEXT("PluginVersionLabel", "Version "))
															]
													]

												+ SHorizontalBox::Slot()
													.AutoWidth()
													.VAlign( VAlign_Bottom )
													.Padding( 0.0f, 0.0f, 2.0f, 0.0f )	// Extra padding from the right edge
													[
														SNew(STextBlock)
															.Text(FText::FromString(PluginDescriptor.VersionName))
															.TextStyle(FPluginStyle::Get(), "PluginTile.VersionNumberText")
													]
											]
									]
			
								+ SVerticalBox::Slot()
									[
										SNew(SVerticalBox)
				
										// Description
										+ SVerticalBox::Slot()
											.Padding( PaddingAmount )
											[
												SNew(STextBlock)
													.Text(FText::FromString(PluginDescriptor.Description))
													.AutoWrapText(true)
											]

										+ SVerticalBox::Slot()
											.Padding(PaddingAmount)
											.AutoHeight()
											[
												SNew(SHorizontalBox)

												// Enable checkbox
												+ SHorizontalBox::Slot()
													.Padding(PaddingAmount)
													.HAlign(HAlign_Left)
													[
														SNew(SCheckBox)
															.OnCheckStateChanged(this, &SPluginTile::OnEnablePluginCheckboxChanged)
															.IsChecked(this, &SPluginTile::IsPluginEnabled)
															.ToolTipText(LOCTEXT("EnableDisableButtonToolTip", "Toggles whether this plugin is enabled for your current project.  You may need to restart the program for this change to take effect."))
															.Content()
															[
																SNew(STextBlock)
																	.Text(LOCTEXT("EnablePluginCheckbox", "Enabled"))
															]
													]

												// edit link
												+ SHorizontalBox::Slot()
													.HAlign(HAlign_Center)
													.AutoWidth()
													.Padding(2.0f, 0.0f, 0.0f, 0.0f)
													[
														SNew(SHorizontalBox)

														+ SHorizontalBox::Slot()
														.AutoWidth()
														.Padding(PaddingAmount)
														[
															SNew(SHyperlink)
															.Visibility(this, &SPluginTile::GetAuthoringButtonsVisibility)	
															.OnNavigate(this, &SPluginTile::OnEditPlugin)
															.Text(LOCTEXT("EditPlugin", "Edit..."))
														]

														+ SHorizontalBox::Slot()
														.AutoWidth()
														.Padding(PaddingAmount)
														[
															SNew(SHyperlink)
															.Visibility(this, &SPluginTile::GetAuthoringButtonsVisibility)
															.OnNavigate(this, &SPluginTile::OnPackagePlugin)
															.Text(LOCTEXT("PackagePlugin", "Package..."))
														]
													]

												// support link
												+SHorizontalBox::Slot()
													.Padding(PaddingAmount)
													.HAlign(HAlign_Right)
													[
														SupportWidget.ToSharedRef()
													]

												// docs link
												+ SHorizontalBox::Slot()
													.AutoWidth()
													.Padding(12.0f, PaddingAmount, PaddingAmount, PaddingAmount)
													.HAlign(HAlign_Right)
													[
														DocumentationWidget.ToSharedRef()
													]

												// vendor link
												+ SHorizontalBox::Slot()
													.AutoWidth()
													.Padding(12.0f, PaddingAmount, PaddingAmount, PaddingAmount)
													.HAlign(HAlign_Right)
													[
														CreatedByWidget.ToSharedRef()
													]
											]
									]
							]
					]
			]
	];
}


ECheckBoxState SPluginTile::IsPluginEnabled() const
{
	FPluginBrowserModule& PluginBrowserModule = FPluginBrowserModule::Get();
	if(PluginBrowserModule.HasPluginPendingEnable(Plugin->GetName()))
	{
		return PluginBrowserModule.GetPluginPendingEnableState(Plugin->GetName()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;;
	}
	else
	{
		return Plugin->IsEnabled()? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
}

void SPluginTile::OnEnablePluginCheckboxChanged(ECheckBoxState NewCheckedState)
{
	const bool bNewEnabledState = (NewCheckedState == ECheckBoxState::Checked);

	const FPluginDescriptor& PluginDescriptor = Plugin->GetDescriptor();
	if (bNewEnabledState && PluginDescriptor.bIsBetaVersion)
	{
		FText WarningMessage = FText::Format(
			LOCTEXT("Warning_EnablingBetaPlugin", "Plugin '{0}' is a beta version and might be unstable or removed without notice. Please use with caution. Are you sure you want to enable the plugin?"),
			FText::FromString(PluginDescriptor.FriendlyName));

		if (EAppReturnType::No == FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage))
		{
			// user chose to keep beta plug-in disabled
			return;
		}
	}

	FText FailMessage;

	if (!IProjectManager::Get().SetPluginEnabled(Plugin->GetName(), bNewEnabledState, FailMessage, PluginDescriptor.MarketplaceURL))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FailMessage);
	}
	else
	{
		FGameProjectGenerationModule::Get().TryMakeProjectFileWriteable(FPaths::GetProjectFilePath());

		if (!IProjectManager::Get().SaveCurrentProjectToDisk(FailMessage))
		{
			FMessageDialog::Open(EAppMsgType::Ok, FailMessage);
		}
		else
		{
			FPluginBrowserModule::Get().SetPluginPendingEnableState(Plugin->GetName(), Plugin->IsEnabled(), bNewEnabledState);
		}
	}


}

EVisibility SPluginTile::GetAuthoringButtonsVisibility() const
{
	return (FApp::IsEngineInstalled() && Plugin->GetLoadedFrom() == EPluginLoadedFrom::Engine)? EVisibility::Hidden : EVisibility::Visible;
}

void SPluginTile::OnEditPlugin()
{
	// Construct the plugin metadata object using the descriptor for this plugin
	UPluginMetadataObject* MetadataObject = NewObject<UPluginMetadataObject>();
	MetadataObject->TargetIconPath = Plugin->GetBaseDir() / TEXT("Resources/Icon128.png");
	MetadataObject->PopulateFromDescriptor(Plugin->GetDescriptor());
	MetadataObject->AddToRoot();

	// Create a property view
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TSharedRef<IDetailsView> PropertyView = EditModule.CreateDetailView(FDetailsViewArgs(false, false, false, FDetailsViewArgs::ActorsUseNameArea, true));
	PropertyView->SetObject(MetadataObject, true);

	// Create the window
	PropertiesWindow = SNew(SWindow)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::FixedSize)
		.ClientSize(FVector2D(700.0f, 575.0f))
		.Title(LOCTEXT("PluginMetadata", "Plugin Properties"))
		.Content()
		[
			SNew(SBorder)
			.Padding(FMargin(8.0f, 8.0f))
			[
				SNew(SVerticalBox)

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(5.0f, 10.0f, 5.0f, 5.0f))
				[
					SNew(STextBlock)
					.Font(FPluginStyle::Get()->GetFontStyle(TEXT("PluginMetadataNameFont")))
					.Text(FText::FromString(Plugin->GetName()))
				]

				+ SVerticalBox::Slot()
				.Padding(5)
				[
					PropertyView
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(5)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ContentPadding(FMargin(20.0f, 2.0f))
					.Text(LOCTEXT("OkButtonLabel", "Ok"))
					.OnClicked(this, &SPluginTile::OnEditPluginFinished, MetadataObject)
				]
			]
		];

	FSlateApplication::Get().AddModalWindow(PropertiesWindow.ToSharedRef(), OwnerWeak.Pin());//Args.ParentWidget);
}

FReply SPluginTile::OnEditPluginFinished(UPluginMetadataObject* MetadataObject)
{
	FPluginDescriptor OldDescriptor = Plugin->GetDescriptor();

	// Update the descriptor with the new metadata
	FPluginDescriptor NewDescriptor = OldDescriptor;
	MetadataObject->CopyIntoDescriptor(NewDescriptor);
	MetadataObject->RemoveFromRoot();

	// Close the properties window
	PropertiesWindow->RequestDestroyWindow();

	// Write both to strings
	FString OldText = OldDescriptor.ToString();
	FString NewText = NewDescriptor.ToString();
	if(OldText.Compare(NewText, ESearchCase::CaseSensitive) != 0)
	{
		FString DescriptorFileName = Plugin->GetDescriptorFileName();

		// First attempt to check out the file if SCC is enabled
		ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
		if(SourceControlModule.IsEnabled())
		{
			ISourceControlProvider& SourceControlProvider = SourceControlModule.GetProvider();
			TSharedPtr<ISourceControlState, ESPMode::ThreadSafe> SourceControlState = SourceControlProvider.GetState(DescriptorFileName, EStateCacheUsage::ForceUpdate);
			if(SourceControlState.IsValid() && SourceControlState->CanCheckout())
			{
				SourceControlProvider.Execute(ISourceControlOperation::Create<FCheckOut>(), DescriptorFileName);
			}
		}

		// Write to the file and update the in-memory metadata
		FText FailReason;
		if(!Plugin->UpdateDescriptor(NewDescriptor, FailReason))
		{
			FMessageDialog::Open(EAppMsgType::Ok, FailReason);
		}

		// Recreate the widgets on this tile
		RecreateWidgets();

		// Refresh the parent too
		if(OwnerWeak.IsValid())
		{
			OwnerWeak.Pin()->GetOwner().SetNeedsRefresh();
		}
	}
	return FReply::Handled();
}

void SPluginTile::OnPackagePlugin()
{
	void* ParentWindowWindowHandle = nullptr;
	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	const TSharedPtr<SWindow>& MainFrameParentWindow = MainFrameModule.GetParentWindow();
	if ( MainFrameParentWindow.IsValid() && MainFrameParentWindow->GetNativeWindow().IsValid() )
	{
		ParentWindowWindowHandle = MainFrameParentWindow->GetNativeWindow()->GetOSWindowHandle();
	}

	FString DefaultDirectory;
	FString OutputDirectory;

	if ( !FDesktopPlatformModule::Get()->OpenDirectoryDialog(ParentWindowWindowHandle, LOCTEXT("PackagePluginDialogTitle", "Package Plugin...").ToString(), DefaultDirectory, OutputDirectory) )
	{
		return;
	}

	// Ensure path is full rather than relative (for macs)
	FString DescriptorFilename = Plugin->GetDescriptorFileName();
	FString DescriptorFullPath = FPaths::ConvertRelativePathToFull(DescriptorFilename);
	FString CommandLine = FString::Printf(TEXT("BuildPlugin -Rocket -Plugin=\"%s\" -Package=\"%s\""), *DescriptorFullPath, *OutputDirectory);

#if PLATFORM_WINDOWS
	FText PlatformName = LOCTEXT("PlatformName_Windows", "Windows");
#elif PLATFORM_MAC
	FText PlatformName = LOCTEXT("PlatformName_Mac", "Mac");
#elif PLATFORM_LINUX
	FText PlatformName = LOCTEXT("PlatformName_Linux", "Linux");
#else
	FText PlatformName = LOCTEXT("PlatformName_Other", "Other OS");
#endif

	IUATHelperModule::Get().CreateUatTask(CommandLine, PlatformName, LOCTEXT("PackagePluginTaskName", "Packaging Plugin"),
		LOCTEXT("PackagePluginTaskShortName", "Package Plugin Task"), FEditorStyle::GetBrush(TEXT("MainFrame.CookContent")));
}

#undef LOCTEXT_NAMESPACE
