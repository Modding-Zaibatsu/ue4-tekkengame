// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.
#pragma once

// Custom serialization version for changes made in Dev-Blueprints stream
struct CORE_API FBlueprintsObjectVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,
		OverridenEventReferenceFixup,
		CleanBlueprintFunctionFlags,
		ArrayGetByRefUpgrade,
		EdGraphPinOptimized,
		AllowDeletionConformed,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FBlueprintsObjectVersion() {}
};
