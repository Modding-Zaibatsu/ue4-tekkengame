// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "AbilitySystemPrivatePCH.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebugger.h"
#include "GameplayDebuggerCategory_Abilities.h"
#endif // WITH_GAMEPLAY_DEBUGGER

class FGameplayAbilitiesModule : public IGameplayAbilitiesModule
{
	// Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End IModuleInterface

	virtual UAbilitySystemGlobals* GetAbilitySystemGlobals() override
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_IGameplayAbilitiesModule_GetAbilitySystemGlobals);
		// Defer loading of globals to the first time it is requested
		if (!AbilitySystemGlobals)
		{
			FStringClassReference AbilitySystemClassName = (UAbilitySystemGlobals::StaticClass()->GetDefaultObject<UAbilitySystemGlobals>())->AbilitySystemGlobalsClassName;

			UClass* SingletonClass = LoadClass<UObject>(NULL, *AbilitySystemClassName.ToString(), NULL, LOAD_None, NULL);

			checkf(SingletonClass != NULL, TEXT("Ability config value AbilitySystemGlobalsClassName is not a valid class name."));

			AbilitySystemGlobals = NewObject<UAbilitySystemGlobals>(GetTransientPackage(), SingletonClass, NAME_None);
			AbilitySystemGlobals->AddToRoot();
		}

		check(AbilitySystemGlobals);
		return AbilitySystemGlobals;
	}

	virtual bool IsAbilitySystemGlobalsAvailable() override
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_IGameplayAbilitiesModule_IsAbilitySystemGlobalsAvailable);
		return AbilitySystemGlobals != nullptr;
	}

	UAbilitySystemGlobals *AbilitySystemGlobals;

private:
	
};

IMPLEMENT_MODULE(FGameplayAbilitiesModule, GameplayAbilities)

void FGameplayAbilitiesModule::StartupModule()
{	
	// This is loaded upon first request
	AbilitySystemGlobals = NULL;

#if WITH_GAMEPLAY_DEBUGGER
	IGameplayDebugger& GameplayDebuggerModule = IGameplayDebugger::Get();
	GameplayDebuggerModule.RegisterCategory("Abilities", IGameplayDebugger::FOnGetCategory::CreateStatic(&FGameplayDebuggerCategory_Abilities::MakeInstance));
	GameplayDebuggerModule.NotifyCategoriesChanged();
#endif // WITH_GAMEPLAY_DEBUGGER
}

void FGameplayAbilitiesModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	AbilitySystemGlobals = NULL;

#if WITH_GAMEPLAY_DEBUGGER
	if (IGameplayDebugger::IsAvailable())
	{
		IGameplayDebugger& GameplayDebuggerModule = IGameplayDebugger::Get();
		GameplayDebuggerModule.UnregisterCategory("Abilities");
		GameplayDebuggerModule.NotifyCategoriesChanged();
	}
#endif // WITH_GAMEPLAY_DEBUGGER
}
