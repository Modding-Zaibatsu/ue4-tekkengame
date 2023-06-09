// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "EdGraph/EdGraphNode.h"
#include "BlueprintNodeSignature.h"
#include "EngineLogs.h"
#include "K2Node.generated.h"

class UActorComponent;
class UBlueprintNodeSpawner;
class FBlueprintActionDatabaseRegistrar;
class UDynamicBlueprintBinding;

/** Helper structure to cache old data for optional pins so the data can be restored during reconstruction */
struct FOldOptionalPinSettings
{
	/** TRUE if optional pin was previously visible */
	bool bOldVisibility;
	/** TRUE if the optional pin's override value was previously enabled */
	bool bIsOldOverrideEnabled;
	/** TRUE if the optional pin's value was previously editable */
	bool bIsOldSetValuePinVisible;
	/** TRUE if the optional pin's override value was previously editable */
	bool bIsOldOverridePinVisible;

	FOldOptionalPinSettings(bool bInOldVisibility, bool bInIsOldOverrideEnabled, bool bInIsOldSetValuePinVisible, bool bInIsOldOverridePinVisible)
		: bOldVisibility(bInOldVisibility)
		, bIsOldOverrideEnabled(bInIsOldOverrideEnabled)
		, bIsOldSetValuePinVisible(bInIsOldSetValuePinVisible)
		, bIsOldOverridePinVisible(bInIsOldOverridePinVisible)
	{}
};

USTRUCT()
struct FOptionalPinFromProperty
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category= OptionalPin, BlueprintReadOnly)
	FName PropertyName;

	UPROPERTY(EditAnywhere, Category= OptionalPin, BlueprintReadOnly)
	FString PropertyFriendlyName;

	UPROPERTY(EditAnywhere, Category= OptionalPin, BlueprintReadOnly)
	FText PropertyTooltip;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=OptionalPin)
	bool bShowPin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=OptionalPin)
	bool bCanToggleVisibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=OptionalPin)
	bool bPropertyIsCustomized;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=OptionalPin)
	FName CategoryName;

	UPROPERTY(EditAnywhere, Category=OptionalPin)
	bool bHasOverridePin;

	/** TRUE if the override value is enabled for use */
	UPROPERTY(EditAnywhere, Category = OptionalPin)
	bool bIsOverrideEnabled;

	/** TRUE if the override value should be set through this pin */
	UPROPERTY(EditAnywhere, Category = OptionalPin)
	bool bIsSetValuePinVisible;

	/** TRUE if the override pin is visible */
	UPROPERTY(EditAnywhere, Category = OptionalPin)
	bool bIsOverridePinVisible;

	FOptionalPinFromProperty()
		: bIsOverrideEnabled(true)
		, bIsSetValuePinVisible(true)
		, bIsOverridePinVisible(true)
	{
	}
	
	FOptionalPinFromProperty(FName InPropertyName, bool bInShowPin, bool bInCanToggleVisibility, const FString& InFriendlyName, const FText& InTooltip, bool bInPropertyIsCustomized, FName InCategoryName, bool bInHasOverridePin)
		: PropertyName(InPropertyName)
		, PropertyFriendlyName(InFriendlyName)
#if WITH_EDITORONLY_DATA
		, PropertyTooltip(InTooltip)
#endif
		, bShowPin(bInShowPin)
		, bCanToggleVisibility(bInCanToggleVisibility)
		, bPropertyIsCustomized(bInPropertyIsCustomized)
		, CategoryName(InCategoryName)
		, bHasOverridePin(bInHasOverridePin)
		, bIsOverrideEnabled(true)
		, bIsSetValuePinVisible(true)
		, bIsOverridePinVisible(true)
	{
	}
};

// Manager to build or refresh a list of optional pins
struct BLUEPRINTGRAPH_API FOptionalPinManager
{
public:
	// Should the specified property be displayed by default
	virtual void GetRecordDefaults(UProperty* TestProperty, FOptionalPinFromProperty& Record) const;

	// Can this property be managed as an optional pin (with the ability to be shown or hidden)
	virtual bool CanTreatPropertyAsOptional(UProperty* TestProperty) const;

	// Reconstructs the specified property array using the SourceStruct
	// @param [in,out] Properties	The property array
	// @param SourceStruct			The source structure to update the properties array from
	// @param bDefaultVisibility
	void RebuildPropertyList(TArray<FOptionalPinFromProperty>& Properties, UStruct* SourceStruct);

	// Creates a pin for each visible property on the specified node
	void CreateVisiblePins(TArray<FOptionalPinFromProperty>& Properties, UStruct* SourceStruct, EEdGraphPinDirection Direction, class UK2Node* TargetNode, uint8* StructBasePtr = NULL);

	// Customize automatically created pins if desired
	virtual void CustomizePinData(UEdGraphPin* Pin, FName SourcePropertyName, int32 ArrayIndex, UProperty* Property = NULL) const {}
protected:
	virtual void PostInitNewPin(UEdGraphPin* Pin, FOptionalPinFromProperty& Record, int32 ArrayIndex, UProperty* Property, uint8* PropertyAddress) const {}
	virtual void PostRemovedOldPin(FOptionalPinFromProperty& Record, int32 ArrayIndex, UProperty* Property, uint8* PropertyAddress) const {}
	void RebuildProperty(UProperty* TestProperty, FName CategoryName, TArray<FOptionalPinFromProperty>& Properties, UStruct* SourceStruct, TMap<FName, FOldOptionalPinSettings>& OldSettings);
};

enum ERenamePinResult
{
	ERenamePinResult_Success,
	ERenamePinResult_NoSuchPin,
	ERenamePinResult_NameCollision
};

/**
 * Abstract base class of all blueprint graph nodes.
 */
UCLASS(abstract, MinimalAPI)
class UK2Node : public UEdGraphNode
{
	GENERATED_UCLASS_BODY()

	// UObject interface
	BLUEPRINTGRAPH_API virtual void PostLoad() override;
	// End of UObject interface

	// UEdGraphNode interface
	BLUEPRINTGRAPH_API virtual void ReconstructNode() override;
	BLUEPRINTGRAPH_API virtual FLinearColor GetNodeTitleColor() const override;
	BLUEPRINTGRAPH_API virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	BLUEPRINTGRAPH_API void PinConnectionListChanged(UEdGraphPin* Pin) override;
    BLUEPRINTGRAPH_API virtual UObject* GetJumpTargetForDoubleClick() const override;
	BLUEPRINTGRAPH_API virtual FString GetDocumentationLink() const override;
	BLUEPRINTGRAPH_API virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;
	BLUEPRINTGRAPH_API virtual bool ShowPaletteIconOnNode() const override { return true; }
	BLUEPRINTGRAPH_API virtual bool AllowSplitPins() const override;
	BLUEPRINTGRAPH_API virtual UEdGraphPin* GetPassThroughPin(const UEdGraphPin* FromPin) const override;
	BLUEPRINTGRAPH_API virtual bool IsInDevelopmentMode() const override;
	// End of UEdGraphNode interface

	// K2Node interface

	/**
	 * Reallocate pins during reconstruction; by default ignores the old pins and calls AllocateDefaultPins()
	 * If you override this to create additional pins you likely need to call RestoreSplitPins to restore any
	 * pins that have been split (e.g. a vector pin split into its components)
	 */
	BLUEPRINTGRAPH_API virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins);

	/** Returns whether this node is considered 'pure' by the compiler */
	virtual bool IsNodePure() const { return false; }

	/** 
	 * Returns whether or not this node has dependencies on an external structure 
	 * If OptionalOutput isn't null, it should be filled with the known dependencies objects (Classes, Structures, Functions, etc).
	 */
	virtual bool HasExternalDependencies(TArray<class UStruct*>* OptionalOutput = NULL) const { return false; }

	/** Returns whether this node can have breakpoints placed on it in the debugger */
	virtual bool CanPlaceBreakpoints() const { return !IsNodePure(); }

	/** Return whether to draw this node as an entry */
	virtual bool DrawNodeAsEntry() const { return false; }

	/** Return whether to draw this node as an entry */
	virtual bool DrawNodeAsExit() const { return false; }

	/** Return whether to draw this node as a small variable node */
	virtual bool DrawNodeAsVariable() const { return false; }

	/** Should draw compact */
	virtual bool ShouldDrawCompact() const { return false; }

	/** Return title if drawing this node in 'compact' mode */
	virtual FText GetCompactNodeTitle() const { return GetNodeTitle(ENodeTitleType::FullTitle); }

	/** */
	BLUEPRINTGRAPH_API virtual FText GetToolTipHeading() const;

	/** Return tooltip text that explains the result of an active breakpoint on this node */
	BLUEPRINTGRAPH_API virtual FText GetActiveBreakpointToolTipText() const;

	/**
	 * Determine if the node of this type should be filtered in the actions menu
	 */
	virtual bool IsActionFilteredOut(class FBlueprintActionFilter const& Filter) { return false; }

	/** Should draw as a bead with no location of it's own */
	virtual bool ShouldDrawAsBead() const { return false; }

	/** Return whether the node's properties display in the blueprint details panel */
	virtual bool ShouldShowNodeProperties() const { return false; }

	/** Return whether the node's execution pins should support the remove execution pin action */
	virtual bool CanEverRemoveExecutionPin() const { return false; }

	/** Called when the connection list of one of the pins of this node is changed in the editor, after the pin has had it's literal cleared */
	virtual void NotifyPinConnectionListChanged(UEdGraphPin* Pin) {}

	/**
	 * Creates the pins required for a function entry or exit node (only inputs as outputs or outputs as inputs respectively)
	 *
	 * @param	Function	The function being implemented that the entry or exit nodes are for.
	 * @param	bForFunctionEntry	True indicates a function entry node, false indicates an exit node.
	 *
	 * @return	true on success.
	 */
	bool CreatePinsForFunctionEntryExit(const UFunction* Function, bool bForFunctionEntry);

	BLUEPRINTGRAPH_API virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph);
	BLUEPRINTGRAPH_API virtual class FNodeHandlingFunctor* CreateNodeHandler(class FKismetCompilerContext& CompilerContext) const { return NULL; }

	BLUEPRINTGRAPH_API void ExpandSplitPin(class FKismetCompilerContext* CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* Pin);

	/**
	 * Query if this is a node that is safe to ignore (e.g., a comment node or other non-structural annotation that can be pruned with no warnings).
	 *
	 * @return	true if node safe to ignore.
	 */
	virtual bool IsNodeSafeToIgnore() const { return false; }

	/** Tries to get a template object from this node. Will only work for 'Add Component' nodes */
	virtual UActorComponent* GetTemplateFromNode() const { return NULL; }

	/** Called at the end of ReconstructNode, allows node specific work to be performed */
	BLUEPRINTGRAPH_API virtual void PostReconstructNode();

	/** Return true if adding/removing this node requires calling MarkBlueprintAsStructurallyModified on the Blueprint */
	virtual bool NodeCausesStructuralBlueprintChange() const { return false; }

	/** Return true if this node has a valid blueprint outer, or false otherwise.  Useful for checking validity of the node for UI list refreshes, which may operate on stale nodes for a frame until the list is refreshed */
	BLUEPRINTGRAPH_API bool HasValidBlueprint() const;

	/** Get the Blueprint object to which this node belongs */
	BLUEPRINTGRAPH_API UBlueprint* GetBlueprint() const;

	/** Get the input execution pin if this node is impure (will return NULL when IsNodePure() returns true) */
	BLUEPRINTGRAPH_API UEdGraphPin* GetExecPin() const;

	/**
	 * If this node references an actor in the level that should be selectable by "Find Actors In Level," this will return a reference to that actor
	 *
	 * @return	Reference to an actor corresponding to this node, or NULL if no actors are referenced
	 */
	virtual AActor* GetReferencedLevelActor() const { return NULL; }

	// Can this node be created under the specified schema
	BLUEPRINTGRAPH_API virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* DesiredSchema) const override;

	// Renames an existing pin on the node.
	BLUEPRINTGRAPH_API virtual ERenamePinResult RenameUserDefinedPin(const FString& OldName, const FString& NewName, bool bTest = false);

	// Returns which dynamic binding class (if any) to use for this node
	BLUEPRINTGRAPH_API virtual UClass* GetDynamicBindingClass() const { return NULL; }

	// Puts information about this node into the dynamic binding object
	BLUEPRINTGRAPH_API virtual void RegisterDynamicBinding(UDynamicBlueprintBinding* BindingObject) const { };

	/**
	 * Handles inserting the node between the FromPin and what the FromPin was original connected to
	 *
	 * @param FromPin			The pin this node is being spawned from
	 * @param NewLinkPin		The new pin the FromPin will connect to
	 * @param OutNodeList		Any nodes that are modified will get added to this list for notification purposes
	 */
	void InsertNewNode(UEdGraphPin* FromPin, UEdGraphPin* NewLinkPin, TSet<UEdGraphNode*>& OutNodeList);

	/** @return true if this function can be called on multiple contexts at once */
	virtual bool AllowMultipleSelfs(bool bInputAsArray) const { return false; }

	/** @return name of brush for special icon in upper-right corner */
	BLUEPRINTGRAPH_API virtual FName GetCornerIcon() const { return FName(); }

	/** @return true, is pins cannot be connected due to node's inner logic, put message for user in OutReason */
	BLUEPRINTGRAPH_API virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const { return false; }

	/** This function if used for nodes that needs CDO for validation (Called before expansion)*/
	BLUEPRINTGRAPH_API virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const {}

	/** This function if used for nodes that should validate after expansion */
	BLUEPRINTGRAPH_API virtual void ValidateNodeAfterPrune(class FCompilerResultsLog& MessageLog) const {}

	/** This function returns an arbitrary number of attributes that describe this node for analytics events */
	BLUEPRINTGRAPH_API virtual void GetNodeAttributes( TArray<TKeyValuePair<FString, FString>>& OutNodeAttributes ) const;

	/** 
	 * Replacement for GetMenuEntries(). Override to add specific 
	 * UBlueprintNodeSpawners pertaining to the sub-class type. Serves as an 
	 * extensible way for new nodes, and game module nodes to add themselves to
	 * context menus.
	 *
	 * @param  ActionListOut	The list to be populated with new spawners.
	 */
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const {}

	/**
	 * Override to provide a default category for specific node types to be 
	 * listed under.
	 *
	 * @return A localized category string (or an empty string if you want this node listed at the menu's root).
	 */
	virtual FText GetMenuCategory() const { return FText::GetEmpty(); }

	/**
	 * Retrieves a unique identifier for this node type. Built from the node's 
	 * class, as well as other signature items (like functions for CallFunction
	 * nodes).
	 *
	 * NOTE: This is not the same as a node identification GUID, two node 
	 *       instances can have the same signature (if both call the same 
	 *       function, etc.).
	 * 
	 * @return A signature struct, discerning this node from others.
	 */
	virtual FBlueprintNodeSignature GetSignature() const { return FBlueprintNodeSignature(GetClass()); }

	enum BLUEPRINTGRAPH_API EBaseNodeRefreshPriority
	{
		Low_UsesDependentWildcard = 100,
		Low_ReceivesDelegateSignature = 150,
		Normal = 200,
	};

	BLUEPRINTGRAPH_API virtual int32 GetNodeRefreshPriority() const { return EBaseNodeRefreshPriority::Normal; }

	BLUEPRINTGRAPH_API virtual bool DoesInputWildcardPinAcceptArray(const UEdGraphPin* Pin) const { return true; }
protected:

	enum ERedirectType
	{
		ERedirectType_None,
		ERedirectType_Name,
		ERedirectType_Value,
		ERedirectType_Custom
	};

	// Handles the actual reconstruction (copying data, links, name, etc...) from two pins that have already been matched together
	BLUEPRINTGRAPH_API void ReconstructSinglePin(UEdGraphPin* NewPin, UEdGraphPin* OldPin, ERedirectType RedirectType);

	// Helper function to rewire old pins to new pins during node reconstruction (or other regeneration of pins)
	BLUEPRINTGRAPH_API void RewireOldPinsToNewPins(TArray<UEdGraphPin*>& InOldPins, TArray<UEdGraphPin*>& InNewPins);

	// Helper function to properly destroy a set of pins
	BLUEPRINTGRAPH_API void DestroyPinList(TArray<UEdGraphPin*>& InPins);

	/** Allows the custom transformation of a param's value when redirecting a matched pin; called only when DoPinsMatchForReconstruction returns ERedirectType_Custom **/
	BLUEPRINTGRAPH_API virtual void CustomMapParamValue(UEdGraphPin& Pin);

	/** Whether or not two pins match for purposes of reconnection after reconstruction.  This allows pins that may have had their names changed via reconstruction to be matched to their old values on a node-by-node basis, if needed*/
	BLUEPRINTGRAPH_API virtual ERedirectType DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex) const;

	/** Determines what the possible redirect pin names are **/
	BLUEPRINTGRAPH_API virtual void GetRedirectPinNames(const UEdGraphPin& Pin, TArray<FString>& RedirectPinNames) const;

	/** 
	 * Searches ParamRedirect Map and find if there is matching new param
	 * 
	 * returns the redirect type
	 */
	BLUEPRINTGRAPH_API ERedirectType ShouldRedirectParam(const TArray<FString>& OldPinNames, FName& NewPinName, const UK2Node * NewPinNode) const;

	// Helper function to restore Split Pins after ReallocatePinsDuringReconstruction, call after recreating all pins to restore split pin state
	BLUEPRINTGRAPH_API void RestoreSplitPins(TArray<UEdGraphPin*>& OldPins);

	/** 
	 * Sends a message to the owning blueprint's CurrentMessageLog, if there is one available.  Otherwise, defaults to logging to the normal channels.
	 * Should use this for node actions that happen during compilation!
	 */
	void Message_Note(const FString& Message);
	void Message_Warn(const FString& Message);
	void Message_Error(const FString& Message);


    friend class FKismetCompilerContext;


protected:
	// Ensures the specified object is preloaded.  ReferencedObject can be NULL.
	void PreloadObject(UObject* ReferencedObject)
	{
		if ((ReferencedObject != NULL) && ReferencedObject->HasAnyFlags(RF_NeedLoad))
		{
			ReferencedObject->GetLinker()->Preload(ReferencedObject);
		}
	}

	void FixupPinDefaultValues();

public:

	/** Util to get the generated class from a node. */
	BLUEPRINTGRAPH_API UClass* GetBlueprintClassFromNode() const;
};