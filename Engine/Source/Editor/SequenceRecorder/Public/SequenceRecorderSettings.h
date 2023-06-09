// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimationRecordingSettings.h"
#include "SequenceRecorderActorFilter.h"
#include "SequenceRecorderSettings.generated.h"

USTRUCT()
struct FPropertiesToRecordForClass
{
	GENERATED_BODY()

	FPropertiesToRecordForClass()
	{}

	FPropertiesToRecordForClass(TSubclassOf<USceneComponent> InClass)
		: Class(InClass)
	{}

	/** The class of the object we can record */
	UPROPERTY(Config, EditAnywhere, AdvancedDisplay, Category = "Sequence Recording")
	TSubclassOf<USceneComponent> Class;

	/** List of properties we want to record for this class */
	UPROPERTY(Config, EditAnywhere, AdvancedDisplay, Category = "Sequence Recording")
	TArray<FName> Properties;
};

USTRUCT()
struct FSettingsForActorClass
{
	GENERATED_BODY()

	/** The class of the actor we want to record */
	UPROPERTY(Config, EditAnywhere, AdvancedDisplay, Category = "Sequence Recording")
	TSubclassOf<AActor> Class;

	/** Whether to record to 'possessable' (i.e. level-owned) or 'spawnable' (i.e. sequence-owned) actors. */
	UPROPERTY(Config, EditAnywhere, AdvancedDisplay, Category = "Sequence Recording")
	bool bRecordToPossessable;
};

UCLASS(config=Editor)
class SEQUENCERECORDER_API USequenceRecorderSettings : public UObject
{
	GENERATED_UCLASS_BODY()

	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;

public:
	/** Whether to create a level sequence when recording. Actors and animations will be inserted into this sequence */
	UPROPERTY(Config, EditAnywhere, Category = "Sequence Recording")
	bool bCreateLevelSequence;

	/** Whether to maximize the viewport when recording */
	UPROPERTY(Config, EditAnywhere, Category = "Sequence Recording")
	bool bImmersiveMode;

	/** The length of the recorded sequence */
	UPROPERTY(Config, EditAnywhere, Category = "Sequence Recording")
	float SequenceLength;

	/** Delay that we will use before starting recording */
	UPROPERTY(Config, EditAnywhere, Category = "Sequence Recording")
	float RecordingDelay;

	/** The base name of the sequence to record to. This name will also be used to auto-generate any assets created by this recording. */
	UPROPERTY(Config, EditAnywhere, Category = "Sequence Recording")
	FString SequenceName;

	/** Base path for this recording. Sub-assets will be created in subdirectories as specified */
	UPROPERTY(Config, EditAnywhere, Category = "Sequence Recording", meta=(ContentDir))
	FDirectoryPath SequenceRecordingBasePath;

	/** The name of the subdirectory animations will be placed in. Leave this empty to place into the same directory as the sequence base path */
	UPROPERTY(Config, EditAnywhere, AdvancedDisplay, Category = "Sequence Recording")
	FString AnimationSubDirectory;

	/** Whether to record nearby spawned actors. */
	UPROPERTY(Config, EditAnywhere, Category = "Sequence Recording")
	bool bRecordNearbySpawnedActors;

	/** Proximity to currently recorded actors to record newly spawned actors. */
	UPROPERTY(Config, EditAnywhere, Category = "Sequence Recording")
	float NearbyActorRecordingProximity;

	/** Whether to record the world settings actor in the sequence (some games use this to attach world SFX) */
	UPROPERTY(Config, EditAnywhere, Category = "Sequence Recording")
	bool bRecordWorldSettingsActor;

	/** Filter to check spawned actors against to see if they should be recorded */
	UPROPERTY(Config, EditAnywhere, Category = "Sequence Recording")
	FSequenceRecorderActorFilter ActorFilter;

	/** Sequence actors to trigger playback on when recording starts (e.g. for recording synchronized sequences) */
	UPROPERTY(Transient, EditAnywhere, Category = "Sequence Recording")
	TArray<TLazyObjectPtr<class ALevelSequenceActor>> LevelSequenceActorsToTrigger;

	/** Default settings applied to animation recording */
	UPROPERTY(Config, EditAnywhere, Category = "Sequence Recording")
	FAnimationRecordingSettings DefaultAnimationSettings;

	/** Whether to record actors that are spawned by sequencer itself (this is usually disabled as results can be unexpected) */
	UPROPERTY(Config, EditAnywhere, AdvancedDisplay, Category = "Sequence Recording")
	bool bRecordSequencerSpawnedActors;

	/** The properties to record for specified classes. Component classes specified here will be recorded. If an actor does not contain one of these classes it will be ignored. */
	UPROPERTY(Config, EditAnywhere, Category = "Sequence Recording")
	TArray<FPropertiesToRecordForClass> ClassesAndPropertiesToRecord;

	/** Settings applied to actors of a specified class */
	UPROPERTY(Config, EditAnywhere, AdvancedDisplay, Category = "Sequence Recording")
	TArray<FSettingsForActorClass> PerActorSettings;
};