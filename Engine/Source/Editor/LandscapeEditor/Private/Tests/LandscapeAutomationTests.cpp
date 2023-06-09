// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "../LandscapeEditorPrivatePCH.h"

#include "AutomationCommon.h"
#include "AutomationEditorCommon.h"

#include "../LandscapeEdMode.h"
#include "../LandscapeEditorDetailCustomization_NewLandscape.h"
#include "ScopedTransaction.h"
#include "Landscape.h"

#if WITH_DEV_AUTOMATION_TESTS

#define LOCTEXT_NAMESPACE "LandscapeEditor.NewLandscape"

DEFINE_LOG_CATEGORY_STATIC(LogLandscapeAutomationTests, Log, All);

/**
* Landscape test helper functions
*/
namespace LandscapeTestUtils
{
	/**
	* Finds the viewport to use for the landscape tool
	*/
	static FLevelEditorViewportClient* FindSelectedViewport()
	{
		FLevelEditorViewportClient* SelectedViewport = NULL;

		for (int32 i = 0; i < GEditor->LevelViewportClients.Num(); i++)
		{
			FLevelEditorViewportClient* ViewportClient = GEditor->LevelViewportClients[i];
			if (!ViewportClient->IsOrtho())
			{
				SelectedViewport = ViewportClient;
			}
		}

		return SelectedViewport;
	}
}

/**
* Latent command to create a new landscape
*/
DEFINE_LATENT_AUTOMATION_COMMAND(FCreateLandscapeCommand);
bool FCreateLandscapeCommand::Update()
{
	//Switch to the Landscape tool
	GLevelEditorModeTools().ActivateMode(FBuiltinEditorModes::EM_Landscape);
	FEdModeLandscape* LandscapeEdMode = (FEdModeLandscape*)GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_Landscape);

	//Modify the "Section size"
	LandscapeEdMode->UISettings->NewLandscape_QuadsPerSection = 7;
	LandscapeEdMode->UISettings->NewLandscape_ClampSize();

	//Create the landscape
	TSharedPtr<FLandscapeEditorDetailCustomization_NewLandscape> Customization_NewLandscape = MakeShareable(new FLandscapeEditorDetailCustomization_NewLandscape);
	Customization_NewLandscape->OnCreateButtonClicked();

	if (LandscapeEdMode->CurrentToolTarget.LandscapeInfo.IsValid())
	{
		UE_LOG(LogLandscapeAutomationTests, Display, TEXT("Created a new landscape"));
	}
	else
	{
		UE_LOG(LogLandscapeAutomationTests, Error, TEXT("Failed to create a new landscape"));
	}

	return true;
}

/**
* Latent command to start using the sculpting tool
*/
DEFINE_LATENT_AUTOMATION_COMMAND(FBeginModifyLandscapeCommand);
bool FBeginModifyLandscapeCommand::Update()
{
	//Find the landscape
	FEdModeLandscape* LandscapeEdMode = (FEdModeLandscape*)GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_Landscape);

	//Find a location on the edge of the landscape along the x axis so the default camera can see it in the distance.
	FVector LandscapeSizePerComponent = LandscapeEdMode->UISettings->NewLandscape_QuadsPerSection * LandscapeEdMode->UISettings->NewLandscape_SectionsPerComponent * LandscapeEdMode->UISettings->NewLandscape_Scale;
	FVector TargetLoctaion(0);
	TargetLoctaion.X = -LandscapeSizePerComponent.X * (LandscapeEdMode->UISettings->NewLandscape_ComponentCount.X / 2.f);

	ALandscapeProxy* Proxy = LandscapeEdMode->CurrentToolTarget.LandscapeInfo.Get()->GetCurrentLevelLandscapeProxy(true);
	if (Proxy)
	{
		TargetLoctaion = Proxy->LandscapeActorToWorld().InverseTransformPosition(TargetLoctaion);
	}

	//Begin using the sculpting tool
	FLevelEditorViewportClient* SelectedViewport = LandscapeTestUtils::FindSelectedViewport();
	LandscapeEdMode->CurrentTool->BeginTool(SelectedViewport, LandscapeEdMode->CurrentToolTarget, TargetLoctaion);
	SelectedViewport->Invalidate();

	UE_LOG(LogLandscapeAutomationTests, Display, TEXT("Modified the landscape using the sculpt tool"));

	return true;
}

/**
*  Latent command stop using the sculpting tool
*/
DEFINE_LATENT_AUTOMATION_COMMAND(FEndModifyLandscapeCommand);
bool FEndModifyLandscapeCommand::Update()
{
	//Find the landscape
	FEdModeLandscape* LandscapeEdMode = (FEdModeLandscape*)GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_Landscape);

	//End using the sculpting tool
	FLevelEditorViewportClient* SelectedViewport = LandscapeTestUtils::FindSelectedViewport();
	LandscapeEdMode->CurrentTool->EndTool(SelectedViewport);
	return true;
}

/**
* Landscape creation / edit test
*/
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeEditorTest, "System.Promotion.Editor.Landscape Editor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter);
bool FLandscapeEditorTest::RunTest(const FString& Parameters)
{
	//New level
	UWorld* NewMap = AutomationEditorCommonUtils::CreateNewMap();
	if (NewMap)
	{
		UE_LOG(LogLandscapeAutomationTests, Display, TEXT("Created an empty level"));
	}
	else
	{
		UE_LOG(LogLandscapeAutomationTests, Error, TEXT("Failed to create an empty level"));
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FCreateLandscapeCommand());

	//For some reason the heightmap component takes a few ticks to register with the nav system.  We crash if we try to modify the heightmap before then.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FBeginModifyLandscapeCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FEndModifyLandscapeCommand());

	//Wait for the landscape to update before the screenshot
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));

	//Update the screenshot name, then take a screenshot.
	if (FAutomationTestFramework::GetInstance().IsScreenshotAllowed())
	{
		WindowScreenshotParameters ScreenshotParameters;

		//Find the main editor window
		TArray<TSharedRef<SWindow> > AllWindows;
		FSlateApplication::Get().GetAllVisibleWindowsOrdered(AllWindows);
		if (AllWindows.Num() == 0)
		{
			UE_LOG(LogLandscapeAutomationTests, Error, TEXT("ERROR: Could not find the main editor window."));
			return true;
		}

		//Create the path
		ScreenshotParameters.CurrentWindow = AllWindows[0];
		const FString LandscapeTestName = TEXT("NewLandscapeTest");
		FString PathName = FPaths::AutomationDir() + LandscapeTestName / FPlatformProperties::PlatformName();
		FPaths::MakePathRelativeTo(PathName, *FPaths::RootDir());
		ScreenshotParameters.ScreenshotName = FString::Printf(TEXT("%s/%d.png"), *PathName, FEngineVersion::Current().GetChangelist());

		//Take a screenshot
		ADD_LATENT_AUTOMATION_COMMAND(FTakeEditorScreenshotCommand(ScreenshotParameters));
	}
	return true;
}

#undef LOCTEXT_NAMESPACE

#endif //WITH_DEV_AUTOMATION_TESTS