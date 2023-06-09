// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "ViewportInteractionTypes.h"
#include "VIGizmoHandle.generated.h"

enum class EGizmoHandleTypes : uint8;

USTRUCT()
struct VIEWPORTINTERACTION_API FGizmoHandle
{
	GENERATED_BODY()

	/** Static mesh for this handle */
	class UStaticMeshComponent* HandleMesh;

	/** Scalar that will advance toward 1.0 over time as we hover over the gizmo handle */
	float HoverAlpha;
};


/**
 * Base class for gizmo handles
 */
UCLASS( ABSTRACT )
class VIEWPORTINTERACTION_API UGizmoHandleGroup : public USceneComponent
{
	GENERATED_BODY()

public:

	/** Default constructor that sets up CDO properties */
	UGizmoHandleGroup();
	
	/** Given the unique index, makes a handle */
	FTransformGizmoHandlePlacement MakeHandlePlacementForIndex( const int32 HandleIndex ) const;

	/** Makes a unique index for a handle */
	int32 MakeHandleIndex( const FTransformGizmoHandlePlacement HandlePlacement ) const;

	/** Makes up a name string for a handle */
	FString MakeHandleName( const FTransformGizmoHandlePlacement HandlePlacement ) const;

	/** Static: Given an axis (0-2) and a facing direction, returns the vector normal for that axis */
	static FVector GetAxisVector( const int32 AxisIndex, const ETransformGizmoHandleDirection HandleDirection );

	/** Updates the Gizmo handles, needs to be implemented by derived classes */
	virtual void UpdateGizmoHandleGroup(  const FTransform& LocalToWorld, const FBox& LocalBounds, const FVector ViewLocation, bool bAllHandlesVisible, class UActorComponent* DraggingHandle, const TArray< UActorComponent* >& HoveringOverHandles, 
		float AnimationAlpha, float GizmoScale, const float GizmoHoverScale, const float GizmoHoverAnimationDuration, bool& bOutIsHoveringOrDraggingThisHandleGroup );

	/** Gets the InteractionType and the HandlePlacement for this Gizmo handle */
	virtual void GetHandleIndexInteractionType( const int32 HandleIndex, ETransformGizmoInteractionType& OutInteractionType, TOptional<FTransformGizmoHandlePlacement>& OutHandlePlacement );

	/** Gets the Gizmo InteractionType, needs to be implemented by derived classes */
	virtual ETransformGizmoInteractionType GetInteractionType() const;

	/** Finds the index of DraggedMesh in HandleMeshes */
	virtual int32 GetDraggedHandleIndex( class UStaticMeshComponent* DraggedMesh );

	/** Sets the Gizmo material */
	void SetGizmoMaterial( UMaterialInterface* Material );
	
	/** Sets the translucent Gizmo material */
	void SetTranslucentGizmoMaterial( UMaterialInterface* Material );

	/** Gets all the handles */
	TArray< FGizmoHandle >& GetHandles();

	/** Gets the GizmoType for this Gizmo handle */
	virtual EGizmoHandleTypes GetHandleType() const;

	/** Returns true if this type of handle is allowed with world space gizmos */
	virtual bool SupportsWorldCoordinateSpace() const
	{
		return true;
	}

	/** Sets if this handlegroup will be visible with the universal gizmo */
	void SetShowOnUniversalGizmo( const bool bShowHandleUniversal );
	
	/** Gets if this handlegroup will be visible with the universal gizmo */
	bool GetShowOnUniversalGizmo() const;

protected:
	/** Updates the colors of the dynamic material instances for the handle passed using its axis index */	
	void UpdateHandleColor( const int32 AxisIndex, FGizmoHandle& Handle, class UActorComponent* DraggingHandle, const TArray< UActorComponent* >& HoveringOverHandles );

	/** Helper function to create gizmo handle meshes */
	class UStaticMeshComponent* CreateMeshHandle( class UStaticMesh* HandleMesh, const FString& ComponentName );

	/** Creates handle meshcomponent and adds it to the Handles list */
	class UStaticMeshComponent* CreateAndAddMeshHandle( class UStaticMesh* HandleMesh, const FString& ComponentName, const FTransformGizmoHandlePlacement& HandlePlacement );

	/** Adds the HandleMeshComponent to the Handles list */
	void AddMeshToHandles( class UStaticMeshComponent* HandleMeshComponent, const FTransformGizmoHandlePlacement& HandlePlacement );

	/** Gets the handleplacement axes */
	FTransformGizmoHandlePlacement GetHandlePlacement( const int32 X, const int32 Y, const int32 Z ) const;

	/** Gizmo material (opaque) */
	UPROPERTY()
	UMaterialInterface* GizmoMaterial;

	/** Gizmo material (translucent) */
	UPROPERTY()
	UMaterialInterface* TranslucentGizmoMaterial;
	
	/** All the StaticMeshes for this handle type */
	UPROPERTY()
	TArray< FGizmoHandle > Handles;

private:

	/** Updates the hover animation for the HoveringOverHandles */
	void UpdateHoverAnimation( class UActorComponent* DraggingHandle, const TArray< UActorComponent* >& HoveringOverHandles, const float GizmoHoverAnimationDuration, bool& bOutIsHoveringOrDraggingThisHandleGroup );

	/** If this handlegroup will be visible with the universal gizmo */
	bool bShowOnUniversalGizmo;
};

/**
 * Base class for gizmo handles on axis
 */
UCLASS(ABSTRACT)
class VIEWPORTINTERACTION_API UAxisGizmoHandleGroup : public UGizmoHandleGroup
{
	GENERATED_BODY()

protected:

	/** Creates mesh for every axis */
	void CreateHandles(UStaticMesh* HandleMesh, const FString& HandleComponentName);
	
	/** 
	 * Calculates the transforms of meshes of this handlegroup 
	 * @param HandleToCenter - The offset from the center of the gizmo in roomspace
	 */
	void UpdateHandlesRelativeTransformOnAxis( const FTransform& HandleToCenter, const float AnimationAlpha, const float GizmoScale, const float GizmoHoverScale, 
		const FVector& ViewLocation, class UActorComponent* DraggingHandle, const TArray< UActorComponent* >& HoveringOverHandles );
};