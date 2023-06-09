// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IInternationalizationArchiveSerializer.h"
#include "IInternationalizationManifestSerializer.h"

typedef TSharedRef<FString, ESPMode::ThreadSafe> FTextDisplayStringRef;
typedef TSharedPtr<FString, ESPMode::ThreadSafe> FTextDisplayStringPtr;

/** Singleton class that manages display strings for FText. */
class CORE_API FTextLocalizationManager
{
	friend CORE_API void BeginInitTextLocalization();
	friend CORE_API void EndInitTextLocalization();

private:
	/** Utility class for loading text localizations from some number of localization resources, tracking conflicts between them. */
	class FLocalizationEntryTracker
	{
	public:
		/** Data struct for tracking a localization entry from a localization resource. */
		struct FEntry
		{
			FString LocResID;
			uint32 SourceStringHash;
			FString LocalizedString;
		};

		typedef TArray<FEntry> FEntryArray;

		struct FKeyTableKeyFuncs : BaseKeyFuncs<FEntryArray, FString, false>
		{
			static FORCEINLINE const FString& GetSetKey(const TPair<FString, FEntryArray>& Element)
			{
				return Element.Key;
			}
			static FORCEINLINE bool Matches(const FString& A, const FString& B)
			{
				return A.Equals(B, ESearchCase::CaseSensitive);
			}
			static FORCEINLINE uint32 GetKeyHash(const FString& Key)
			{
				return FCrc::StrCrc32<TCHAR>(*Key);
			}
		};
		typedef TMap<FString, FEntryArray, FDefaultSetAllocator, FKeyTableKeyFuncs> FKeysTable;

		struct FNamespaceTableKeyFuncs : BaseKeyFuncs<FKeysTable, FString, false>
		{
			static FORCEINLINE const FString& GetSetKey(const TPair<FString, FKeysTable>& Element)
			{
				return Element.Key;
			}
			static FORCEINLINE bool Matches(const FString& A, const FString& B)
			{
				return A.Equals(B, ESearchCase::CaseSensitive);
			}
			static FORCEINLINE uint32 GetKeyHash(const FString& Key)
			{
				return FCrc::StrCrc32<TCHAR>(*Key);
			}
		};
		typedef TMap<FString, FKeysTable, FDefaultSetAllocator, FNamespaceTableKeyFuncs> FNamespacesTable;

		FNamespacesTable Namespaces;

	public:
		/** Loads all text localizations from all localization resource files in the specified directory. */
		void LoadFromDirectory(const FString& DirectoryPath);
		/** Loads all text localizations from the specified localization resource file. */
		bool LoadFromFile(const FString& FilePath);
		/** Loads all text localizations from the specified localization resource archive, associating the entries with the specified identifier. */
		void LoadFromLocalizationResource(FArchive& Archive, const FString& LocResID);

		/** Detects conflicts between loaded localization resources and logs them as warnings. */
		void DetectAndLogConflicts() const;
	};

	/** Utility class for managing the currently loaded or registered text localizations. */
	class FDisplayStringLookupTable
	{
	public:
		/** Data struct for tracking a display string. */
		struct FDisplayStringEntry
		{
			bool bIsLocalized;
			FString LocResID;
			uint32 SourceStringHash;
			FTextDisplayStringRef DisplayString;

			FDisplayStringEntry(const bool InIsLocalized, const FString& InLocResID, const uint32 InSourceStringHash, const FTextDisplayStringRef& InDisplayString)
				: bIsLocalized(InIsLocalized)
				, LocResID(InLocResID)
				, SourceStringHash(InSourceStringHash)
				, DisplayString(InDisplayString)
			{
			}
		};

		struct FKeyTableKeyFuncs : BaseKeyFuncs<FDisplayStringEntry, FString, false>
		{
			static FORCEINLINE const FString& GetSetKey(const TPair<FString, FDisplayStringEntry>& Element)
			{
				return Element.Key;
			}
			static FORCEINLINE bool Matches(const FString& A, const FString& B)
			{
				return A.Equals(B, ESearchCase::CaseSensitive);
			}
			static FORCEINLINE uint32 GetKeyHash(const FString& Key)
			{
				return FCrc::StrCrc32<TCHAR>(*Key);
			}
		};
		typedef TMap<FString, FDisplayStringEntry, FDefaultSetAllocator, FKeyTableKeyFuncs> FKeysTable;

		struct FNamespaceTableKeyFuncs : BaseKeyFuncs<FKeysTable, FString, false>
		{
			static FORCEINLINE const FString& GetSetKey(const TPair<FString, FKeysTable>& Element)
			{
				return Element.Key;
			}
			static FORCEINLINE bool Matches(const FString& A, const FString& B)
			{
				return A.Equals(B, ESearchCase::CaseSensitive);
			}
			static FORCEINLINE uint32 GetKeyHash(const FString& Key)
			{
				return FCrc::StrCrc32<TCHAR>(*Key);
			}
		};
		typedef TMap<FString, FKeysTable, FDefaultSetAllocator, FNamespaceTableKeyFuncs> FNamespacesTable;

		FNamespacesTable NamespacesTable;

	public:
		/** Finds the keys table for the specified namespace and the display string entry for the specified namespace and key combination. If not found, the out parameters are set to null. */
		void Find(const FString& InNamespace, FKeysTable*& OutKeysTableForNamespace, const FString& InKey, FDisplayStringEntry*& OutDisplayStringEntry);
		/** Finds the keys table for the specified namespace and the display string entry for the specified namespace and key combination. If not found, the out parameters are set to null. */
		void Find(const FString& InNamespace, const FKeysTable*& OutKeysTableForNamespace, const FString& InKey, const FDisplayStringEntry*& OutDisplayStringEntry) const;
	};

	/** Simple data structure containing the name of the namespace and key associated with a display string, for use in looking up namespace and key from a display string. */
	struct FNamespaceKeyEntry
	{
		FString Namespace;
		FString Key;

		FNamespaceKeyEntry(const FString& InNamespace, const FString& InKey)
			: Namespace(InNamespace)
			, Key(InKey)
		{}
	};
	typedef TMap<FTextDisplayStringRef, FNamespaceKeyEntry> FNamespaceKeyLookupTable;

private:
	bool bIsInitialized;

	FCriticalSection SynchronizationObject;
	FDisplayStringLookupTable DisplayStringLookupTable;
	FNamespaceKeyLookupTable NamespaceKeyLookupTable;
	TMap<FTextDisplayStringRef, uint16> LocalTextRevisions;
	uint16 TextRevisionCounter;

private:
	FTextLocalizationManager() 
		: bIsInitialized(false)
		, SynchronizationObject()
		, TextRevisionCounter(0)
	{}

public:
	/** Singleton accessor */
	static FTextLocalizationManager& Get();

	/**	Finds and returns the display string with the given namespace and key, if it exists.
	 *	Additionally, if a source string is specified and the found localized display string was not localized from that source string, null will be returned. */
	FTextDisplayStringPtr FindDisplayString(const FString& Namespace, const FString& Key, const FString* const SourceString = nullptr);

	/**	Returns a display string with the given namespace and key.
	 *	If no display string exists, it will be created using the source string or an empty string if no source string is provided.
	 *	If a display string exists ...
	 *		... but it was not localized from the specified source string, the display string will be set to the specified source and returned.
	 *		... and it was localized from the specified source string (or none was provided), the display string will be returned.
	*/
	FTextDisplayStringRef GetDisplayString(const FString& Namespace, const FString& Key, const FString* const SourceString);

	/** If an entry exists for the specified namespace and key, returns true and provides the localization resource identifier from which it was loaded. Otherwise, returns false. */
	bool GetLocResID(const FString& Namespace, const FString& Key, FString& OutLocResId);

	/**	Finds the namespace and key associated with the specified display string.
	 *	Returns true if found and sets the out parameters. Otherwise, returns false.
	 */
	bool FindNamespaceAndKeyFromDisplayString(const FTextDisplayStringRef& InDisplayString, FString& OutNamespace, FString& OutKey);
	
	/**
	 * Attempts to find a local revision history for the given display string.
	 * This will only be set if the display string has been changed since the localization manager version has been changed (eg, if it has been edited while keeping the same key).
	 * @return The local revision, or 0 if there have been no changes since a global history change.
	 */
	uint16 GetLocalRevisionForDisplayString(const FTextDisplayStringRef& InDisplayString);

	/**	Attempts to register the specified display string, associating it with the specified namespace and key.
	 *	Returns true if the display string has been or was already associated with the namespace and key.
	 *	Returns false if the display string was already associated with another namespace and key or the namespace and key are already in use by another display string.
	 */
	bool AddDisplayString(const FTextDisplayStringRef& DisplayString, const FString& Namespace, const FString& Key);

	/**
	 * Updates the underlying value of a display string and associates it with a specified namespace and key, then returns true.
	 * If the namespace and key are already in use by another display string, no changes occur and false is returned.
	 */
	bool UpdateDisplayString(const FTextDisplayStringRef& DisplayString, const FString& Value, const FString& Namespace, const FString& Key);

	/** Loads localizations for the current culture based on a configuration file specifying which manifests and archives to use. Effectively generates a localization resource in memory. */
	void LoadFromManifestAndArchives(const FString& ConfigFilePath, IInternationalizationArchiveSerializer& ArchiveSerializer, IInternationalizationManifestSerializer& ManifestSerializer);

	/** Updates display string entries and adds new display string entries based on localizations found in a specified localization resource. */
	void UpdateFromLocalizationResource(const FString& LocalizationResourceFilePath);

	/** Reloads resources for the current culture. */
	void RefreshResources();

	/**	Returns the current text revision number. This value can be cached when caching information from the text localization manager.
	 *	If the revision does not match, cached information may be invalid and should be recached. */
	uint16 GetTextRevision() const { return TextRevisionCounter; }

	/** Event type for immediately reacting to changes in display strings for text. */
	DECLARE_EVENT(FTextLocalizationManager, FTextRevisionChangedEvent)
	FTextRevisionChangedEvent OnTextRevisionChangedEvent;

private:
	/** Callback for changes in culture. Loads the new culture's localization resources. */
	void OnCultureChanged();

	/** Loads localization resources for the specified culture, optionally loading localization resources that are editor-specific or game-specific. */
	void LoadLocalizationResourcesForCulture(const FString& CultureName, const bool ShouldLoadEditor, const bool ShouldLoadGame);

	/** Updates display string entries and adds new display string entries based on provided localizations. */
	void UpdateFromLocalizations(const TArray<FLocalizationEntryTracker>& LocalizationEntryTrackers);

	/** Dirties the local revision counter for the given display string by incrementing it (or adding it) */
	void DirtyLocalRevisionForDisplayString(const FTextDisplayStringRef& InDisplayString);

	/** Dirties the text revision counter by incrementing it, causing a revision mismatch for any information cached before this happens.  */
	void DirtyTextRevision();
};