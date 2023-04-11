// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"
#include "AssetViewerSettings.h"

#include "Editor/EditorPerProjectUserSettings.h"
#include "Classes/Engine/TextureCube.h"

UAssetViewerSettings::UAssetViewerSettings()
{
	// Make sure there always is one profile as default
	Profiles.AddDefaulted(1);
	Profiles[0].ProfileName = TEXT("Profile_0");

	NumProfiles = Profiles.Num();
}

UAssetViewerSettings* UAssetViewerSettings::Get()
{
	// This is a singleton, use default object
	UAssetViewerSettings* DefaultSettings = GetMutableDefault<UAssetViewerSettings>();	
	return DefaultSettings;
}

void UAssetViewerSettings::Save()
{
	SaveConfig();
	UpdateDefaultConfigFile();
}

void UAssetViewerSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;	
	UObject* Outer = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetOuter() : nullptr;
	if (Outer != nullptr && ( Outer->GetName() == "PostProcessSettings" || Outer->GetName() == "Vector" || Outer->GetName() == "LinearColor"))
	{
		PropertyName = GET_MEMBER_NAME_CHECKED(FPreviewSceneProfile, PostProcessingSettings);
	}

	// Check for identical names (temporary until we validate in the property settings view text input box)
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FPreviewSceneProfile, ProfileName))
	{	
		for (int32 ProfileIndex = 0; ProfileIndex < Profiles.Num(); ++ProfileIndex)
		{
			for (int32 CheckIndex = 0; CheckIndex < Profiles.Num(); ++CheckIndex)
			{
				// Simply append a string if two profile names are the same
				if (ProfileIndex != CheckIndex && Profiles[ProfileIndex].ProfileName == Profiles[CheckIndex].ProfileName)
				{
					FString ProfileName = Profiles[CheckIndex].ProfileName;
					Profiles[CheckIndex].ProfileName += "_duplicatename";
				}
			}			
		}
	}

	if (NumProfiles != Profiles.Num())
	{
		OnAssetViewerProfileAddRemovedEvent.Broadcast();
		NumProfiles = Profiles.Num();
	}
	
	OnAssetViewerSettingsChangedEvent.Broadcast(PropertyName);
}

void UAssetViewerSettings::PostInitProperties()
{
	Super::PostInitProperties();
	NumProfiles = Profiles.Num();

	UEditorPerProjectUserSettings* ProjectSettings = GetMutableDefault<UEditorPerProjectUserSettings>();
	if (!Profiles.IsValidIndex(ProjectSettings->AssetViewerProfileIndex))
	{
		ProjectSettings->AssetViewerProfileIndex = 0;
	}
}

