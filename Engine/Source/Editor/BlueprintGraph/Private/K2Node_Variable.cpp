// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.


#include "BlueprintGraphPrivatePCH.h"

#include "CompilerResultsLog.h"
#include "SlateIconFinder.h"
#include "MessageLog.h"

#define LOCTEXT_NAMESPACE "K2Node"

UK2Node_Variable::UK2Node_Variable(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UK2Node_Variable::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	// Fix old content 
	if(Ar.IsLoading())
	{
		if(Ar.UE4Ver() < VER_UE4_VARK2NODE_USE_MEMBERREFSTRUCT)
		{
			// Copy info into new struct
			VariableReference.SetDirect(VariableName_DEPRECATED, FGuid(), VariableSourceClass_DEPRECATED, bSelfContext_DEPRECATED);
		}

		if(Ar.UE4Ver() < VER_UE4_K2NODE_VAR_REFERENCEGUIDS)
		{
			FGuid VarGuid;
			
			// Do not let this code run for local variables
			if (!VariableReference.IsLocalScope())
			{
				const bool bSelf = VariableReference.IsSelfContext();
				UClass* MemberParentClass = VariableReference.GetMemberParentClass(nullptr);
				if (UBlueprint::GetGuidFromClassByFieldName<UProperty>(bSelf? *GetBlueprint()->GeneratedClass : MemberParentClass, VariableReference.GetMemberName(), VarGuid))
				{
					VariableReference.SetDirect(VariableReference.GetMemberName(), VarGuid, bSelf ? nullptr : MemberParentClass, bSelf);
				}
			}
		}
	}
}

void UK2Node_Variable::SetFromProperty(const UProperty* Property, bool bSelfContext)
{
	SelfContextInfo = bSelfContext ? ESelfContextInfo::Unspecified : ESelfContextInfo::NotSelfContext;
	VariableReference.SetFromField<UProperty>(Property, bSelfContext);
}

bool UK2Node_Variable::CreatePinForVariable(EEdGraphPinDirection Direction, FString InPinName/* = FString()*/)
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	UProperty* VariableProperty = GetPropertyForVariable();
	// favor the skeleton property if possible (in case the property type has 
	// been changed, and not yet compiled).
	if (!VariableReference.IsSelfContext())
	{
		UClass* VariableClass = VariableReference.GetMemberParentClass(GetBlueprint()->GeneratedClass);
		if (UBlueprintGeneratedClass* BpClassOwner = Cast<UBlueprintGeneratedClass>(VariableClass))
		{
			// this variable could currently only be a part of some skeleton 
			// class (the blueprint has not be compiled with it yet), so let's 
			// check the skeleton class as well, see if we can pull pin data 
			// from there...
			UBlueprint* VariableBlueprint = CastChecked<UBlueprint>(BpClassOwner->ClassGeneratedBy, ECastCheckedType::NullAllowed);
			if (VariableBlueprint)
			{
				if (UProperty* SkelProperty = FindField<UProperty>(VariableBlueprint->SkeletonGeneratedClass, VariableReference.GetMemberName()))
				{
					VariableProperty = SkelProperty;
				}
			}
		}
	}

	if (VariableProperty != NULL)
	{
		const FString PinName = InPinName.IsEmpty()? GetVarNameString() : InPinName;
		// Create the pin
		UEdGraphPin* VariablePin = CreatePin(Direction, TEXT(""), TEXT(""), NULL, false, false, PinName);
		K2Schema->ConvertPropertyToPinType(VariableProperty, /*out*/ VariablePin->PinType);
		K2Schema->SetPinDefaultValueBasedOnType(VariablePin);
	}
	else
	{
		if (!VariableReference.IsLocalScope())
		{
			FString WarningMsg = FString::Printf(TEXT("'%s' variable not found. Base class was probably changed."), *GetVarNameString());

			UBlueprint* OwnerBP = GetBlueprint();
			if (OwnerBP && OwnerBP->CurrentMessageLog)
			{
				OwnerBP->CurrentMessageLog->Warning(*FString::Printf(TEXT("@@: %s"), *WarningMsg), this);
			}
			else
			{
				Message_Warn(*WarningMsg);
			}			
		}
		return false;
	}

	return true;
}

void UK2Node_Variable::CreatePinForSelf()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	// Create the self pin
	if (!K2Schema->FindSelfPin(*this, EGPD_Input))
	{
		// Do not create a self pin for locally scoped variables
		if( !VariableReference.IsLocalScope() )
		{
			bool bSelfTarget = VariableReference.IsSelfContext() && (ESelfContextInfo::NotSelfContext != SelfContextInfo);
			UClass* MemberParentClass = VariableReference.GetMemberParentClass(GetBlueprintClassFromNode());
			UClass* TargetClass = MemberParentClass;
			
			// Self Target pins should always make the class be the owning class of the property,
			// so if the node is from a Macro Blueprint, it will hook up as self in any placed Blueprint
			if(bSelfTarget)
			{
				if(UProperty* Property = VariableReference.ResolveMember<UProperty>(GetBlueprintClassFromNode()))
				{
					TargetClass = Property->GetOwnerClass()->GetAuthoritativeClass();
				}
				else
				{
					TargetClass = GetBlueprint()->SkeletonGeneratedClass->GetAuthoritativeClass();
				}
			}
			else if(MemberParentClass && MemberParentClass->ClassGeneratedBy)
			{
				TargetClass = MemberParentClass->GetAuthoritativeClass();
			}

			UEdGraphPin* TargetPin = CreatePin(EGPD_Input, K2Schema->PC_Object, TEXT(""), TargetClass, false, false, K2Schema->PN_Self);
			TargetPin->PinFriendlyName =  LOCTEXT("Target", "Target");

			if (bSelfTarget)
			{
				TargetPin->bHidden = true; // don't show in 'self' context
			}
		}
	}
	else
	{
		//@TODO: Check that the self pin types match!
	}
}

bool UK2Node_Variable::RecreatePinForVariable(EEdGraphPinDirection Direction, TArray<UEdGraphPin*>& OldPins, FString InPinName/* = FString()*/)
{
	// probably the node was pasted to a blueprint without the variable
	// we don't want to beak any connection, so the pin will be recreated from old one, but compiler will throw error

	// find old variable pin
	const UEdGraphPin* OldVariablePin = NULL;
	const FString PinName = InPinName.IsEmpty()? GetVarNameString() : InPinName;
	for(auto Iter = OldPins.CreateConstIterator(); Iter; ++Iter)
	{
		if(const UEdGraphPin* Pin = *Iter)
		{
			if(PinName == Pin->PinName)
			{
				OldVariablePin = Pin;
				break;
			}
		}
	}

	if(NULL != OldVariablePin)
	{
		// create new pin from old one
		UEdGraphPin* VariablePin = CreatePin(Direction, TEXT(""), TEXT(""), NULL, false, false, PinName);
		VariablePin->PinType = OldVariablePin->PinType;
		
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		K2Schema->SetPinDefaultValueBasedOnType(VariablePin);

		Message_Note(*FString::Printf(TEXT("Pin for variable '%s' recreated, but the variable is missing."), *PinName));
		return true;
	}
	else
	{
		Message_Warn(*FString::Printf(TEXT("RecreatePinForVariable: '%s' pin not found"), *PinName));
		return false;
	}
}

FLinearColor UK2Node_Variable::GetNodeTitleColor() const
{
	UProperty* VariableProperty = GetPropertyForVariable();
	if (VariableProperty)
	{
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

		FEdGraphPinType VariablePinType;
		K2Schema->ConvertPropertyToPinType(VariableProperty, VariablePinType);

		return K2Schema->GetPinTypeColor(VariablePinType);
	}

	return FLinearColor::White;
}

FString UK2Node_Variable::GetFindReferenceSearchString() const
{
	FString ResultSearchString;
	if (VariableReference.IsLocalScope())
	{
		ResultSearchString = VariableReference.GetReferenceSearchString(nullptr);
	}
	else
	{
		UProperty* VariableProperty = VariableReference.ResolveMember<UProperty>(GetBlueprintClassFromNode());
		ResultSearchString = VariableReference.GetReferenceSearchString(VariableProperty->GetOwnerClass());
	}
	return ResultSearchString;
}

UK2Node::ERedirectType UK2Node_Variable::DoPinsMatchForReconstruction( const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex ) const 
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	if( OldPin->PinType.PinCategory == K2Schema->PC_Exec )
	{
		return Super::DoPinsMatchForReconstruction(NewPin, NewPinIndex, OldPin, OldPinIndex);
	}

	const bool bPinNamesMatch = (OldPin->PinName == NewPin->PinName);
	const bool bCanMatchSelfs = bPinNamesMatch || ((OldPin->PinName == K2Schema->PN_Self) == (NewPin->PinName == K2Schema->PN_Self));
	const bool bTheSameDirection = (NewPin->Direction == OldPin->Direction);

	if (bCanMatchSelfs && bTheSameDirection)
	{
		// the order that the PinTypes are passed to ArePinTypesCompatible() 
		// matters; object pin types are seen as compatible when the output-
		// pin's type is a subclass of the input-pin's type, so we want to keep 
		// that in mind here (should the pins "MatchForReconstruction" if the 
		// variable has been changed to a super class of the original? what 
		// about a subclass?
		// 
		// if these are output nodes, then it is perfectly acceptable that the 
		// variable has been altered to be a sub-class ref (meaning we should 
		// treat the NewPin as an output)... the opposite applies if the pins 
		// are inputs
		const FEdGraphPinType& InputType  = (OldPin->Direction == EGPD_Output) ? OldPin->PinType : NewPin->PinType;
		const FEdGraphPinType& OutputType = (OldPin->Direction == EGPD_Output) ? NewPin->PinType : OldPin->PinType;

		if (K2Schema->ArePinTypesCompatible(OutputType, InputType))
		{
			// If these are split pins, we need to do some name checking logic
			if (NewPin->ParentPin)
			{
				// If the OldPin is not split, then these don't match
				if (OldPin->ParentPin == nullptr)
				{
					return ERedirectType_None;
				}

				// Go through and find the original variable pin.
				// If the number of steps out to the original variable pin is not the same then these don't match
				const UEdGraphPin* ParentmostNewPin = NewPin;
				const UEdGraphPin* ParentmostOldPin = OldPin;

				while (ParentmostNewPin->ParentPin)
				{
					if (ParentmostOldPin->ParentPin == nullptr)
					{
						return ERedirectType_None;
					}
					ParentmostNewPin = ParentmostNewPin->ParentPin;
					ParentmostOldPin = ParentmostOldPin->ParentPin;
				}

				if (ParentmostOldPin->ParentPin)
				{
					return ERedirectType_None;
				}

				// Compare whether the names, ignoring the original variable's name in the case of renames, match
				FString NewPinPropertyName = NewPin->PinName.RightChop(ParentmostNewPin->PinName.Len() + 1);
				FString OldPinPropertyName = OldPin->PinName.RightChop(ParentmostOldPin->PinName.Len() + 1);

				if (NewPinPropertyName != OldPinPropertyName)
				{
					return ERedirectType_None;
				}
			}

			return ERedirectType_Name;
		}
		else
		{
			const bool bNewPinIsObject = (NewPin->PinType.PinCategory == K2Schema->PC_Object);

			// Special Case: If we had a pin match, and the class isn't loaded 
			//               yet because of a cyclic dependency, temporarily 
			//               cast away the const, and fix up.
			if ( bPinNamesMatch &&
				(bNewPinIsObject || (NewPin->PinType.PinCategory == K2Schema->PC_Interface)) &&
				(NewPin->PinType.PinSubCategoryObject == NULL) )
			{
				// @TODO:  Fix this up to be less hacky
				UBlueprintGeneratedClass* TypeClass = Cast<UBlueprintGeneratedClass>(OldPin->PinType.PinSubCategoryObject.Get());
				if (TypeClass && TypeClass->ClassGeneratedBy && TypeClass->ClassGeneratedBy->HasAnyFlags(RF_BeingRegenerated))
				{
					UEdGraphPin* NonConstNewPin = (UEdGraphPin*)NewPin;
					NonConstNewPin->PinType.PinSubCategoryObject = OldPin->PinType.PinSubCategoryObject.Get();
					return ERedirectType_Name;
				}
			}
			// Special Case: if we have object pins that are "compatible" in the
			//               reverse order (meaning one's type is a sub-class of 
			//               the other's), then they could still be acceptable 
			//               if all their connections are still valid (for 
			//               example: if the OldPin was an output only connected 
			//               to super-class pins)
			else if (bNewPinIsObject && K2Schema->ArePinTypesCompatible(InputType, OutputType))
			{
				bool bLinksCompatible = (OldPin->LinkedTo.Num() > 0) && (OldPin->DefaultObject == nullptr);
				for (UEdGraphPin* OldLink : OldPin->LinkedTo)
				{
					const FEdGraphPinType& LinkInputType  = (OldPin->Direction == EGPD_Input) ? NewPin->PinType : OldLink->PinType;
					const FEdGraphPinType& LinkOutputType = (OldPin->Direction == EGPD_Input) ? OldLink->PinType : NewPin->PinType;
				
					if (!K2Schema->ArePinTypesCompatible(LinkOutputType, LinkInputType))
					{
						bLinksCompatible = false;
						break;
					}
				}

				if (bLinksCompatible)
				{
					return ERedirectType_Name;
				}
			}
			else
			{
				const UClass* PSCOClass = Cast<UClass>(OldPin->PinType.PinSubCategoryObject.Get());
				const bool bOldIsBlueprint = PSCOClass && PSCOClass->IsChildOf(UBlueprint::StaticClass());
				const bool bNewIsClass     = (NewPin->PinType.PinCategory == K2Schema->PC_Class);
				// Special Case: If we're migrating from old blueprint references 
				//               to class references, allow pins to be reconnected if coerced
				if (bNewIsClass && bOldIsBlueprint)
				{
					UEdGraphPin* OldPinNonConst = (UEdGraphPin*)OldPin;
					OldPinNonConst->PinName = NewPin->PinName;
					return ERedirectType_Name;
				}
			}
		}
	}

	return ERedirectType_None;
}

UClass* UK2Node_Variable::GetVariableSourceClass() const
{
	UClass* Result = VariableReference.GetMemberParentClass(GetBlueprintClassFromNode());
	return Result;
}

UProperty* UK2Node_Variable::GetPropertyForVariable() const
{
	const FName VarName = GetVarName();
	UEdGraphPin* VariablePin = FindPin(GetVarNameString());

	UProperty* VariableProperty = VariableReference.ResolveMember<UProperty>(GetBlueprintClassFromNode());

	// if the variable has been deprecated, don't use it
	if(VariableProperty != NULL)
	{
		if (VariableProperty->HasAllPropertyFlags(CPF_Deprecated))
		{
			VariableProperty = NULL;
		}
		// If the variable has been remapped update the pin
		else if (VariablePin && VarName != GetVarName())
		{
			VariablePin->PinName = GetVarNameString();
		}
	}

	return VariableProperty;
}

UEdGraphPin* UK2Node_Variable::GetValuePin() const
{
	UEdGraphPin* Pin = FindPin(GetVarNameString());
	check(Pin == NULL || Pin->Direction == EGPD_Output);
	return Pin;
}

void UK2Node_Variable::ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);

	UProperty* VariableProperty = GetPropertyForVariable();

	// Local variables do not exist until much later in the compilation than this function can provide
	if (VariableProperty == NULL && !VariableReference.IsLocalScope())
	{
		if (!VariableReference.IsDeprecated())
		{
			FString OwnerName;

			UBlueprint* Blueprint = GetBlueprint();
			if (Blueprint != nullptr)
			{
				OwnerName = Blueprint->GetName();
				if (UClass* VarOwnerClass = VariableReference.GetMemberParentClass(Blueprint->GeneratedClass))
				{
					OwnerName = VarOwnerClass->GetName();
				}
			}
			FString const VarName = VariableReference.GetMemberName().ToString();

			FText const WarningFormat = LOCTEXT("VariableNotFound", "Could not find a variable named \"%s\" in '%s'.\nMake sure '%s' has been compiled for @@");
			MessageLog.Warning(*FString::Printf(*WarningFormat.ToString(), *VarName, *OwnerName, *OwnerName), this);
		}
		else
		{
			MessageLog.Warning(*FString::Printf(*LOCTEXT("VariableDeprecated", "Variable '%s' for @@ was deprecated.  Please update it.").ToString(), *VariableReference.GetMemberName().ToString()), this);
		}
	}

	if (VariableProperty && (VariableProperty->ArrayDim > 1))
	{
		MessageLog.Warning(*LOCTEXT("StaticArray_Warning", "@@ - the native property is a static array, which is not supported by blueprints").ToString(), this);
	}
}

FSlateIcon UK2Node_Variable::GetIconAndTint(FLinearColor& ColorOut) const
{
	const UStruct* VarScope = VariableReference.IsLocalScope() ?
		VariableReference.GetMemberScope(GetBlueprintClassFromNode()) :
		GetVariableSourceClass();

	return GetVariableIconAndColor(VarScope, GetVarName(), ColorOut);
}

FSlateIcon UK2Node_Variable::GetVarIconFromPinType(const FEdGraphPinType& InPinType, FLinearColor& IconColorOut)
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	IconColorOut = K2Schema->GetPinTypeColor(InPinType);

	if(InPinType.bIsArray)
	{
		return FSlateIcon("EditorStyle", "Kismet.AllClasses.ArrayVariableIcon");
	}
	else if(InPinType.PinSubCategoryObject.IsValid())
	{
		if(UClass* Class = Cast<UClass>(InPinType.PinSubCategoryObject.Get()))
		{
			return FSlateIconFinder::FindIconForClass( Class );
		}
	}

	return FSlateIcon("EditorStyle", "Kismet.AllClasses.VariableIcon");
}

FText UK2Node_Variable::GetToolTipHeading() const
{
	FText Heading = Super::GetToolTipHeading();

	// attempt to reflect the node's GetCornerIcon() with some tooltip documentation 
	FText IconTag;
	if ( UProperty const* VariableProperty = VariableReference.ResolveMember<UProperty>(GetBlueprintClassFromNode()) )
	{
		if (VariableProperty->HasAllPropertyFlags(CPF_Net | CPF_EditorOnly))
		{
			IconTag = LOCTEXT("ReplicatedEditorOnlyVar", "Editor-Only | Replicated");
		}
		else if (VariableProperty->HasAnyPropertyFlags(CPF_Net))
		{
			IconTag = LOCTEXT("ReplicatedVar", "Replicated");
		}
		else if (VariableProperty->HasAnyPropertyFlags(CPF_EditorOnly))
		{
			IconTag = LOCTEXT("EditorOnlyVar", "Editor-Only");
		}
	}

	if (Heading.IsEmpty())
	{
		return IconTag;
	}
	else if (!IconTag.IsEmpty())
	{
		Heading = FText::Format(FText::FromString("{0}\n{1}"), IconTag, Heading);
	}
	return Heading;
}

void UK2Node_Variable::GetNodeAttributes( TArray<TKeyValuePair<FString, FString>>& OutNodeAttributes ) const
{
	UProperty* VariableProperty = GetPropertyForVariable();
	const FString VariableName = VariableProperty ? VariableProperty->GetName() : TEXT( "InvalidVariable" );
	OutNodeAttributes.Add( TKeyValuePair<FString, FString>( TEXT( "Type" ), TEXT( "Variable" ) ));
	OutNodeAttributes.Add( TKeyValuePair<FString, FString>( TEXT( "Class" ), GetClass()->GetName() ));
	OutNodeAttributes.Add( TKeyValuePair<FString, FString>( TEXT( "Name" ), VariableName ));
}

FSlateIcon UK2Node_Variable::GetVariableIconAndColor(const UStruct* VarScope, FName VarName, FLinearColor& IconColorOut)
{
	if(VarScope != NULL)
	{
		UProperty* Property = FindField<UProperty>(VarScope, VarName);
		if(Property != NULL)
		{
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

			FEdGraphPinType PinType;
			if(K2Schema->ConvertPropertyToPinType(Property,  PinType)) // use schema to get the color
			{
				return GetVarIconFromPinType(PinType, IconColorOut);
			}
		}
	}

	return FSlateIcon("EditorStyle", "Kismet.AllClasses.VariableIcon");
}


void UK2Node_Variable::CheckForErrors(const UEdGraphSchema_K2* Schema, FCompilerResultsLog& MessageLog)
{
	if(!VariableReference.IsSelfContext() && VariableReference.GetMemberParentClass(GetBlueprintClassFromNode()) != NULL)
	{
		// Check to see if we're not a self context, if we have a valid context.  It may have been purged because of a dead execution chain
		UEdGraphPin* ContextPin = Schema->FindSelfPin(*this, EGPD_Input);
		if((ContextPin != NULL) && (ContextPin->LinkedTo.Num() == 0) && (ContextPin->DefaultObject == NULL))
		{
			MessageLog.Error(*LOCTEXT("VarNodeError_InvalidVarTarget", "Variable node @@ uses an invalid target.  It may depend on a node that is not connected to the execution chain, and got purged.").ToString(), this);
		}
	}
}

void UK2Node_Variable::ReconstructNode()
{
	// update the variable reference if the property was renamed
	UClass* const VarClass = GetVariableSourceClass();
	if (VarClass)
	{
		bool bRemappedProperty = false;
		UClass* SearchClass = VarClass;
		while (SearchClass != NULL)
		{
			const TMap<FName, FName>* const ClassTaggedPropertyRedirects = UStruct::TaggedPropertyRedirects.Find( SearchClass->GetFName() );
			if (ClassTaggedPropertyRedirects)
			{
				const FName* const NewPropertyName = ClassTaggedPropertyRedirects->Find( VariableReference.GetMemberName() );
				if (NewPropertyName)
				{
					if (VariableReference.IsSelfContext())
					{
						VariableReference.SetSelfMember( *NewPropertyName );
					}
					else
					{
						VariableReference.SetExternalMember( *NewPropertyName, VarClass );
					}

					// found, can break
					bRemappedProperty = true;
					break;
				}
			}

			SearchClass = SearchClass->GetSuperClass();
		}

		if (!bRemappedProperty)
		{
			static FName OldVariableName(TEXT("UpdatedComponent"));
			static FName NewVariableName(TEXT("UpdatedPrimitive"));
			bRemappedProperty = RemapRestrictedLinkReference(OldVariableName, NewVariableName, UMovementComponent::StaticClass(), UPrimitiveComponent::StaticClass(), true);
		}
	}

	const FGuid VarGuid = VariableReference.GetMemberGuid();
	if (VarGuid.IsValid())
	{
		const FName VarName = UBlueprint::GetFieldNameFromClassByGuid<UProperty>(VarClass, VarGuid);
		if (VarName != NAME_None && VarName != VariableReference.GetMemberName())
		{
			if (VariableReference.IsSelfContext())
			{
				VariableReference.SetSelfMember( VarName );
			}
			else
			{
				VariableReference.SetExternalMember( VarName, VarClass );
			}
		}
	}

	Super::ReconstructNode();
}


bool UK2Node_Variable::RemapRestrictedLinkReference(FName OldVariableName, FName NewVariableName, const UClass* MatchInVariableClass, const UClass* RemapIfLinkedToClass, bool bLogWarning)
{
	bool bRemapped = false;
	if (VariableReference.GetMemberName() == OldVariableName)
	{
		UClass* const VarClass = GetVariableSourceClass();
		if (VarClass->IsChildOf(MatchInVariableClass))
		{
			UEdGraphPin* VariablePin = GetValuePin();
			if (VariablePin)
			{
				for (UEdGraphPin* OtherPin : VariablePin->LinkedTo)
				{
					if (OtherPin != nullptr && VariablePin->PinType.PinCategory == OtherPin->PinType.PinCategory)
					{
						// If any other pin we are linked to is a more restricted type, we need to do the remap.
						const UClass* OtherPinClass = Cast<UClass>(OtherPin->PinType.PinSubCategoryObject.Get());
						if (OtherPinClass && OtherPinClass->IsChildOf(RemapIfLinkedToClass))
						{
							if (VariableReference.IsSelfContext())
							{
								VariableReference.SetSelfMember(NewVariableName);
							}
							else
							{
								VariableReference.SetExternalMember(NewVariableName, VarClass);
							}
							bRemapped = true;
							break;
						}
					}
				}
			}
		}
	}

	if (bRemapped && bLogWarning && GetBlueprint())
	{
		FMessageLog("BlueprintLog").Warning(
			FText::Format(
			LOCTEXT("RemapRestrictedLinkReference", "{0}: Variable '{1}' was automatically changed to '{2}'. Verify that logic works as intended. (This warning will disappear once the blueprint has been resaved)"),
			FText::FromString(GetBlueprint()->GetPathName()),
			FText::FromString(MatchInVariableClass->GetName() + TEXT(".") + OldVariableName.ToString()),
			FText::FromString(MatchInVariableClass->GetName() + TEXT(".") + NewVariableName.ToString())
			));
	}

	return bRemapped;
}


FName UK2Node_Variable::GetCornerIcon() const
{
	if (const UProperty* VariableProperty = VariableReference.ResolveMember<UProperty>(GetBlueprintClassFromNode()))
	{
		if (VariableProperty->HasAllPropertyFlags(CPF_Net))
		{
			return TEXT("Graph.Replication.Replicated");
		}
		else if (VariableProperty->HasAllPropertyFlags(CPF_EditorOnly))
		{
			return TEXT("Graph.Editor.EditorOnlyIcon");
		}
	}

	return Super::GetCornerIcon();
}

bool UK2Node_Variable::HasExternalDependencies(TArray<class UStruct*>* OptionalOutput) const
{
	UClass* SourceClass = GetVariableSourceClass();
	UBlueprint* SourceBlueprint = GetBlueprint();
	const bool bResult = (SourceClass && (SourceClass->ClassGeneratedBy != SourceBlueprint));
	if (bResult && OptionalOutput)
	{
		OptionalOutput->AddUnique(SourceClass);
	}
	const bool bSuperResult = Super::HasExternalDependencies(OptionalOutput);
	return bSuperResult || bResult;
}

FString UK2Node_Variable::GetDocumentationLink() const
{
	if( UProperty* Property = GetPropertyForVariable() )
	{
		// discover if the variable property is a non blueprint user variable
		UClass* SourceClass = Property->GetOwnerClass();
		if( SourceClass && SourceClass->ClassGeneratedBy == NULL )
		{
			UStruct* OwnerStruct = Property->GetOwnerStruct();

			if( OwnerStruct )
			{
				return FString::Printf( TEXT("Shared/Types/%s%s"), OwnerStruct->GetPrefixCPP(), *OwnerStruct->GetName() );
			}
		}
	}
	return TEXT( "" );
}

FString UK2Node_Variable::GetDocumentationExcerptName() const
{
	return GetVarName().ToString();
}

void UK2Node_Variable::AutowireNewNode(UEdGraphPin* FromPin)
{
	const UEdGraphSchema_K2* K2Schema = CastChecked<UEdGraphSchema_K2>(GetSchema());

	// Do some auto-connection
	if (FromPin != NULL)
	{
		bool bConnected = false;
		if(FromPin->Direction == EGPD_Output)
		{
			// If the source pin has a valid PinSubCategoryObject, we might be doing BP Comms, so check if it is a class
			if(FromPin->PinType.PinSubCategoryObject.IsValid() && FromPin->PinType.PinSubCategoryObject->IsA(UClass::StaticClass()))
			{
				UProperty* VariableProperty = VariableReference.ResolveMember<UProperty>(GetBlueprintClassFromNode());
				if(VariableProperty)
				{
					UClass* PropertyOwner = VariableProperty->GetOwnerClass();
					if (PropertyOwner != nullptr)
					{
						PropertyOwner = PropertyOwner->GetAuthoritativeClass();
					}

					// BP Comms is highly likely at this point, if the source pin's type is a child of the variable's owner class, let's conform the "Target" pin
					if(FromPin->PinType.PinSubCategoryObject == PropertyOwner || dynamic_cast<UClass*>(FromPin->PinType.PinSubCategoryObject.Get())->IsChildOf(PropertyOwner))
					{
						UEdGraphPin* TargetPin = FindPin(K2Schema->PN_Self);
						if (TargetPin)
						{
							TargetPin->PinType.PinSubCategoryObject = PropertyOwner;

							if(K2Schema->TryCreateConnection(FromPin, TargetPin))
							{
								bConnected = true;

								// Setup the VariableReference correctly since it may no longer be a self member
								VariableReference.SetFromField<UProperty>(GetPropertyForVariable(), false);
								TargetPin->bHidden = false;
								FromPin->GetOwningNode()->NodeConnectionListChanged();
								this->NodeConnectionListChanged();
							}
						}
					}
				}
			}
		}

		if(!bConnected)
		{
			Super::AutowireNewNode(FromPin);
		}
	}
}

FBPVariableDescription const* UK2Node_Variable::GetBlueprintVarDescription() const
{
	FName const& VarName = VariableReference.GetMemberName();
	UStruct const* VariableScope = VariableReference.GetMemberScope(GetBlueprintClassFromNode());

	bool const bIsLocalVariable = (VariableScope != nullptr);
	if (bIsLocalVariable)
	{
		return FBlueprintEditorUtils::FindLocalVariable(GetBlueprint(), VariableScope, VarName);
	}
	else if (UProperty const* VarProperty = GetPropertyForVariable())
	{
		UClass const* SourceClass = VarProperty->GetOwnerClass();
		UBlueprint const* SourceBlueprint = (SourceClass != nullptr) ? Cast<UBlueprint>(SourceClass->ClassGeneratedBy) : nullptr;

		if (SourceBlueprint != nullptr)
		{
			int32 const VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(SourceBlueprint, VarName);
			return &SourceBlueprint->NewVariables[VarIndex];
		}
	}
	return nullptr;
}

bool UK2Node_Variable::CanPasteHere(const UEdGraph* TargetGraph) const
{
	// Do not allow pasting of variables in BPs that cannot handle them
	if ( FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph)->BlueprintType == BPTYPE_MacroLibrary && VariableReference.IsSelfContext() )
	{
		// Self variables must be from a parent class to the macro BP
		if(UProperty* Property = VariableReference.ResolveMember<UProperty>(GetBlueprintClassFromNode()))
		{
			const UClass* CurrentClass = GetBlueprint()->SkeletonGeneratedClass->GetAuthoritativeClass();
			const UClass* PropertyClass = Property->GetOwnerClass()->GetAuthoritativeClass();
			const bool bIsChildOf = CurrentClass->IsChildOf(PropertyClass);
			return bIsChildOf;
		}
		return false;
	}
	return true;
}

void UK2Node_Variable::PostPasteNode()
{
	Super::PostPasteNode();

	UBlueprint* Blueprint = GetBlueprint();
	bool bInvalidateVariable = false;

	if (VariableReference.ResolveMember<UProperty>(Blueprint) == nullptr)
	{
		bInvalidateVariable = true;
	}
	else if (VariableReference.IsLocalScope())
	{
		// Local scoped variables should always validate whether they are being placed in the same graph as their scope
		// ResolveMember will not return nullptr when the graph changes but the Blueprint remains the same.
		UEdGraph* ScopeGraph = FBlueprintEditorUtils::FindScopeGraph(Blueprint, VariableReference.GetMemberScope(GetBlueprintClassFromNode()));
		if(ScopeGraph != GetGraph())
		{
			bInvalidateVariable = true;
		}
	}
		
	if (bInvalidateVariable)
	{
		// This invalidates the local scope
		VariableReference.InvalidateScope();

		// If the current graph is a Function graph, look to see if there is a compatible local variable (same name)
		if (GetGraph()->GetSchema()->GetGraphType(GetGraph()) == GT_Function)
		{
			UEdGraph* FunctionGraph = FBlueprintEditorUtils::GetTopLevelGraph(GetGraph());
			FBPVariableDescription* VariableDescription = FBlueprintEditorUtils::FindLocalVariable(Blueprint, FunctionGraph, VariableReference.GetMemberName());
			if(VariableDescription)
			{
				VariableReference.SetLocalMember(VariableReference.GetMemberName(), FunctionGraph->GetName(), VariableReference.GetMemberGuid());
			}
		}
		// If no variable was found, ResolveMember should automatically find a member variable with the same name in the current Blueprint and hook up to it as expected
	}
}

bool UK2Node_Variable::IsDeprecated() const
{
	if (Super::IsDeprecated() || VariableReference.IsDeprecated())
	{
		return true;
	}

	UProperty* VariableProperty = VariableReference.ResolveMember<UProperty>(GetBlueprintClassFromNode());
	if (VariableProperty 
		&& (VariableProperty->HasAllPropertyFlags(CPF_Deprecated) || VariableProperty->HasMetaData(FBlueprintMetadata::MD_DeprecationMessage)))
	{
		return true;
	}
	return false;
}

FString UK2Node_Variable::GetDeprecationMessage() const
{
	UProperty* VariableProperty = VariableReference.ResolveMember<UProperty>(GetBlueprintClassFromNode());
	if (VariableProperty && VariableProperty->HasMetaData(FBlueprintMetadata::MD_DeprecationMessage))
	{
		return FString::Printf(TEXT("%s %s"), *LOCTEXT("PropertyDeprecated_Warning", "@@ is deprecated;").ToString(), *VariableProperty->GetMetaData(FBlueprintMetadata::MD_DeprecationMessage));
	}

	return Super::GetDeprecationMessage();
}

#undef LOCTEXT_NAMESPACE
