// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

//#include "Engine/Scene.h"

#include "CameraRig_Crane.generated.h"

class USceneComponent;

/** 
 * A simple rig for to simulate crane-like camera movements.
 */
UCLASS(Blueprintable)
class CINEMATICCAMERA_API ACameraRig_Crane : public AActor
{
	GENERATED_BODY()
	
public:

	// ctor
	ACameraRig_Crane(const FObjectInitializer& ObjectInitialier);

	virtual void Tick(float DeltaTime) override;
	virtual bool ShouldTickIfViewportsOnly() const override;

	/** Controls the pitch of the crane arm (but does not affect the rotation of the mount). */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, Category = "Crane Controls", meta = (UIMin = "-360", UIMax = "360", Units = deg))
	float CranePitch;
	
	/** Controls the yaw of the crane arm. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, Category = "Crane Controls", meta = (UIMin = "-360", UIMax = "360", Units = deg))
	float CraneYaw;
	
	/** Controls the length of the crane arm. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, Category = "Crane Controls", meta = (ClampMin=0, Units = cm))
	float CraneArmLength;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual class USceneComponent* GetDefaultAttachComponent() const override;
#endif
	
private:
	void UpdatePreviewMeshes();
	void UpdateCraneComponents();

	/** Root component to give the whole actor a transform. */
	UPROPERTY(EditDefaultsOnly, Category = "Crane Components")
	USceneComponent* TransformComponent;

	/** Component to control Yaw. */
	UPROPERTY(EditDefaultsOnly, Category = "Crane Components")
	USceneComponent* CraneYawControl;

	/** Component to control Pitch. */
	UPROPERTY(EditDefaultsOnly, Category = "Crane Components")
	USceneComponent* CranePitchControl;

	/** Component to define the attach point for cameras. */
	UPROPERTY(EditDefaultsOnly, Category = "Crane Components")
	USceneComponent* CraneCameraMount;

	/** Preview meshes for visualization */
	UPROPERTY()
	UStaticMeshComponent* PreviewMesh_CraneArm;

	UPROPERTY()
	UStaticMeshComponent* PreviewMesh_CraneBase;

	UPROPERTY()
	UStaticMeshComponent* PreviewMesh_CraneMount;

	UPROPERTY()
	UStaticMeshComponent* PreviewMesh_CraneCounterWeight;
};
