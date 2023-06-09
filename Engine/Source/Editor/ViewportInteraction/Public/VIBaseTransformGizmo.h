// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewportInteractionTypes.h"
#include "GameFramework/Actor.h"
#include "VIBaseTransformGizmo.generated.h"

UENUM()
enum class EGizmoHandleTypes : uint8
{
	All = 0,
	Translate = 1,
	Rotate = 2,
	Scale = 3
};

/**
* Displays measurements along the bounds of selected objects
*/
USTRUCT()
struct FTransformGizmoMeasurement
{
	GENERATED_BODY()

		/** The text that displays the actual measurement and units */
		UPROPERTY()
	class UTextRenderComponent* MeasurementText;
};


/**
 * Base class for transform gizmo
 */
UCLASS( Abstract )
class VIEWPORTINTERACTION_API ABaseTransformGizmo : public AActor
{
	GENERATED_BODY()

public:
	
	/** Default constructor that sets up CDO properties */
	ABaseTransformGizmo();

	/** Deconstructor */
	virtual ~ABaseTransformGizmo();

	/** Call this when new objects become selected.  This triggers an animation transition. */
	void OnNewObjectsSelected();

	/** Called by the world interaction system when one of our components is dragged by the user to find out
	    what type of interaction to do.  If null is passed in then we'll treat it as dragging the whole object 
		(rather than a specific axis/handle) */
	ETransformGizmoInteractionType GetInteractionType( UActorComponent* DraggedComponent, TOptional<FTransformGizmoHandlePlacement>& OutHandlePlacement );

	/** Updates the animation with the current time and selected time */
	float GetAnimationAlpha();

	/** Sets the owner */
	void SetOwnerWorldInteraction( class UViewportWorldInteraction* InWorldInteraction );

	/** Gets the owner */
	class UViewportWorldInteraction* GetOwnerWorldInteraction() const;

	/** Called by the world interaction system after we've been spawned into the world, to allow
	    us to create components and set everything up nicely for the selected objects that we'll be
		used to manipulate */
	virtual void UpdateGizmo( const EGizmoHandleTypes GizmoType, const ECoordSystem GizmoCoordinateSpace, const FTransform& LocalToWorld, const FBox& LocalBounds, const FVector ViewLocation, bool bAllHandlesVisible, 
		class UActorComponent* DraggingHandle, const TArray< UActorComponent* >& HoveringOverHandles, const float GizmoHoverScale, const float GizmoHoverAnimationDuration ) {}

protected:

	/** Static: Given a bounding box and information about which edge to query, returns the vertex positions of that edge */
	static void GetBoundingBoxEdge( const FBox& Box, const int32 AxisIndex, const int32 EdgeIndex, FVector& OutVertex0, FVector& OutVertex1 );

	/** Updates the visibility of all the handles */
	void UpdateHandleVisibility( const EGizmoHandleTypes GizmoType, const ECoordSystem GizmoCoordinateSpace, bool bAllHandlesVisible, UActorComponent* DraggingHandle );

	/** Gets if the gizmo shows measurement texts */
	bool GetShowMeasurementText() const;

	/** Real time that the gizmo was last attached to a selected set of objects.  This is used for animation transitions */
	FTimespan SelectedAtTime;
	
	/** Scene component root of this actor */
	UPROPERTY()
	USceneComponent* SceneComponent;

	/** Gizmo material (opaque) */
	UPROPERTY()
	UMaterialInterface* GizmoMaterial;

	/** Gizmo material (translucent) */
	UPROPERTY()
	UMaterialInterface* TranslucentGizmoMaterial;

	/** All gizmo components */
	UPROPERTY()
	TArray< class UGizmoHandleGroup* > AllHandleGroups;

	/** Owning object */
	UPROPERTY()
	class UViewportWorldInteraction* WorldInteraction;
};	