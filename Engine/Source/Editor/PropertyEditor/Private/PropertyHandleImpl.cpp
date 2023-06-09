// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "PropertyEditorPrivatePCH.h"
#include "PropertyHandleImpl.h"
#include "PropertyNode.h"
#include "PropertyEditorHelpers.h"
#include "ObjectPropertyNode.h"
#include "StructurePropertyNode.h"
#include "EditorSupportDelegates.h"
#include "ScopedTransaction.h"
#include "Editor/UnrealEd/Public/Kismet2/KismetEditorUtilities.h"
#include "IPropertyUtilities.h"
#include "PropertyEditor.h"
#include "Engine/Selection.h"
#include "Engine/UserDefinedStruct.h"

DEFINE_LOG_CATEGORY_STATIC(LogPropertyView, Log, All);

FPropertyValueImpl::FPropertyValueImpl( TSharedPtr<FPropertyNode> InPropertyNode, FNotifyHook* InNotifyHook, TSharedPtr<IPropertyUtilities> InPropertyUtilities )
	: PropertyNode( InPropertyNode )
	, PropertyUtilities( InPropertyUtilities )
	, NotifyHook( InNotifyHook )
	, bInteractiveChangeInProgress( false )
{

}

FPropertyValueImpl::~FPropertyValueImpl()
{
}

void FPropertyValueImpl::EnumerateObjectsToModify( FPropertyNode* InPropertyNode, const EnumerateObjectsToModifyFuncRef& InObjectsToModifyCallback ) const
{
	// Find the parent object node which contains offset addresses for reading a property value on an object
	FComplexPropertyNode* ComplexNode = InPropertyNode->FindComplexParent();
	if (ComplexNode)
	{
		const bool bIsStruct = FComplexPropertyNode::EPT_StandaloneStructure == ComplexNode->GetPropertyType();
		const int32 NumInstances = ComplexNode->GetInstancesNum();
		for (int32 Index = 0; Index < NumInstances; ++Index)
		{
			UObject*	Object = ComplexNode->GetInstanceAsUObject(Index).Get();
			uint8*		Addr = InPropertyNode->GetValueBaseAddress(ComplexNode->GetMemoryOfInstance(Index));
			if (!InObjectsToModifyCallback(FObjectBaseAddress(Object, Addr, bIsStruct), Index, NumInstances))
			{
				break;
			}
		}
	}
}

void FPropertyValueImpl::GetObjectsToModify( TArray<FObjectBaseAddress>& ObjectsToModify, FPropertyNode* InPropertyNode ) const
{
	EnumerateObjectsToModify(InPropertyNode, [&](const FObjectBaseAddress& ObjectToModify, const int32 ObjectIndex, const int32 NumObjects) -> bool
	{
		if (ObjectIndex == 0)
		{
			ObjectsToModify.Reserve(ObjectsToModify.Num() + NumObjects);
		}
		ObjectsToModify.Add(ObjectToModify);
		return true;
	});
}

FPropertyAccess::Result FPropertyValueImpl::GetPropertyValueString( FString& OutString, FPropertyNode* InPropertyNode, const bool bAllowAlternateDisplayValue ) const
{
	FPropertyAccess::Result Result = FPropertyAccess::Success;

	uint8* ValueAddress = nullptr;
	FReadAddressList ReadAddresses;
	bool bAllValuesTheSame = InPropertyNode->GetReadAddress( !!InPropertyNode->HasNodeFlags(EPropertyNodeFlags::SingleSelectOnly), ReadAddresses, false, true );

	if( (ReadAddresses.Num() > 0 && bAllValuesTheSame) || ReadAddresses.Num() == 1 ) 
	{
		ValueAddress = ReadAddresses.GetAddress(0);

		if( ValueAddress != nullptr )
		{
			UProperty* Property = InPropertyNode->GetProperty();

			// Check for bogus data
			if( Property != nullptr && InPropertyNode->GetParentNode() != nullptr )
			{
				Property->ExportText_Direct(OutString, ValueAddress, ValueAddress, nullptr, PPF_PropertyWindow );

				UByteProperty* ByteProperty = Cast<UByteProperty>(Property);
				if ( ByteProperty != nullptr && ByteProperty->Enum != nullptr )
				{
					const uint8 EnumValue = ByteProperty->GetPropertyValue(ValueAddress);

					if (ByteProperty->Enum->IsValidEnumValue(EnumValue))
					{
						// See if we specified an alternate name for this value using metadata
						OutString = ByteProperty->Enum->GetDisplayNameTextByValue(EnumValue).ToString();
						if(!bAllowAlternateDisplayValue || OutString.Len() == 0) 
						{
							OutString = ByteProperty->Enum->GetEnumNameStringByValue(EnumValue);
						}
					}
					else
					{
						Result = FPropertyAccess::Fail;
					}
				}
			}
			else
			{
				Result = FPropertyAccess::Fail;
			}
		}

	}
	else
	{
		Result = ReadAddresses.Num() > 1 ? FPropertyAccess::MultipleValues : FPropertyAccess::Fail;
	}

	return Result;
}

FPropertyAccess::Result FPropertyValueImpl::GetPropertyValueText( FText& OutText, FPropertyNode* InPropertyNode, const bool bAllowAlternateDisplayValue ) const
{
	FPropertyAccess::Result Result = FPropertyAccess::Success;

	uint8* ValueAddress = nullptr;
	FReadAddressList ReadAddresses;
	bool bAllValuesTheSame = InPropertyNode->GetReadAddress( !!InPropertyNode->HasNodeFlags(EPropertyNodeFlags::SingleSelectOnly), ReadAddresses, false, true );

	if( ReadAddresses.Num() > 0 && InPropertyNode->GetProperty() != nullptr && (bAllValuesTheSame || ReadAddresses.Num() == 1) ) 
	{
		ValueAddress = ReadAddresses.GetAddress(0);

		if( ValueAddress != nullptr )
		{
			UProperty* Property = InPropertyNode->GetProperty();

			if(Property->IsA(UTextProperty::StaticClass()))
			{
				OutText = Cast<UTextProperty>(Property)->GetPropertyValue(ValueAddress);
			}
			else
			{
				FString ExportedTextString;
				Property->ExportText_Direct(ExportedTextString, ValueAddress, ValueAddress, nullptr, PPF_PropertyWindow );

				UByteProperty* ByteProperty = Cast<UByteProperty>(Property);
				if ( ByteProperty != nullptr && ByteProperty->Enum != nullptr )
				{
					const uint8 EnumValueIndex = ByteProperty->GetPropertyValue(ValueAddress);
					
					if (EnumValueIndex >= 0 && EnumValueIndex < ByteProperty->Enum->NumEnums())
					{
						// See if we specified an alternate name for this value using metadata
						OutText = ByteProperty->Enum->GetDisplayNameText(EnumValueIndex);
						if(!bAllowAlternateDisplayValue || OutText.IsEmpty()) 
						{
							OutText = ByteProperty->Enum->GetEnumText(EnumValueIndex);
						}
					}
					else
					{
						Result = FPropertyAccess::Fail;
					}
				}
				else
				{
					OutText = FText::FromString(ExportedTextString);
				}
			}
		}

	}
	else
	{
		Result = ReadAddresses.Num() > 1 ? FPropertyAccess::MultipleValues : FPropertyAccess::Fail;
	}

	return Result;
}

FPropertyAccess::Result FPropertyValueImpl::GetValueData( void*& OutAddress ) const
{
	FPropertyAccess::Result Res = FPropertyAccess::Fail;
	OutAddress = nullptr;
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid() )
	{
		uint8* ValueAddress = nullptr;
		FReadAddressList ReadAddresses;
		bool bAllValuesTheSame = PropertyNodePin->GetReadAddress( !!PropertyNodePin->HasNodeFlags(EPropertyNodeFlags::SingleSelectOnly), ReadAddresses, false, true );

		if( (ReadAddresses.Num() > 0 && bAllValuesTheSame) || ReadAddresses.Num() == 1 ) 
		{
			ValueAddress = ReadAddresses.GetAddress(0);
			const UProperty* Property = PropertyNodePin->GetProperty();
			if (ValueAddress && Property)
			{
				const int32 Index = 0;
				OutAddress = ValueAddress + Index * Property->ElementSize;
				Res = FPropertyAccess::Success;
			}
		}
		else if (ReadAddresses.Num() > 1)
		{
			Res = FPropertyAccess::MultipleValues;
		}
	}

	return Res;
}

FPropertyAccess::Result FPropertyValueImpl::ImportText( const FString& InValue, EPropertyValueSetFlags::Type Flags )
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid() && !PropertyNodePin->IsEditConst() )
	{
		return ImportText( InValue, PropertyNodePin.Get(), Flags );
	}

	// The property node is not valid or cant be set.  If not valid it probably means this value was stored somewhere and selection changed causing the node to be destroyed
	return FPropertyAccess::Fail;
}

FString FPropertyValueImpl::GetPropertyValueArray() const
{
	FString String;
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid() )
	{
		uint8* Addr = nullptr;
		FReadAddressList ReadAddresses;
		UProperty* NodeProperty = PropertyNodePin->GetProperty();

		bool bSingleValue = PropertyNodePin->GetReadAddress( !!PropertyNodePin->HasNodeFlags(EPropertyNodeFlags::SingleSelectOnly), ReadAddresses, false );

		Addr = ReadAddresses.GetAddress(0);
		if( bSingleValue && Addr )
		{
			if ( NodeProperty != nullptr && Cast<UArrayProperty>(NodeProperty) != nullptr )
			{
				String = FString::Printf( TEXT("%(%d)"), FScriptArrayHelper::Num(Addr) );
			}
			else
			{
				String = FString::Printf( TEXT("%[%d]"), NodeProperty->ArrayDim );
			}
		}
		else if( !bSingleValue )
		{
			String = NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values").ToString();
		}
	}

	return String;
}

bool FPropertyValueImpl::SendTextToObjectProperty( const FString& Text, EPropertyValueSetFlags::Type Flags )
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid() )
	{
		FComplexPropertyNode* ParentNode = PropertyNodePin->FindComplexParent();

		// If more than one object is selected, an empty field indicates their values for this property differ.
		// Don't send it to the objects value in this case (if we did, they would all get set to None which isn't good).
		if ((!ParentNode || ParentNode->GetInstancesNum() > 1) && !Text.Len())
		{
			return false;
		}

		return ImportText( Text, PropertyNodePin.Get(), Flags ) != FPropertyAccess::Fail;

	}

	return false;
}

void FPropertyValueImpl::GenerateArrayIndexMapToObjectNode( TMap<FString,int32>& OutArrayIndexMap, FPropertyNode* PropertyNode )
{
	if( PropertyNode )
	{
		OutArrayIndexMap.Empty();
		for (FPropertyNode* IterationNode = PropertyNode; (IterationNode != nullptr) && (IterationNode->AsObjectNode() == nullptr); IterationNode = IterationNode->GetParentNode())
		{
			UProperty* Property = IterationNode->GetProperty();
			if (Property)
			{
				//since we're starting from the lowest level, we have to take the first entry.  In the case of an array, the entries and the array itself have the same name, except the parent has an array index of -1
				if (!OutArrayIndexMap.Contains(Property->GetName()))
				{
					OutArrayIndexMap.Add(Property->GetName(), IterationNode->GetArrayIndex());
				}
			}
		}
	}
}

FPropertyAccess::Result FPropertyValueImpl::ImportText( const FString& InValue, FPropertyNode* InPropertyNode, EPropertyValueSetFlags::Type Flags )
{
	TArray<FObjectBaseAddress> ObjectsToModify;
	GetObjectsToModify( ObjectsToModify, InPropertyNode );

	TArray<FString> Values;
	for( int32 ObjectIndex = 0 ; ObjectIndex < ObjectsToModify.Num() ; ++ObjectIndex )
	{
		if (ObjectsToModify[ObjectIndex].Object != nullptr || ObjectsToModify[ObjectIndex].bIsStruct)
		{
			Values.Add( InValue );
		}
	}

	FPropertyAccess::Result Result = FPropertyAccess::Fail;
	if( Values.Num() > 0 )
	{
		Result = ImportText( ObjectsToModify, Values, InPropertyNode, Flags );
	}

	return Result;
}

FPropertyAccess::Result FPropertyValueImpl::ImportText( const TArray<FObjectBaseAddress>& InObjects, const TArray<FString>& InValues, FPropertyNode* InPropertyNode, EPropertyValueSetFlags::Type Flags )
{
	check(InPropertyNode);
	UProperty* NodeProperty = InPropertyNode->GetProperty();

	FPropertyAccess::Result Result = FPropertyAccess::Success;

	if( !NodeProperty )
	{
		// The property has been deleted out from under this
		Result = FPropertyAccess::Fail;
	}
	else if( NodeProperty->IsA<UObjectProperty>() || NodeProperty->IsA<UNameProperty>() )
	{
		// certain properties have requirements on the size of string values that can be imported.  Search for strings that are too large.
		for( int32 ValueIndex = 0; ValueIndex < InValues.Num(); ++ValueIndex )
		{
			if( InValues[ValueIndex].Len() > NAME_SIZE )
			{
				Result = FPropertyAccess::Fail;
				break;
			}
		}
	}

	if( Result != FPropertyAccess::Fail )
	{
		UWorld* OldGWorld = nullptr;

		bool bIsGameWorld = false;
		// If the object we are modifying is in the PIE world, than make the PIE world the active
		// GWorld.  Assumes all objects managed by this property window belong to the same world.
		if (UPackage* ObjectPackage = (InObjects[0].Object ? InObjects[0].Object->GetOutermost() : nullptr))
		{
			const bool bIsPIEPackage = ObjectPackage->HasAnyPackageFlags(PKG_PlayInEditor);
			if (GUnrealEd && GUnrealEd->PlayWorld && bIsPIEPackage && !GIsPlayInEditorWorld)
			{
				OldGWorld = SetPlayInEditorWorld(GUnrealEd->PlayWorld);
				bIsGameWorld = true;
			}
		}
		///////////////

		// Send the values and assemble a list of pre/posteditchange values.
		bool bNotifiedPreChange = false;
		UObject *NotifiedObj = nullptr;
		TArray< TMap<FString,int32> > ArrayIndicesPerObject;

		const bool bTransactable = (Flags & EPropertyValueSetFlags::NotTransactable) == 0;
		const bool bFinished = ( Flags & EPropertyValueSetFlags::InteractiveChange) == 0;
		
		for ( int32 ObjectIndex = 0 ; ObjectIndex < InObjects.Num() ; ++ObjectIndex )
		{	
			const FObjectBaseAddress& Cur = InObjects[ ObjectIndex ];
			if (Cur.BaseAddress == nullptr)
			{
				//Fully abort this procedure.  The data has changed out from under the object
				Result = FPropertyAccess::Fail;
				break;
			}

			// Cache the value of the property before modifying it.
			FString PreviousValue;
			NodeProperty->ExportText_Direct(PreviousValue, Cur.BaseAddress, Cur.BaseAddress, nullptr, 0 );

			// If this property is the inner-property of an array, cache the current value as well
			FString PreviousArrayValue;
			if (Cur.Object)
			{
				FPropertyNode* ParentNode = InPropertyNode->GetParentNode();
				if (ParentNode != nullptr && ParentNode->GetProperty() && ParentNode->GetProperty()->IsA(UArrayProperty::StaticClass()))
				{
					UArrayProperty* ArrayProp = Cast<UArrayProperty>(ParentNode->GetProperty());
					if (ArrayProp->Inner == NodeProperty)
					{
						uint8* Addr = ParentNode->GetValueBaseAddress((uint8*)Cur.Object);

						ArrayProp->ExportText_Direct(PreviousArrayValue, Addr, Addr, nullptr, 0);
					}
				}
			}

			// Check if we need to call PreEditChange on all objects.
			// Remove quotes from the original value because FName properties  
			// are wrapped in quotes before getting here. This causes the 
			// string comparison to fail even when the name is unchanged. 
			if ( !bNotifiedPreChange && ( FCString::Strcmp(*InValues[ObjectIndex].TrimQuotes(), *PreviousValue) != 0 || ( bFinished && bInteractiveChangeInProgress ) ) )
			{
				bNotifiedPreChange = true;
				NotifiedObj = Cur.Object;

				if (!bInteractiveChangeInProgress)
				{
					// Begin a transaction only if we need to call PreChange
					if (GEditor && bTransactable)
					{
						GEditor->BeginTransaction(TEXT("PropertyEditor"), FText::Format(NSLOCTEXT("PropertyEditor", "EditPropertyTransaction", "Edit {0}"), InPropertyNode->GetDisplayName()), NodeProperty);
					}
				}

				InPropertyNode->NotifyPreChange(NodeProperty, NotifyHook);

				bInteractiveChangeInProgress = (Flags & EPropertyValueSetFlags::InteractiveChange) != 0;
			}

			// Set the new value.
			const TCHAR* NewValue = *InValues[ObjectIndex];
			NodeProperty->ImportText( NewValue, Cur.BaseAddress, 0, Cur.Object );

			if (Cur.Object)
			{
				// Cache the value of the property after having modified it.
				FString ValueAfterImport;
				NodeProperty->ExportText_Direct(ValueAfterImport, Cur.BaseAddress, Cur.BaseAddress, nullptr, 0);

				if ((Cur.Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) ||
					(Cur.Object->HasAnyFlags(RF_DefaultSubObject) && Cur.Object->GetOuter()->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))) &&
					!bIsGameWorld)
				{
					InPropertyNode->PropagatePropertyChange(Cur.Object, NewValue, PreviousArrayValue.IsEmpty() ? PreviousValue : PreviousArrayValue);
				}

				// If the values before and after setting the property differ, mark the object dirty.
				if (FCString::Strcmp(*PreviousValue, *ValueAfterImport) != 0)
				{
					Cur.Object->MarkPackageDirty();
				}
			}

			//add on array index so we can tell which entry just changed
			ArrayIndicesPerObject.Add(TMap<FString,int32>());
			FPropertyValueImpl::GenerateArrayIndexMapToObjectNode( ArrayIndicesPerObject[ObjectIndex], InPropertyNode );
		}

		// If PreEditChange was called, so should PostEditChange.
		if ( bNotifiedPreChange )
		{
			// Call PostEditChange on all objects.
			FPropertyChangedEvent ChangeEvent(NodeProperty, bFinished ? EPropertyChangeType::ValueSet : EPropertyChangeType::Interactive );
			ChangeEvent.SetArrayIndexPerObject(ArrayIndicesPerObject);

			InPropertyNode->NotifyPostChange( ChangeEvent, NotifyHook );

			if (bFinished)
			{
				bInteractiveChangeInProgress = false;

				if (bTransactable)
				{
					// End the transaction if we called PreChange
					GEditor->EndTransaction();
				}
			}
		}

		if (OldGWorld)
		{
			// restore the original (editor) GWorld
			RestoreEditorWorld( OldGWorld );
		}

		if (PropertyUtilities.IsValid() && !bInteractiveChangeInProgress)
		{
			FPropertyChangedEvent ChangeEvent(NodeProperty, bFinished ? EPropertyChangeType::ValueSet : EPropertyChangeType::Interactive);
			InPropertyNode->FixPropertiesInEvent(ChangeEvent);
			PropertyUtilities.Pin()->NotifyFinishedChangingProperties(ChangeEvent);
		}

		// Redraw
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}

	return Result;
}

void FPropertyValueImpl::EnumerateRawData( const IPropertyHandle::EnumerateRawDataFuncRef& InRawDataCallback )
{
	EnumerateObjectsToModify(PropertyNode.Pin().Get(), [&](const FObjectBaseAddress& ObjectToModify, const int32 ObjectIndex, const int32 NumObjects) -> bool
	{
		return InRawDataCallback(ObjectToModify.BaseAddress, ObjectIndex, NumObjects);
	});
}

void FPropertyValueImpl::EnumerateConstRawData( const IPropertyHandle::EnumerateConstRawDataFuncRef& InRawDataCallback ) const
{
	EnumerateObjectsToModify(PropertyNode.Pin().Get(), [&](const FObjectBaseAddress& ObjectToModify, const int32 ObjectIndex, const int32 NumObjects) -> bool
	{
		return InRawDataCallback(ObjectToModify.BaseAddress, ObjectIndex, NumObjects);
	});
}

void FPropertyValueImpl::AccessRawData( TArray<void*>& RawData )
{
	RawData.Empty();
	EnumerateObjectsToModify(PropertyNode.Pin().Get(), [&](const FObjectBaseAddress& ObjectToModify, const int32 ObjectIndex, const int32 NumObjects) -> bool
	{
		if (ObjectIndex == 0)
		{
			RawData.Reserve(NumObjects);
		}
		RawData.Add(ObjectToModify.BaseAddress);
		return true;
	});
}

void FPropertyValueImpl::AccessRawData( TArray<const void*>& RawData ) const
{
	RawData.Empty();
	EnumerateObjectsToModify(PropertyNode.Pin().Get(), [&](const FObjectBaseAddress& ObjectToModify, const int32 ObjectIndex, const int32 NumObjects) -> bool
	{
		if (ObjectIndex == 0)
		{
			RawData.Reserve(NumObjects);
		}
		RawData.Add(ObjectToModify.BaseAddress);
		return true;
	});
}

void FPropertyValueImpl::SetOnPropertyValueChanged( const FSimpleDelegate& InOnPropertyValueChanged )
{
	if( PropertyNode.IsValid() )
	{
		PropertyNode.Pin()->OnPropertyValueChanged().Add( InOnPropertyValueChanged );
	}
}

void FPropertyValueImpl::SetOnChildPropertyValueChanged( const FSimpleDelegate& InOnChildPropertyValueChanged )
{
	if( PropertyNode.IsValid() )
	{
		PropertyNode.Pin()->OnChildPropertyValueChanged().Add( InOnChildPropertyValueChanged );
	}
}

void FPropertyValueImpl::SetOnRebuildChildren( const FSimpleDelegate& InOnRebuildChildren )
{
	if( PropertyNode.IsValid() )
	{
		PropertyNode.Pin()->SetOnRebuildChildren( InOnRebuildChildren );
	}
}
/**
 * Gets the max valid index for a array property of an object
 * @param InObjectNode - The parent of the variable being clamped
 * @param InArrayName - The array name we're hoping to clamp to the extents of
 * @return LastValidEntry in the array (if the array is found)
 */
static int32 GetArrayPropertyLastValidIndex( FObjectPropertyNode* InObjectNode, const FString& InArrayName )
{
	int32 ClampMax = MAX_int32;

	check(InObjectNode->GetNumObjects()==1);
	UObject* ParentObject = InObjectNode->GetUObject(0);

	//find the associated property
	UProperty* FoundProperty = nullptr;
	for( TFieldIterator<UProperty> It(ParentObject->GetClass()); It; ++It )
	{
		UProperty* CurProp = *It;
		if (CurProp->GetName()==InArrayName)
		{
			FoundProperty = CurProp;
			break;
		}
	}

	if (FoundProperty && (FoundProperty->ArrayDim == 1))
	{
		UArrayProperty* ArrayProperty = CastChecked<UArrayProperty>( FoundProperty );
		if (ArrayProperty)
		{
			uint8* PropertyAddressBase = ArrayProperty->ContainerPtrToValuePtr<uint8>(ParentObject);
			ClampMax = FScriptArrayHelper::Num(PropertyAddressBase) - 1;
		}
		else
		{
			UE_LOG(LogPropertyNode, Warning, TEXT("The property (%s) passed for array clamping use is not an array.  Clamp will only ensure greater than zero."), *InArrayName);
		}
	}
	else
	{
		UE_LOG(LogPropertyNode, Warning, TEXT("The property (%s) passed for array clamping was not found.  Clamp will only ensure greater than zero."), *InArrayName);
	}

	return ClampMax;
}


FPropertyAccess::Result FPropertyValueImpl::GetValueAsString( FString& OutString ) const
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();

	FPropertyAccess::Result Res = FPropertyAccess::Success;

	if( PropertyNodePin.IsValid() )
	{
		Res = GetPropertyValueString( OutString, PropertyNodePin.Get(), false/*bAllowAlternateDisplayValue*/ );
	}
	else
	{
		Res = FPropertyAccess::Fail;
	}

	return Res;
}

FPropertyAccess::Result FPropertyValueImpl::GetValueAsDisplayString( FString& OutString ) const
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();

	FPropertyAccess::Result Res = FPropertyAccess::Success;

	if( PropertyNodePin.IsValid() )
	{
		Res = GetPropertyValueString( OutString, PropertyNodePin.Get(), true/*bAllowAlternateDisplayValue*/ );
	}
	else
	{
		Res = FPropertyAccess::Fail;
	}

	return Res;
}

FPropertyAccess::Result FPropertyValueImpl::GetValueAsText( FText& OutText ) const
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();

	FPropertyAccess::Result Res = FPropertyAccess::Success;

	if( PropertyNodePin.IsValid() )
	{
		Res = GetPropertyValueText( OutText, PropertyNodePin.Get(), false/*bAllowAlternateDisplayValue*/ );
	}
	else
	{
		Res = FPropertyAccess::Fail;
	}

	return Res;
}

FPropertyAccess::Result FPropertyValueImpl::GetValueAsDisplayText( FText& OutText ) const
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();

	FPropertyAccess::Result Res = FPropertyAccess::Success;

	if( PropertyNodePin.IsValid() )
	{
		Res = GetPropertyValueText( OutText, PropertyNodePin.Get(), true/*bAllowAlternateDisplayValue*/ );
	}
	else
	{
		Res = FPropertyAccess::Fail;
	}

	return Res;
}

FPropertyAccess::Result FPropertyValueImpl::SetValueAsString( const FString& InValue, EPropertyValueSetFlags::Type Flags )
{
	FPropertyAccess::Result Result = FPropertyAccess::Fail;

	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid() )
	{
		UProperty* NodeProperty = PropertyNodePin->GetProperty();

		FString Value = InValue;

		// Strip any leading underscores and spaces from names.
		if( NodeProperty && NodeProperty->IsA( UNameProperty::StaticClass() ) )
		{
			while ( true )
			{
				if ( Value.StartsWith( TEXT("_") ) )
				{
					// Strip leading underscores.
					do
					{
						Value = Value.Right( Value.Len()-1 );
					} while ( Value.StartsWith( TEXT("_") ) );
				}
				else if ( Value.StartsWith( TEXT(" ") ) )
				{
					// Strip leading spaces.
					do
					{
						Value = Value.Right( Value.Len()-1 );
					} while ( Value.StartsWith( TEXT(" ") ) );
				}
				else
				{
					// Starting with something valid -- break.
					break;
				}
			}
		}

		// If more than one object is selected, an empty field indicates their values for this property differ.
		// Don't send it to the objects value in this case (if we did, they would all get set to None which isn't good).
		FComplexPropertyNode* ParentNode = PropertyNodePin->FindComplexParent();

		FString PreviousValue;
		GetValueAsString( PreviousValue );

		const bool bDidValueChange = Value.Len() && (FCString::Strcmp(*PreviousValue, *Value) != 0);
		const bool bComingOutOfInteractiveChange = bInteractiveChangeInProgress && ( ( Flags & EPropertyValueSetFlags::InteractiveChange ) != EPropertyValueSetFlags::InteractiveChange );

		if ( ParentNode && ( ParentNode->GetInstancesNum() == 1 || bComingOutOfInteractiveChange || bDidValueChange ) )
		{
			ImportText( Value, PropertyNodePin.Get(), Flags );
		}

		Result = FPropertyAccess::Success;
	}

	return Result;
}

bool FPropertyValueImpl::SetObject( const UObject* NewObject, EPropertyValueSetFlags::Type Flags )
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid() )
	{
		FString ObjectPathName = NewObject ? NewObject->GetPathName() : TEXT("None");
		bool bResult = SendTextToObjectProperty( ObjectPathName, Flags );

		// @todo PropertyEditing:  This fails when it should not.  some UProperties remove the object class from the name and some dont. 
		// So comparing the path name to one that adds the object class name will always fail even though the value was set correctly
#if 0
		if( bResult )
		{
			UProperty* NodeProperty = PropertyNodePin->GetProperty();

			const FString CompareName = NewObject ? FString::Printf( TEXT("%s'%s'"), *NewObject->GetClass()->GetName(), *ObjectPathName ) : TEXT("None");

			// Read values back and report any failures.
			TArray<FObjectBaseAddress> ObjectsThatWereModified;
			FObjectPropertyNode* ObjectNode = PropertyNodePin->FindObjectItemParent();
			
			if( ObjectNode )
			{
				bool bAllObjectPropertyValuesMatch = true;
				for ( TPropObjectIterator Itor( ObjectNode->ObjectIterator() ) ; Itor ; ++Itor )
				{
					TWeakObjectPtr<UObject>	Object = *Itor;
					uint8*		Addr = PropertyNodePin->GetValueBaseAddress( (uint8*) Object.Get() );

					FString PropertyValue;
					NodeProperty->ExportText_Direct(PropertyValue, Addr, Addr, nullptr, 0 );
					if ( PropertyValue != CompareName )
					{
						bAllObjectPropertyValuesMatch = false;
						break;
					}
				}

				// Warn that some object assignments failed.
				if ( !bAllObjectPropertyValuesMatch )
				{
					bResult = false;
				}
			}
			else
			{
				bResult = false;
			}
		}
#endif
		return bResult;
	}

	return false;
}

FPropertyAccess::Result FPropertyValueImpl::OnUseSelected()
{
	FPropertyAccess::Result Res = FPropertyAccess::Fail;
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid() )
	{
		UProperty* NodeProperty = PropertyNodePin->GetProperty();

		UObjectPropertyBase* ObjProp = Cast<UObjectPropertyBase>( NodeProperty );
		UInterfaceProperty* IntProp = Cast<UInterfaceProperty>( NodeProperty );
		UClassProperty* ClassProp = Cast<UClassProperty>( NodeProperty );
		UAssetClassProperty* AssetClassProperty = Cast<UAssetClassProperty>( NodeProperty );
		UClass* const InterfaceThatMustBeImplemented = ObjProp ? ObjProp->GetOwnerProperty()->GetClassMetaData(TEXT("MustImplement")) : nullptr;

		if(ClassProp || AssetClassProperty)
		{
			FEditorDelegates::LoadSelectedAssetsIfNeeded.Broadcast();

			const UClass* const SelectedClass = GEditor->GetFirstSelectedClass(ClassProp ? ClassProp->MetaClass : AssetClassProperty->MetaClass);
			if(SelectedClass)
			{
				if (!InterfaceThatMustBeImplemented || SelectedClass->ImplementsInterface(InterfaceThatMustBeImplemented))
				{
					FString const ClassPathName = SelectedClass->GetPathName();

					TArray<FText> RestrictReasons;
					if (PropertyNodePin->IsRestricted(ClassPathName, RestrictReasons))
					{
						check(RestrictReasons.Num() > 0);
						FMessageDialog::Open(EAppMsgType::Ok, RestrictReasons[0]);
					}
					else 
					{
						
						Res = SetValueAsString(ClassPathName, EPropertyValueSetFlags::DefaultFlags);
					}
				}
			}
		}
		else
		{
			FEditorDelegates::LoadSelectedAssetsIfNeeded.Broadcast();

			UClass* ObjPropClass = UObject::StaticClass();
			if( ObjProp )
			{
				ObjPropClass = ObjProp->PropertyClass;
			}
			else if( IntProp ) 
			{
				ObjPropClass = IntProp->InterfaceClass;
			}

			bool const bMustBeLevelActor = ObjProp ? ObjProp->GetOwnerProperty()->GetBoolMetaData(TEXT("MustBeLevelActor")) : false;

			// Find best appropriate selected object
			UObject* SelectedObject = nullptr;

			if (bMustBeLevelActor)
			{
				// @todo: ensure at compile time that MustBeLevelActor flag is only set on actor properties

				// looking only for level actors here
				USelection* const SelectedSet = GEditor->GetSelectedActors();
				SelectedObject = SelectedSet->GetTop( ObjPropClass, InterfaceThatMustBeImplemented);
			}
			else
			{
				// normal behavior, where actor classes will look for level actors and 
				USelection* const SelectedSet = GEditor->GetSelectedSet( ObjPropClass );
				SelectedObject = SelectedSet->GetTop( ObjPropClass, InterfaceThatMustBeImplemented );
			}

			if( SelectedObject )
			{
				FString const ObjPathName = SelectedObject->GetPathName();

				TArray<FText> RestrictReasons;
				if (PropertyNodePin->IsRestricted(ObjPathName, RestrictReasons))
				{
					check(RestrictReasons.Num() > 0);
					FMessageDialog::Open(EAppMsgType::Ok, RestrictReasons[0]);
				}
				else if (!SetObject(SelectedObject, EPropertyValueSetFlags::DefaultFlags))
				{
					// Warn that some object assignments failed.
					FMessageDialog::Open( EAppMsgType::Ok, FText::Format(
						NSLOCTEXT("UnrealEd", "ObjectAssignmentsFailed", "Failed to assign {0} to the {1} property, see log for details."),
						FText::FromString(SelectedObject->GetPathName()), PropertyNodePin->GetDisplayName()) );
				}
				else
				{
					Res = FPropertyAccess::Success;
				}
			}
		}
	}

	return Res;
}

bool FPropertyValueImpl::IsPropertyTypeOf( UClass* ClassType ) const
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid() )
	{
		if(UProperty* Property = PropertyNodePin->GetProperty())
		{
			return Property->IsA( ClassType );
		}
	}
	return false;
}

template< typename Type>
static Type ClampValueFromMetaData(Type InValue, FPropertyNode& InPropertyNode )
{
	UProperty* Property = InPropertyNode.GetProperty();

	Type RetVal = InValue;
	if( Property )
	{
		//enforce min
		const FString& MinString = Property->GetMetaData(TEXT("ClampMin"));
		if(MinString.Len())
		{
			checkSlow(MinString.IsNumeric());
			Type MinValue;
			TTypeFromString<Type>::FromString(MinValue, *MinString);
			RetVal = FMath::Max<Type>(MinValue, RetVal);
		}
		//Enforce max 
		const FString& MaxString = Property->GetMetaData(TEXT("ClampMax"));
		if(MaxString.Len())
		{
			checkSlow(MaxString.IsNumeric());
			Type MaxValue;
			TTypeFromString<Type>::FromString(MaxValue, *MaxString);
			RetVal = FMath::Min<Type>(MaxValue, RetVal);
		}
	}

	return RetVal;
}

template <typename Type>
static Type ClampIntegerValueFromMetaData(Type InValue, FPropertyNode& InPropertyNode )
{
	Type RetVal = ClampValueFromMetaData<Type>( InValue, InPropertyNode );

	UProperty* Property = InPropertyNode.GetProperty();


	//if there is "Multiple" meta data, the selected number is a multiple
	const FString& MultipleString = Property->GetMetaData(TEXT("Multiple"));
	if (MultipleString.Len())
	{
		check(MultipleString.IsNumeric());
		Type MultipleValue;
		TTypeFromString<Type>::FromString(MultipleValue, *MultipleString);
		if (MultipleValue != 0)
		{
			RetVal -= Type(RetVal) % MultipleValue;
		}
	}

	//enforce array bounds
	const FString& ArrayClampString = Property->GetMetaData(TEXT("ArrayClamp"));
	if (ArrayClampString.Len())
	{
		FObjectPropertyNode* ObjectPropertyNode = InPropertyNode.FindObjectItemParent();
		if (ObjectPropertyNode && ObjectPropertyNode->GetNumObjects() == 1)
		{
			Type LastValidIndex = GetArrayPropertyLastValidIndex(ObjectPropertyNode, ArrayClampString);
			RetVal = FMath::Clamp<Type>(RetVal, 0, LastValidIndex);
		}
		else
		{
			UE_LOG(LogPropertyNode, Warning, TEXT("Array Clamping isn't supported in multi-select (Param Name: %s)"), *Property->GetName());
		}
	}

	return RetVal;
}


int32 FPropertyValueImpl::GetNumChildren() const
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid() )
	{
		return PropertyNodePin->GetNumChildNodes();
	}

	return 0;
}

TSharedPtr<FPropertyNode> FPropertyValueImpl::GetPropertyNode() const
{
	return PropertyNode.Pin();
}

TSharedPtr<FPropertyNode> FPropertyValueImpl::GetChildNode( FName ChildName, bool bRecurse ) const
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid() )
	{
		return PropertyNodePin->FindChildPropertyNode(ChildName, bRecurse);
	}

	return nullptr;
}


TSharedPtr<FPropertyNode> FPropertyValueImpl::GetChildNode( int32 ChildIndex ) const
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid() )
	{
		return PropertyNodePin->GetChildNode( ChildIndex );
	}

	return nullptr;
}


void FPropertyValueImpl::ResetToDefault()
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid() && !PropertyNodePin->IsEditConst() && PropertyNodePin->GetDiffersFromDefault() )
	{
		PropertyNodePin->ResetToDefault( NotifyHook );
	}

}

bool FPropertyValueImpl::DiffersFromDefault() const
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid()  )
	{
		return PropertyNodePin->GetDiffersFromDefault();
	}

	return false;
}

bool FPropertyValueImpl::IsEditConst() const
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid()  )
	{
		return PropertyNodePin->IsEditConst();
	} 

	return false;
}

FText FPropertyValueImpl::GetResetToDefaultLabel() const
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid()  )
	{
		return PropertyNodePin->GetResetToDefaultLabel();
	} 

	return FText::GetEmpty();
}

void FPropertyValueImpl::AddChild()
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid()  )
	{
		UProperty* NodeProperty = PropertyNodePin->GetProperty();

		FReadAddressList ReadAddresses;
		PropertyNodePin->GetReadAddress( !!PropertyNodePin->HasNodeFlags(EPropertyNodeFlags::SingleSelectOnly), ReadAddresses, true, false, true );
		if ( ReadAddresses.Num() )
		{
			// determines whether we actually changed any values (if the user clicks the "emtpy" button when the array is already empty,
			// we don't want the objects to be marked dirty)
			bool bNotifiedPreChange = false;

			TArray< TMap<FString,int32> > ArrayIndicesPerObject;

			// Begin a property edit transaction.
			FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "AddChild", "Add Child") );
			FObjectPropertyNode* ObjectNode = PropertyNodePin->FindObjectItemParent();
			UArrayProperty* Array = CastChecked<UArrayProperty>( NodeProperty );
			for ( int32 i = 0 ; i < ReadAddresses.Num() ; ++i )
			{
				void* Addr = ReadAddresses.GetAddress(i);
				if( Addr )
				{
					if ( !bNotifiedPreChange )
					{
						bNotifiedPreChange = true;

						// send the PreEditChange notification to all selected objects
						PropertyNodePin->NotifyPreChange( NodeProperty, NotifyHook );
					}

					//add on array index so we can tell which entry just changed
					ArrayIndicesPerObject.Add(TMap<FString,int32>());
					FPropertyValueImpl::GenerateArrayIndexMapToObjectNode(ArrayIndicesPerObject[i], PropertyNodePin.Get());
					
					UObject* Obj = ObjectNode ? ObjectNode->GetUObject(i) : nullptr;
					if (Obj)
					{
						if ((Obj->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) ||
							(Obj->HasAnyFlags(RF_DefaultSubObject) && Obj->GetOuter()->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))) &&
							!FApp::IsGame())
						{
							FString OrgArrayContent;
							Array->ExportText_Direct(OrgArrayContent, Addr, Addr, nullptr, 0);

							PropertyNodePin->PropagateArrayPropertyChange(Obj, OrgArrayContent, EPropertyArrayChangeType::Add, -1);
						}
					}

					FScriptArrayHelper	ArrayHelper(Array,Addr);
					const int32 ArrayIndex = ArrayHelper.AddValue();
					FPropertyNode::AdditionalInitializationUDS(Array->Inner, ArrayHelper.GetRawPtr(ArrayIndex));
					
					ArrayIndicesPerObject[i].Add(NodeProperty->GetName(), ArrayIndex);
				}
			}

			if ( bNotifiedPreChange )
			{
				// send the PostEditChange notification; it will be propagated to all selected objects
				FPropertyChangedEvent ChangeEvent(NodeProperty, EPropertyChangeType::ArrayAdd);
				ChangeEvent.SetArrayIndexPerObject(ArrayIndicesPerObject);
				PropertyNodePin->NotifyPostChange( ChangeEvent, NotifyHook );
			}

			if (PropertyUtilities.IsValid())
			{
				FPropertyChangedEvent ChangeEvent(NodeProperty, EPropertyChangeType::ArrayAdd);
				PropertyNodePin->FixPropertiesInEvent(ChangeEvent);
				PropertyUtilities.Pin()->NotifyFinishedChangingProperties(ChangeEvent);
			}
		}
	}
}

void FPropertyValueImpl::ClearChildren()
{
	TSharedPtr<FPropertyNode> PropertyNodePin = PropertyNode.Pin();
	if( PropertyNodePin.IsValid()  )
	{
		UProperty* NodeProperty = PropertyNodePin->GetProperty();

		FReadAddressList ReadAddresses;
		PropertyNodePin->GetReadAddress( !!PropertyNodePin->HasNodeFlags(EPropertyNodeFlags::SingleSelectOnly), ReadAddresses,
			true, //bComparePropertyContents
			false, //bObjectForceCompare
			true ); //bArrayPropertiesCanDifferInSize

		if ( ReadAddresses.Num() )
		{
			// determines whether we actually changed any values (if the user clicks the "emtpy" button when the array is already empty,
			// we don't want the objects to be marked dirty)
			bool bNotifiedPreChange = false;

			// Begin a property edit transaction.
			FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "ClearChildren", "Clear Children") );
			FObjectPropertyNode* ObjectNode = PropertyNodePin->FindObjectItemParent();
			UArrayProperty* Array = CastChecked<UArrayProperty>( NodeProperty );
			for ( int32 i = 0 ; i < ReadAddresses.Num() ; ++i )
			{
				void* Addr = ReadAddresses.GetAddress(i);
				if( Addr )
				{
					if ( !bNotifiedPreChange )
					{
						bNotifiedPreChange = true;

						// send the PreEditChange notification to all selected objects
						PropertyNodePin->NotifyPreChange( NodeProperty, NotifyHook );
					}

					UObject* Obj = ObjectNode ? ObjectNode->GetUObject(i) : nullptr;
					if (Obj)
					{
						if ((Obj->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) ||
							(Obj->HasAnyFlags(RF_DefaultSubObject) && Obj->GetOuter()->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))) &&
							!FApp::IsGame())
						{
							FString OrgArrayContent;
							Array->ExportText_Direct(OrgArrayContent, Addr, Addr, nullptr, 0);

							PropertyNodePin->PropagateArrayPropertyChange(Obj, OrgArrayContent, EPropertyArrayChangeType::Clear, -1);
						}
					}

					FScriptArrayHelper	ArrayHelper(Array,Addr);
					ArrayHelper.EmptyValues();
				}
			}

			if ( bNotifiedPreChange )
			{
				// send the PostEditChange notification; it will be propagated to all selected objects
				FPropertyChangedEvent ChangeEvent(NodeProperty, EPropertyChangeType::ValueSet);
				PropertyNodePin->NotifyPostChange( ChangeEvent, NotifyHook );
			}
		}

		if (PropertyUtilities.IsValid())
		{
			FPropertyChangedEvent ChangeEvent(NodeProperty, EPropertyChangeType::ValueSet);
			PropertyNodePin->FixPropertiesInEvent(ChangeEvent);
			PropertyUtilities.Pin()->NotifyFinishedChangingProperties(ChangeEvent);
		}
	}
}

void FPropertyValueImpl::InsertChild( int32 Index )
{
	if( PropertyNode.IsValid() )
	{
		InsertChild( PropertyNode.Pin()->GetChildNode(Index));
	}
}

void FPropertyValueImpl::InsertChild( TSharedPtr<FPropertyNode> ChildNodeToInsertAfter )
{
	FPropertyNode* ChildNodePtr = ChildNodeToInsertAfter.Get();

	FPropertyNode* ParentNode = ChildNodePtr->GetParentNode();
	FObjectPropertyNode* ObjectNode = ChildNodePtr->FindObjectItemParent();

	UProperty* NodeProperty = ChildNodePtr->GetProperty();
	UArrayProperty* ArrayProperty = CastChecked<UArrayProperty>(NodeProperty->GetOuter());

	FReadAddressList ReadAddresses;
	void* Addr = nullptr;
	ParentNode->GetReadAddress( !!ParentNode->HasNodeFlags(EPropertyNodeFlags::SingleSelectOnly), ReadAddresses );
	if ( ReadAddresses.Num() )
	{
		Addr = ReadAddresses.GetAddress(0);
	}

	if( Addr )
	{
		// Begin a property edit transaction.
		FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "InsertChild", "Insert Child") );

		ChildNodePtr->NotifyPreChange( ParentNode->GetProperty(), NotifyHook );

		FScriptArrayHelper	ArrayHelper(ArrayProperty,Addr);
		int32 Index = ChildNodePtr->GetArrayIndex();

		UObject* Obj = ObjectNode ? ObjectNode->GetUObject(0) : nullptr;
		if (Obj)
		{
			if ((Obj->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) ||
				(Obj->HasAnyFlags(RF_DefaultSubObject) && Obj->GetOuter()->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))) &&
				!FApp::IsGame())
			{
				FString OrgArrayContent;
				ArrayProperty->ExportText_Direct(OrgArrayContent, Addr, Addr, nullptr, 0);

				ChildNodePtr->PropagateArrayPropertyChange(Obj, OrgArrayContent, EPropertyArrayChangeType::Insert, Index);
			}
		}

		ArrayHelper.InsertValues(Index, 1 );

		FPropertyNode::AdditionalInitializationUDS(ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index));

		//set up indices for the coming events
		TArray< TMap<FString,int32> > ArrayIndicesPerObject;
		for (int32 ObjectIndex = 0; ObjectIndex < ReadAddresses.Num(); ++ObjectIndex)
		{
			//add on array index so we can tell which entry just changed
			ArrayIndicesPerObject.Add(TMap<FString,int32>());
			FPropertyValueImpl::GenerateArrayIndexMapToObjectNode(ArrayIndicesPerObject[ObjectIndex], ChildNodePtr );
		}

		{
			FPropertyChangedEvent ChangeEvent(ParentNode->GetProperty(), EPropertyChangeType::ArrayAdd);
			ChangeEvent.SetArrayIndexPerObject(ArrayIndicesPerObject);
			ChildNodePtr->NotifyPostChange(ChangeEvent, NotifyHook);
		}

		if (PropertyUtilities.IsValid())
		{
			FPropertyChangedEvent ChangeEvent(ParentNode->GetProperty(), EPropertyChangeType::ArrayAdd);
			ChildNodePtr->FixPropertiesInEvent(ChangeEvent);
			PropertyUtilities.Pin()->NotifyFinishedChangingProperties(ChangeEvent);
		}
	}
}


void FPropertyValueImpl::DeleteChild( int32 Index )
{
	TSharedPtr<FPropertyNode> ArrayParentPin = PropertyNode.Pin();
	if( ArrayParentPin.IsValid()  )
	{
		DeleteChild( ArrayParentPin->GetChildNode( Index ) );
	}
}

void FPropertyValueImpl::DeleteChild( TSharedPtr<FPropertyNode> ChildNodeToDelete )
{
	FPropertyNode* ChildNodePtr = ChildNodeToDelete.Get();

	FPropertyNode* ParentNode = ChildNodePtr->GetParentNode();
	FObjectPropertyNode* ObjectNode = ChildNodePtr->FindObjectItemParent();

	UProperty* NodeProperty = ChildNodePtr->GetProperty();
	UArrayProperty* ArrayProperty = CastChecked<UArrayProperty>(NodeProperty->GetOuter());

	FReadAddressList ReadAddresses;
	ParentNode->GetReadAddress( !!ParentNode->HasNodeFlags(EPropertyNodeFlags::SingleSelectOnly), ReadAddresses ); 
	if ( ReadAddresses.Num() )
	{
		FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "DeleteChild", "Delete Child") );

		ChildNodePtr->NotifyPreChange( NodeProperty, NotifyHook );

		// perform the operation on the array for all selected objects
		for ( int32 i = 0 ; i < ReadAddresses.Num() ; ++i )
		{
			uint8* Address = ReadAddresses.GetAddress(i);

			if( Address ) 
			{
				FScriptArrayHelper ArrayHelper(ArrayProperty,Address);
				int32 Index = ChildNodePtr->GetArrayIndex();

				UObject* Obj = ObjectNode ? ObjectNode->GetUObject(i) : nullptr;
				if (Obj)
				{
					if ((Obj->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) ||
						(Obj->HasAnyFlags(RF_DefaultSubObject) && Obj->GetOuter()->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))) &&
						!FApp::IsGame())
					{
						FString OrgArrayContent;
						ArrayProperty->ExportText_Direct(OrgArrayContent, Address, Address, nullptr, 0);

						ChildNodePtr->PropagateArrayPropertyChange(Obj, OrgArrayContent, EPropertyArrayChangeType::Delete, Index);
					}
				}

				ArrayHelper.RemoveValues(ChildNodePtr->GetArrayIndex());
			}
		}

		{
			FPropertyChangedEvent ChangeEvent(ParentNode->GetProperty());
			ChildNodePtr->NotifyPostChange(ChangeEvent, NotifyHook);
		}

		if (PropertyUtilities.IsValid())
		{
			FPropertyChangedEvent ChangeEvent(ParentNode->GetProperty());
			ChildNodePtr->FixPropertiesInEvent(ChangeEvent);
			PropertyUtilities.Pin()->NotifyFinishedChangingProperties(ChangeEvent);
		}
	}
}

void FPropertyValueImpl::DuplicateChild( int32 Index )
{
	TSharedPtr<FPropertyNode> ArrayParentPin = PropertyNode.Pin();
	if( ArrayParentPin.IsValid() )
	{
		DuplicateChild( ArrayParentPin->GetChildNode( Index ) );
	}
}

void FPropertyValueImpl::DuplicateChild( TSharedPtr<FPropertyNode> ChildNodeToDuplicate )
{
	FPropertyNode* ChildNodePtr = ChildNodeToDuplicate.Get();

	FPropertyNode* ParentNode = ChildNodePtr->GetParentNode();
	FObjectPropertyNode* ObjectNode = ChildNodePtr->FindObjectItemParent();

	UProperty* NodeProperty = ChildNodePtr->GetProperty();
	UArrayProperty* ArrayProperty = CastChecked<UArrayProperty>(NodeProperty->GetOuter());

	FReadAddressList ReadAddresses;
	void* Addr = nullptr;
	ParentNode->GetReadAddress( !!ParentNode->HasNodeFlags(EPropertyNodeFlags::SingleSelectOnly), ReadAddresses );
	if ( ReadAddresses.Num() )
	{
		Addr = ReadAddresses.GetAddress(0);
	}

	if( Addr )
	{
		FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "DuplicateChild", "Duplicate Child") );

		ChildNodePtr->NotifyPreChange( ParentNode->GetProperty(), NotifyHook );

		FScriptArrayHelper	ArrayHelper(ArrayProperty,Addr);

		int32 Index = ChildNodePtr->GetArrayIndex();
		UObject* Obj = ObjectNode ? ObjectNode->GetUObject(0) : nullptr;

		TArray< TMap<FString, int32> > ArrayIndicesPerObject;
		if (Obj)
		{
			if ((Obj->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) ||
				(Obj->HasAnyFlags(RF_DefaultSubObject) && Obj->GetOuter()->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))) &&
				!FApp::IsGame())
			{
				FString OrgArrayContent;
				ArrayProperty->ExportText_Direct(OrgArrayContent, Addr, Addr, nullptr, 0);

				ChildNodePtr->PropagateArrayPropertyChange(Obj, OrgArrayContent, EPropertyArrayChangeType::Duplicate, Index);
			}
		}

		ArrayHelper.InsertValues(Index);

		// Copy the selected item's value to the new item.
		NodeProperty->CopyCompleteValue(ArrayHelper.GetRawPtr(Index), ArrayHelper.GetRawPtr(Index + 1));

		if (UObjectProperty* ObjProp = Cast<UObjectProperty>(NodeProperty))
		{
			UObject* CurrentObject = ObjProp->GetObjectPropertyValue(ArrayHelper.GetRawPtr(Index));

			// Fpr DefaultSubObjects and ArchetypeObjects we need to do a deep copy instead of a shallow copy
			if (CurrentObject && CurrentObject->HasAnyFlags(RF_DefaultSubObject | RF_ArchetypeObject))
			{
				// Make a deep copy and assign it into the array.
				UObject* DuplicatedObject = DuplicateObject(CurrentObject, CurrentObject->GetOuter());
				ObjProp->SetObjectPropertyValue(ArrayHelper.GetRawPtr(Index + 1), DuplicatedObject);
			}
		}
		
		// Find the object that owns the array and instance any subobjects
		if (FObjectPropertyNode* ObjectPropertyNode = ChildNodePtr->FindObjectItemParent())
		{
			UObject* ArrayOwner = nullptr;
			for (TPropObjectIterator Itor(ObjectPropertyNode->ObjectIterator()); Itor && !ArrayOwner; ++Itor)
			{
				ArrayOwner = Itor->Get();
			}
			if (ArrayOwner)
			{
				ArrayOwner->InstanceSubobjectTemplates();
			}
		}

		if(Obj)
		{

			ArrayIndicesPerObject.Add(TMap<FString, int32>());
			FPropertyValueImpl::GenerateArrayIndexMapToObjectNode(ArrayIndicesPerObject[0], ChildNodePtr);
		}



		//@todo Slate Property Window
		// 		const bool bExpand = true;
		// 		const bool bRecurse = false;
		// 		ParentNode->SetExpanded(bExpand, bRecurse);

		FPropertyChangedEvent ChangeEvent(ParentNode->GetProperty(), EPropertyChangeType::Duplicate);
		ChangeEvent.SetArrayIndexPerObject(ArrayIndicesPerObject);
		{
			
			ChildNodePtr->NotifyPostChange(ChangeEvent, NotifyHook);
		}
		if (PropertyUtilities.IsValid())
		{
			ChildNodePtr->FixPropertiesInEvent(ChangeEvent);
			PropertyUtilities.Pin()->NotifyFinishedChangingProperties(ChangeEvent);
		}
	}
}

bool FPropertyValueImpl::HasValidPropertyNode() const
{
	return PropertyNode.IsValid();
}

FText FPropertyValueImpl::GetDisplayName() const
{
	return PropertyNode.IsValid() ? PropertyNode.Pin()->GetDisplayName() : FText::GetEmpty();
}

#define IMPLEMENT_PROPERTY_ACCESSOR( ValueType ) \
	FPropertyAccess::Result FPropertyHandleBase::SetValue( ValueType const& InValue, EPropertyValueSetFlags::Type Flags ) \
	{ \
		return FPropertyAccess::Fail; \
	} \
	FPropertyAccess::Result FPropertyHandleBase::GetValue( ValueType& OutValue ) const \
	{ \
		return FPropertyAccess::Fail; \
	}

IMPLEMENT_PROPERTY_ACCESSOR( bool )
IMPLEMENT_PROPERTY_ACCESSOR( int8 )
IMPLEMENT_PROPERTY_ACCESSOR( int16 )
IMPLEMENT_PROPERTY_ACCESSOR( int32 )
IMPLEMENT_PROPERTY_ACCESSOR( int64 )
IMPLEMENT_PROPERTY_ACCESSOR( uint8 )
IMPLEMENT_PROPERTY_ACCESSOR( uint16 )
IMPLEMENT_PROPERTY_ACCESSOR( uint32 )
IMPLEMENT_PROPERTY_ACCESSOR( uint64 )
IMPLEMENT_PROPERTY_ACCESSOR( float )
IMPLEMENT_PROPERTY_ACCESSOR( FString )
IMPLEMENT_PROPERTY_ACCESSOR( FText )
IMPLEMENT_PROPERTY_ACCESSOR( FName )
IMPLEMENT_PROPERTY_ACCESSOR( FVector )
IMPLEMENT_PROPERTY_ACCESSOR( FVector2D )
IMPLEMENT_PROPERTY_ACCESSOR( FVector4 )
IMPLEMENT_PROPERTY_ACCESSOR( FQuat )
IMPLEMENT_PROPERTY_ACCESSOR( FRotator )
IMPLEMENT_PROPERTY_ACCESSOR( UObject* )
IMPLEMENT_PROPERTY_ACCESSOR( const UObject* )
IMPLEMENT_PROPERTY_ACCESSOR( FAssetData )

FPropertyHandleBase::FPropertyHandleBase( TSharedPtr<FPropertyNode> PropertyNode, FNotifyHook* NotifyHook, TSharedPtr<IPropertyUtilities> PropertyUtilities )
	: Implementation( MakeShareable( new FPropertyValueImpl( PropertyNode, NotifyHook, PropertyUtilities ) ) )
{

}
 
bool FPropertyHandleBase::IsValidHandle() const
{
	return Implementation->HasValidPropertyNode();
}

FText FPropertyHandleBase::GetPropertyDisplayName() const
{
	return Implementation->GetDisplayName();
}
	
void FPropertyHandleBase::ResetToDefault()
{
	Implementation->ResetToDefault();
}

bool FPropertyHandleBase::DiffersFromDefault() const
{
	return Implementation->DiffersFromDefault();
}

FText FPropertyHandleBase::GetResetToDefaultLabel() const
{
	return Implementation->GetResetToDefaultLabel();
}

void FPropertyHandleBase::MarkHiddenByCustomization()
{
	if( Implementation->GetPropertyNode().IsValid() )
	{
		Implementation->GetPropertyNode()->SetNodeFlags( EPropertyNodeFlags::IsCustomized, true );
	}
}

bool FPropertyHandleBase::IsCustomized() const
{
	return Implementation->GetPropertyNode()->HasNodeFlags( EPropertyNodeFlags::IsCustomized ) != 0;
}

FString FPropertyHandleBase::GeneratePathToProperty() const
{
	FString OutPath;
	if( Implementation.IsValid() && Implementation->GetPropertyNode().IsValid() )
	{
		const bool bArrayIndex = true;
		const bool bIgnoreCategories = true;
		FPropertyNode* StopParent = Implementation->GetPropertyNode()->FindObjectItemParent();
		Implementation->GetPropertyNode()->GetQualifiedName( OutPath, bArrayIndex, StopParent, bIgnoreCategories );
	}

	return OutPath;

}

TSharedRef<SWidget> FPropertyHandleBase::CreatePropertyNameWidget( const FText& NameOverride, const FText& ToolTipOverride, bool bDisplayResetToDefault, bool bDisplayText, bool bDisplayThumbnail ) const
{
	if( Implementation.IsValid() && Implementation->GetPropertyNode().IsValid() )
	{
		if( !NameOverride.IsEmpty() )
		{
			Implementation->GetPropertyNode()->SetDisplayNameOverride( NameOverride );
		}

		if( !ToolTipOverride.IsEmpty() )
		{
			Implementation->GetPropertyNode()->SetToolTipOverride( ToolTipOverride );
		}

		TSharedPtr<FPropertyEditor> PropertyEditor = FPropertyEditor::Create( Implementation->GetPropertyNode().ToSharedRef(), Implementation->GetPropertyUtilities().ToSharedRef() );

		return SNew( SPropertyNameWidget, PropertyEditor )
				.DisplayResetToDefault( bDisplayResetToDefault );
	}

	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> FPropertyHandleBase::CreatePropertyValueWidget( bool bDisplayDefaultPropertyButtons ) const
{
	if( Implementation.IsValid() && Implementation->GetPropertyNode().IsValid() )
	{
		TSharedPtr<FPropertyEditor> PropertyEditor = FPropertyEditor::Create( Implementation->GetPropertyNode().ToSharedRef(), Implementation->GetPropertyUtilities().ToSharedRef() );

		return SNew( SPropertyValueWidget, PropertyEditor, Implementation->GetPropertyUtilities() )
				.ShowPropertyButtons( bDisplayDefaultPropertyButtons );
	}

	return SNullWidget::NullWidget;
}

bool FPropertyHandleBase::IsEditConst() const
{
	return Implementation->IsEditConst();
}

FPropertyAccess::Result FPropertyHandleBase::GetValueAsFormattedString( FString& OutValue ) const
{
	return Implementation->GetValueAsString(OutValue);
}

FPropertyAccess::Result FPropertyHandleBase::GetValueAsDisplayString( FString& OutValue ) const
{
	return Implementation->GetValueAsDisplayString(OutValue);
}

FPropertyAccess::Result FPropertyHandleBase::GetValueAsFormattedText( FText& OutValue ) const
{
	return Implementation->GetValueAsText(OutValue);
}

FPropertyAccess::Result FPropertyHandleBase::GetValueAsDisplayText( FText& OutValue ) const
{
	return Implementation->GetValueAsDisplayText(OutValue);
}

FPropertyAccess::Result FPropertyHandleBase::SetValueFromFormattedString( const FString& InValue, EPropertyValueSetFlags::Type Flags )
{
	return Implementation->SetValueAsString( InValue, Flags );
}

TSharedPtr<IPropertyHandle> FPropertyHandleBase::GetChildHandle( FName ChildName, bool bRecurse ) const
{
	// Array children cannot be accessed in this manner
	if( !Implementation->IsPropertyTypeOf(UArrayProperty::StaticClass() ) )
	{
		TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetChildNode( ChildName, bRecurse );

		if( PropertyNode.IsValid() )
		{
			return PropertyEditorHelpers::GetPropertyHandle( PropertyNode.ToSharedRef(), Implementation->GetNotifyHook(), Implementation->GetPropertyUtilities() );
		}
	}
	
	return nullptr;
}

TSharedPtr<IPropertyHandle> FPropertyHandleBase::GetChildHandle( uint32 ChildIndex ) const
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetChildNode( ChildIndex );

	if( PropertyNode.IsValid() )
	{
		return PropertyEditorHelpers::GetPropertyHandle( PropertyNode.ToSharedRef(), Implementation->GetNotifyHook(), Implementation->GetPropertyUtilities() );
	}
	return nullptr;
}

TSharedPtr<IPropertyHandle> FPropertyHandleBase::GetParentHandle() const
{
	TSharedPtr<FPropertyNode> ParentNode = Implementation->GetPropertyNode()->GetParentNodeSharedPtr();
	if( ParentNode.IsValid() )
	{
		return PropertyEditorHelpers::GetPropertyHandle( ParentNode.ToSharedRef(), Implementation->GetNotifyHook(), Implementation->GetPropertyUtilities() ).ToSharedRef();
	}

	return nullptr;
}

FPropertyAccess::Result FPropertyHandleBase::GetNumChildren( uint32& OutNumChildren ) const
{
	OutNumChildren = Implementation->GetNumChildren();
	return FPropertyAccess::Success;
}

uint32 FPropertyHandleBase::GetNumOuterObjects() const
{
	FObjectPropertyNode* ObjectNode = Implementation->GetPropertyNode()->FindObjectItemParent();

	uint32 NumObjects = 0;
	if( ObjectNode )
	{
		NumObjects = ObjectNode->GetNumObjects();
	}

	return NumObjects;
}

void FPropertyHandleBase::GetOuterObjects( TArray<UObject*>& OuterObjects ) const
{
	FObjectPropertyNode* ObjectNode = Implementation->GetPropertyNode()->FindObjectItemParent();
	if( ObjectNode )
	{
		for( int32 ObjectIndex = 0; ObjectIndex < ObjectNode->GetNumObjects(); ++ObjectIndex )
		{
			OuterObjects.Add( ObjectNode->GetUObject( ObjectIndex ) );
		}
	}

}

void FPropertyHandleBase::GetOuterPackages(TArray<UPackage*>& OuterPackages) const
{
	FComplexPropertyNode* ComplexNode = Implementation->GetPropertyNode()->FindComplexParent();
	if (ComplexNode)
	{
		switch (ComplexNode->GetPropertyType())
		{
		case FComplexPropertyNode::EPT_Object:
			{
				FObjectPropertyNode* ObjectNode = static_cast<FObjectPropertyNode*>(ComplexNode);
				for (int32 ObjectIndex = 0; ObjectIndex < ObjectNode->GetNumObjects(); ++ObjectIndex)
				{
					OuterPackages.Add(ObjectNode->GetUPackage(ObjectIndex));
				}
			}
			break;

		case FComplexPropertyNode::EPT_StandaloneStructure:
			{
				FStructurePropertyNode* StructNode = static_cast<FStructurePropertyNode*>(ComplexNode);
				OuterPackages.Add(StructNode->GetOwnerPackage());
			}
			break;

		default:
			break;
		}
	}
}

void FPropertyHandleBase::EnumerateRawData( const EnumerateRawDataFuncRef& InRawDataCallback )
{
	Implementation->EnumerateRawData( InRawDataCallback );
}

void FPropertyHandleBase::EnumerateConstRawData( const EnumerateConstRawDataFuncRef& InRawDataCallback ) const
{
	Implementation->EnumerateConstRawData( InRawDataCallback );
}

void FPropertyHandleBase::AccessRawData( TArray<void*>& RawData )
{
	Implementation->AccessRawData( RawData );
}

void FPropertyHandleBase::AccessRawData( TArray<const void*>& RawData ) const
{
	Implementation->AccessRawData(RawData);
}

void FPropertyHandleBase::SetOnPropertyValueChanged( const FSimpleDelegate& InOnPropertyValueChanged )
{
	return Implementation->SetOnPropertyValueChanged(InOnPropertyValueChanged);
}

void FPropertyHandleBase::SetOnChildPropertyValueChanged( const FSimpleDelegate& InOnChildPropertyValueChanged )
{
	return Implementation->SetOnChildPropertyValueChanged( InOnChildPropertyValueChanged );
}

TSharedPtr<FPropertyNode> FPropertyHandleBase::GetPropertyNode() const
{
	return Implementation->GetPropertyNode();
}

int32 FPropertyHandleBase::GetIndexInArray() const
{
	if( Implementation->GetPropertyNode().IsValid() )
	{
		return Implementation->GetPropertyNode()->GetArrayIndex();
	}

	return INDEX_NONE;
}

const UClass* FPropertyHandleBase::GetPropertyClass() const
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() && PropertyNode->GetProperty() )
	{
		return PropertyNode->GetProperty()->GetClass();
	}

	return nullptr;
}

UProperty* FPropertyHandleBase::GetProperty() const
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() )
	{
		return PropertyNode->GetProperty();
	}

	return nullptr;
}

UProperty* FPropertyHandleBase::GetMetaDataProperty() const
{
	UProperty* MetaDataProperty = nullptr;

	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() )
	{
		MetaDataProperty = PropertyNode->GetProperty();
		
		// If we are part of an array, we need to take our meta-data from the array property
		if( PropertyNode->GetArrayIndex() != INDEX_NONE )
		{
			TSharedPtr<FPropertyNode> ParentNode = PropertyNode->GetParentNodeSharedPtr();
			check(ParentNode.IsValid());
			MetaDataProperty = ParentNode->GetProperty();
		}
	}

	return MetaDataProperty;
}

bool FPropertyHandleBase::HasMetaData(const FName& Key) const
{
	UProperty* const MetaDataProperty = GetMetaDataProperty();
	return (MetaDataProperty) ? MetaDataProperty->HasMetaData(Key) : false;
}

const FString& FPropertyHandleBase::GetMetaData(const FName& Key) const
{
	// if not found, return a static empty string
	static const FString EmptyString = TEXT("");

	UProperty* const MetaDataProperty = GetMetaDataProperty();
	return (MetaDataProperty) ? MetaDataProperty->GetMetaData(Key) : EmptyString;
}

bool FPropertyHandleBase::GetBoolMetaData(const FName& Key) const
{
	UProperty* const MetaDataProperty = GetMetaDataProperty();
	return (MetaDataProperty) ? MetaDataProperty->GetBoolMetaData(Key) : false;
}

int32 FPropertyHandleBase::GetINTMetaData(const FName& Key) const
{
	UProperty* const MetaDataProperty = GetMetaDataProperty();
	return (MetaDataProperty) ? MetaDataProperty->GetINTMetaData(Key) : 0;
}

float FPropertyHandleBase::GetFLOATMetaData(const FName& Key) const
{
	UProperty* const MetaDataProperty = GetMetaDataProperty();
	return (MetaDataProperty) ? MetaDataProperty->GetFLOATMetaData(Key) : 0.0f;
}

UClass* FPropertyHandleBase::GetClassMetaData(const FName& Key) const
{
	UProperty* const MetaDataProperty = GetMetaDataProperty();
	return (MetaDataProperty) ? MetaDataProperty->GetClassMetaData(Key) : nullptr;
}


void FPropertyHandleBase::SetInstanceMetaData(const FName& Key, const FString& Value)
{
	GetPropertyNode()->SetInstanceMetaData(Key, Value);
}

const FString* FPropertyHandleBase::GetInstanceMetaData(const FName& Key) const
{
	return GetPropertyNode()->GetInstanceMetaData(Key);
}

FText FPropertyHandleBase::GetToolTipText() const
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() )
	{
		return PropertyNode->GetToolTipText();
	}

	return FText::GetEmpty();
}

void FPropertyHandleBase::SetToolTipText( const FText& ToolTip )
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if (PropertyNode.IsValid())
	{
		return PropertyNode->SetToolTipOverride( ToolTip );
	}
}

int32 FPropertyHandleBase::GetNumPerObjectValues() const
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if (PropertyNode.IsValid() && PropertyNode->GetProperty())
	{
		FComplexPropertyNode* ComplexNode = PropertyNode->FindComplexParent();
		if (ComplexNode)
		{
			return ComplexNode->GetInstancesNum();
		}
	}
	return 0;
}

FPropertyAccess::Result FPropertyHandleBase::SetPerObjectValues( const TArray<FString>& InPerObjectValues, EPropertyValueSetFlags::Type Flags )
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() && PropertyNode->GetProperty() )
	{
		FComplexPropertyNode* ComplexNode = PropertyNode->FindComplexParent();

		if (ComplexNode && InPerObjectValues.Num() == ComplexNode->GetInstancesNum())
		{
			TArray<FObjectBaseAddress> ObjectsToModify;
			Implementation->GetObjectsToModify( ObjectsToModify, PropertyNode.Get() );

			Implementation->ImportText( ObjectsToModify, InPerObjectValues, PropertyNode.Get(), Flags );

			return FPropertyAccess::Success;
		}
	}

	return FPropertyAccess::Fail;
}

FPropertyAccess::Result FPropertyHandleBase::GetPerObjectValues( TArray<FString>& OutPerObjectValues ) const
{
	FPropertyAccess::Result Result = FPropertyAccess::Fail;
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() && PropertyNode->GetProperty() )
	{
		// Get a list of addresses for objects handled by the property window.
		FReadAddressList ReadAddresses;
		PropertyNode->GetReadAddress( !!PropertyNode->HasNodeFlags(EPropertyNodeFlags::SingleSelectOnly), ReadAddresses, false );

		UProperty* NodeProperty = PropertyNode->GetProperty();

		if( ReadAddresses.Num() > 0 )
		{
			// Copy each object's value into the value list
			OutPerObjectValues.SetNum( ReadAddresses.Num(), /*bAllowShrinking*/false );
			for ( int32 AddrIndex = 0 ; AddrIndex < ReadAddresses.Num() ; ++AddrIndex )
			{
				uint8* Address = ReadAddresses.GetAddress(AddrIndex);
				if( Address )
				{
					NodeProperty->ExportText_Direct(OutPerObjectValues[AddrIndex], Address, Address, nullptr, 0 );
				}
				else
				{
					OutPerObjectValues[AddrIndex].Reset();
				}
			}

			Result = FPropertyAccess::Success;
		}
	}
	
	return Result;
}

FPropertyAccess::Result FPropertyHandleBase::SetPerObjectValue( const int32 ObjectIndex, const FString& ObjectValue, EPropertyValueSetFlags::Type Flags )
{
	FPropertyAccess::Result Result = FPropertyAccess::Fail;

	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if (PropertyNode.IsValid() && PropertyNode->GetProperty())
	{
		Implementation->EnumerateObjectsToModify(PropertyNode.Get(), [&](const FObjectBaseAddress& ObjectToModify, const int32 ObjIndex, const int32 NumObjects) -> bool
		{
			if (ObjIndex == ObjectIndex)
			{
				TArray<FObjectBaseAddress> ObjectsToModify;
				ObjectsToModify.Add(ObjectToModify);

				TArray<FString> PerObjectValues;
				PerObjectValues.Add(ObjectValue);

				Implementation->ImportText(ObjectsToModify, PerObjectValues, PropertyNode.Get(), Flags);

				Result = FPropertyAccess::Success;
				return false; // End enumeration
			}
			return true;
		});
	}

	return Result;
}

FPropertyAccess::Result FPropertyHandleBase::GetPerObjectValue( const int32 ObjectIndex, FString& OutObjectValue ) const
{
	FPropertyAccess::Result Result = FPropertyAccess::Fail;

	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if (PropertyNode.IsValid() && PropertyNode->GetProperty())
	{
		// Get a list of addresses for objects handled by the property window.
		FReadAddressList ReadAddresses;
		PropertyNode->GetReadAddress(!!PropertyNode->HasNodeFlags(EPropertyNodeFlags::SingleSelectOnly), ReadAddresses, false);

		UProperty* NodeProperty = PropertyNode->GetProperty();

		if (ReadAddresses.IsValidIndex(ObjectIndex))
		{
			uint8* Address = ReadAddresses.GetAddress(ObjectIndex);
			if (Address)
			{
				NodeProperty->ExportText_Direct(OutObjectValue, Address, Address, nullptr, 0);
			}
			else
			{
				OutObjectValue.Reset();
			}

			Result = FPropertyAccess::Success;
		}
	}

	return Result;
}


bool FPropertyHandleBase::GeneratePossibleValues(TArray< TSharedPtr<FString> >& OutOptionStrings, TArray< FText >& OutToolTips, TArray<bool>& OutRestrictedItems)
{
	UProperty* Property = GetProperty();

	bool bUsesAlternateDisplayValues = false;
	const UByteProperty* ByteProperty = Cast<const UByteProperty>( Property );

	if( ByteProperty || ( Property->IsA(UStrProperty::StaticClass()) && Property->HasMetaData( TEXT("Enum") ) ) )
	{
		UEnum* Enum = nullptr;
		if( ByteProperty )
		{
			Enum = ByteProperty->Enum;
		}
		else
		{

			FString EnumName = Property->GetMetaData(TEXT("Enum"));
			Enum = FindObject<UEnum>(ANY_PACKAGE, *EnumName, true);
		}

		check( Enum );

		const TArray<FName> ValidEnumValues = PropertyEditorHelpers::GetValidEnumsFromPropertyOverride(Property, Enum);

		//NumEnums() - 1, because the last item in an enum is the _MAX item
		for( int32 EnumIndex = 0; EnumIndex < Enum->NumEnums() - 1; ++EnumIndex )
		{
			FString EnumValueName;

			// Ignore hidden enums
			bool bShouldBeHidden = Enum->HasMetaData(TEXT("Hidden"), EnumIndex ) || Enum->HasMetaData(TEXT("Spacer"), EnumIndex );
			if (!bShouldBeHidden && ValidEnumValues.Num() != 0)
			{
				bShouldBeHidden = ValidEnumValues.Find(Enum->GetEnum(EnumIndex)) == INDEX_NONE;
			}

			if( !bShouldBeHidden )
			{
				// See if we specified an alternate name for this value using metadata
				EnumValueName = Enum->GetDisplayNameText( EnumIndex ).ToString();


				if( EnumValueName.Len() == 0 ) 
				{
					EnumValueName = Enum->GetEnumName(EnumIndex);
				}
				else
				{
					bUsesAlternateDisplayValues = true;
				}

				FText RestrictionTooltip;
				bool bIsRestricted = GenerateRestrictionToolTip(EnumValueName, RestrictionTooltip);
				OutRestrictedItems.Add(bIsRestricted);

				TSharedPtr< FString > EnumStr( new FString( EnumValueName ) );
				OutOptionStrings.Add( EnumStr );

				FText EnumValueToolTip = bIsRestricted ? RestrictionTooltip : Enum->GetToolTipText(EnumIndex);
				OutToolTips.Add( EnumValueToolTip );
			}
		}
	}
	else if( Property->IsA(UClassProperty::StaticClass()) || Property->IsA(UAssetClassProperty::StaticClass()) )		
	{
		UClass* MetaClass = Property->IsA(UClassProperty::StaticClass()) 
			? CastChecked<UClassProperty>(Property)->MetaClass
			: CastChecked<UAssetClassProperty>(Property)->MetaClass;

		TSharedPtr< FString > NoneStr( new FString( TEXT("None") ) );
		OutOptionStrings.Add( NoneStr );

		const bool bAllowAbstract = Property->GetOwnerProperty()->HasMetaData(TEXT("AllowAbstract"));
		const bool bBlueprintBaseOnly = Property->GetOwnerProperty()->HasMetaData(TEXT("BlueprintBaseOnly"));
		const bool bAllowOnlyPlaceable = Property->GetOwnerProperty()->HasMetaData(TEXT("OnlyPlaceable"));
		UClass* InterfaceThatMustBeImplemented = Property->GetOwnerProperty()->GetClassMetaData(TEXT("MustImplement"));

		if (!bAllowOnlyPlaceable || MetaClass->IsChildOf<AActor>())
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->IsChildOf(MetaClass)
					&& PropertyEditorHelpers::IsEditInlineClassAllowed(*It, bAllowAbstract)
					&& (!bBlueprintBaseOnly || FKismetEditorUtilities::CanCreateBlueprintOfClass(*It))
					&& (!InterfaceThatMustBeImplemented || It->ImplementsInterface(InterfaceThatMustBeImplemented))
					&& (!bAllowOnlyPlaceable || !It->HasAnyClassFlags(CLASS_Abstract | CLASS_NotPlaceable)))
				{
					OutOptionStrings.Add(TSharedPtr< FString >(new FString(It->GetName())));
				}
			}
		}
	}

	return bUsesAlternateDisplayValues;
}

void FPropertyHandleBase::NotifyPreChange()
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() )
	{
		PropertyNode->NotifyPreChange( PropertyNode->GetProperty(), Implementation->GetNotifyHook() );
	}
}

void FPropertyHandleBase::NotifyPostChange()
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() )
	{
		FPropertyChangedEvent PropertyChangedEvent( PropertyNode->GetProperty() );
		PropertyNode->NotifyPostChange( PropertyChangedEvent, Implementation->GetNotifyHook() );
	}
}

void FPropertyHandleBase::NotifyFinishedChangingProperties()
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() )
	{
		FPropertyChangedEvent ChangeEvent(PropertyNode->GetProperty(), EPropertyChangeType::ValueSet);
		PropertyNode->FixPropertiesInEvent(ChangeEvent);
		Implementation->GetPropertyUtilities()->NotifyFinishedChangingProperties(ChangeEvent);
	}
}

FPropertyAccess::Result FPropertyHandleBase::SetObjectValueFromSelection()
{
	return Implementation->OnUseSelected();
}

void FPropertyHandleBase::AddRestriction( TSharedRef<const FPropertyRestriction> Restriction )
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() )
	{
		PropertyNode->AddRestriction(Restriction);
	}
}

bool FPropertyHandleBase::IsRestricted(const FString& Value) const
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() )
	{
		return PropertyNode->IsRestricted(Value);
	}
	return false;
}

bool FPropertyHandleBase::IsRestricted(const FString& Value, TArray<FText>& OutReasons) const
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() )
	{
		return PropertyNode->IsRestricted(Value, OutReasons);
	}
	return false;
}

bool FPropertyHandleBase::GenerateRestrictionToolTip(const FString& Value, FText& OutTooltip)const
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() )
	{
		return PropertyNode->GenerateRestrictionToolTip(Value, OutTooltip);
	}
	return false;
}

void FPropertyHandleBase::SetIgnoreValidation(bool bInIgnore)
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetPropertyNode();
	if( PropertyNode.IsValid() )
	{
		PropertyNode->SetNodeFlags( EPropertyNodeFlags::SkipChildValidation, bInIgnore); 
	}
}

/** Implements common property value functions */
#define IMPLEMENT_PROPERTY_VALUE( ClassName ) \
	ClassName::ClassName( TSharedRef<FPropertyNode> PropertyNode, FNotifyHook* NotifyHook, TSharedPtr<IPropertyUtilities> PropertyUtilities ) \
	: FPropertyHandleBase( PropertyNode, NotifyHook, PropertyUtilities )  \
	{}

IMPLEMENT_PROPERTY_VALUE( FPropertyHandleInt )
IMPLEMENT_PROPERTY_VALUE( FPropertyHandleFloat )
IMPLEMENT_PROPERTY_VALUE( FPropertyHandleBool )
IMPLEMENT_PROPERTY_VALUE( FPropertyHandleByte )
IMPLEMENT_PROPERTY_VALUE( FPropertyHandleString )
IMPLEMENT_PROPERTY_VALUE( FPropertyHandleObject )
IMPLEMENT_PROPERTY_VALUE( FPropertyHandleArray )
IMPLEMENT_PROPERTY_VALUE( FPropertyHandleText )

// int32 
bool FPropertyHandleInt::Supports( TSharedRef<FPropertyNode> PropertyNode )
{
	UProperty* Property = PropertyNode->GetProperty();

	if ( Property == nullptr )
	{
		return false;
	}

	const bool bIsInteger = 
			Property->IsA<UInt8Property>()
		||	Property->IsA<UInt16Property>()
		||  Property->IsA<UIntProperty>()
		||	Property->IsA<UInt64Property>()
		||  Property->IsA<UUInt16Property>()
		||  Property->IsA<UUInt32Property>()
		||  Property->IsA<UUInt64Property>();

	// The value is an integer
	return bIsInteger;
}

template <typename PropertyClass, typename ValueType>
ValueType GetIntegerValue(void* PropValue, FPropertyValueImpl& Implementation )
{
	check( Implementation.IsPropertyTypeOf( PropertyClass::StaticClass() ) );
	return Implementation.GetPropertyValue<PropertyClass>(PropValue);
}

FPropertyAccess::Result FPropertyHandleInt::GetValue(int8& OutValue) const
{
	void* PropValue = nullptr;
	FPropertyAccess::Result Res = Implementation->GetValueData(PropValue);

	if (Res == FPropertyAccess::Success)
	{
		OutValue = GetIntegerValue<UInt8Property, int8>(PropValue, *Implementation);
	}

	return Res;
}

FPropertyAccess::Result FPropertyHandleInt::GetValue(int16& OutValue) const
{
	void* PropValue = nullptr;
	FPropertyAccess::Result Res = Implementation->GetValueData(PropValue);

	if (Res == FPropertyAccess::Success)
	{
		OutValue = GetIntegerValue<UInt16Property, int16>(PropValue, *Implementation);
	}

	return Res;
}

FPropertyAccess::Result FPropertyHandleInt::GetValue(int32& OutValue) const
{
	void* PropValue = nullptr;
	FPropertyAccess::Result Res = Implementation->GetValueData(PropValue);

	if (Res == FPropertyAccess::Success)
	{
		OutValue = GetIntegerValue<UIntProperty, int32>(PropValue, *Implementation);
	}

	return Res;
}
FPropertyAccess::Result FPropertyHandleInt::GetValue( int64& OutValue ) const
{
	void* PropValue = nullptr;
	FPropertyAccess::Result Res = Implementation->GetValueData( PropValue );

	if( Res == FPropertyAccess::Success )
	{
		OutValue = GetIntegerValue<UInt64Property,int64>(PropValue, *Implementation);
	}

	return Res;
}

FPropertyAccess::Result FPropertyHandleInt::GetValue(uint16& OutValue) const
{
	void* PropValue = nullptr;
	FPropertyAccess::Result Res = Implementation->GetValueData(PropValue);

	if (Res == FPropertyAccess::Success)
	{
		OutValue = GetIntegerValue<UUInt16Property, uint16>(PropValue, *Implementation);
	}

	return Res;
}

FPropertyAccess::Result FPropertyHandleInt::GetValue(uint32& OutValue) const
{
	void* PropValue = nullptr;
	FPropertyAccess::Result Res = Implementation->GetValueData(PropValue);

	if (Res == FPropertyAccess::Success)
	{
		OutValue = GetIntegerValue<UUInt32Property, uint32>(PropValue, *Implementation);
	}

	return Res;
}

FPropertyAccess::Result FPropertyHandleInt::GetValue(uint64& OutValue) const
{
	void* PropValue = nullptr;
	FPropertyAccess::Result Res = Implementation->GetValueData(PropValue);

	if (Res == FPropertyAccess::Success)
	{
		OutValue = GetIntegerValue<UUInt64Property, uint64>(PropValue, *Implementation);
	}

	return Res;
}

FPropertyAccess::Result FPropertyHandleInt::SetValue(const int8& NewValue, EPropertyValueSetFlags::Type Flags)
{
	FPropertyAccess::Result Res;
	// Clamp the value from any meta data ranges stored on the property value
	int8 FinalValue = ClampIntegerValueFromMetaData<int8>( NewValue, *Implementation->GetPropertyNode() );

	const FString ValueStr = LexicalConversion::ToString(FinalValue);
	Res = Implementation->ImportText(ValueStr, Flags);

	return Res;
}


FPropertyAccess::Result FPropertyHandleInt::SetValue(const int16& NewValue, EPropertyValueSetFlags::Type Flags)
{
	FPropertyAccess::Result Res;
	// Clamp the value from any meta data ranges stored on the property value
	int16 FinalValue = ClampIntegerValueFromMetaData<int16>(NewValue, *Implementation->GetPropertyNode());

	const FString ValueStr = LexicalConversion::ToString(FinalValue);
	Res = Implementation->ImportText(ValueStr, Flags);

	return Res;
}


FPropertyAccess::Result FPropertyHandleInt::SetValue( const int32& NewValue, EPropertyValueSetFlags::Type Flags )
{
	FPropertyAccess::Result Res;
	// Clamp the value from any meta data ranges stored on the property value
	int32 FinalValue = ClampIntegerValueFromMetaData<int32>( NewValue, *Implementation->GetPropertyNode() );

	const FString ValueStr = LexicalConversion::ToString(FinalValue);
	Res = Implementation->ImportText( ValueStr, Flags );

	return Res;
}

FPropertyAccess::Result FPropertyHandleInt::SetValue(const int64& NewValue, EPropertyValueSetFlags::Type Flags)
{
	FPropertyAccess::Result Res;

	// Clamp the value from any meta data ranges stored on the property value
	int64 FinalValue = ClampIntegerValueFromMetaData<int64>(NewValue, *Implementation->GetPropertyNode());

	const FString ValueStr = LexicalConversion::ToString(FinalValue);
	Res = Implementation->ImportText(ValueStr, Flags);
	return Res;
}

FPropertyAccess::Result FPropertyHandleInt::SetValue(const uint16& NewValue, EPropertyValueSetFlags::Type Flags)
{
	FPropertyAccess::Result Res;
	// Clamp the value from any meta data ranges stored on the property value
	uint16 FinalValue = ClampIntegerValueFromMetaData<uint16>(NewValue, *Implementation->GetPropertyNode());

	const FString ValueStr = LexicalConversion::ToString(FinalValue);
	Res = Implementation->ImportText(ValueStr, Flags);

	return Res;
}


FPropertyAccess::Result FPropertyHandleInt::SetValue(const uint32& NewValue, EPropertyValueSetFlags::Type Flags)
{
	FPropertyAccess::Result Res;
	// Clamp the value from any meta data ranges stored on the property value
	uint32 FinalValue = ClampIntegerValueFromMetaData<uint32>(NewValue, *Implementation->GetPropertyNode());

	const FString ValueStr = LexicalConversion::ToString(FinalValue);
	Res = Implementation->ImportText(ValueStr, Flags);

	return Res;
}

FPropertyAccess::Result FPropertyHandleInt::SetValue(const uint64& NewValue, EPropertyValueSetFlags::Type Flags)
{
	FPropertyAccess::Result Res;
	// Clamp the value from any meta data ranges stored on the property value
	uint64 FinalValue = ClampIntegerValueFromMetaData<uint64>(NewValue, *Implementation->GetPropertyNode());

	const FString ValueStr = LexicalConversion::ToString(FinalValue);
	Res = Implementation->ImportText(ValueStr, Flags);
	return Res;
}

// float
bool FPropertyHandleFloat::Supports( TSharedRef<FPropertyNode> PropertyNode )
{
	UProperty* Property = PropertyNode->GetProperty();

	if ( Property == nullptr )
	{
		return false;
	}

	return Property->IsA(UFloatProperty::StaticClass());
}

FPropertyAccess::Result FPropertyHandleFloat::GetValue( float& OutValue ) const
{
	void* PropValue = nullptr;
	FPropertyAccess::Result Res = Implementation->GetValueData( PropValue );

	if( Res == FPropertyAccess::Success )
	{
		OutValue = Implementation->GetPropertyValue<UFloatProperty>(PropValue);
	}

	return Res;
}

FPropertyAccess::Result FPropertyHandleFloat::SetValue( const float& NewValue, EPropertyValueSetFlags::Type Flags )
{
	FPropertyAccess::Result Res;
	// Clamp the value from any meta data ranges stored on the property value
	float FinalValue = ClampValueFromMetaData<float>( NewValue, *Implementation->GetPropertyNode() );

	const FString ValueStr = FString::Printf( TEXT("%f"), FinalValue );
	Res = Implementation->ImportText( ValueStr, Flags );

	return Res;
}

// bool
bool FPropertyHandleBool::Supports( TSharedRef<FPropertyNode> PropertyNode )
{
	UProperty* Property = PropertyNode->GetProperty();

	if ( Property == nullptr )
	{
		return false;
	}

	return Property->IsA(UBoolProperty::StaticClass());
}

FPropertyAccess::Result FPropertyHandleBool::GetValue( bool& OutValue ) const
{
	void* PropValue = nullptr;
	FPropertyAccess::Result Res = Implementation->GetValueData( PropValue );

	if( Res == FPropertyAccess::Success )
	{
		OutValue = Implementation->GetPropertyValue<UBoolProperty>(PropValue);
	}

	return Res;
}

FPropertyAccess::Result FPropertyHandleBool::SetValue( const bool& NewValue, EPropertyValueSetFlags::Type Flags )
{
	FPropertyAccess::Result Res = FPropertyAccess::Fail;

	//These are not localized values because ImportText does not accept localized values!
	FString ValueStr; 
	if( NewValue == false )
	{
		ValueStr = TEXT("False");
	}
	else
	{
		ValueStr = TEXT("True");
	}

	Res = Implementation->ImportText( ValueStr, Flags );

	return Res;
}

bool FPropertyHandleByte::Supports( TSharedRef<FPropertyNode> PropertyNode )
{
	UProperty* Property = PropertyNode->GetProperty();

	if ( Property == nullptr )
	{
		return false;
	}

	return Property->IsA( UByteProperty::StaticClass() );
}

FPropertyAccess::Result FPropertyHandleByte::GetValue( uint8& OutValue ) const
{
	void* PropValue = nullptr;
	FPropertyAccess::Result Res = Implementation->GetValueData( PropValue );

	if( Res == FPropertyAccess::Success )
	{
		OutValue = Implementation->GetPropertyValue<UByteProperty>(PropValue);
	}

	return Res;
}

FPropertyAccess::Result FPropertyHandleByte::SetValue( const uint8& NewValue, EPropertyValueSetFlags::Type Flags )
{
	FPropertyAccess::Result Res;
	FString ValueStr;
	
	UByteProperty* ByteProperty = Cast<UByteProperty>(GetProperty());
	if (ByteProperty && ByteProperty->Enum)
	{
		// Handle Enums using enum names to make sure they're compatible with UByteProperty::ExportText.
		ValueStr = ByteProperty->Enum->GetEnumName(NewValue);
	}
	else
	{
		// Ordinary byte, convert value to string.
		ValueStr = FString::Printf( TEXT("%i"), NewValue );
	}
	Res = Implementation->ImportText( ValueStr, Flags );

	return Res;
}


// String
bool FPropertyHandleString::Supports( TSharedRef<FPropertyNode> PropertyNode )
{
	UProperty* Property = PropertyNode->GetProperty();

	if ( Property == nullptr )
	{
		return false;
	}

	// Supported if the property is a name, string or object/interface that can be set via string
	return	( Property->IsA(UNameProperty::StaticClass()) && Property->GetFName() != NAME_InitialState )
		||	Property->IsA( UStrProperty::StaticClass() )
		||	( Property->IsA( UObjectPropertyBase::StaticClass() ) && !Property->HasAnyPropertyFlags(CPF_InstancedReference) )
		||	Property->IsA(UInterfaceProperty::StaticClass());
}

FPropertyAccess::Result FPropertyHandleString::GetValue( FString& OutValue ) const
{
	return Implementation->GetValueAsString( OutValue );
}

FPropertyAccess::Result FPropertyHandleString::SetValue( const FString& NewValue, EPropertyValueSetFlags::Type Flags )
{
	return Implementation->SetValueAsString( NewValue, Flags );
}

FPropertyAccess::Result FPropertyHandleString::GetValue( FName& OutValue ) const
{
	void* PropValue = nullptr;
	FPropertyAccess::Result Res = Implementation->GetValueData( PropValue );

	if( Res == FPropertyAccess::Success )
	{
		OutValue = Implementation->GetPropertyValue<UNameProperty>(PropValue);
	}

	return Res;
}

FPropertyAccess::Result FPropertyHandleString::SetValue( const FName& NewValue, EPropertyValueSetFlags::Type Flags )
{
	return Implementation->SetValueAsString( NewValue.ToString(), Flags );
}

// Object

bool FPropertyHandleObject::Supports( TSharedRef<FPropertyNode> PropertyNode )
{
	UProperty* Property = PropertyNode->GetProperty();

	if ( Property == nullptr )
	{
		return false;
	}

	return Property->IsA( UObjectPropertyBase::StaticClass() ) || Property->IsA(UInterfaceProperty::StaticClass());
}

FPropertyAccess::Result FPropertyHandleObject::GetValue( UObject*& OutValue ) const
{
	return FPropertyHandleObject::GetValue((const UObject*&)OutValue);
}

FPropertyAccess::Result FPropertyHandleObject::GetValue( const UObject*& OutValue ) const
{
	void* PropValue = nullptr;
	FPropertyAccess::Result Res = Implementation->GetValueData( PropValue );

	if( Res == FPropertyAccess::Success )
	{
		OutValue = Implementation->GetObjectPropertyValue(PropValue);
	}

	return Res;
}

FPropertyAccess::Result FPropertyHandleObject::SetValue( UObject* const& NewValue, EPropertyValueSetFlags::Type Flags )
{
	return FPropertyHandleObject::SetValue((const UObject*)NewValue);
}

FPropertyAccess::Result FPropertyHandleObject::SetValue( const UObject* const& NewValue, EPropertyValueSetFlags::Type Flags )
{
	UProperty* Property = Implementation->GetPropertyNode()->GetProperty();

	bool bResult = false;
	// Instanced references can not be set this way (most likely editinlinenew )
	if( !Property->HasAnyPropertyFlags(CPF_InstancedReference) )
	{
		FString ObjectPathName = NewValue ? NewValue->GetPathName() : TEXT("None");
		bResult = Implementation->SendTextToObjectProperty( ObjectPathName, Flags );
	}

	return bResult ? FPropertyAccess::Success : FPropertyAccess::Fail;
}

FPropertyAccess::Result FPropertyHandleObject::GetValue(FAssetData& OutValue) const
{
	UObject* ObjectValue = nullptr;
	FPropertyAccess::Result	Result = GetValue(ObjectValue);
	
	if ( Result == FPropertyAccess::Success )
	{
		OutValue = FAssetData(ObjectValue);
	}

	return Result;
}

FPropertyAccess::Result FPropertyHandleObject::SetValue(const FAssetData& NewValue, EPropertyValueSetFlags::Type Flags)
{
	UProperty* Property = Implementation->GetPropertyNode()->GetProperty();

	bool bResult = false;
	// Instanced references can not be set this way (most likely editinlinenew )
	if (!Property->HasAnyPropertyFlags(CPF_InstancedReference))
	{
		if ( !Property->IsA( UAssetObjectProperty::StaticClass() ) )
		{
			// Make sure the asset is loaded if we are not a string asset reference.
			NewValue.GetAsset();
		}

		FString ObjectPathName = NewValue.IsValid() ? NewValue.ObjectPath.ToString() : TEXT("None");
		bResult = Implementation->SendTextToObjectProperty(ObjectPathName, Flags);
	}

	return bResult ? FPropertyAccess::Success : FPropertyAccess::Fail;
}

// Vector
bool FPropertyHandleVector::Supports( TSharedRef<FPropertyNode> PropertyNode )
{
	UProperty* Property = PropertyNode->GetProperty();

	if ( Property == nullptr )
	{
		return false;
	}

	UStructProperty* StructProp = Cast<UStructProperty>(Property);

	bool bSupported = false;
	if( StructProp && StructProp->Struct )
	{
		FName StructName = StructProp->Struct->GetFName();

		bSupported = StructName == NAME_Vector ||
			StructName == NAME_Vector2D ||
			StructName == NAME_Vector4 ||
			StructName == NAME_Quat;
	}

	return bSupported;
}

FPropertyHandleVector::FPropertyHandleVector( TSharedRef<class FPropertyNode> PropertyNode, class FNotifyHook* NotifyHook, TSharedPtr<IPropertyUtilities> PropertyUtilities )
	: FPropertyHandleBase( PropertyNode, NotifyHook, PropertyUtilities ) 
{
	const bool bRecurse = false;
	// A vector is a struct property that has 3 children.  We get/set the values from the children
	VectorComponents.Add( MakeShareable( new FPropertyHandleFloat( Implementation->GetChildNode("X", bRecurse).ToSharedRef(), NotifyHook, PropertyUtilities ) ) );

	VectorComponents.Add( MakeShareable( new FPropertyHandleFloat( Implementation->GetChildNode("Y", bRecurse).ToSharedRef(), NotifyHook, PropertyUtilities ) ) );

	if( Implementation->GetNumChildren() > 2 )
	{
		// at least a 3 component vector
		VectorComponents.Add( MakeShareable( new FPropertyHandleFloat( Implementation->GetChildNode("Z",bRecurse).ToSharedRef(), NotifyHook, PropertyUtilities ) ) );
	}
	if( Implementation->GetNumChildren() > 3 )
	{
		// a 4 component vector
		VectorComponents.Add( MakeShareable( new FPropertyHandleFloat( Implementation->GetChildNode("W",bRecurse).ToSharedRef(), NotifyHook, PropertyUtilities ) ) );
	}
}

FPropertyAccess::Result FPropertyHandleVector::GetValue( FVector2D& OutValue ) const
{
	if( VectorComponents.Num() == 2 )
	{
		// To get the value from the vector we read each child.  If reading a child fails, the value for that component is not set
		FPropertyAccess::Result ResX = VectorComponents[0]->GetValue( OutValue.X );
		FPropertyAccess::Result ResY = VectorComponents[1]->GetValue( OutValue.Y );

		if( ResX == FPropertyAccess::Fail || ResY == FPropertyAccess::Fail )
		{
			// If reading any value failed the entire value fails
			return FPropertyAccess::Fail;
		}
		else if( ResX == FPropertyAccess::MultipleValues || ResY == FPropertyAccess::MultipleValues )
		{
			// At least one component had multiple values
			return FPropertyAccess::MultipleValues;
		}
		else
		{
			return FPropertyAccess::Success;
		}
	}

	// Not a 2 component vector
	return FPropertyAccess::Fail;
}

FPropertyAccess::Result FPropertyHandleVector::SetValue( const FVector2D& NewValue, EPropertyValueSetFlags::Type Flags )
{
	// To set the value from the vector we set each child. 
	FPropertyAccess::Result ResX = VectorComponents[0]->SetValue( NewValue.X, Flags );
	FPropertyAccess::Result ResY = VectorComponents[1]->SetValue( NewValue.Y, Flags );

	if( ResX == FPropertyAccess::Fail || ResY == FPropertyAccess::Fail )
	{
		return FPropertyAccess::Fail;
	}
	else
	{
		return FPropertyAccess::Success;
	}
}

FPropertyAccess::Result FPropertyHandleVector::GetValue( FVector& OutValue ) const
{
	if( VectorComponents.Num() == 3 )
	{
		// To get the value from the vector we read each child.  If reading a child fails, the value for that component is not set
		FPropertyAccess::Result ResX = VectorComponents[0]->GetValue( OutValue.X );
		FPropertyAccess::Result ResY = VectorComponents[1]->GetValue( OutValue.Y );
		FPropertyAccess::Result ResZ = VectorComponents[2]->GetValue( OutValue.Z );

		if( ResX == FPropertyAccess::Fail || ResY == FPropertyAccess::Fail || ResZ == FPropertyAccess::Fail )
		{
			// If reading any value failed the entire value fails
			return FPropertyAccess::Fail;
		}
		else if( ResX == FPropertyAccess::MultipleValues || ResY == FPropertyAccess::MultipleValues || ResZ == FPropertyAccess::MultipleValues )
		{
			// At least one component had multiple values
			return FPropertyAccess::MultipleValues;
		}
		else
		{
			return FPropertyAccess::Success;
		}
	}

	// Not a 3 component vector
	return FPropertyAccess::Fail;
}

FPropertyAccess::Result FPropertyHandleVector::SetValue( const FVector& NewValue, EPropertyValueSetFlags::Type Flags )
{
	if( VectorComponents.Num() == 3)
	{
		// To set the value from the vector we set each child. 
		FPropertyAccess::Result ResX = VectorComponents[0]->SetValue( NewValue.X, Flags );
		FPropertyAccess::Result ResY = VectorComponents[1]->SetValue( NewValue.Y, Flags );
		FPropertyAccess::Result ResZ = VectorComponents[2]->SetValue( NewValue.Z, Flags );

		if( ResX == FPropertyAccess::Fail || ResY == FPropertyAccess::Fail || ResZ == FPropertyAccess::Fail )
		{
			return FPropertyAccess::Fail;
		}
		else
		{
			return FPropertyAccess::Success;
		}
	}

	return FPropertyAccess::Fail;
}

FPropertyAccess::Result FPropertyHandleVector::GetValue( FVector4& OutValue ) const
{
	if( VectorComponents.Num() == 4 )
	{
		// To get the value from the vector we read each child.  If reading a child fails, the value for that component is not set
		FPropertyAccess::Result ResX = VectorComponents[0]->GetValue( OutValue.X );
		FPropertyAccess::Result ResY = VectorComponents[1]->GetValue( OutValue.Y );
		FPropertyAccess::Result ResZ = VectorComponents[2]->GetValue( OutValue.Z );
		FPropertyAccess::Result ResW = VectorComponents[3]->GetValue( OutValue.W );

		if( ResX == FPropertyAccess::Fail || ResY == FPropertyAccess::Fail || ResZ == FPropertyAccess::Fail || ResW == FPropertyAccess::Fail )
		{
			// If reading any value failed the entire value fails
			return FPropertyAccess::Fail;
		}
		else if( ResX == FPropertyAccess::MultipleValues || ResY == FPropertyAccess::MultipleValues || ResZ == FPropertyAccess::MultipleValues || ResW == FPropertyAccess::MultipleValues )
		{
			// At least one component had multiple values
			return FPropertyAccess::MultipleValues;
		}
		else
		{
			return FPropertyAccess::Success;
		}
	}

	// Not a 4 component vector
	return FPropertyAccess::Fail;
}


FPropertyAccess::Result FPropertyHandleVector::SetValue( const FVector4& NewValue, EPropertyValueSetFlags::Type Flags )
{
	// To set the value from the vector we set each child. 
	FPropertyAccess::Result ResX = VectorComponents[0]->SetValue( NewValue.X, Flags );
	FPropertyAccess::Result ResY = VectorComponents[1]->SetValue( NewValue.Y, Flags );
	FPropertyAccess::Result ResZ = VectorComponents[2]->SetValue( NewValue.Z, Flags );
	FPropertyAccess::Result ResW = VectorComponents[3]->SetValue( NewValue.W, Flags );

	if( ResX == FPropertyAccess::Fail || ResY == FPropertyAccess::Fail || ResZ == FPropertyAccess::Fail || ResW == FPropertyAccess::Fail )
	{
		return FPropertyAccess::Fail;
	}
	else
	{
		return FPropertyAccess::Success;
	}
}

FPropertyAccess::Result FPropertyHandleVector::GetValue( FQuat& OutValue ) const
{
	FVector4 VectorProxy;
	FPropertyAccess::Result Res = GetValue(VectorProxy);
	if (Res == FPropertyAccess::Success)
	{
		OutValue.X = VectorProxy.X;
		OutValue.Y = VectorProxy.Y;
		OutValue.Z = VectorProxy.Z;
		OutValue.W = VectorProxy.W;
	}

	return Res;
}

FPropertyAccess::Result FPropertyHandleVector::SetValue( const FQuat& NewValue, EPropertyValueSetFlags::Type Flags )
{
	FVector4 VectorProxy;
	VectorProxy.X = NewValue.X;
	VectorProxy.Y = NewValue.Y;
	VectorProxy.Z = NewValue.Z;
	VectorProxy.W = NewValue.W;

	return SetValue(VectorProxy);
}

FPropertyAccess::Result FPropertyHandleVector::SetX( float InValue, EPropertyValueSetFlags::Type Flags )
{
	FPropertyAccess::Result Res = VectorComponents[0]->SetValue( InValue, Flags );

	return Res;
}

FPropertyAccess::Result FPropertyHandleVector::SetY( float InValue, EPropertyValueSetFlags::Type Flags )
{
	FPropertyAccess::Result Res = VectorComponents[1]->SetValue( InValue, Flags );

	return Res;
}

FPropertyAccess::Result FPropertyHandleVector::SetZ( float InValue, EPropertyValueSetFlags::Type Flags )
{
	if( VectorComponents.Num() > 2 )
	{
		FPropertyAccess::Result Res = VectorComponents[2]->SetValue( InValue, Flags );

		return Res;
	}
	
	return FPropertyAccess::Fail;
}

FPropertyAccess::Result FPropertyHandleVector::SetW( float InValue, EPropertyValueSetFlags::Type Flags )
{
	if( VectorComponents.Num() == 4 )
	{
		FPropertyAccess::Result Res = VectorComponents[3]->SetValue( InValue, Flags );
	}

	return FPropertyAccess::Fail;
}

// Rotator

bool FPropertyHandleRotator::Supports( TSharedRef<FPropertyNode> PropertyNode )
{
	UProperty* Property = PropertyNode->GetProperty();

	if ( Property == nullptr )
	{
		return false;
	}

	UStructProperty* StructProp = Cast<UStructProperty>(Property);
	return StructProp && StructProp->Struct->GetFName() == NAME_Rotator;
}

FPropertyHandleRotator::FPropertyHandleRotator( TSharedRef<class FPropertyNode> PropertyNode, FNotifyHook* NotifyHook, TSharedPtr<IPropertyUtilities> PropertyUtilities )
	: FPropertyHandleBase( PropertyNode, NotifyHook, PropertyUtilities ) 
{
	const bool bRecurse = false;
	// A vector is a struct property that has 3 children.  We get/set the values from the children
	RollValue = MakeShareable( new FPropertyHandleFloat( Implementation->GetChildNode("Roll", bRecurse).ToSharedRef(), NotifyHook, PropertyUtilities ) );

	PitchValue = MakeShareable( new FPropertyHandleFloat( Implementation->GetChildNode("Pitch", bRecurse).ToSharedRef(), NotifyHook, PropertyUtilities ) );

	YawValue = MakeShareable( new FPropertyHandleFloat( Implementation->GetChildNode("Yaw", bRecurse).ToSharedRef(), NotifyHook, PropertyUtilities ) );
}


FPropertyAccess::Result FPropertyHandleRotator::GetValue( FRotator& OutValue ) const
{
	// To get the value from the rotator we read each child.  If reading a child fails, the value for that component is not set
	FPropertyAccess::Result ResR = RollValue->GetValue( OutValue.Roll );
	FPropertyAccess::Result ResP = PitchValue->GetValue( OutValue.Pitch );
	FPropertyAccess::Result ResY = YawValue->GetValue( OutValue.Yaw );

	if( ResR == FPropertyAccess::MultipleValues || ResP == FPropertyAccess::MultipleValues || ResY == FPropertyAccess::MultipleValues )
	{
		return FPropertyAccess::MultipleValues;
	}
	else if( ResR == FPropertyAccess::Fail || ResP == FPropertyAccess::Fail || ResY == FPropertyAccess::Fail )
	{
		return FPropertyAccess::Fail;
	}
	else
	{
		return FPropertyAccess::Success;
	}
}

FPropertyAccess::Result FPropertyHandleRotator::SetValue( const FRotator& NewValue, EPropertyValueSetFlags::Type Flags )
{
	// To set the value from the rotator we set each child. 
	FPropertyAccess::Result ResR = RollValue->SetValue( NewValue.Roll, Flags );
	FPropertyAccess::Result ResP = PitchValue->SetValue( NewValue.Pitch, Flags );
	FPropertyAccess::Result ResY = YawValue->SetValue( NewValue.Yaw, Flags );

	if( ResR == FPropertyAccess::Fail || ResP == FPropertyAccess::Fail || ResY == FPropertyAccess::Fail )
	{
		return FPropertyAccess::Fail;
	}
	else
	{
		return FPropertyAccess::Success;
	}
}

FPropertyAccess::Result FPropertyHandleRotator::SetRoll( float InRoll, EPropertyValueSetFlags::Type Flags )
{
	FPropertyAccess::Result Res = RollValue->SetValue( InRoll, Flags );
	return Res;
}

FPropertyAccess::Result FPropertyHandleRotator::SetPitch( float InPitch, EPropertyValueSetFlags::Type Flags )
{
	FPropertyAccess::Result Res = PitchValue->SetValue( InPitch, Flags );
	return Res;
}

FPropertyAccess::Result FPropertyHandleRotator::SetYaw( float InYaw, EPropertyValueSetFlags::Type Flags )
{
	FPropertyAccess::Result Res = YawValue->SetValue( InYaw, Flags );
	return Res;
}


bool FPropertyHandleArray::Supports( TSharedRef<FPropertyNode> PropertyNode )
{
	UProperty* Property = PropertyNode->GetProperty();
	int32 ArrayIndex = PropertyNode->GetArrayIndex();

	// Static array or dynamic array
	return ( ( Property && Property->ArrayDim != 1 && ArrayIndex == -1 ) || Cast<const UArrayProperty>(Property) );
}


FPropertyAccess::Result FPropertyHandleArray::AddItem()
{
	FPropertyAccess::Result Result = FPropertyAccess::Fail;
	if( IsEditable() )
	{
		Implementation->AddChild();
		Result = FPropertyAccess::Success;
	}

	return Result;
}

FPropertyAccess::Result FPropertyHandleArray::EmptyArray()
{
	FPropertyAccess::Result Result = FPropertyAccess::Fail;
	if( IsEditable() )
	{
		Implementation->ClearChildren();
		Result = FPropertyAccess::Success;
	}

	return Result;
}

FPropertyAccess::Result FPropertyHandleArray::Insert( int32 Index )
{
	FPropertyAccess::Result Result = FPropertyAccess::Fail;
	if( IsEditable() && Index < Implementation->GetNumChildren()  )
	{
		Implementation->InsertChild( Index );
		Result = FPropertyAccess::Success;
	}

	return Result;
}

FPropertyAccess::Result FPropertyHandleArray::DuplicateItem( int32 Index )
{
	FPropertyAccess::Result Result = FPropertyAccess::Fail;
	if( IsEditable() && Index < Implementation->GetNumChildren() )
	{
		Implementation->DuplicateChild( Index );
		Result = FPropertyAccess::Success;
	}

	return Result;
}

FPropertyAccess::Result FPropertyHandleArray::DeleteItem( int32 Index )
{
	FPropertyAccess::Result Result = FPropertyAccess::Fail;
	if( IsEditable() && Index < Implementation->GetNumChildren() )
	{
		Implementation->DeleteChild( Index );
		Result = FPropertyAccess::Success;
	}

	return Result;
}

FPropertyAccess::Result FPropertyHandleArray::GetNumElements( uint32 &OutNumItems ) const
{
	OutNumItems = Implementation->GetNumChildren();
	return FPropertyAccess::Success;
}

void FPropertyHandleArray::SetOnNumElementsChanged( FSimpleDelegate& OnChildrenChanged )
{
	Implementation->SetOnRebuildChildren( OnChildrenChanged );
}

TSharedPtr<IPropertyHandleArray> FPropertyHandleArray::AsArray()
{
	return SharedThis(this);
}

TSharedRef<IPropertyHandle> FPropertyHandleArray::GetElement( int32 Index ) const
{
	TSharedPtr<FPropertyNode> PropertyNode = Implementation->GetChildNode( Index );
	return PropertyEditorHelpers::GetPropertyHandle( PropertyNode.ToSharedRef(), Implementation->GetNotifyHook(), Implementation->GetPropertyUtilities() ).ToSharedRef();
}

bool FPropertyHandleArray::IsEditable() const
{
	// Property is editable if its a non-const dynamic array
	return Implementation->HasValidPropertyNode() && !Implementation->IsEditConst() && Implementation->IsPropertyTypeOf(UArrayProperty::StaticClass());
}

// Localized Text
bool FPropertyHandleText::Supports(TSharedRef<FPropertyNode> PropertyNode)
{
	UProperty* Property = PropertyNode->GetProperty();

	if ( Property == nullptr )
	{
		return false;
	}

	// Supported if the property is a text property only
	return Property->IsA(UTextProperty::StaticClass());
}

FPropertyAccess::Result FPropertyHandleText::GetValue(FText& OutValue) const
{
	return Implementation->GetValueAsText(OutValue);
}

FPropertyAccess::Result FPropertyHandleText::SetValue(const FText& NewValue, EPropertyValueSetFlags::Type Flags)
{
	FString StringValue;
	FTextStringHelper::WriteToString(StringValue, NewValue);
	return Implementation->ImportText(StringValue, Flags);
}

FPropertyAccess::Result FPropertyHandleText::SetValue(const FString& NewValue, EPropertyValueSetFlags::Type Flags)
{
	return SetValue(FText::FromString(NewValue), Flags);
}
