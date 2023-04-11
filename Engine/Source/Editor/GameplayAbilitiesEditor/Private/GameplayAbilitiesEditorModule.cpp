// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "AbilitySystemEditorPrivatePCH.h"
#include "Editor/PropertyEditor/Public/PropertyEditorModule.h"
#include "AttributeDetails.h"
#include "AttributeSet.h"
#include "GameplayEffectDetails.h"
#include "GameplayEffectExecutionScopedModifierInfoDetails.h"
#include "GameplayEffectExecutionDefinitionDetails.h"
#include "GameplayEffectModifierMagnitudeDetails.h"

#include "IAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "AssetTypeActions_GameplayAbilitiesBlueprint.h"
#include "AssetTypeActions_GameplayEffect.h"
#include "GameplayAbilitiesGraphPanelPinFactory.h"
#include "GameplayAbilitiesGraphPanelNodeFactory.h"
#include "GameplayCueTagDetails.h"

#include "Runtime/GameplayTags/Public/GameplayTagsModule.h"
#include "Editor/BlueprintGraph/Public/BlueprintActionDatabase.h"
#include "K2Node_GameplayCueEvent.h"

#include "SGameplayCueEditor.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructureModule.h"
#include "SDockTab.h"

class FGameplayAbilitiesEditorModule : public IGameplayAbilitiesEditorModule
{
	// Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End IModuleInterface

	TSharedRef<SDockTab> SpawnGameplayCueEditorTab(const FSpawnTabArgs& Args);
	TSharedPtr<SWidget> SummonGameplayCueEditorUI();

	FGetGameplayCueNotifyClasses& GetGameplayCueNotifyClassesDelegate() override
	{
		return GetGameplayCueNotifyClasses;
	}

	FGetGameplayCuePath& GetGameplayCueNotifyPathDelegate()
	{
		return GetGameplayCueNotifyPath;
	}

	FGetGameplayCueInterfaceClasses& GetGameplayCueInterfaceClassesDelegate()
	{
		return GetGameplayCueInterfaceClasses;

	}

	FGetGameplayCueEditorStrings& GetGameplayCueEditorStringsDelegate()
	{
		return GetGameplayCueEditorStrings;
	}

protected:
	void RegisterAssetTypeAction(class IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action);
	static void GameplayTagTreeChanged();

private:
	/** All created asset type actions.  Cached here so that we can unregister it during shutdown. */
	TArray< TSharedPtr<IAssetTypeActions> > CreatedAssetTypeActions;

	/** Pin factory for abilities graph; Cached so it can be unregistered */
	TSharedPtr<FGameplayAbilitiesGraphPanelPinFactory> GameplayAbilitiesGraphPanelPinFactory;

	/** Node factory for abilities graph; Cached so it can be unregistered */
	TSharedPtr<FGameplayAbilitiesGraphPanelNodeFactory> GameplayAbilitiesGraphPanelNodeFactory;

	/** Handle to the registered GameplayTagTreeChanged delegate */
	FDelegateHandle GameplayTagTreeChangedDelegateHandle;

	FGetGameplayCueNotifyClasses GetGameplayCueNotifyClasses;

	FGetGameplayCuePath GetGameplayCueNotifyPath;

	FGetGameplayCueInterfaceClasses GetGameplayCueInterfaceClasses;
	
	FGetGameplayCueEditorStrings GetGameplayCueEditorStrings;
};

IMPLEMENT_MODULE(FGameplayAbilitiesEditorModule, GameplayAbilitiesEditor)

void FGameplayAbilitiesEditorModule::StartupModule()
{
	// Register the details customizer
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout( "GameplayAttribute", FOnGetPropertyTypeCustomizationInstance::CreateStatic( &FAttributePropertyDetails::MakeInstance ) );
	PropertyModule.RegisterCustomPropertyTypeLayout( "ScalableFloat", FOnGetPropertyTypeCustomizationInstance::CreateStatic( &FScalableFloatDetails::MakeInstance ) );
	PropertyModule.RegisterCustomPropertyTypeLayout( "GameplayEffectExecutionScopedModifierInfo", FOnGetPropertyTypeCustomizationInstance::CreateStatic( &FGameplayEffectExecutionScopedModifierInfoDetails::MakeInstance ) );
	PropertyModule.RegisterCustomPropertyTypeLayout( "GameplayEffectExecutionDefinition", FOnGetPropertyTypeCustomizationInstance::CreateStatic( &FGameplayEffectExecutionDefinitionDetails::MakeInstance ) );
	PropertyModule.RegisterCustomPropertyTypeLayout( "GameplayEffectModifierMagnitude", FOnGetPropertyTypeCustomizationInstance::CreateStatic( &FGameplayEffectModifierMagnitudeDetails::MakeInstance ) );
	PropertyModule.RegisterCustomPropertyTypeLayout( "GameplayCueTag", FOnGetPropertyTypeCustomizationInstance::CreateStatic( &FGameplayCueTagDetails::MakeInstance ) );

	PropertyModule.RegisterCustomClassLayout( "AttributeSet", FOnGetDetailCustomizationInstance::CreateStatic( &FAttributeDetails::MakeInstance ) );
	PropertyModule.RegisterCustomClassLayout( "GameplayEffect", FOnGetDetailCustomizationInstance::CreateStatic( &FGameplayEffectDetails::MakeInstance ) );

	// Register asset types
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TSharedRef<IAssetTypeActions> GABAction = MakeShareable(new FAssetTypeActions_GameplayAbilitiesBlueprint());
	TSharedRef<IAssetTypeActions> GEAction = MakeShareable(new FAssetTypeActions_GameplayEffect());
	RegisterAssetTypeAction(AssetTools, GABAction);
	RegisterAssetTypeAction(AssetTools, GEAction);

	// Register factories for pins and nodes
	GameplayAbilitiesGraphPanelPinFactory = MakeShareable(new FGameplayAbilitiesGraphPanelPinFactory());
	FEdGraphUtilities::RegisterVisualPinFactory(GameplayAbilitiesGraphPanelPinFactory);

	GameplayAbilitiesGraphPanelNodeFactory = MakeShareable(new FGameplayAbilitiesGraphPanelNodeFactory());
	FEdGraphUtilities::RegisterVisualNodeFactory(GameplayAbilitiesGraphPanelNodeFactory);

	// Listen for changes to the gameplay tag tree so we can refresh blueprint actions for the GameplayCueEvent node
	UGameplayTagsManager& GameplayTagsManager = IGameplayTagsModule::GetGameplayTagsManager();
	GameplayTagTreeChangedDelegateHandle = GameplayTagsManager.OnGameplayTagTreeChanged().AddStatic(&FGameplayAbilitiesEditorModule::GameplayTagTreeChanged);

	// GameplayCue editor
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner( FName(TEXT("GameplayCueApp")), FOnSpawnTab::CreateRaw(this, &FGameplayAbilitiesEditorModule::SpawnGameplayCueEditorTab))
		.SetDisplayName(NSLOCTEXT("GameplayAbilitiesEditorModule", "GameplayCueTabTitle", "GameplayCue Editor"))
		.SetTooltipText(NSLOCTEXT("GameplayAbilitiesEditorModule", "GameplayCueTooltipText", "Open GameplayCue Editor tab."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		//.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory());
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "Profiler.EventGraph.ExpandHotPath16"));
}

void FGameplayAbilitiesEditorModule::RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action)
{
	AssetTools.RegisterAssetTypeActions(Action);
	CreatedAssetTypeActions.Add(Action);
}

void FGameplayAbilitiesEditorModule::GameplayTagTreeChanged()
{
	// The tag tree changed so we should refresh which actions are provided by the gameplay cue event
#if STATS
	FString PerfMessage = FString::Printf(TEXT("FGameplayAbilitiesEditorModule::GameplayTagTreeChanged"));
	SCOPE_LOG_TIME_IN_SECONDS(*PerfMessage, nullptr)
#endif
	FBlueprintActionDatabase::Get().RefreshClassActions(UK2Node_GameplayCueEvent::StaticClass());
}

void FGameplayAbilitiesEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	// Unregister customizations
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout("GameplayEffect");
		PropertyModule.UnregisterCustomClassLayout("AttributeSet");

		PropertyModule.UnregisterCustomPropertyTypeLayout("GameplayEffectModifierMagnitude");
		PropertyModule.UnregisterCustomPropertyTypeLayout("GameplayEffectExecutionDefinition");
		PropertyModule.UnregisterCustomPropertyTypeLayout("GameplayEffectExecutionScopedModifierInfo");
		PropertyModule.UnregisterCustomPropertyTypeLayout("GameplayModifierInfo");
		PropertyModule.UnregisterCustomPropertyTypeLayout("ScalableFloat");
		PropertyModule.UnregisterCustomPropertyTypeLayout("GameplayAttribute");
	}

	// Unregister asset type actions
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (auto& AssetTypeAction : CreatedAssetTypeActions)
		{
			if (AssetTypeAction.IsValid())
			{
				AssetToolsModule.UnregisterAssetTypeActions(AssetTypeAction.ToSharedRef());
			}
		}
	}
	CreatedAssetTypeActions.Empty();

	// Unregister graph factories
	if (GameplayAbilitiesGraphPanelPinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(GameplayAbilitiesGraphPanelPinFactory);
		GameplayAbilitiesGraphPanelPinFactory.Reset();
	}

	if (GameplayAbilitiesGraphPanelNodeFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualNodeFactory(GameplayAbilitiesGraphPanelNodeFactory);
		GameplayAbilitiesGraphPanelNodeFactory.Reset();
	}

	if ( UObjectInitialized() && IGameplayTagsModule::IsAvailable() )
	{
		UGameplayTagsManager& GameplayTagsManager = IGameplayTagsModule::GetGameplayTagsManager();
		GameplayTagsManager.OnGameplayTagTreeChanged().Remove(GameplayTagTreeChangedDelegateHandle);
	}
}


TSharedRef<SDockTab> FGameplayAbilitiesEditorModule::SpawnGameplayCueEditorTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SummonGameplayCueEditorUI().ToSharedRef()
		];
}


TSharedPtr<SWidget> FGameplayAbilitiesEditorModule::SummonGameplayCueEditorUI()
{
	TSharedPtr<SWidget> ReturnWidget;
	if( IsInGameThread() )
	{
		ReturnWidget = SNew(SGameplayCueEditor);			
	}
	return ReturnWidget;
}