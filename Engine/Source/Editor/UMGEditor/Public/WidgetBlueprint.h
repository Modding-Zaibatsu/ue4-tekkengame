// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "UserWidget.h"
#include "WidgetAnimation.h"
#include "DynamicPropertyPath.h"

#include "WidgetBlueprint.generated.h"

class UMovieScene;

/** */
USTRUCT()
struct UMGEDITOR_API FEditorPropertyPathSegment
{
	GENERATED_USTRUCT_BODY()

public:
	FEditorPropertyPathSegment();
	FEditorPropertyPathSegment(const UProperty* InProperty);
	FEditorPropertyPathSegment(const UFunction* InFunction);
	FEditorPropertyPathSegment(const UEdGraph* InFunctionGraph);

	UStruct* GetStruct() const { return Struct; }
	UField* GetMember() const;

	void Rebase(UBlueprint* SegmentBase);
	bool ValidateMember(UDelegateProperty* DelegateProperty, FText& OutError) const;

	FName GetMemberName() const;
	FText GetMemberDisplayText() const;
	FGuid GetMemberGuid() const;

private:

	/** The owner of the path segment (ie. What class or structure was this property from) */
	UPROPERTY()
	UStruct* Struct;

	/** The member name in the structure this segment represents. */
	UPROPERTY()
	FName MemberName;

	/**
	 * The member guid in this structure this segment represents.  If this is valid it should 
	 * be used instead of Name to get the true name.
	 */
	UPROPERTY()
	FGuid MemberGuid;

	/** true if property, false if function */
	UPROPERTY()
	bool IsProperty;
};


/**  */
USTRUCT()
struct UMGEDITOR_API FEditorPropertyPath
{
	GENERATED_USTRUCT_BODY()

public:

	/**  */
	FEditorPropertyPath();

	/**  */
	FEditorPropertyPath(const TArray<UField*>& BindingChain);

	/**  */
	bool Rebase(UBlueprint* SegmentBase);

	/**  */
	bool IsEmpty() const { return Segments.Num() == 0; }

	/**  */
	bool Validate(UDelegateProperty* Destination, FText& OutError) const;

	/**  */
	FText GetDisplayText() const;

	/**  */
	FDynamicPropertyPath ToPropertyPath() const;

public:

	/** The path of properties. */
	UPROPERTY()
	TArray<FEditorPropertyPathSegment> Segments;
};


/** */
USTRUCT()
struct UMGEDITOR_API FDelegateEditorBinding
{
	GENERATED_USTRUCT_BODY()

	/** The member widget the binding is on, must be a direct variable of the UUserWidget. */
	UPROPERTY()
	FString ObjectName;

	/** The property on the ObjectName that we are binding to. */
	UPROPERTY()
	FName PropertyName;

	/** The function that was generated to return the SourceProperty */
	UPROPERTY()
	FName FunctionName;

	/** The property we are bindings to directly on the source object. */
	UPROPERTY()
	FName SourceProperty;

	/**  */
	UPROPERTY()
	FEditorPropertyPath SourcePath;

	/** If it's an actual Function Graph in the blueprint that we're bound to, there's a GUID we can use to lookup that function, to deal with renames better.  This is that GUID. */
	UPROPERTY()
	FGuid MemberGuid;

	UPROPERTY()
	EBindingKind Kind;

	bool operator==( const FDelegateEditorBinding& Other ) const
	{
		// NOTE: We intentionally only compare object name and property name, the function is irrelevant since
		// you're only allowed to bind a property on an object to a single function.
		return ObjectName == Other.ObjectName && PropertyName == Other.PropertyName;
	}

	bool IsBindingValid(UClass* Class, class UWidgetBlueprint* Blueprint, FCompilerResultsLog& MessageLog) const;

	FDelegateRuntimeBinding ToRuntimeBinding(class UWidgetBlueprint* Blueprint) const;
};


/** Struct used only for loading old animations */
USTRUCT()
struct FWidgetAnimation_DEPRECATED
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	UMovieScene* MovieScene;

	UPROPERTY()
	TArray<FWidgetAnimationBinding> AnimationBindings;

	bool SerializeFromMismatchedTag(struct FPropertyTag const& Tag, FArchive& Ar);

};

template<>
struct TStructOpsTypeTraits<FWidgetAnimation_DEPRECATED> : public TStructOpsTypeTraitsBase
{
	enum
	{
		WithSerializeFromMismatchedTag = true,
	};
};


/**
 * The widget blueprint enables extending UUserWidget the user extensible UWidget.
 */
UCLASS(BlueprintType)
class UMGEDITOR_API UWidgetBlueprint : public UBlueprint
{
	GENERATED_UCLASS_BODY()

public:

	//~ Begin UObject Interface
	virtual bool NeedsLoadForClient() const override;
	//~ End UObject Interface

public:

#if WITH_EDITORONLY_DATA
	/** A tree of the widget templates to be created */
	UPROPERTY()
	class UWidgetTree* WidgetTree;

	UPROPERTY()
	TArray< FDelegateEditorBinding > Bindings;

	UPROPERTY()
	TArray<FWidgetAnimation_DEPRECATED> AnimationData_DEPRECATED;

	UPROPERTY()
	TArray<UWidgetAnimation*> Animations;

	/**
	 * Don't directly modify this property to change the palette category.  The actual value is stored 
	 * in the CDO of the UUserWidget, but a copy is stored here so that it's available in the serialized 
	 * Tag data in the asset header for access in the FAssetData.
	 */
	UPROPERTY(AssetRegistrySearchable)
	FString PaletteCategory;
#endif

public:

	/** UObject interface */
	virtual void PostLoad() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	
	//~ Begin UBlueprint Interface
	virtual UClass* GetBlueprintClass() const override;

	virtual bool AllowsDynamicBinding() const override;

	virtual bool SupportedByDefaultBlueprintFactory() const override
	{
		return false;
	}

	virtual void GetReparentingRules(TSet< const UClass* >& AllowedChildrenOfClasses, TSet< const UClass* >& DisallowedChildrenOfClasses) const override;

	/** UWidget blueprints are never data only, should always compile on load (data only blueprints cannot declare new variables) */
	virtual bool AlwaysCompileOnLoad() const override { return true; }
	//~ End UBlueprint Interface

	virtual void GatherDependencies(TSet<TWeakObjectPtr<UBlueprint>>& InDependencies) const override;

	static bool ValidateGeneratedClass(const UClass* InClass);
};
