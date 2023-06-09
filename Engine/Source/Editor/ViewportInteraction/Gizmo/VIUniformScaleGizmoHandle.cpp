// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "ViewportInteractionModule.h"
#include "VIUniformScaleGizmoHandle.h"
#include "ViewportWorldInteraction.h"
#include "VIBaseTransformGizmo.h"

UUniformScaleGizmoHandleGroup::UUniformScaleGizmoHandleGroup()
	: Super(),
	bUsePivotAsLocation( true )
{
	// Setup uniform scaling
	UStaticMesh* UniformScaleMesh = nullptr;
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> ObjectFinder( TEXT( "/Engine/VREditor/TransformGizmo/UniformScaleHandle" ) );
		UniformScaleMesh = ObjectFinder.Object;
		check( UniformScaleMesh != nullptr );
	}

	UStaticMeshComponent* UniformScaleHandle = CreateMeshHandle( UniformScaleMesh, FString( "UniformScaleHandle" ) );
	check( UniformScaleHandle != nullptr );

	FGizmoHandle& NewHandle = *new( Handles ) FGizmoHandle();
	NewHandle.HandleMesh = UniformScaleHandle;
}

void UUniformScaleGizmoHandleGroup::UpdateGizmoHandleGroup( const FTransform& LocalToWorld, const FBox& LocalBounds, const FVector ViewLocation, bool bAllHandlesVisible, class UActorComponent* DraggingHandle, const TArray< UActorComponent* >& HoveringOverHandles, 
	float AnimationAlpha, float GizmoScale, const float GizmoHoverScale, const float GizmoHoverAnimationDuration, bool& bOutIsHoveringOrDraggingThisHandleGroup )
{
	// Call parent implementation (updates hover animation)
	Super::UpdateGizmoHandleGroup( LocalToWorld, LocalBounds, ViewLocation, bAllHandlesVisible, DraggingHandle, HoveringOverHandles, 
		AnimationAlpha, GizmoScale, GizmoHoverScale, GizmoHoverAnimationDuration, bOutIsHoveringOrDraggingThisHandleGroup );

	FGizmoHandle& Handle = Handles[ 0 ];
	UStaticMeshComponent* UniformScaleHandle = Handle.HandleMesh;
	if (UniformScaleHandle != nullptr)	// Can be null if no handle for this specific placement
	{
		FVector HandleRelativeLocation;
		if ( !bUsePivotAsLocation )
		{
			UniformScaleHandle->SetRelativeLocation( HandleRelativeLocation );
		}

		float GizmoHandleScale = GizmoScale;

		// Make the handle bigger while hovered (but don't affect the offset -- we want it to scale about it's origin)
		GizmoHandleScale *= FMath::Lerp( 1.0f, GizmoHoverScale, Handle.HoverAlpha );

		UniformScaleHandle->SetRelativeScale3D( FVector( GizmoHandleScale ) );

		// Update material
		{
			if (!UniformScaleHandle->GetMaterial( 0 )->IsA( UMaterialInstanceDynamic::StaticClass() ))
			{
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create( GizmoMaterial, this );
				UniformScaleHandle->SetMaterial( 0, MID );
			}
			if (!UniformScaleHandle->GetMaterial( 1 )->IsA( UMaterialInstanceDynamic::StaticClass() ))
			{
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create( TranslucentGizmoMaterial, this );
				UniformScaleHandle->SetMaterial( 1, MID );
			}
			UMaterialInstanceDynamic* MID0 = CastChecked<UMaterialInstanceDynamic>( UniformScaleHandle->GetMaterial( 0 ) );
			UMaterialInstanceDynamic* MID1 = CastChecked<UMaterialInstanceDynamic>( UniformScaleHandle->GetMaterial( 1 ) );

			ABaseTransformGizmo* GizmoActor = CastChecked<ABaseTransformGizmo>( GetOwner() );
			if( GizmoActor )
			{
				UViewportWorldInteraction* WorldInteraction = GizmoActor->GetOwnerWorldInteraction();
				if( WorldInteraction )
				{
					FLinearColor HandleColor = WorldInteraction->GetColor( UViewportWorldInteraction::EColors::DefaultColor );
					if (UniformScaleHandle == DraggingHandle)
					{
						HandleColor = WorldInteraction->GetColor( UViewportWorldInteraction::EColors::Dragging );
					}
					else if (HoveringOverHandles.Contains( UniformScaleHandle ))
					{
						HandleColor = FLinearColor::LerpUsingHSV( HandleColor, WorldInteraction->GetColor( UViewportWorldInteraction::EColors::Hover ), Handle.HoverAlpha );
					}

					MID0->SetVectorParameterValue( "Color", HandleColor );
					MID1->SetVectorParameterValue( "Color", HandleColor );
				}
			}
		}
	}
}

ETransformGizmoInteractionType UUniformScaleGizmoHandleGroup::GetInteractionType() const
{
	return ETransformGizmoInteractionType::UniformScale;
}

EGizmoHandleTypes UUniformScaleGizmoHandleGroup::GetHandleType() const
{
	return EGizmoHandleTypes::Scale;
}

void UUniformScaleGizmoHandleGroup::SetUsePivotPointAsLocation( const bool bInUsePivotAsLocation )
{
	bUsePivotAsLocation = bInUsePivotAsLocation;
}
