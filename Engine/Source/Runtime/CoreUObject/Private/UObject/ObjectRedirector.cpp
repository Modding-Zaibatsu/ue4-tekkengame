// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UnRedirector.cpp: Object redirector implementation.
=============================================================================*/

#include "CoreUObjectPrivate.h"

/*-----------------------------------------------------------------------------
	UObjectRedirector
-----------------------------------------------------------------------------*/


// If this object redirector is pointing to an object that won't be serialized anyway, set the RF_Transient flag
// so that this redirector is also removed from the package.
void UObjectRedirector::PreSave(const class ITargetPlatform* TargetPlatform)
{
	if (DestinationObject == NULL
	||	DestinationObject->HasAnyFlags(RF_Transient)
	||	DestinationObject->IsIn(GetTransientPackage()) )
	{
		Modify();
		SetFlags(RF_Transient);

		if ( DestinationObject != NULL )
		{
			DestinationObject->Modify();
			DestinationObject->SetFlags(RF_Transient);
		}
	}
}

void UObjectRedirector::Serialize( FArchive& Ar )
{
	Super::Serialize(Ar);

	Ar << DestinationObject;
}

bool UObjectRedirector::NeedsLoadForClient() const
{
	return false;
}

bool UObjectRedirector::NeedsLoadForEditorGame() const
{
	return true;
}

void UObjectRedirector::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	FString DestVal;
	if ( DestinationObject != NULL )
	{
		DestVal = FString::Printf(TEXT("%s'%s'"), *DestinationObject->GetClass()->GetName(), *DestinationObject->GetPathName());
	}
	else
	{
		DestVal = TEXT("None");
	}

	OutTags.Add(FAssetRegistryTag("DestinationObject", DestVal, UObject::FAssetRegistryTag::TT_Alphabetical));
}

/**
 * Callback for retrieving a textual representation of natively serialized properties.  Child classes should implement this method if they wish
 * to have natively serialized property values included in things like diffcommandlet output.
 *
 * @param	out_PropertyValues	receives the property names and values which should be reported for this object.  The map's key should be the name of
 *								the property and the map's value should be the textual representation of the property's value.  The property value should
 *								be formatted the same way that UProperty::ExportText formats property values (i.e. for arrays, wrap in quotes and use a comma
 *								as the delimiter between elements, etc.)
 * @param	ExportFlags			bitmask of EPropertyPortFlags used for modifying the format of the property values
 *
 * @return	return true if property values were added to the map.
 */
bool UObjectRedirector::GetNativePropertyValues( TMap<FString,FString>& out_PropertyValues, uint32 ExportFlags/*=0*/ ) const
{
	UObject* StopOuter = NULL;

	// determine how the caller wants object values to be formatted
	if ( (ExportFlags&PPF_SimpleObjectText) != 0 )
	{
		StopOuter = GetOutermost();
	}

	out_PropertyValues.Add(TEXT("DestinationObject"), DestinationObject->GetFullName(StopOuter));
	return true;
}

IMPLEMENT_CORE_INTRINSIC_CLASS(UObjectRedirector, UObject,
	{
		Class->EmitObjectReference(STRUCT_OFFSET(UObjectRedirector, DestinationObject), TEXT("DestinationObject"));
	}
);

/**
 * Constructor for the callback device, will register itself with the RedirectorFollowed delegate
 *
 * @param InObjectPathNameToMatch Full pathname of the object refrence that is being compiled by script
 */
FScopedRedirectorCatcher::FScopedRedirectorCatcher(const FString& InObjectPathNameToMatch)
: ObjectPathNameToMatch(InObjectPathNameToMatch)
, bWasRedirectorFollowed(false)
{
	// register itself on construction to see if the object is a redirector 
	FCoreUObjectDelegates::RedirectorFollowed.AddRaw(this, &FScopedRedirectorCatcher::OnRedirectorFollowed);
}

/**
 * Destructor. Will unregister the callback
 */
FScopedRedirectorCatcher::~FScopedRedirectorCatcher()
{
	// register itself on construction to see if the object is a redirector 
	FCoreUObjectDelegates::RedirectorFollowed.RemoveAll(this);
}


/**
 * Responds to FCoreDelegates::RedirectorFollowed. Records all followed redirections
 * so they can be cleaned later.
 *
 * @param InType Callback type (should only be FCoreDelegates::RedirectorFollowed)
 * @param InString Name of the package that pointed to the redirect
 * @param InObject The UObjectRedirector that was followed
 */
void FScopedRedirectorCatcher::OnRedirectorFollowed( const FString& InString, UObject* InObject)
{
	// this needs to be the redirector
	check(dynamic_cast<UObjectRedirector*>(InObject));

	// if the path of the redirector was the same as the path to the object constant
	// being compiled, then the script code has a text reference to a redirector, 
	// which will cause FixupRedirects to break the reference
	if ( InObject->GetPathName() == ObjectPathNameToMatch )
	{
		bWasRedirectorFollowed = true;
	}
}
