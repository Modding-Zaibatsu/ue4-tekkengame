// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.
#pragma once


class FLocMetadataObject;


struct CORE_API FManifestContext
{
public:
	FManifestContext()
		: bIsOptional(false)
	{}

	/** Copy ctor */
	FManifestContext( const FManifestContext& Other );

	FManifestContext& operator=( const FManifestContext& Other );
	bool operator==(const FManifestContext& Other) const;
	inline bool operator!=(const FManifestContext& Other) const { return !(*this == Other); }
	bool operator<(const FManifestContext& Other) const;

public:

	FString Key;
	FString SourceLocation;
	bool bIsOptional;

	TSharedPtr<FLocMetadataObject> InfoMetadataObj;
	TSharedPtr<FLocMetadataObject> KeyMetadataObj;

};


struct CORE_API FLocItem
{
public:
	FLocItem( const FString& InSourceText )
		: Text( InSourceText )
		, MetadataObj( NULL )
	{ }

	/** Copy ctor */
	FLocItem( const FLocItem& Other );

	FLocItem& operator=( const FLocItem& Other );
	bool operator==( const FLocItem& Other ) const;
	inline bool operator!=(const FLocItem& Other) const { return !(*this == Other); }
	bool operator<( const FLocItem& Other ) const;

	/** Similar functionality to == operator but ensures everything matches(ex. ignores COMPARISON_MODIFIER_PREFIX on metadata). */
	bool IsExactMatch( const FLocItem& Other ) const;
public:
	FString Text;
	TSharedPtr<FLocMetadataObject> MetadataObj;
};


class FManifestEntry
{
public:
	FManifestEntry(const FString& InNamespace, const FLocItem& InSource)
		: Namespace(InNamespace)
		, Source(InSource)
		, Contexts()
	{
	}

	CORE_API FManifestContext* FindContext(const FString& ContextKey, const TSharedPtr<FLocMetadataObject>& KeyMetadata = nullptr);
	CORE_API const FManifestContext* FindContext(const FString& ContextKey, const TSharedPtr<FLocMetadataObject>& KeyMetadata = nullptr) const;

	CORE_API FManifestContext* FindContextByKey(const FString& ContextKey);
	CORE_API const FManifestContext* FindContextByKey(const FString& ContextKey) const;

	const FString Namespace;
	const FLocItem Source;
	TArray<FManifestContext> Contexts;

private:
	const FManifestContext* FindContextImpl(const FString& ContextKey, const TSharedPtr<FLocMetadataObject>& KeyMetadata) const;
	const FManifestContext* FindContextByKeyImpl(const FString& ContextKey) const;
};


struct FInternationalizationManifestSourceTextKeyFuncs : BaseKeyFuncs<TSharedRef< FManifestEntry >, FString, true>
{
	static FORCEINLINE const FString& GetSetKey(const TPair<FString,TSharedRef< FManifestEntry >>& Element)
	{
		return Element.Key;
	}
	static FORCEINLINE bool Matches(const FString& A, const FString& B)
	{
		return A.Equals( B, ESearchCase::CaseSensitive );
	}
	static FORCEINLINE uint32 GetKeyHash(const FString& Key)
	{
		return FCrc::StrCrc32<TCHAR>(*Key);
	}
};
typedef TMultiMap< FString, TSharedRef< FManifestEntry >, FDefaultSetAllocator, FInternationalizationManifestSourceTextKeyFuncs > TManifestEntryBySourceTextContainer;


struct FInternationalizationManifestContextIdKeyFuncs : BaseKeyFuncs<TSharedRef< FManifestEntry >, FString, true>
{
	static FORCEINLINE const FString& GetSetKey(const TPair<FString,TSharedRef< FManifestEntry >>& Element)
	{
		return Element.Key;
	}
	static FORCEINLINE bool Matches(const FString& A, const FString& B)
	{
		return A.Equals( B, ESearchCase::CaseSensitive );
	}
	static FORCEINLINE uint32 GetKeyHash(const FString& Key)
	{
		return FCrc::StrCrc32<TCHAR>(*Key);
	}
};
typedef TMultiMap< FString, TSharedRef< FManifestEntry >, FDefaultSetAllocator, FInternationalizationManifestContextIdKeyFuncs > TManifestEntryByContextIdContainer;


class CORE_API FInternationalizationManifest 
{
public:
	enum EFormatVersion
	{
		Initial = 0,
		EscapeFixes,

		LatestPlusOne,
		Latest = LatestPlusOne - 1
	};

	//Default constructor
	FInternationalizationManifest( )
		: FormatVersion(static_cast<int32>(EFormatVersion::Latest))
	{
	}

	/**
	 * Adds a manifest entry.
	 *
	 * @return Returns true if add was successful or a matching entry already exists, false is only returned in the case where a duplicate context was found with different text.
	 */
	bool AddSource( const FString& Namespace, const FLocItem& Source, const FManifestContext& Context );

	TSharedPtr< FManifestEntry > FindEntryBySource( const FString& Namespace, const FLocItem& Source ) const;

	TSharedPtr< FManifestEntry > FindEntryByContext( const FString& Namespace, const FManifestContext& Context ) const;

	TSharedPtr< FManifestEntry > FindEntryByKey( const FString& Namespace, const FString& Key, const FString* SourceText = nullptr ) const;

	TManifestEntryByContextIdContainer::TConstIterator GetEntriesByContextIdIterator() const
	{
		return EntriesByContextId.CreateConstIterator();
	}

	int32 GetNumEntriesByContextId() const
	{
		return EntriesByContextId.Num();
	}

	TManifestEntryBySourceTextContainer::TConstIterator GetEntriesBySourceTextIterator() const
	{
		return EntriesBySourceText.CreateConstIterator();
	}

	int32 GetNumEntriesBySourceText() const
	{
		return EntriesBySourceText.Num();
	}

	void UpdateEntry(const TSharedRef<FManifestEntry>& OldEntry, TSharedRef<FManifestEntry>& NewEntry);

	void SetFormatVersion(const EFormatVersion Version)
	{
		FormatVersion = static_cast<int>(Version);
	}

	EFormatVersion GetFormatVersion() const
	{
		return EFormatVersion(FormatVersion);
	}

	friend class IInternationalizationManifestSerializer;

private:

	int32 FormatVersion;
	TManifestEntryBySourceTextContainer EntriesBySourceText;
	TManifestEntryByContextIdContainer EntriesByContextId;
};
