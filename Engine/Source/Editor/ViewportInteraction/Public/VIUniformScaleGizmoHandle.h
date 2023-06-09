// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VIGizmoHandle.h"
#include "VIUniformScaleGizmoHandle.generated.h"

/**
 * Gizmo handle for uniform scaling
 */
UCLASS()
class VIEWPORTINTERACTION_API UUniformScaleGizmoHandleGroup : public UGizmoHandleGroup
{
	GENERATED_BODY()

public:

	/** Default constructor that sets up CDO properties */
	UUniformScaleGizmoHandleGroup();
	
	/** Updates the Gizmo handles */
	virtual void UpdateGizmoHandleGroup( const FTransform& LocalToWorld, const FBox& LocalBounds, const FVector ViewLocation, bool bAllHandlesVisible, class UActorComponent* DraggingHandle,
		const TArray< UActorComponent* >& HoveringOverHandles, float AnimationAlpha, float GizmoScale, const float GizmoHoverScale, const float GizmoHoverAnimationDuration, bool& bOutIsHoveringOrDraggingThisHandleGroup ) override;

	/** Gets the InteractionType for this Gizmo handle */
	virtual ETransformGizmoInteractionType GetInteractionType() const override;

	/** Gets the GizmoType for this Gizmo handle */
	virtual EGizmoHandleTypes GetHandleType() const override;

	/** Sets if the pivot point is used as location for the handle */
	void SetUsePivotPointAsLocation( const bool bInUsePivotAsLocation );

private:

	/** If the pivot point is used for the uniform scaling handle */
	bool bUsePivotAsLocation;
};