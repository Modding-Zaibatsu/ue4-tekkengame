// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "EnginePrivate.h"
#include "MessageLog.h"
#include "UObjectToken.h"
#include "MapErrors.h"
#include "Camera/CameraComponent.h"
#include "Components/DrawFrustumComponent.h"
#include "IHeadMountedDisplay.h"

#define LOCTEXT_NAMESPACE "CameraComponent"

static void SetDeprecatedControllerViewRotation(UCameraComponent& Component, bool bValue);


//////////////////////////////////////////////////////////////////////////
// UCameraComponent

UCameraComponent::UCameraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	if (!IsRunningCommandlet())
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> EditorCameraMesh(TEXT("/Engine/EditorMeshes/MatineeCam_SM"));
		CameraMesh = EditorCameraMesh.Object;
	}
#endif

	FieldOfView = 90.0f;
	AspectRatio = 1.777778f;
	OrthoWidth = 512.0f;
	OrthoNearClipPlane = 0.0f;
	OrthoFarClipPlane = WORLD_MAX;
	bConstrainAspectRatio = false;
	bUseFieldOfViewForLOD = true;
	PostProcessBlendWeight = 1.0f;
	bUseControllerViewRotation_DEPRECATED = true; // the previous default value before bUsePawnControlRotation replaced this var.
	bUsePawnControlRotation = false;
	bAutoActivate = true;
	bLockToHmd = true;

	// Init deprecated var, for old code that may refer to it.
	SetDeprecatedControllerViewRotation(*this, bUsePawnControlRotation);
}

#if WITH_EDITORONLY_DATA

void UCameraComponent::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UCameraComponent* This = CastChecked<UCameraComponent>(InThis);
	Collector.AddReferencedObject(This->ProxyMeshComponent);
	Collector.AddReferencedObject(This->DrawFrustum);

	Super::AddReferencedObjects(InThis, Collector);
}

void UCameraComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);

	if (ProxyMeshComponent)
	{
		ProxyMeshComponent->DestroyComponent();
	}
	if (DrawFrustum)
	{
		DrawFrustum->DestroyComponent();
	}
}
#endif

void UCameraComponent::OnRegister()
{
#if WITH_EDITORONLY_DATA
	if (AActor* MyOwner = GetOwner())
	{
		if (ProxyMeshComponent == nullptr)
		{
			ProxyMeshComponent = NewObject<UStaticMeshComponent>(MyOwner, NAME_None, RF_Transactional | RF_TextExportTransient);
			ProxyMeshComponent->SetupAttachment(this);
			ProxyMeshComponent->AlwaysLoadOnClient = false;
			ProxyMeshComponent->AlwaysLoadOnServer = false;
			ProxyMeshComponent->StaticMesh = CameraMesh;
			ProxyMeshComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			ProxyMeshComponent->bHiddenInGame = true;
			ProxyMeshComponent->CastShadow = false;
			ProxyMeshComponent->PostPhysicsComponentTick.bCanEverTick = false;
			ProxyMeshComponent->CreationMethod = CreationMethod;
			ProxyMeshComponent->RegisterComponentWithWorld(GetWorld());
		}

		if (DrawFrustum == nullptr)
		{
			DrawFrustum = NewObject<UDrawFrustumComponent>(MyOwner, NAME_None, RF_Transactional | RF_TextExportTransient);
			DrawFrustum->SetupAttachment(this);
			DrawFrustum->AlwaysLoadOnClient = false;
			DrawFrustum->AlwaysLoadOnServer = false;
			DrawFrustum->CreationMethod = CreationMethod;
			DrawFrustum->RegisterComponentWithWorld(GetWorld());
		}
	}

	RefreshVisualRepresentation();
#endif

	Super::OnRegister();

	// Init deprecated var, for old code that may refer to it.
	SetDeprecatedControllerViewRotation(*this, bUsePawnControlRotation);
}

void UCameraComponent::PostLoad()
{
	Super::PostLoad();

	const int32 LinkerUE4Ver = GetLinkerUE4Version();

	if (LinkerUE4Ver < VER_UE4_RENAME_CAMERA_COMPONENT_VIEW_ROTATION)
	{
		bUsePawnControlRotation = bUseControllerViewRotation_DEPRECATED;
	}

	// Init deprecated var, for old code that may refer to it.
	SetDeprecatedControllerViewRotation(*this, bUsePawnControlRotation);
}

#if WITH_EDITORONLY_DATA

 void UCameraComponent::SetCameraMesh(UStaticMesh* Mesh)
 {
	 if (Mesh != CameraMesh)
	 {
		 CameraMesh = Mesh;

		 if (ProxyMeshComponent)
		 {
			 ProxyMeshComponent->SetStaticMesh(Mesh);
		 }
	 }
 }

void UCameraComponent::ResetProxyMeshTransform()
{
	if (ProxyMeshComponent != nullptr)
	{
		ProxyMeshComponent->ResetRelativeTransform();
	}
}


void UCameraComponent::RefreshVisualRepresentation()
{
	if (DrawFrustum != nullptr)
	{
		const float FrustumDrawDistance = 1000.0f;
		if (ProjectionMode == ECameraProjectionMode::Perspective)
		{
			DrawFrustum->FrustumAngle = FieldOfView;
			DrawFrustum->FrustumStartDist = 10.f;
			DrawFrustum->FrustumEndDist = DrawFrustum->FrustumStartDist + FrustumDrawDistance;
		}
		else
		{
			DrawFrustum->FrustumAngle = -OrthoWidth;
			DrawFrustum->FrustumStartDist = OrthoNearClipPlane;
			DrawFrustum->FrustumEndDist = FMath::Min(OrthoFarClipPlane - OrthoNearClipPlane, FrustumDrawDistance);
		}
		DrawFrustum->FrustumAspectRatio = AspectRatio;
		DrawFrustum->MarkRenderStateDirty();
	}

	ResetProxyMeshTransform();
}

void UCameraComponent::OverrideFrustumColor(FColor OverrideColor)
{
	if (DrawFrustum != nullptr)
	{
		DrawFrustum->FrustumColor = OverrideColor;
	}
}

void UCameraComponent::RestoreFrustumColor()
{
	if (DrawFrustum != nullptr)
	{
		//@TODO: 
		const FColor DefaultFrustumColor(255, 0, 255, 255);
		DrawFrustum->FrustumColor = DefaultFrustumColor;
		//ACameraActor* DefCam = Cam->GetClass()->GetDefaultObject<ACameraActor>();
		//Cam->DrawFrustum->FrustumColor = DefCam->DrawFrustum->FrustumColor;
	}
}
#endif	// WITH_EDITORONLY_DATA

#if WITH_EDITORONLY_DATA
void UCameraComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	RefreshVisualRepresentation();
}
#endif	// WITH_EDITORONLY_DATA

void UCameraComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		PostProcessSettings.OnAfterLoad();
	}
}

void UCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
	if (bLockToHmd && GEngine->HMDDevice.IsValid() && GEngine->HMDDevice->IsHeadTrackingAllowed())
	{
		ResetRelativeTransform();
		const FTransform ParentWorld = GetComponentToWorld();
		GEngine->HMDDevice->SetupLateUpdate(ParentWorld, this);

		FQuat Orientation;
		FVector Position;
		if (GEngine->HMDDevice->UpdatePlayerCamera(Orientation, Position))
		{
			SetRelativeTransform(FTransform(Orientation, Position));
		}
	}

	if (bUsePawnControlRotation)
	{
		const APawn* OwningPawn = Cast<APawn>(GetOwner());
		const AController* OwningController = OwningPawn ? OwningPawn->GetController() : nullptr;
		if (OwningController && OwningController->IsLocalPlayerController())
		{
			const FRotator PawnViewRotation = OwningPawn->GetViewRotation();
			if (!PawnViewRotation.Equals(GetComponentRotation()))
			{
				SetWorldRotation(PawnViewRotation);
			}
		}
	}

	if (bUseAdditiveOffset)
	{
		FTransform OffsetCamToBaseCam = AdditiveOffset;
		FTransform BaseCamToWorld = GetComponentToWorld();
		FTransform OffsetCamToWorld = OffsetCamToBaseCam * BaseCamToWorld;

		DesiredView.Location = OffsetCamToWorld.GetLocation();
		DesiredView.Rotation = OffsetCamToWorld.Rotator();

#if WITH_EDITORONLY_DATA
		if (ProxyMeshComponent)
		{
			ResetProxyMeshTransform();

			FTransform LocalTransform = ProxyMeshComponent->GetRelativeTransform();
			FTransform WorldTransform = LocalTransform * OffsetCamToWorld;

			ProxyMeshComponent->SetWorldTransform(WorldTransform);
		}
#endif
	}
	else
	{
		DesiredView.Location = GetComponentLocation();
		DesiredView.Rotation = GetComponentRotation();
	}

	DesiredView.FOV = bUseAdditiveOffset ? (FieldOfView + AdditiveFOVOffset) : FieldOfView;
	DesiredView.AspectRatio = AspectRatio;
	DesiredView.bConstrainAspectRatio = bConstrainAspectRatio;
	DesiredView.bUseFieldOfViewForLOD = bUseFieldOfViewForLOD;
	DesiredView.ProjectionMode = ProjectionMode;
	DesiredView.OrthoWidth = OrthoWidth;
	DesiredView.OrthoNearClipPlane = OrthoNearClipPlane;
	DesiredView.OrthoFarClipPlane = OrthoFarClipPlane;

	// See if the CameraActor wants to override the PostProcess settings used.
	DesiredView.PostProcessBlendWeight = PostProcessBlendWeight;
	if (PostProcessBlendWeight > 0.0f)
	{
		DesiredView.PostProcessSettings = PostProcessSettings;
	}

#if WITH_EDITOR
	ResetProxyMeshTransform();
#endif //WITH_EDITOR
}

#if WITH_EDITOR
void UCameraComponent::CheckForErrors()
{
	Super::CheckForErrors();

	if (AspectRatio <= 0.0f)
	{
		FMessageLog("MapCheck").Warning()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(LOCTEXT( "MapCheck_Message_CameraAspectRatioIsZero", "Camera has AspectRatio=0 - please set this to something non-zero" ) ))
			->AddToken(FMapErrorToken::Create(FMapErrors::CameraAspectRatioIsZero));
	}
}
#endif	// WITH_EDITOR


void SetDeprecatedControllerViewRotation(UCameraComponent& Component, bool bValue)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Component.bUseControllerViewRotation = bValue;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UCameraComponent::NotifyCameraCut()
{
	// if we are owned by a camera actor, notify it too
	// note: many camera components are not part of camera actors, so notification should begin at the
	// component level.
	ACameraActor* const OwningCamera = Cast<ACameraActor>(GetOwner());
	if (OwningCamera)
	{
		OwningCamera->NotifyCameraCut();
	}
};

void UCameraComponent::AddAdditiveOffset(FTransform const& Transform, float FOV)
{
	bUseAdditiveOffset = true;
	AdditiveOffset = AdditiveOffset * Transform;
	AdditiveFOVOffset += FOV;
}

/** Removes any additive offset. */
void UCameraComponent::ClearAdditiveOffset()
{
	bUseAdditiveOffset = false;
	AdditiveOffset = FTransform::Identity;
	AdditiveFOVOffset = 0.f;
}


void UCameraComponent::AddExtraPostProcessBlend(FPostProcessSettings const& PPSettings, float PPBlendWeight)
{
	checkSlow(ExtraPostProcessBlends.Num() == ExtraPostProcessBlendWeights.Num());
	ExtraPostProcessBlends.Add(PPSettings);
	ExtraPostProcessBlendWeights.Add(PPBlendWeight);
}

void UCameraComponent::ClearExtraPostProcessBlends()
{
	ExtraPostProcessBlends.Empty();
	ExtraPostProcessBlendWeights.Empty();
}

void UCameraComponent::GetExtraPostProcessBlends(TArray<FPostProcessSettings>& OutSettings, TArray<float>& OutWeights) const
{
	OutSettings = ExtraPostProcessBlends;
	OutWeights = ExtraPostProcessBlendWeights;
}


#undef LOCTEXT_NAMESPACE

