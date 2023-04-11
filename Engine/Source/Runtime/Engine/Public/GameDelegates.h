// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once 

/**
 * Collection of delegates for various components to call into game code
 */

// enum class with size so it can be forward declared.
enum class EGameDelegates_SaveGame : short
{	
	MaxSize,
	Icon,
	Title,
	SubTitle,
	Detail,	
};

/** Delegate to modify cooking behavior - return extra packages to cook, load up the asset registry, etc */
DECLARE_DELEGATE_OneParam(FCookModificationDelegate, TArray<FString>& /*ExtraPackagesToCook*/);
DECLARE_DELEGATE_FiveParams(FAssignStreamingChunkDelegate, const FString& /*PackageToAdd*/, const FString& /*LastLoadedMapName*/, const TArray<int32>& /*AssetRegistryChunkIDs*/, const TArray<int32>& /*ExistingChunkIds*/, TArray<int32>& /*OutChunkIndexList*/);

/** Delegate to assign a disc layer to a chunk */
typedef const TMap<FName, FString> FAssignLayerChunkMap;
DECLARE_DELEGATE_FourParams(FAssignLayerChunkDelegate, const FAssignLayerChunkMap* /*ChunkManifest*/, const FString& /*Platform*/, const int32 /*ChunkIndex*/, int32& /*OutChunkLayer*/);

/** A delegate for platforms that need extra information to flesh out save data information (name of an icon, for instance) */
DECLARE_DELEGATE_ThreeParams(FExtendedSaveGameInfoDelegate, const TCHAR* /*SaveName*/, const EGameDelegates_SaveGame /*Key*/, FString& /*Value*/);

/** A delegate for a web server running in engine to tell the game about events received from a client, and for game to respond to the client */
// using a TMap in the DECLARE_DELEGATE macro caused compiler problems (in clang anyway), a typedef solves it
typedef TMap<FString, FString> StringStringMap;
DECLARE_DELEGATE_FiveParams(FWebServerActionDelegate, int32 /*UserIndex*/, const FString& /*Action*/, const FString& /*URL*/, const StringStringMap& /*Params*/, StringStringMap& /*Response*/);




// a helper define to make defining the delegate members easy
#define DEFINE_GAME_DELEGATE(DelegateType) \
	public: F##DelegateType& Get##DelegateType() { return DelegateType; } \
	private: F##DelegateType DelegateType;



/** Class to set and get game callbacks */
class ENGINE_API FGameDelegates
{
public:

	/** Return a single FGameDelegates object */
	static FGameDelegates& Get();

	// Called when an exit command is received
	FSimpleMulticastDelegate& GetExitCommandDelegate()
	{
		return ExitCommandDelegate;
	}

	// Called when ending playing a map
	FSimpleMulticastDelegate& GetEndPlayMapDelegate()
	{
		return EndPlayMapDelegate;
	}

	// Implement all delegates declared above
	DEFINE_GAME_DELEGATE(CookModificationDelegate);
	DEFINE_GAME_DELEGATE(AssignStreamingChunkDelegate);
	DEFINE_GAME_DELEGATE(AssignLayerChunkDelegate);
	DEFINE_GAME_DELEGATE(ExtendedSaveGameInfoDelegate);
	DEFINE_GAME_DELEGATE(WebServerActionDelegate);	

private:

	FSimpleMulticastDelegate ExitCommandDelegate;
	FSimpleMulticastDelegate EndPlayMapDelegate;
};
