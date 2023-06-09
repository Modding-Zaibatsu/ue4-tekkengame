// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TranslucentLighting.cpp: Translucent lighting implementation.
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "OneColorShader.h"
#include "LightRendering.h"
#include "SceneFilterRendering.h"
#include "ScreenRendering.h"
#include "AmbientCubemapParameters.h"
#include "SceneUtils.h"

class FMaterial;

/** Whether to allow rendering translucency shadow depths. */
bool GUseTranslucencyShadowDepths = true;

DECLARE_FLOAT_COUNTER_STAT(TEXT("Translucent Lighting"), Stat_GPU_TranslucentLighting, STATGROUP_GPU);
 
int32 GUseTranslucentLightingVolumes = 1;
FAutoConsoleVariableRef CVarUseTranslucentLightingVolumes(
	TEXT("r.TranslucentLightingVolume"),
	GUseTranslucentLightingVolumes,
	TEXT("Whether to allow updating the translucent lighting volumes.\n")
	TEXT("0:off, otherwise on, default is 1"),
	ECVF_RenderThreadSafe
	);

float GTranslucentVolumeMinFOV = 45;
static FAutoConsoleVariableRef CVarTranslucentVolumeMinFOV(
	TEXT("r.TranslucentVolumeMinFOV"),
	GTranslucentVolumeMinFOV,
	TEXT("Minimum FOV for translucent lighting volume.  Prevents popping in lighting when zooming in."),
	ECVF_RenderThreadSafe
	);

float GTranslucentVolumeFOVSnapFactor = 10;
static FAutoConsoleVariableRef CTranslucentVolumeFOVSnapFactor(
	TEXT("r.TranslucentVolumeFOVSnapFactor"),
	GTranslucentVolumeFOVSnapFactor,
	TEXT("FOV will be snapped to a factor of this before computing volume bounds."),
	ECVF_RenderThreadSafe
	);

int32 GUseTranslucencyVolumeBlur = 1;
FAutoConsoleVariableRef CVarUseTranslucentLightingVolumeBlur(
	TEXT("r.TranslucencyVolumeBlur"),
	GUseTranslucencyVolumeBlur,
	TEXT("Whether to blur the translucent lighting volumes.\n")
	TEXT("0:off, otherwise on, default is 1"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GTranslucencyLightingVolumeDim = 64;
FAutoConsoleVariableRef CVarTranslucencyLightingVolumeDim(
	TEXT("r.TranslucencyLightingVolumeDim"),
	GTranslucencyLightingVolumeDim,
	TEXT("Dimensions of the volume textures used for translucency lighting.  Larger textures result in higher resolution but lower performance."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<float> CVarTranslucencyLightingVolumeInnerDistance(
	TEXT("r.TranslucencyLightingVolumeInnerDistance"),
	1500.0f,
	TEXT("Distance from the camera that the first volume cascade should end"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarTranslucencyLightingVolumeOuterDistance(
	TEXT("r.TranslucencyLightingVolumeOuterDistance"),
	5000.0f,
	TEXT("Distance from the camera that the second volume cascade should end"),
	ECVF_RenderThreadSafe);

void FViewInfo::CalcTranslucencyLightingVolumeBounds(FBox* InOutCascadeBoundsArray, int32 NumCascades) const
{
	for (int32 CascadeIndex = 0; CascadeIndex < NumCascades; CascadeIndex++)
	{
		float InnerDistance = CVarTranslucencyLightingVolumeInnerDistance.GetValueOnRenderThread();
		float OuterDistance = CVarTranslucencyLightingVolumeOuterDistance.GetValueOnRenderThread();

		const float FrustumStartDistance = CascadeIndex == 0 ? 0 : InnerDistance;
		const float FrustumEndDistance = CascadeIndex == 0 ? InnerDistance : OuterDistance;

		float FOV = PI / 4.0f;
		float AspectRatio = 1.0f;

		if (IsPerspectiveProjection())
		{
			// Derive FOV and aspect ratio from the perspective projection matrix
			FOV = FMath::Atan(1.0f / ShadowViewMatrices.ProjMatrix.M[0][0]);
			// Clamp to prevent shimmering when zooming in
			FOV = FMath::Max(FOV, GTranslucentVolumeMinFOV * (float)PI / 180.0f);
			const float RoundFactorRadians = GTranslucentVolumeFOVSnapFactor * (float)PI / 180.0f;
			// Round up to a fixed factor
			// This causes the volume lighting to make discreet jumps as the FOV animates, instead of slowly crawling over a long period
			FOV = FOV + RoundFactorRadians - FMath::Fmod(FOV, RoundFactorRadians);
			AspectRatio = ShadowViewMatrices.ProjMatrix.M[1][1] / ShadowViewMatrices.ProjMatrix.M[0][0];
		}

		const float StartHorizontalLength = FrustumStartDistance * FMath::Tan(FOV);
		const FVector StartCameraRightOffset = ShadowViewMatrices.ViewMatrix.GetColumn(0) * StartHorizontalLength;
		const float StartVerticalLength = StartHorizontalLength / AspectRatio;
		const FVector StartCameraUpOffset = ShadowViewMatrices.ViewMatrix.GetColumn(1) * StartVerticalLength;

		const float EndHorizontalLength = FrustumEndDistance * FMath::Tan(FOV);
		const FVector EndCameraRightOffset = ShadowViewMatrices.ViewMatrix.GetColumn(0) * EndHorizontalLength;
		const float EndVerticalLength = EndHorizontalLength / AspectRatio;
		const FVector EndCameraUpOffset = ShadowViewMatrices.ViewMatrix.GetColumn(1) * EndVerticalLength;

		FVector SplitVertices[8];
		SplitVertices[0] = ShadowViewMatrices.ViewOrigin + GetViewDirection() * FrustumStartDistance + StartCameraRightOffset + StartCameraUpOffset;
		SplitVertices[1] = ShadowViewMatrices.ViewOrigin + GetViewDirection() * FrustumStartDistance + StartCameraRightOffset - StartCameraUpOffset;
		SplitVertices[2] = ShadowViewMatrices.ViewOrigin + GetViewDirection() * FrustumStartDistance - StartCameraRightOffset + StartCameraUpOffset;
		SplitVertices[3] = ShadowViewMatrices.ViewOrigin + GetViewDirection() * FrustumStartDistance - StartCameraRightOffset - StartCameraUpOffset;

		SplitVertices[4] = ShadowViewMatrices.ViewOrigin + GetViewDirection() * FrustumEndDistance + EndCameraRightOffset + EndCameraUpOffset;
		SplitVertices[5] = ShadowViewMatrices.ViewOrigin + GetViewDirection() * FrustumEndDistance + EndCameraRightOffset - EndCameraUpOffset;
		SplitVertices[6] = ShadowViewMatrices.ViewOrigin + GetViewDirection() * FrustumEndDistance - EndCameraRightOffset + EndCameraUpOffset;
		SplitVertices[7] = ShadowViewMatrices.ViewOrigin + GetViewDirection() * FrustumEndDistance - EndCameraRightOffset - EndCameraUpOffset;

		FVector Center(0,0,0);
		// Weight the far vertices more so that the bounding sphere will be further from the camera
		// This minimizes wasted shadowmap space behind the viewer
		const float FarVertexWeightScale = 10.0f;
		for (int32 VertexIndex = 0; VertexIndex < 8; VertexIndex++)
		{
			const float Weight = VertexIndex > 3 ? 1 / (4.0f + 4.0f / FarVertexWeightScale) : 1 / (4.0f + 4.0f * FarVertexWeightScale);
			Center += SplitVertices[VertexIndex] * Weight;
		}

		float RadiusSquared = 0;
		for (int32 VertexIndex = 0; VertexIndex < 8; VertexIndex++)
		{
			RadiusSquared = FMath::Max(RadiusSquared, (Center - SplitVertices[VertexIndex]).SizeSquared());
		}

		FSphere SphereBounds(Center, FMath::Sqrt(RadiusSquared));

		// Snap the center to a multiple of the volume dimension for stability
		SphereBounds.Center.X = SphereBounds.Center.X - FMath::Fmod(SphereBounds.Center.X, SphereBounds.W * 2 / GTranslucencyLightingVolumeDim);
		SphereBounds.Center.Y = SphereBounds.Center.Y - FMath::Fmod(SphereBounds.Center.Y, SphereBounds.W * 2 / GTranslucencyLightingVolumeDim);
		SphereBounds.Center.Z = SphereBounds.Center.Z - FMath::Fmod(SphereBounds.Center.Z, SphereBounds.W * 2 / GTranslucencyLightingVolumeDim);

		InOutCascadeBoundsArray[CascadeIndex] = FBox(SphereBounds.Center - SphereBounds.W, SphereBounds.Center + SphereBounds.W);
	}
}

/**
 * Vertex shader used to render shadow maps for translucency.
 */
class FTranslucencyShadowDepthVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FTranslucencyShadowDepthVS,MeshMaterial);
public:

	static bool ShouldCache(EShaderPlatform Platform,const FMaterial* Material,const FVertexFactoryType* VertexFactoryType)
	{
		return IsTranslucentBlendMode(Material->GetBlendMode()) && IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4);
	}

	FTranslucencyShadowDepthVS() {}
	FTranslucencyShadowDepthVS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) :
		FMeshMaterialShader(Initializer)
	{
		ShadowParameters.Bind(Initializer.ParameterMap);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		Ar << ShadowParameters;
		return bShaderHasOutdatedParameters;
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FMaterialRenderProxy* MaterialRenderProxy,
		const FSceneView& View,
		const FProjectedShadowInfo* ShadowInfo
		)
	{
		FMeshMaterialShader::SetParameters(RHICmdList, GetVertexShader(), MaterialRenderProxy, *MaterialRenderProxy->GetMaterial(View.GetFeatureLevel()), View, ESceneRenderTargetsMode::DontSet);
		ShadowParameters.SetVertexShader(RHICmdList, this, View, ShadowInfo, MaterialRenderProxy);
	}

	void SetMesh(FRHICommandList& RHICmdList, const FVertexFactory* VertexFactory,const FSceneView& View,const FPrimitiveSceneProxy* Proxy,const FMeshBatchElement& BatchElement,const FMeshDrawingRenderState& DrawRenderState)
	{
		FMeshMaterialShader::SetMesh(RHICmdList, GetVertexShader(),VertexFactory,View,Proxy,BatchElement,DrawRenderState);
	}

private:
	FShadowDepthShaderParameters ShadowParameters;
};

enum ETranslucencyShadowDepthShaderMode
{
	TranslucencyShadowDepth_PerspectiveCorrect,
	TranslucencyShadowDepth_Standard,
};

template <ETranslucencyShadowDepthShaderMode ShaderMode>
class TTranslucencyShadowDepthVS : public FTranslucencyShadowDepthVS
{
	DECLARE_SHADER_TYPE(TTranslucencyShadowDepthVS,MeshMaterial);
public:

	TTranslucencyShadowDepthVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FTranslucencyShadowDepthVS(Initializer)
	{
	}

	TTranslucencyShadowDepthVS() {}

	static void ModifyCompilationEnvironment( EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment )
	{
		FTranslucencyShadowDepthVS::ModifyCompilationEnvironment(Platform, Material, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("PERSPECTIVE_CORRECT_DEPTH"), (uint32)(ShaderMode == TranslucencyShadowDepth_PerspectiveCorrect ? 1 : 0));
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TTranslucencyShadowDepthVS<TranslucencyShadowDepth_PerspectiveCorrect>,TEXT("TranslucentShadowDepthShaders"),TEXT("MainVS"),SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TTranslucencyShadowDepthVS<TranslucencyShadowDepth_Standard>,TEXT("TranslucentShadowDepthShaders"),TEXT("MainVS"),SF_Vertex);

/**
 * Pixel shader used for accumulating translucency layer densities
 */
class FTranslucencyShadowDepthPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FTranslucencyShadowDepthPS,MeshMaterial);
public:

	static bool ShouldCache(EShaderPlatform Platform,const FMaterial* Material,const FVertexFactoryType* VertexFactoryType)
	{
		return IsTranslucentBlendMode(Material->GetBlendMode()) && IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4);
	}

	FTranslucencyShadowDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FMeshMaterialShader(Initializer)
	{
		TranslInvMaxSubjectDepth.Bind(Initializer.ParameterMap,TEXT("TranslInvMaxSubjectDepth"));
		TranslucentShadowStartOffset.Bind(Initializer.ParameterMap,TEXT("TranslucentShadowStartOffset"));
		TranslucencyProjectionParameters.Bind(Initializer.ParameterMap);
	}

	FTranslucencyShadowDepthPS() {}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FMaterialRenderProxy* MaterialRenderProxy,
		const FSceneView& View,
		const FProjectedShadowInfo* ShadowInfo
		)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		const auto FeatureLevel = View.GetFeatureLevel();

		//@todo - scene depth can be bound by the material for use in depth fades
		// This is incorrect when rendering a shadowmap as it's not from the camera's POV
		// Set the scene depth texture to something safe when rendering shadow depths
		FMeshMaterialShader::SetParameters(RHICmdList, ShaderRHI, MaterialRenderProxy, *MaterialRenderProxy->GetMaterial(FeatureLevel), View, ESceneRenderTargetsMode::DontSet);

		SetShaderValue(RHICmdList, ShaderRHI, TranslInvMaxSubjectDepth, ShadowInfo->InvMaxSubjectDepth);

		const float LocalToWorldScale = ShadowInfo->GetParentSceneInfo()->Proxy->GetLocalToWorld().GetScaleVector().GetMax();
		const float TranslucentShadowStartOffsetValue = MaterialRenderProxy->GetMaterial(FeatureLevel)->GetTranslucentShadowStartOffset() * LocalToWorldScale;
		SetShaderValue(RHICmdList, ShaderRHI,TranslucentShadowStartOffset, TranslucentShadowStartOffsetValue / (ShadowInfo->MaxSubjectZ - ShadowInfo->MinSubjectZ));
		TranslucencyProjectionParameters.Set(RHICmdList, this, ShadowInfo);
	}

	void SetMesh(FRHICommandList& RHICmdList, const FVertexFactory* VertexFactory,const FSceneView& View,const FPrimitiveSceneProxy* Proxy,const FMeshBatchElement& BatchElement,const FMeshDrawingRenderState& DrawRenderState)
	{
		FMeshMaterialShader::SetMesh(RHICmdList, GetPixelShader(),VertexFactory,View,Proxy,BatchElement,DrawRenderState);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		Ar << TranslInvMaxSubjectDepth;
		Ar << TranslucentShadowStartOffset;
		Ar << TranslucencyProjectionParameters;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter TranslInvMaxSubjectDepth;
	FShaderParameter TranslucentShadowStartOffset;
	FTranslucencyShadowProjectionShaderParameters TranslucencyProjectionParameters;
};

template <ETranslucencyShadowDepthShaderMode ShaderMode>
class TTranslucencyShadowDepthPS : public FTranslucencyShadowDepthPS
{
	DECLARE_SHADER_TYPE(TTranslucencyShadowDepthPS,MeshMaterial);
public:

	TTranslucencyShadowDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FTranslucencyShadowDepthPS(Initializer)
	{
	}

	TTranslucencyShadowDepthPS() {}

	static void ModifyCompilationEnvironment( EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment )
	{
		FTranslucencyShadowDepthPS::ModifyCompilationEnvironment(Platform, Material, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("PERSPECTIVE_CORRECT_DEPTH"), (uint32)(ShaderMode == TranslucencyShadowDepth_PerspectiveCorrect ? 1 : 0));
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TTranslucencyShadowDepthPS<TranslucencyShadowDepth_PerspectiveCorrect>,TEXT("TranslucentShadowDepthShaders"),TEXT("MainOpacityPS"),SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TTranslucencyShadowDepthPS<TranslucencyShadowDepth_Standard>,TEXT("TranslucentShadowDepthShaders"),TEXT("MainOpacityPS"),SF_Pixel);

/**
 * Drawing policy used to create Fourier opacity maps
 */
class FTranslucencyShadowDepthDrawingPolicy : public FMeshDrawingPolicy
{
public:

	struct ContextDataType : public FMeshDrawingPolicy::ContextDataType
	{
		const FProjectedShadowInfo* ShadowInfo;

		explicit ContextDataType(const FProjectedShadowInfo* InShadowInfo)
			: ShadowInfo(InShadowInfo)
		{}
	};

	FTranslucencyShadowDepthDrawingPolicy(
		const FVertexFactory* InVertexFactory,
		const FMaterialRenderProxy* InMaterialRenderProxy,
		const FMaterial& InMaterialResource,
		bool bInDirectionalLight
		):
		FMeshDrawingPolicy(InVertexFactory,InMaterialRenderProxy,InMaterialResource)
	{
		const bool bUsePerspectiveCorrectShadowDepths = !bInDirectionalLight;

		if (bUsePerspectiveCorrectShadowDepths)
		{
			VertexShader = InMaterialResource.GetShader<TTranslucencyShadowDepthVS<TranslucencyShadowDepth_PerspectiveCorrect> >(InVertexFactory->GetType());
			PixelShader = InMaterialResource.GetShader<TTranslucencyShadowDepthPS<TranslucencyShadowDepth_PerspectiveCorrect> >(InVertexFactory->GetType());
		}
		else
		{
			VertexShader = InMaterialResource.GetShader<TTranslucencyShadowDepthVS<TranslucencyShadowDepth_Standard> >(InVertexFactory->GetType());
			PixelShader = InMaterialResource.GetShader<TTranslucencyShadowDepthPS<TranslucencyShadowDepth_Standard> >(InVertexFactory->GetType());
		}
	}

	void SetSharedState(FRHICommandList& RHICmdList, const FSceneView* View, const ContextDataType PolicyContext) const
	{
		// Set the shared mesh resources.
		FMeshDrawingPolicy::SetSharedState(RHICmdList, View, PolicyContext);

		VertexShader->SetParameters(RHICmdList, MaterialRenderProxy, *View, PolicyContext.ShadowInfo);
		PixelShader->SetParameters(RHICmdList, MaterialRenderProxy, *View, PolicyContext.ShadowInfo);
	}

	/** 
	* Create bound shader state using the vertex decl from the mesh draw policy
	* as well as the shaders needed to draw the mesh
	* @return new bound shader state object
	*/
	FBoundShaderStateInput GetBoundShaderStateInput(ERHIFeatureLevel::Type InFeatureLevel)
	{
		return FBoundShaderStateInput(
			FMeshDrawingPolicy::GetVertexDeclaration(), 
			VertexShader->GetVertexShader(),
			NULL, 
			NULL,
			PixelShader->GetPixelShader(),
			NULL);
	}

	void SetMeshRenderState(
		FRHICommandList& RHICmdList, 
		const FSceneView& View,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& Mesh,
		int32 BatchElementIndex,
		bool bBackFace,
		const FMeshDrawingRenderState& DrawRenderState,
		const ElementDataType& ElementData,
		const ContextDataType PolicyContext
		) const
	{
		const FMeshBatchElement& BatchElement = Mesh.Elements[BatchElementIndex];
		VertexShader->SetMesh(RHICmdList, VertexFactory,View,PrimitiveSceneProxy,BatchElement,DrawRenderState);
		PixelShader->SetMesh(RHICmdList, VertexFactory,View,PrimitiveSceneProxy,BatchElement,DrawRenderState);

		FMeshDrawingPolicy::SetMeshRenderState(RHICmdList, View,PrimitiveSceneProxy,Mesh,BatchElementIndex,bBackFace,DrawRenderState,ElementData,PolicyContext);
	}

private:
	FTranslucencyShadowDepthVS* VertexShader;
	FTranslucencyShadowDepthPS* PixelShader;
};

class FTranslucencyShadowDepthDrawingPolicyFactory
{
public:

	enum { bAllowSimpleElements = false };
	struct ContextType 
	{
		ContextType(const FProjectedShadowInfo* InShadowInfo, bool bInDirectionalLight)
			: ShadowInfo(InShadowInfo)
			, bDirectionalLight(bInDirectionalLight)
		{}

		const FProjectedShadowInfo* ShadowInfo;
		bool bDirectionalLight;
	};

	static bool DrawDynamicMesh(
		FRHICommandList& RHICmdList, 
		const FSceneView& View,
		ContextType DrawingContext,
		const FMeshBatch& Mesh,
		bool bBackFace,
		bool bPreFog,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		FHitProxyId HitProxyId
		)
	{
		bool bDirty = false;
		const auto FeatureLevel = View.GetFeatureLevel();

		if (Mesh.CastShadow)
		{
			const FMaterialRenderProxy* MaterialRenderProxy = Mesh.MaterialRenderProxy;
			const EBlendMode BlendMode = MaterialRenderProxy->GetMaterial(FeatureLevel)->GetBlendMode();

			// Only render translucent meshes into the Fourier opacity maps
			if (IsTranslucentBlendMode(BlendMode))
			{
				FTranslucencyShadowDepthDrawingPolicy DrawingPolicy(Mesh.VertexFactory, MaterialRenderProxy, *MaterialRenderProxy->GetMaterial(FeatureLevel), DrawingContext.bDirectionalLight);
				RHICmdList.BuildAndSetLocalBoundShaderState(DrawingPolicy.GetBoundShaderStateInput(View.GetFeatureLevel()));
				DrawingPolicy.SetSharedState(RHICmdList, &View, FTranslucencyShadowDepthDrawingPolicy::ContextDataType(DrawingContext.ShadowInfo));
				const FMeshDrawingRenderState DrawRenderState(Mesh.DitheredLODTransitionAlpha);

				for (int32 BatchElementIndex = 0; BatchElementIndex < Mesh.Elements.Num(); BatchElementIndex++)
				{
					TDrawEvent<FRHICommandList> MeshEvent;
					BeginMeshDrawEvent(RHICmdList, PrimitiveSceneProxy, Mesh, MeshEvent);

					DrawingPolicy.SetMeshRenderState(RHICmdList, View,PrimitiveSceneProxy,Mesh,BatchElementIndex,bBackFace,DrawRenderState,
						FTranslucencyShadowDepthDrawingPolicy::ElementDataType(),
						FTranslucencyShadowDepthDrawingPolicy::ContextDataType(DrawingContext.ShadowInfo)
						);
					DrawingPolicy.DrawMesh(RHICmdList, Mesh,BatchElementIndex);
				}
				bDirty = true;
			}
		}
	
		return bDirty;
	}

	static bool DrawStaticMesh(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View,
		ContextType DrawingContext,
		const FStaticMesh& StaticMesh,
		bool bPreFog,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		FHitProxyId HitProxyId
		)
	{
		bool bDirty = false;

		bDirty |= DrawDynamicMesh(
			RHICmdList, 
			View,
			DrawingContext,
			StaticMesh,
			false,
			bPreFog,
			PrimitiveSceneProxy,
			HitProxyId
			);

		return bDirty;
	}
};

void FProjectedShadowInfo::RenderTranslucencyDepths(FRHICommandList& RHICmdList, FSceneRenderer* SceneRenderer)
{
	check(IsInRenderingThread());
	checkSlow(!bWholeSceneShadow);
	SCOPE_CYCLE_COUNTER(STAT_RenderPerObjectShadowDepthsTime);

	{
#if WANTS_DRAW_MESH_EVENTS
		FString EventName;
		GetShadowTypeNameForDrawEvent(EventName);
		SCOPED_DRAW_EVENTF(RHICmdList, EventShadowDepthActor, *EventName);
#endif
		// Clear the shadow and its border
		RHICmdList.SetViewport(
			X,
			Y,
			0.0f,
			(X + BorderSize * 2 + ResolutionX),
			(Y + BorderSize * 2 + ResolutionY),
			1.0f
			);

		FLinearColor ClearColors[2] = {FLinearColor(0,0,0,0), FLinearColor(0,0,0,0)};
		DrawClearQuadMRT(RHICmdList, SceneRenderer->FeatureLevel, true, ARRAY_COUNT(ClearColors), ClearColors, false, 1.0f, false, 0);

		// Set the viewport for the shadow.
		RHICmdList.SetViewport(
			(X + BorderSize),
			(Y + BorderSize),
			0.0f,
			(X + BorderSize + ResolutionX),
			(Y + BorderSize + ResolutionY),
			1.0f
			);

		RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		RHICmdList.SetBlendState(TStaticBlendState<
			CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One,
			CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());

		FTranslucencyShadowDepthDrawingPolicyFactory::ContextType DrawingContext(this,bDirectionalLight);

		for (int32 MeshBatchIndex = 0; MeshBatchIndex < DynamicSubjectTranslucentMeshElements.Num(); MeshBatchIndex++)
		{
			const FMeshBatchAndRelevance& MeshBatchAndRelevance = DynamicSubjectTranslucentMeshElements[MeshBatchIndex];
			const FMeshBatch& MeshBatch = *MeshBatchAndRelevance.Mesh;
			FTranslucencyShadowDepthDrawingPolicyFactory::DrawDynamicMesh(RHICmdList, *ShadowDepthView, DrawingContext, MeshBatch, false, true, MeshBatchAndRelevance.PrimitiveSceneProxy, MeshBatch.BatchHitProxyId);
		}

		for (int32 PrimitiveIndex = 0; PrimitiveIndex < SubjectTranslucentPrimitives.Num(); PrimitiveIndex++)
		{
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = SubjectTranslucentPrimitives[PrimitiveIndex];
			int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
			FPrimitiveViewRelevance ViewRelevance = ShadowDepthView->PrimitiveViewRelevanceMap[PrimitiveId];

			if (!ViewRelevance.bInitializedThisFrame)
			{
				// Compute the subject primitive's view relevance since it wasn't cached
				ViewRelevance = PrimitiveSceneInfo->Proxy->GetViewRelevance(ShadowDepthView);
			}

			if (ViewRelevance.bDrawRelevance && ViewRelevance.bStaticRelevance)
			{
				for (int32 MeshIndex = 0; MeshIndex < PrimitiveSceneInfo->StaticMeshes.Num(); MeshIndex++)
				{
					FTranslucencyShadowDepthDrawingPolicyFactory::DrawStaticMesh(
						RHICmdList, 
						*ShadowDepthView,
						DrawingContext,
						PrimitiveSceneInfo->StaticMeshes[MeshIndex],
						true,
						PrimitiveSceneInfo->Proxy,
						FHitProxyId());
				}
			}
		}
	}
}

IMPLEMENT_SHADER_TYPE(,FWriteToSliceGS,TEXT("TranslucentLightingShaders"),TEXT("WriteToSliceMainGS"),SF_Geometry);
IMPLEMENT_SHADER_TYPE(,FWriteToSliceVS,TEXT("TranslucentLightingShaders"),TEXT("WriteToSliceMainVS"),SF_Vertex);

/** Pixel shader used to filter a single volume lighting cascade. */
class FFilterTranslucentVolumePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FFilterTranslucentVolumePS,Global);
public:

	static bool ShouldCache(EShaderPlatform Platform) 
	{ 
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4); 
	}

	FFilterTranslucentVolumePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		TexelSize.Bind(Initializer.ParameterMap, TEXT("TexelSize"));
		TranslucencyLightingVolumeAmbient.Bind(Initializer.ParameterMap, TEXT("TranslucencyLightingVolumeAmbient"));
		TranslucencyLightingVolumeAmbientSampler.Bind(Initializer.ParameterMap, TEXT("TranslucencyLightingVolumeAmbientSampler"));
		TranslucencyLightingVolumeDirectional.Bind(Initializer.ParameterMap, TEXT("TranslucencyLightingVolumeDirectional"));
		TranslucencyLightingVolumeDirectionalSampler.Bind(Initializer.ParameterMap, TEXT("TranslucencyLightingVolumeDirectionalSampler"));
	}
	FFilterTranslucentVolumePS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, int32 VolumeCascadeIndex)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters(RHICmdList, ShaderRHI, View);

		SetShaderValue(RHICmdList, ShaderRHI, TexelSize, 1.0f / GTranslucencyLightingVolumeDim);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI, 
			TranslucencyLightingVolumeAmbient, 
			TranslucencyLightingVolumeAmbientSampler, 
			TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), 
			SceneContext.TranslucencyLightingVolumeAmbient[VolumeCascadeIndex]->GetRenderTargetItem().ShaderResourceTexture);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI, 
			TranslucencyLightingVolumeDirectional, 
			TranslucencyLightingVolumeDirectionalSampler, 
			TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), 
			SceneContext.TranslucencyLightingVolumeDirectional[VolumeCascadeIndex]->GetRenderTargetItem().ShaderResourceTexture);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << TexelSize;
		Ar << TranslucencyLightingVolumeAmbient;
		Ar << TranslucencyLightingVolumeAmbientSampler;
		Ar << TranslucencyLightingVolumeDirectional;
		Ar << TranslucencyLightingVolumeDirectionalSampler;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter TexelSize;
	FShaderResourceParameter TranslucencyLightingVolumeAmbient;
	FShaderResourceParameter TranslucencyLightingVolumeAmbientSampler;
	FShaderResourceParameter TranslucencyLightingVolumeDirectional;
	FShaderResourceParameter TranslucencyLightingVolumeDirectionalSampler;
};

IMPLEMENT_SHADER_TYPE(,FFilterTranslucentVolumePS,TEXT("TranslucentLightingShaders"),TEXT("FilterMainPS"),SF_Pixel);

/** Shader parameters needed to inject direct lighting into a volume. */
class FTranslucentInjectParameters
{
public:

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		WorldToShadowMatrix.Bind(ParameterMap,TEXT("WorldToShadowMatrix"));
		ShadowmapMinMax.Bind(ParameterMap,TEXT("ShadowmapMinMax"));
		VolumeCascadeIndex.Bind(ParameterMap,TEXT("VolumeCascadeIndex"));
	}

	template<typename ShaderRHIParamRef>
	void Set(
		FRHICommandList& RHICmdList, 
		const ShaderRHIParamRef ShaderRHI, 
		FShader* Shader, 
		const FViewInfo& View, 
		const FLightSceneInfo* LightSceneInfo, 
		const FProjectedShadowInfo* ShadowMap, 
		uint32 VolumeCascadeIndexValue,
		bool bDynamicallyShadowed) const
	{
		SetDeferredLightParameters(RHICmdList, ShaderRHI, Shader->GetUniformBufferParameter<FDeferredLightUniformStruct>(), LightSceneInfo, View);

		if (bDynamicallyShadowed)
		{
			FVector4 ShadowmapMinMaxValue;
			FMatrix WorldToShadowMatrixValue = ShadowMap->GetWorldToShadowMatrix(ShadowmapMinMaxValue);

			SetShaderValue(RHICmdList, ShaderRHI, WorldToShadowMatrix, WorldToShadowMatrixValue);
			SetShaderValue(RHICmdList, ShaderRHI, ShadowmapMinMax, ShadowmapMinMaxValue);
		}

		SetShaderValue(RHICmdList, ShaderRHI, VolumeCascadeIndex, VolumeCascadeIndexValue);
	}

	/** Serializer. */ 
	friend FArchive& operator<<(FArchive& Ar,FTranslucentInjectParameters& P)
	{
		Ar << P.WorldToShadowMatrix;
		Ar << P.ShadowmapMinMax;
		Ar << P.VolumeCascadeIndex;
		return Ar;
	}

private:

	FShaderParameter WorldToShadowMatrix;
	FShaderParameter ShadowmapMinMax;
	FShaderParameter VolumeCascadeIndex;
};

/** Pixel shader used to accumulate per-object translucent shadows into a volume texture. */
class FTranslucentObjectShadowingPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FTranslucentObjectShadowingPS,Global);
public:

	static void ModifyCompilationEnvironment( EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment )
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("INJECTION_PIXEL_SHADER"), 1);
	}

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4);
	}

	FTranslucentObjectShadowingPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		TranslucencyProjectionParameters.Bind(Initializer.ParameterMap);
		TranslucentInjectParameters.Bind(Initializer.ParameterMap);
	}
	FTranslucentObjectShadowingPS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FLightSceneInfo* LightSceneInfo, const FProjectedShadowInfo* ShadowMap, uint32 VolumeCascadeIndex)
	{
		FGlobalShader::SetParameters(RHICmdList, GetPixelShader(), View);
		TranslucencyProjectionParameters.Set(RHICmdList, this, ShadowMap);
		TranslucentInjectParameters.Set(RHICmdList, GetPixelShader(), this, View, LightSceneInfo, ShadowMap, VolumeCascadeIndex, true);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << TranslucencyProjectionParameters;
		Ar << TranslucentInjectParameters;
		return bShaderHasOutdatedParameters;
	}

private:
	FTranslucencyShadowProjectionShaderParameters TranslucencyProjectionParameters;
	FTranslucentInjectParameters TranslucentInjectParameters;
};

IMPLEMENT_SHADER_TYPE(,FTranslucentObjectShadowingPS,TEXT("TranslucentLightingShaders"),TEXT("PerObjectShadowingMainPS"),SF_Pixel);

/** Shader that adds direct lighting contribution from the given light to the current volume lighting cascade. */
template<ELightComponentType InjectionType, bool bDynamicallyShadowed, bool bApplyLightFunction, bool bInverseSquared>
class TTranslucentLightingInjectPS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(TTranslucentLightingInjectPS,Material);
public:

	static void ModifyCompilationEnvironment( EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment )
	{
		FMaterialShader::ModifyCompilationEnvironment(Platform, Material, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RADIAL_ATTENUATION"), (uint32)(InjectionType != LightType_Directional));
		OutEnvironment.SetDefine(TEXT("INJECTION_PIXEL_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("DYNAMICALLY_SHADOWED"), (uint32)bDynamicallyShadowed);
		OutEnvironment.SetDefine(TEXT("APPLY_LIGHT_FUNCTION"), (uint32)bApplyLightFunction);
		OutEnvironment.SetDefine(TEXT("INVERSE_SQUARED_FALLOFF"), (uint32)bInverseSquared);
	}

	/**
	  * Makes sure only shaders for materials that are explicitly flagged
	  * as 'UsedAsLightFunction' in the Material Editor gets compiled into
	  * the shader cache.
	  */
	static bool ShouldCache(EShaderPlatform Platform, const FMaterial* Material)
	{
		return (Material->IsLightFunction() || Material->IsSpecialEngineMaterial()) && IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4);
	}

	TTranslucentLightingInjectPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FMaterialShader(Initializer)
	{
		DepthBiasParameters.Bind(Initializer.ParameterMap, TEXT("DepthBiasParameters"));
		CascadeBounds.Bind(Initializer.ParameterMap, TEXT("CascadeBounds"));
		InnerCascadeBounds.Bind(Initializer.ParameterMap, TEXT("InnerCascadeBounds"));
		ClippingPlanes.Bind(Initializer.ParameterMap, TEXT("ClippingPlanes"));
		ShadowInjectParams.Bind(Initializer.ParameterMap, TEXT("ShadowInjectParams"));
		SpotlightMask.Bind(Initializer.ParameterMap, TEXT("SpotlightMask"));
		ShadowDepthTexture.Bind(Initializer.ParameterMap, TEXT("ShadowDepthTexture"));
		ShadowDepthTextureSampler.Bind(Initializer.ParameterMap, TEXT("ShadowDepthTextureSampler"));
		OnePassShadowParameters.Bind(Initializer.ParameterMap);
		LightFunctionParameters.Bind(Initializer.ParameterMap);
		TranslucentInjectParameters.Bind(Initializer.ParameterMap);
		LightFunctionWorldToLight.Bind(Initializer.ParameterMap, TEXT("LightFunctionWorldToLight"));
		bStaticallyShadowed.Bind(Initializer.ParameterMap, TEXT("bStaticallyShadowed"));
		StaticShadowDepthTexture.Bind(Initializer.ParameterMap, TEXT("StaticShadowDepthTexture"));
		StaticShadowDepthTextureSampler.Bind(Initializer.ParameterMap, TEXT("StaticShadowDepthTextureSampler"));
		WorldToStaticShadowMatrix.Bind(Initializer.ParameterMap, TEXT("WorldToStaticShadowMatrix"));
	}
	TTranslucentLightingInjectPS() {}

	// @param InnerSplitIndex which CSM shadow map level, INDEX_NONE if no directional light
	// @param VolumeCascadeIndexValue which volume we render to
	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View, 
		const FLightSceneInfo* LightSceneInfo, 
		const FMaterialRenderProxy* MaterialProxy, 
		const FProjectedShadowInfo* ShadowMap, 
		int32 InnerSplitIndex, 
		int32 VolumeCascadeIndexValue)
	{
		check(ShadowMap || !bDynamicallyShadowed);
		
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();

		FMaterialShader::SetParameters(RHICmdList, ShaderRHI, MaterialProxy, *MaterialProxy->GetMaterial(View.GetFeatureLevel()), View, false, ESceneRenderTargetsMode::SetTextures);
		
		FSphere ShadowBounds = ShadowMap ? ShadowMap->ShadowBounds : FSphere(FVector(0,0,0), HALF_WORLD_MAX);
		SetShaderValue(RHICmdList, ShaderRHI, CascadeBounds, ShadowBounds);

		FSphere InnerBounds(0);
		// default to ignore the plane
		FVector4 Planes[2] = { FVector4(0, 0, 0, -1), FVector4(0, 0, 0, -1) };
		// .zw:DistanceFadeMAD to use MAD for efficiency in the shader, default to ignore the plane
		FVector4 ShadowInjectParamValue(1, 1, 0, 0);

		if (InnerSplitIndex >= 0)
		{
			FShadowCascadeSettings ShadowCascadeSettings;

			// todo: optimize, not all computed data is needed
			InnerBounds = LightSceneInfo->Proxy->GetShadowSplitBounds(View, (uint32)InnerSplitIndex, LightSceneInfo->IsPrecomputedLightingValid(), &ShadowCascadeSettings);

			// near cascade plane
			{
				ShadowInjectParamValue.X = ShadowCascadeSettings.SplitNearFadeRegion == 0 ? 1.0f : 1.0f / ShadowCascadeSettings.SplitNearFadeRegion;
				Planes[0] = FVector4((FVector)(ShadowCascadeSettings.NearFrustumPlane),  -ShadowCascadeSettings.NearFrustumPlane.W);
			}

			uint32 CascadeCount = LightSceneInfo->Proxy->GetNumViewDependentWholeSceneShadows(View, LightSceneInfo->IsPrecomputedLightingValid());

			// far cascade plane
			if(InnerSplitIndex != CascadeCount - 1)
			{
				ShadowInjectParamValue.Y = 1.0f / ShadowCascadeSettings.SplitFarFadeRegion;
				Planes[1] = FVector4((FVector)(ShadowCascadeSettings.FarFrustumPlane), -ShadowCascadeSettings.FarFrustumPlane.W);
			}

			const FVector2D FadeParams = LightSceneInfo->Proxy->GetDirectionalLightDistanceFadeParameters(View.GetFeatureLevel(), LightSceneInfo->IsPrecomputedLightingValid());

			// setup constants for the MAD in shader
			ShadowInjectParamValue.Z = FadeParams.Y;
			ShadowInjectParamValue.W = -FadeParams.X * FadeParams.Y;
		}

		SetShaderValue(RHICmdList, ShaderRHI, ShadowInjectParams, ShadowInjectParamValue);
		SetShaderValue(RHICmdList, ShaderRHI, InnerCascadeBounds, InnerBounds);

		SetShaderValueArray(RHICmdList, ShaderRHI, ClippingPlanes, Planes, ARRAY_COUNT(Planes));
	
		bool bIsSpotlight = LightSceneInfo->Proxy->GetLightType() == LightType_Spot;
		//@todo - needs to be a permutation to reduce shadow filtering work
		SetShaderValue(RHICmdList, ShaderRHI, SpotlightMask, (bIsSpotlight ? 1.0f : 0.0f));

		if (bDynamicallyShadowed)
		{
			SetShaderValue(RHICmdList, ShaderRHI, DepthBiasParameters, FVector2D(ShadowMap->GetShaderDepthBias(), 1.0f / (ShadowMap->MaxSubjectZ - ShadowMap->MinSubjectZ)));

			FTextureRHIParamRef ShadowDepthTextureResource = nullptr;
			if (InjectionType == LightType_Point && !bIsSpotlight)
			{
				if (GBlackTexture && GBlackTexture->TextureRHI)
				{
					ShadowDepthTextureResource = GBlackTexture->TextureRHI->GetTexture2D();
				}
			}
			else
			{
				ShadowDepthTextureResource = ShadowMap->RenderTargets.DepthTarget->GetRenderTargetItem().ShaderResourceTexture.GetReference();
			}

			SetTextureParameter(
				RHICmdList, 
				ShaderRHI,
				ShadowDepthTexture,
				ShadowDepthTextureSampler,
				TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
				ShadowDepthTextureResource
				);
		}

		OnePassShadowParameters.Set(RHICmdList, ShaderRHI, bDynamicallyShadowed && InjectionType == LightType_Point ? ShadowMap : NULL);

		LightFunctionParameters.Set(RHICmdList, ShaderRHI, LightSceneInfo, 1);
		TranslucentInjectParameters.Set(RHICmdList, ShaderRHI, this, View, LightSceneInfo, ShadowMap, VolumeCascadeIndexValue, bDynamicallyShadowed);

		if (LightFunctionWorldToLight.IsBound())
		{
			const FVector Scale = LightSceneInfo->Proxy->GetLightFunctionScale();
			// Switch x and z so that z of the user specified scale affects the distance along the light direction
			const FVector InverseScale = FVector( 1.f / Scale.Z, 1.f / Scale.Y, 1.f / Scale.X );
			const FMatrix WorldToLight = LightSceneInfo->Proxy->GetWorldToLight() * FScaleMatrix(FVector(InverseScale));	

			SetShaderValue(RHICmdList, ShaderRHI, LightFunctionWorldToLight, WorldToLight);
		}

		const FStaticShadowDepthMap* StaticShadowDepthMap = LightSceneInfo->Proxy->GetStaticShadowDepthMap();
		const uint32 bStaticallyShadowedValue = LightSceneInfo->IsPrecomputedLightingValid() && StaticShadowDepthMap && StaticShadowDepthMap->TextureRHI ? 1 : 0;
		FTextureRHIParamRef StaticShadowDepthMapTexture = bStaticallyShadowedValue ? StaticShadowDepthMap->TextureRHI : GWhiteTexture->TextureRHI;
		const FMatrix WorldToStaticShadow = bStaticallyShadowedValue ? StaticShadowDepthMap->Data.WorldToLight : FMatrix::Identity;

		SetShaderValue(RHICmdList, ShaderRHI, bStaticallyShadowed, bStaticallyShadowedValue);

		SetTextureParameter(
			RHICmdList, 
			ShaderRHI,
			StaticShadowDepthTexture,
			StaticShadowDepthTextureSampler,
			TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			StaticShadowDepthMapTexture
			);

		SetShaderValue(RHICmdList, ShaderRHI, WorldToStaticShadowMatrix, WorldToStaticShadow);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMaterialShader::Serialize(Ar);
		Ar << DepthBiasParameters;
		Ar << CascadeBounds;
		Ar << InnerCascadeBounds;
		Ar << ClippingPlanes;
		Ar << ShadowInjectParams;
		Ar << SpotlightMask;
		Ar << ShadowDepthTexture;
		Ar << ShadowDepthTextureSampler;
		Ar << OnePassShadowParameters;
		Ar << LightFunctionParameters;
		Ar << TranslucentInjectParameters;
		Ar << LightFunctionWorldToLight;
		Ar << bStaticallyShadowed;
		Ar << StaticShadowDepthTexture;
		Ar << StaticShadowDepthTextureSampler;
		Ar << WorldToStaticShadowMatrix;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter DepthBiasParameters;
	FShaderParameter CascadeBounds;
	FShaderParameter InnerCascadeBounds;
	FShaderParameter ClippingPlanes;
	FShaderParameter ShadowInjectParams;
	FShaderParameter SpotlightMask;
	FShaderResourceParameter ShadowDepthTexture;
	FShaderResourceParameter ShadowDepthTextureSampler;
	FOnePassPointShadowProjectionShaderParameters OnePassShadowParameters;
	FLightFunctionSharedParameters LightFunctionParameters;
	FTranslucentInjectParameters TranslucentInjectParameters;
	FShaderParameter LightFunctionWorldToLight;
	FShaderParameter bStaticallyShadowed;
	FShaderResourceParameter StaticShadowDepthTexture;
	FShaderResourceParameter StaticShadowDepthTextureSampler;
	FShaderParameter WorldToStaticShadowMatrix;
};

#define IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType,bDynamicallyShadowed,bApplyLightFunction,bInverseSquared) \
	typedef TTranslucentLightingInjectPS<LightType,bDynamicallyShadowed,bApplyLightFunction,bInverseSquared> TTranslucentLightingInjectPS##LightType##bDynamicallyShadowed##bApplyLightFunction##bInverseSquared; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TTranslucentLightingInjectPS##LightType##bDynamicallyShadowed##bApplyLightFunction##bInverseSquared,TEXT("TranslucentLightInjectionShaders"),TEXT("InjectMainPS"),SF_Pixel);

/** Versions with a light function. */
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Directional,true,true,false); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Directional,false,true,false); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,true,true,true); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,false,true,true); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,true,true,false); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,false,true,false); 

/** Versions without a light function. */
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Directional,true,false,false); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Directional,false,false,false); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,true,false,true); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,false,false,true); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,true,false,false); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,false,false,false); 

/** Vertex buffer used for rendering into a volume texture. */
class FVolumeRasterizeVertexBuffer : public FVertexBuffer
{
public:

	virtual void InitRHI() override
	{
		// Used as a non-indexed triangle strip, so 4 vertices per quad
		const uint32 Size = 4 * sizeof(FScreenVertex);
		FRHIResourceCreateInfo CreateInfo;
		void* Buffer = nullptr;
		VertexBufferRHI = RHICreateAndLockVertexBuffer(Size, BUF_Static, CreateInfo, Buffer);		
		FScreenVertex* DestVertex = (FScreenVertex*)Buffer;

		// Setup a full - render target quad
		// A viewport and UVScaleBias will be used to implement rendering to a sub region
		DestVertex[0].Position = FVector2D(1, -GProjectionSignY);
		DestVertex[0].UV = FVector2D(1, 1);
		DestVertex[1].Position = FVector2D(1, GProjectionSignY);
		DestVertex[1].UV = FVector2D(1, 0);
		DestVertex[2].Position = FVector2D(-1, -GProjectionSignY);
		DestVertex[2].UV = FVector2D(0, 1);
		DestVertex[3].Position = FVector2D(-1, GProjectionSignY);
		DestVertex[3].UV = FVector2D(0, 0);

		RHIUnlockVertexBuffer(VertexBufferRHI);      
	}
};

TGlobalResource<FVolumeRasterizeVertexBuffer> GVolumeRasterizeVertexBuffer;

/** Draws a quad per volume texture slice to the subregion of the volume texture specified. */
void RasterizeToVolumeTexture(FRHICommandList& RHICmdList, FVolumeBounds VolumeBounds)
{
	// Setup the viewport to only render to the given bounds
	RHICmdList.SetViewport(VolumeBounds.MinX, VolumeBounds.MinY, 0, VolumeBounds.MaxX, VolumeBounds.MaxY, 0);
	RHICmdList.SetStreamSource(0, GVolumeRasterizeVertexBuffer.VertexBufferRHI, sizeof(FScreenVertex), 0);
	const int32 NumInstances = VolumeBounds.MaxZ - VolumeBounds.MinZ;
	// Render a quad per slice affected by the given bounds
	RHICmdList.DrawPrimitive(PT_TriangleStrip, 0, 2, NumInstances);
}
        
/** Helper function that clears the given volume texture render targets. */
template<int32 NumRenderTargets>
void ClearVolumeTextures(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, const FTextureRHIParamRef* RenderTargets, const FLinearColor* ClearColors)
{
	SetRenderTargets(RHICmdList, NumRenderTargets, RenderTargets, FTextureRHIRef(), 0, NULL, true);
#if PLATFORM_XBOXONE
	// ClearMRT is faster on Xbox
	if (true)
#else
	// Currently using a manual clear, which is ~10x faster than a hardware clear of the volume textures on AMD PC GPU's
	if (false)
#endif
	{
		RHICmdList.ClearMRT(true, NumRenderTargets, ClearColors, false, 0, false, 0, FIntRect());
	}
	else
	{
		static FGlobalBoundShaderState VolumeClearBoundShaderState;

		RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
		RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());

		const FVolumeBounds VolumeBounds(GTranslucencyLightingVolumeDim);
		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FWriteToSliceVS> VertexShader(ShaderMap);
		TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(ShaderMap);
		TShaderMapRef<TOneColorPixelShaderMRT<NumRenderTargets> > PixelShader(ShaderMap);
		
		SetGlobalBoundShaderState(RHICmdList, FeatureLevel, VolumeClearBoundShaderState, GScreenVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader, *GeometryShader);

		VertexShader->SetParameters(RHICmdList, VolumeBounds, GTranslucencyLightingVolumeDim);
		if(GeometryShader.IsValid())
		{
			GeometryShader->SetParameters(RHICmdList, VolumeBounds);
		}
		PixelShader->SetColors(RHICmdList, ClearColors, NumRenderTargets);

		RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
	}
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, (FTextureRHIParamRef*)RenderTargets, NumRenderTargets);
}

void FDeferredShadingSceneRenderer::ClearTranslucentVolumeLighting(FRHICommandListImmediate& RHICmdList)
{
	if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering)
	{
		SCOPED_DRAW_EVENT(RHICmdList, ClearTranslucentVolumeLighting);
		SCOPED_GPU_STAT(RHICmdList, Stat_GPU_TranslucentLighting);

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		// Clear all volume textures in the same draw with MRT, which is faster than individually
		static_assert(TVC_MAX == 2, "Only expecting two translucency lighting cascades.");
		FTextureRHIParamRef RenderTargets[4];
		RenderTargets[0] = SceneContext.TranslucencyLightingVolumeAmbient[0]->GetRenderTargetItem().TargetableTexture;
		RenderTargets[1] = SceneContext.TranslucencyLightingVolumeDirectional[0]->GetRenderTargetItem().TargetableTexture;
		RenderTargets[2] = SceneContext.TranslucencyLightingVolumeAmbient[1]->GetRenderTargetItem().TargetableTexture;
		RenderTargets[3] = SceneContext.TranslucencyLightingVolumeDirectional[1]->GetRenderTargetItem().TargetableTexture;

		FLinearColor ClearColors[4];
		ClearColors[0] = FLinearColor(0, 0, 0, 0);
		ClearColors[1] = FLinearColor(0, 0, 0, 0);
		ClearColors[2] = FLinearColor(0, 0, 0, 0);
		ClearColors[3] = FLinearColor(0, 0, 0, 0);

		ClearVolumeTextures<ARRAY_COUNT(RenderTargets)>(RHICmdList, FeatureLevel, RenderTargets, ClearColors);
	}
}

class FClearTranslucentLightingVolumeCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FClearTranslucentLightingVolumeCS, Global)
public:

	static const int32 CLEAR_BLOCK_SIZE = 4;

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("CLEAR_COMPUTE_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("CLEAR_BLOCK_SIZE"), CLEAR_BLOCK_SIZE);
	}

	FClearTranslucentLightingVolumeCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Ambient0.Bind(Initializer.ParameterMap, TEXT("Ambient0"));
		Directional0.Bind(Initializer.ParameterMap, TEXT("Directional0"));
		Ambient1.Bind(Initializer.ParameterMap, TEXT("Ambient1"));
		Directional1.Bind(Initializer.ParameterMap, TEXT("Directional1"));
	}

	FClearTranslucentLightingVolumeCS()
	{
	}

	void SetParameters(
		FRHIAsyncComputeCommandListImmediate& RHICmdList,
		FUnorderedAccessViewRHIParamRef* VolumeUAVs,
		int32 NumUAVs
	)
	{
		check(NumUAVs == 4);
		const FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		Ambient0.SetTexture(RHICmdList, ShaderRHI, NULL, VolumeUAVs[0]);
		Directional0.SetTexture(RHICmdList, ShaderRHI, NULL, VolumeUAVs[1]);
		Ambient1.SetTexture(RHICmdList, ShaderRHI, NULL, VolumeUAVs[2]);
		Directional1.SetTexture(RHICmdList, ShaderRHI, NULL, VolumeUAVs[3]);
	}

	void UnsetParameters(FRHIAsyncComputeCommandListImmediate& RHICmdList)
	{
		const FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();
		Ambient0.UnsetUAV(RHICmdList, ShaderRHI);
		Directional0.UnsetUAV(RHICmdList, ShaderRHI);
		Ambient1.UnsetUAV(RHICmdList, ShaderRHI);
		Directional1.UnsetUAV(RHICmdList, ShaderRHI);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << Ambient0;
		Ar << Directional0;
		Ar << Ambient1;
		Ar << Directional1;
		return bShaderHasOutdatedParameters;
	}

private:
	FRWShaderParameter Ambient0;
	FRWShaderParameter Directional0;
	FRWShaderParameter Ambient1;
	FRWShaderParameter Directional1;
};

IMPLEMENT_SHADER_TYPE(, FClearTranslucentLightingVolumeCS, TEXT("TranslucentLightInjectionShaders"), TEXT("ClearTranslucentLightingVolumeCS"), SF_Compute)

void FDeferredShadingSceneRenderer::ClearTranslucentVolumeLightingAsyncCompute(FRHICommandListImmediate& RHICmdList)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	FUnorderedAccessViewRHIParamRef VolumeUAVs[4] = {
	SceneContext.TranslucencyLightingVolumeAmbient[0]->GetRenderTargetItem().UAV,
	SceneContext.TranslucencyLightingVolumeDirectional[0]->GetRenderTargetItem().UAV,
	SceneContext.TranslucencyLightingVolumeAmbient[1]->GetRenderTargetItem().UAV,
	SceneContext.TranslucencyLightingVolumeDirectional[1]->GetRenderTargetItem().UAV
	};

	FClearTranslucentLightingVolumeCS* ComputeShader = *TShaderMapRef<FClearTranslucentLightingVolumeCS>(GetGlobalShaderMap(FeatureLevel));
	static const FName EndComputeFenceName(TEXT("TranslucencyLightingVolumeClearEndComputeFence"));
	TranslucencyLightingVolumeClearEndFence = RHICmdList.CreateComputeFence(EndComputeFenceName);

	static const FName BeginComputeFenceName(TEXT("TranslucencyLightingVolumeClearBeginComputeFence"));
	FComputeFenceRHIRef ClearBeginFence = RHICmdList.CreateComputeFence(BeginComputeFenceName);

	//write fence on the Gfx pipe so the async clear compute shader won't clear until the Gfx pipe is caught up.
	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, VolumeUAVs, 4, ClearBeginFence);

	//Grab the async compute commandlist.
	FRHIAsyncComputeCommandListImmediate& RHICmdListComputeImmediate = FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();
	{
		SCOPED_COMPUTE_EVENTF(RHICmdListComputeImmediate, ClearTranslucencyLightingVolume, TEXT("Translucency lighting volume clear compute shader. %d"), GTranslucencyLightingVolumeDim);

		//we must wait on the fence written from the Gfx pipe to let us know all our dependencies are ready.
		RHICmdListComputeImmediate.WaitComputeFence(ClearBeginFence);

		//standard compute setup, but on the async commandlist.
		RHICmdListComputeImmediate.SetComputeShader(ComputeShader->GetComputeShader());

		ComputeShader->SetParameters(RHICmdListComputeImmediate, VolumeUAVs, 4);
		
		int32 GroupsPerDim = GTranslucencyLightingVolumeDim / FClearTranslucentLightingVolumeCS::CLEAR_BLOCK_SIZE;
		DispatchComputeShader(RHICmdListComputeImmediate, ComputeShader, GroupsPerDim, GroupsPerDim, GroupsPerDim);

		ComputeShader->UnsetParameters(RHICmdListComputeImmediate);

		//transition the output to readable and write the fence to allow the Gfx pipe to carry on.
		RHICmdListComputeImmediate.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, VolumeUAVs, 4, TranslucencyLightingVolumeClearEndFence);
	}

	//immediately dispatch our async compute commands to the RHI thread to be submitted to the GPU as soon as possible.
	//dispatch after the scope so the drawevent pop is inside the dispatch
	FRHIAsyncComputeCommandListImmediate::ImmediateDispatch(RHICmdListComputeImmediate);
}

/** Encapsulates a pixel shader that is adding ambient cubemap to the volume. */
class FInjectAmbientCubemapPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FInjectAmbientCubemapPS, Global);

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4);
	}

	/** Default constructor. */
	FInjectAmbientCubemapPS() {}

public:
	FCubemapShaderParameters CubemapShaderParameters;

	/** Initialization constructor. */
	FInjectAmbientCubemapPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		CubemapShaderParameters.Bind(Initializer.ParameterMap);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << CubemapShaderParameters;
		return bShaderHasOutdatedParameters;
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FFinalPostProcessSettings::FCubemapEntry& CubemapEntry)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters(RHICmdList, ShaderRHI, View);

		CubemapShaderParameters.SetParameters(RHICmdList, ShaderRHI, CubemapEntry);
	}
};

IMPLEMENT_SHADER_TYPE(,FInjectAmbientCubemapPS,TEXT("TranslucentLightingShaders"),TEXT("InjectAmbientCubemapMainPS"),SF_Pixel);

void FDeferredShadingSceneRenderer::InjectAmbientCubemapTranslucentVolumeLighting(FRHICommandList& RHICmdList)
{
	//@todo - support multiple views
	const FViewInfo& View = Views[0];

	if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering && View.FinalPostProcessSettings.ContributingCubemaps.Num())
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		SCOPED_DRAW_EVENT(RHICmdList, InjectAmbientCubemapTranslucentVolumeLighting);
		SCOPED_GPU_STAT(RHICmdList, Stat_GPU_TranslucentLighting);

		const FVolumeBounds VolumeBounds(GTranslucencyLightingVolumeDim);

		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
		
		for (int32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
		{
			//Checks to detect/prevent UE-31578
			const IPooledRenderTarget* RT0 = SceneContext.TranslucencyLightingVolumeAmbient[VolumeCascadeIndex];

			// we don't update the directional volume (could be a HQ option)
			SetRenderTarget(RHICmdList, RT0->GetRenderTargetItem().TargetableTexture, FTextureRHIRef(), true);

			RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
			RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
			RHICmdList.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());

			TShaderMapRef<FWriteToSliceVS> VertexShader(ShaderMap);
			TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(ShaderMap);
			TShaderMapRef<FInjectAmbientCubemapPS> PixelShader(ShaderMap);

			static FGlobalBoundShaderState BoundShaderState;
									
			SetGlobalBoundShaderState(RHICmdList, FeatureLevel, BoundShaderState, GScreenVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader, *GeometryShader);

			VertexShader->SetParameters(RHICmdList, VolumeBounds, GTranslucencyLightingVolumeDim);
			if(GeometryShader.IsValid())
			{
				GeometryShader->SetParameters(RHICmdList, VolumeBounds);
			}

			uint32 Count = View.FinalPostProcessSettings.ContributingCubemaps.Num();
			for(uint32 i = 0; i < Count; ++i)
			{
				const FFinalPostProcessSettings::FCubemapEntry& CubemapEntry = View.FinalPostProcessSettings.ContributingCubemaps[i];

				PixelShader->SetParameters(RHICmdList, View, CubemapEntry);

				RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
			}

			RHICmdList.CopyToResolveTarget(RT0->GetRenderTargetItem().TargetableTexture,
			RT0->GetRenderTargetItem().ShaderResourceTexture, true, FResolveParams());
		}
	}
}

void FDeferredShadingSceneRenderer::ClearTranslucentVolumePerObjectShadowing(FRHICommandList& RHICmdList)
{
	if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		SCOPED_DRAW_EVENT(RHICmdList, ClearTranslucentVolumePerLightShadowing);
		SCOPED_GPU_STAT(RHICmdList, Stat_GPU_TranslucentLighting);

		static_assert(TVC_MAX == 2, "Only expecting two translucency lighting cascades.");
		FTextureRHIParamRef RenderTargets[2];
		RenderTargets[0] = SceneContext.GetTranslucencyVolumeAmbient(TVC_Inner)->GetRenderTargetItem().TargetableTexture;
		RenderTargets[1] = SceneContext.GetTranslucencyVolumeDirectional(TVC_Inner)->GetRenderTargetItem().TargetableTexture;

		FLinearColor ClearColors[2];
		ClearColors[0] = FLinearColor(1, 1, 1, 1);
		ClearColors[1] = FLinearColor(1, 1, 1, 1);

		ClearVolumeTextures<ARRAY_COUNT(RenderTargets)>(RHICmdList, FeatureLevel, RenderTargets, ClearColors);
	}
}

/** Calculates volume texture bounds for the given light in the given translucent lighting volume cascade. */
FVolumeBounds CalculateLightVolumeBounds(const FSphere& LightBounds, const FViewInfo& View, uint32 VolumeCascadeIndex, bool bDirectionalLight)
{
	FVolumeBounds VolumeBounds;

	if (bDirectionalLight)
	{
		VolumeBounds = FVolumeBounds(GTranslucencyLightingVolumeDim);
	}
	else
	{
		// Determine extents in the volume texture
		const FVector MinPosition = (LightBounds.Center - LightBounds.W - View.TranslucencyLightingVolumeMin[VolumeCascadeIndex]) / View.TranslucencyVolumeVoxelSize[VolumeCascadeIndex];
		const FVector MaxPosition = (LightBounds.Center + LightBounds.W - View.TranslucencyLightingVolumeMin[VolumeCascadeIndex]) / View.TranslucencyVolumeVoxelSize[VolumeCascadeIndex];

		VolumeBounds.MinX = FMath::Max(FMath::TruncToInt(MinPosition.X), 0);
		VolumeBounds.MinY = FMath::Max(FMath::TruncToInt(MinPosition.Y), 0);
		VolumeBounds.MinZ = FMath::Max(FMath::TruncToInt(MinPosition.Z), 0);

		VolumeBounds.MaxX = FMath::Min(FMath::TruncToInt(MaxPosition.X) + 1, GTranslucencyLightingVolumeDim);
		VolumeBounds.MaxY = FMath::Min(FMath::TruncToInt(MaxPosition.Y) + 1, GTranslucencyLightingVolumeDim);
		VolumeBounds.MaxZ = FMath::Min(FMath::TruncToInt(MaxPosition.Z) + 1, GTranslucencyLightingVolumeDim);
	}

	return VolumeBounds;
}

FGlobalBoundShaderState ObjectShadowingBoundShaderState;

void FDeferredShadingSceneRenderer::AccumulateTranslucentVolumeObjectShadowing(FRHICommandList& RHICmdList, const FProjectedShadowInfo* InProjectedShadowInfo, bool bClearVolume)
{
	const FLightSceneInfo* LightSceneInfo = &InProjectedShadowInfo->GetLightSceneInfo();

	if (bClearVolume)
	{
		ClearTranslucentVolumePerObjectShadowing(RHICmdList);
	}

	if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering)
	{
		SCOPED_DRAW_EVENT(RHICmdList, AccumulateTranslucentVolumeShadowing);
		SCOPED_GPU_STAT(RHICmdList, Stat_GPU_TranslucentLighting);

		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		// Inject into each volume cascade
		for (uint32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
		{
			//@todo - support multiple views
			const FViewInfo& View = Views[0];
			const bool bDirectionalLight = LightSceneInfo->Proxy->GetLightType() == LightType_Directional;
			const FVolumeBounds VolumeBounds = CalculateLightVolumeBounds(LightSceneInfo->Proxy->GetBoundingSphere(), View, VolumeCascadeIndex, bDirectionalLight);

			if (VolumeBounds.IsValid())
			{
				FTextureRHIParamRef RenderTarget;

				if (VolumeCascadeIndex == 0)
				{
					RenderTarget = SceneContext.GetTranslucencyVolumeAmbient(TVC_Inner)->GetRenderTargetItem().TargetableTexture;
				}
				else
				{
					RenderTarget = SceneContext.GetTranslucencyVolumeDirectional(TVC_Inner)->GetRenderTargetItem().TargetableTexture;
				}

				SetRenderTarget(RHICmdList, RenderTarget, FTextureRHIRef());

				RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
				RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

				// Modulate the contribution of multiple object shadows in rgb
				RHICmdList.SetBlendState(TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_Zero>::GetRHI());

				TShaderMapRef<FWriteToSliceVS> VertexShader(ShaderMap);
				TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(ShaderMap);
				TShaderMapRef<FTranslucentObjectShadowingPS> PixelShader(ShaderMap);

				SetGlobalBoundShaderState(RHICmdList, FeatureLevel, ObjectShadowingBoundShaderState, GScreenVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader, *GeometryShader);
				VertexShader->SetParameters(RHICmdList, VolumeBounds, GTranslucencyLightingVolumeDim);
				if(GeometryShader.IsValid())
				{
					GeometryShader->SetParameters(RHICmdList, VolumeBounds);
				}
				PixelShader->SetParameters(RHICmdList, View, LightSceneInfo, InProjectedShadowInfo, VolumeCascadeIndex);
				
				RasterizeToVolumeTexture(RHICmdList, VolumeBounds);

				RHICmdList.CopyToResolveTarget(SceneContext.GetTranslucencyVolumeAmbient((ETranslucencyVolumeCascade)VolumeCascadeIndex)->GetRenderTargetItem().TargetableTexture,
					SceneContext.GetTranslucencyVolumeAmbient((ETranslucencyVolumeCascade)VolumeCascadeIndex)->GetRenderTargetItem().ShaderResourceTexture, true, FResolveParams());
			}
		}
	}
}

/**
 * Helper function for finding and setting the right version of TTranslucentLightingInjectPS given template parameters.
 * @param MaterialProxy must not be 0
 * @param InnerSplitIndex todo: get from ShadowMap, INDEX_NONE if no directional light
 */
template<ELightComponentType InjectionType, bool bDynamicallyShadowed>
void SetInjectionShader(
	FRHICommandList& RHICmdList,
	const FViewInfo& View, 
	const FMaterialRenderProxy* MaterialProxy,
	const FLightSceneInfo* LightSceneInfo, 
	const FProjectedShadowInfo* ShadowMap, 
	int32 InnerSplitIndex, 
	int32 VolumeCascadeIndexValue,
	FWriteToSliceVS* VertexShader,
	FWriteToSliceGS* GeometryShader,
	bool bApplyLightFunction,
	bool bInverseSquared)
{
	check(ShadowMap || !bDynamicallyShadowed);

	const FMaterialShaderMap* MaterialShaderMap = MaterialProxy->GetMaterial(View.GetFeatureLevel())->GetRenderingThreadShaderMap();
	FMaterialShader* PixelShader = NULL;

	const bool Directional = InjectionType == LightType_Directional;

	if (bApplyLightFunction)
	{
		if( bInverseSquared )
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, true, true && !Directional> >();

			check(InjectionPixelShader);
			PixelShader = InjectionPixelShader;
		}
		else
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, true, false> >();

			check(InjectionPixelShader);
			PixelShader = InjectionPixelShader;
		}
	}
	else
	{
		if( bInverseSquared )
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, false, true && !Directional> >();

			check(InjectionPixelShader);
			PixelShader = InjectionPixelShader;
		}
		else
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, false, false> >();

			check(InjectionPixelShader);
			PixelShader = InjectionPixelShader;
		}
	}
	
	FBoundShaderStateRHIRef& BoundShaderState = LightSceneInfo->TranslucentInjectBoundShaderState[InjectionType][bDynamicallyShadowed][bApplyLightFunction][bInverseSquared];
	const FMaterialShaderMap*& CachedShaderMap = LightSceneInfo->TranslucentInjectCachedShaderMaps[InjectionType][bDynamicallyShadowed][bApplyLightFunction][bInverseSquared];

	// Recreate the bound shader state if the shader map has changed since the last cache
	// This can happen due to async shader compiling

	if (!IsValidRef(BoundShaderState) || CachedShaderMap != MaterialShaderMap)
	{
		CachedShaderMap = MaterialShaderMap;
		check(IsInRenderingThread()); // I didn't know quite how to deal with this caching. It won't work with threads.
		BoundShaderState = 
			RHICreateBoundShaderState( GScreenVertexDeclaration.VertexDeclarationRHI, VertexShader->GetVertexShader(), FHullShaderRHIRef(), FDomainShaderRHIRef(), PixelShader->GetPixelShader(), GeometryShader ? GeometryShader->GetGeometryShader() : nullptr);
	}

	RHICmdList.SetBoundShaderState(BoundShaderState);

	// Now shader is set, bind parameters
	if (bApplyLightFunction)
	{
		if( bInverseSquared )
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, true, true && !Directional> >();
			check(InjectionPixelShader);
			InjectionPixelShader->SetParameters(RHICmdList, View, LightSceneInfo, MaterialProxy, ShadowMap, InnerSplitIndex, VolumeCascadeIndexValue);
		}
		else
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, true, false> >();
			check(InjectionPixelShader);
			InjectionPixelShader->SetParameters(RHICmdList, View, LightSceneInfo, MaterialProxy, ShadowMap, InnerSplitIndex, VolumeCascadeIndexValue);
		}
	}
	else
	{
		if( bInverseSquared )
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, false, true && !Directional> >();
			check(InjectionPixelShader);
			InjectionPixelShader->SetParameters(RHICmdList, View, LightSceneInfo, MaterialProxy, ShadowMap, InnerSplitIndex, VolumeCascadeIndexValue);
		}
		else
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, false, false> >();
			check(InjectionPixelShader);
			InjectionPixelShader->SetParameters(RHICmdList, View, LightSceneInfo, MaterialProxy, ShadowMap, InnerSplitIndex, VolumeCascadeIndexValue);
		}
	}
}

/** 
 * Information about a light to be injected.
 * Cached in this struct to avoid recomputing multiple times (multiple cascades).
 */
struct FTranslucentLightInjectionData
{
	// must not be 0
	const FLightSceneInfo* LightSceneInfo;
	// can be 0
	const FProjectedShadowInfo* ProjectedShadowInfo;
	//
	bool bApplyLightFunction;
	// must not be 0
	const FMaterialRenderProxy* LightFunctionMaterialProxy;
};

/**
 * Adds a light to LightInjectionData if it should be injected into the translucent volume, and caches relevant information in a FTranslucentLightInjectionData.
 * @param InProjectedShadowInfo is 0 for unshadowed lights
 */
static void AddLightForInjection(
	FDeferredShadingSceneRenderer& SceneRenderer,
	const FLightSceneInfo& LightSceneInfo, 
	const FProjectedShadowInfo* InProjectedShadowInfo,
	TArray<FTranslucentLightInjectionData, SceneRenderingAllocator>& LightInjectionData)
{
	if (LightSceneInfo.Proxy->AffectsTranslucentLighting())
	{
		const FVisibleLightInfo& VisibleLightInfo = SceneRenderer.VisibleLightInfos[LightSceneInfo.Id];

		const ERHIFeatureLevel::Type FeatureLevel = SceneRenderer.Scene->GetFeatureLevel();

		const bool bApplyLightFunction = (SceneRenderer.ViewFamily.EngineShowFlags.LightFunctions &&
			LightSceneInfo.Proxy->GetLightFunctionMaterial() && 
			LightSceneInfo.Proxy->GetLightFunctionMaterial()->GetMaterial(FeatureLevel)->IsLightFunction());

		const FMaterialRenderProxy* MaterialProxy = bApplyLightFunction ? 
			LightSceneInfo.Proxy->GetLightFunctionMaterial() : 
			UMaterial::GetDefaultMaterial(MD_LightFunction)->GetRenderProxy(false);

		// Skip rendering if the DefaultLightFunctionMaterial isn't compiled yet
		if (MaterialProxy->GetMaterial(FeatureLevel)->IsLightFunction())
		{
			FTranslucentLightInjectionData InjectionData;
			InjectionData.LightSceneInfo = &LightSceneInfo;
			InjectionData.ProjectedShadowInfo = InProjectedShadowInfo;
			InjectionData.bApplyLightFunction = bApplyLightFunction;
			InjectionData.LightFunctionMaterialProxy = MaterialProxy;
			LightInjectionData.Add(InjectionData);
		}
	}
}

/** Injects all the lights in LightInjectionData into the translucent lighting volume textures. */
static void InjectTranslucentLightArray(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const TArray<FTranslucentLightInjectionData, SceneRenderingAllocator>& LightInjectionData)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	INC_DWORD_STAT_BY(STAT_NumLightsInjectedIntoTranslucency, LightInjectionData.Num());

	// Inject into each volume cascade
	// Operate on one cascade at a time to reduce render target switches
	for (uint32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
	{
		const IPooledRenderTarget* RT0 = SceneContext.TranslucencyLightingVolumeAmbient[VolumeCascadeIndex];
		const IPooledRenderTarget* RT1 = SceneContext.TranslucencyLightingVolumeDirectional[VolumeCascadeIndex];

		GRenderTargetPool.VisualizeTexture.SetCheckPoint(RHICmdList, RT0);
		GRenderTargetPool.VisualizeTexture.SetCheckPoint(RHICmdList, RT1);

		FTextureRHIParamRef RenderTargets[2];
		RenderTargets[0] = RT0->GetRenderTargetItem().TargetableTexture;
		RenderTargets[1] = RT1->GetRenderTargetItem().TargetableTexture;

		SetRenderTargets(RHICmdList, ARRAY_COUNT(RenderTargets), RenderTargets, FTextureRHIRef(), 0, NULL, true);

		RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
		RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

		for (int32 LightIndex = 0; LightIndex < LightInjectionData.Num(); LightIndex++)
		{
			const FTranslucentLightInjectionData& InjectionData = LightInjectionData[LightIndex];
			const FLightSceneInfo* const LightSceneInfo = InjectionData.LightSceneInfo;
			const bool bInverseSquared = LightSceneInfo->Proxy->IsInverseSquared();
			const bool bDirectionalLight = LightSceneInfo->Proxy->GetLightType() == LightType_Directional;
			const FVolumeBounds VolumeBounds = CalculateLightVolumeBounds(LightSceneInfo->Proxy->GetBoundingSphere(), View, VolumeCascadeIndex, bDirectionalLight);

			if (VolumeBounds.IsValid())
			{
				TShaderMapRef<FWriteToSliceVS> VertexShader(View.ShaderMap);
				TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(View.ShaderMap);

				if (bDirectionalLight)
				{
					// Accumulate the contribution of multiple lights
					// Directional lights write their shadowing into alpha of the ambient texture
					RHICmdList.SetBlendState(TStaticBlendState<
						CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One,
						CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());

					if (InjectionData.ProjectedShadowInfo)
					{
						// shadows, restricting light contribution to the cascade bounds (except last cascade far to get light functions and no shadows there)
						SetInjectionShader<LightType_Directional, true>(RHICmdList, View, InjectionData.LightFunctionMaterialProxy, LightSceneInfo,
							InjectionData.ProjectedShadowInfo, InjectionData.ProjectedShadowInfo->CascadeSettings.ShadowSplitIndex, VolumeCascadeIndex,
							*VertexShader, *GeometryShader, InjectionData.bApplyLightFunction, false);
					}
					else
					{
						// no shadows
						SetInjectionShader<LightType_Directional, false>(RHICmdList, View, InjectionData.LightFunctionMaterialProxy, LightSceneInfo,
							InjectionData.ProjectedShadowInfo, -1, VolumeCascadeIndex,
							*VertexShader, *GeometryShader, InjectionData.bApplyLightFunction, false);
					}
				}
				else
				{
					// Accumulate the contribution of multiple lights
					RHICmdList.SetBlendState(TStaticBlendState<
						CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One,
						CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One>::GetRHI());

					if (InjectionData.ProjectedShadowInfo)
					{
						SetInjectionShader<LightType_Point, true>(RHICmdList, View, InjectionData.LightFunctionMaterialProxy, LightSceneInfo,
							InjectionData.ProjectedShadowInfo, -1, VolumeCascadeIndex,
							*VertexShader, *GeometryShader, InjectionData.bApplyLightFunction, bInverseSquared);
					}
					else
					{
						SetInjectionShader<LightType_Point, false>(RHICmdList, View, InjectionData.LightFunctionMaterialProxy, LightSceneInfo,
							InjectionData.ProjectedShadowInfo, -1, VolumeCascadeIndex,
							*VertexShader, *GeometryShader, InjectionData.bApplyLightFunction, bInverseSquared);
					}
				}

				VertexShader->SetParameters(RHICmdList, VolumeBounds, GTranslucencyLightingVolumeDim);
				if(GeometryShader.IsValid())
				{
					GeometryShader->SetParameters(RHICmdList, VolumeBounds);
				}
				RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
			}
		}

		RHICmdList.CopyToResolveTarget(RT0->GetRenderTargetItem().TargetableTexture, RT0->GetRenderTargetItem().ShaderResourceTexture, true, FResolveParams());
		RHICmdList.CopyToResolveTarget(RT1->GetRenderTargetItem().TargetableTexture, RT1->GetRenderTargetItem().ShaderResourceTexture, true, FResolveParams());
	}
}

void FDeferredShadingSceneRenderer::InjectTranslucentVolumeLighting(FRHICommandListImmediate& RHICmdList, const FLightSceneInfo& LightSceneInfo, const FProjectedShadowInfo* InProjectedShadowInfo)
{
	if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering)
	{
		SCOPE_CYCLE_COUNTER(STAT_TranslucentInjectTime);

		//@todo - support multiple views
		const FViewInfo& View = Views[0];

		TArray<FTranslucentLightInjectionData, SceneRenderingAllocator> LightInjectionData;

		AddLightForInjection(*this, LightSceneInfo, InProjectedShadowInfo, LightInjectionData);

		// shadowed or unshadowed (InProjectedShadowInfo==0)
		InjectTranslucentLightArray(RHICmdList, View, LightInjectionData);
	}
}

void FDeferredShadingSceneRenderer::InjectTranslucentVolumeLightingArray(FRHICommandListImmediate& RHICmdList, const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& SortedLights, int32 NumLights)
{
	SCOPE_CYCLE_COUNTER(STAT_TranslucentInjectTime);

	//@todo - support multiple views
	const FViewInfo& View = Views[0];

	TArray<FTranslucentLightInjectionData, SceneRenderingAllocator> LightInjectionData;
	LightInjectionData.Empty(NumLights);

	for (int32 LightIndex = 0; LightIndex < NumLights; LightIndex++)
	{
		const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
		const FLightSceneInfoCompact& LightSceneInfoCompact = SortedLightInfo.SceneInfo;
		const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

		AddLightForInjection(*this, *LightSceneInfo, NULL, LightInjectionData);
	}

	// non-shadowed, non-light function lights
	InjectTranslucentLightArray(RHICmdList, View, LightInjectionData);
}

/** Pixel shader used to inject simple lights into the translucent lighting volume */
class FSimpleLightTranslucentLightingInjectPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FSimpleLightTranslucentLightingInjectPS,Global);
public:

	static bool ShouldCache(EShaderPlatform Platform) 
	{ 
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4); 
	}

	FSimpleLightTranslucentLightingInjectPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		VolumeCascadeIndex.Bind(Initializer.ParameterMap, TEXT("VolumeCascadeIndex"));
		SimpleLightPositionAndRadius.Bind(Initializer.ParameterMap, TEXT("SimpleLightPositionAndRadius"));
		SimpleLightColorAndExponent.Bind(Initializer.ParameterMap, TEXT("SimpleLightColorAndExponent"));
	}
	FSimpleLightTranslucentLightingInjectPS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FSimpleLightEntry& SimpleLight, const FSimpleLightPerViewEntry& SimpleLightPerViewData, int32 VolumeCascadeIndexValue)
	{
		FGlobalShader::SetParameters(RHICmdList, GetPixelShader(), View);

		FVector4 PositionAndRadius(SimpleLightPerViewData.Position, SimpleLight.Radius);
		SetShaderValue(RHICmdList, GetPixelShader(), VolumeCascadeIndex, VolumeCascadeIndexValue);
		SetShaderValue(RHICmdList, GetPixelShader(), SimpleLightPositionAndRadius, PositionAndRadius);

		FVector4 LightColorAndExponent(SimpleLight.Color, SimpleLight.Exponent);

		if (SimpleLight.Exponent == 0)
		{
			// Correction for lumen units
			LightColorAndExponent.X *= 16.0f;
			LightColorAndExponent.Y *= 16.0f;
			LightColorAndExponent.Z *= 16.0f;
		}

		SetShaderValue(RHICmdList, GetPixelShader(), SimpleLightColorAndExponent, LightColorAndExponent);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << VolumeCascadeIndex;
		Ar << SimpleLightPositionAndRadius;
		Ar << SimpleLightColorAndExponent;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter VolumeCascadeIndex;
	FShaderParameter SimpleLightPositionAndRadius;
	FShaderParameter SimpleLightColorAndExponent;
};

IMPLEMENT_SHADER_TYPE(,FSimpleLightTranslucentLightingInjectPS,TEXT("TranslucentLightInjectionShaders"),TEXT("SimpleLightInjectMainPS"),SF_Pixel);

FGlobalBoundShaderState InjectSimpleLightBoundShaderState;

void FDeferredShadingSceneRenderer::InjectSimpleTranslucentVolumeLightingArray(FRHICommandListImmediate& RHICmdList, const FSimpleLightArray& SimpleLights)
{
	SCOPE_CYCLE_COUNTER(STAT_TranslucentInjectTime);

	int32 NumLightsToInject = 0;

	for (int32 LightIndex = 0; LightIndex < SimpleLights.InstanceData.Num(); LightIndex++)
	{
		if (SimpleLights.InstanceData[LightIndex].bAffectTranslucency)
		{
			NumLightsToInject++;
		}
	}

	if (NumLightsToInject > 0)
	{
		//@todo - support multiple views
		const FViewInfo& View = Views[0];
		const int32 ViewIndex = 0;

		INC_DWORD_STAT_BY(STAT_NumLightsInjectedIntoTranslucency, NumLightsToInject);
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		// Inject into each volume cascade
		// Operate on one cascade at a time to reduce render target switches
		for (int32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
		{
			const IPooledRenderTarget* RT0 = SceneContext.TranslucencyLightingVolumeAmbient[VolumeCascadeIndex];
			const IPooledRenderTarget* RT1 = SceneContext.TranslucencyLightingVolumeDirectional[VolumeCascadeIndex];

			GRenderTargetPool.VisualizeTexture.SetCheckPoint(RHICmdList, RT0);
			GRenderTargetPool.VisualizeTexture.SetCheckPoint(RHICmdList, RT1);

			FTextureRHIParamRef RenderTargets[2];
			RenderTargets[0] = RT0->GetRenderTargetItem().TargetableTexture;
			RenderTargets[1] = RT1->GetRenderTargetItem().TargetableTexture;

			SetRenderTargets(RHICmdList, ARRAY_COUNT(RenderTargets), RenderTargets, FTextureRHIRef(), 0, NULL, true);

			RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
			RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

		
			for (int32 LightIndex = 0; LightIndex < SimpleLights.InstanceData.Num(); LightIndex++)
			{
				const FSimpleLightEntry& SimpleLight = SimpleLights.InstanceData[LightIndex];
				const FSimpleLightPerViewEntry& SimpleLightPerViewData = SimpleLights.GetViewDependentData(LightIndex, ViewIndex, Views.Num());

				if (SimpleLight.bAffectTranslucency)
				{
					const FSphere LightBounds(SimpleLightPerViewData.Position, SimpleLight.Radius);
					const FVolumeBounds VolumeBounds = CalculateLightVolumeBounds(LightBounds, View, VolumeCascadeIndex, false);

					if (VolumeBounds.IsValid())
					{
						TShaderMapRef<FWriteToSliceVS> VertexShader(View.ShaderMap);
						TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(View.ShaderMap);
						TShaderMapRef<FSimpleLightTranslucentLightingInjectPS> PixelShader(View.ShaderMap);
						SetGlobalBoundShaderState(RHICmdList, FeatureLevel, InjectSimpleLightBoundShaderState, GScreenVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader, *GeometryShader);

						VertexShader->SetParameters(RHICmdList, VolumeBounds, GTranslucencyLightingVolumeDim);
						if(GeometryShader.IsValid())
						{
							GeometryShader->SetParameters(RHICmdList, VolumeBounds);
						}
						PixelShader->SetParameters(RHICmdList, View, SimpleLight, SimpleLightPerViewData, VolumeCascadeIndex);

						// Accumulate the contribution of multiple lights
						RHICmdList.SetBlendState(TStaticBlendState<
							CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One,
							CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One>::GetRHI());

						RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
					}
				}
			}

			RHICmdList.CopyToResolveTarget(RT0->GetRenderTargetItem().TargetableTexture, RT0->GetRenderTargetItem().ShaderResourceTexture, true, FResolveParams());
			RHICmdList.CopyToResolveTarget(RT1->GetRenderTargetItem().TargetableTexture, RT1->GetRenderTargetItem().ShaderResourceTexture, true, FResolveParams());
		}
	}
}

FGlobalBoundShaderState FilterBoundShaderState;

void FDeferredShadingSceneRenderer::FilterTranslucentVolumeLighting(FRHICommandListImmediate& RHICmdList)
{
	if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
#if 0
		// textures have to be finalized before reading.
		for (int32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
		{
			const IPooledRenderTarget* RT0 = SceneContext.TranslucencyLightingVolumeAmbient[VolumeCascadeIndex];
			const IPooledRenderTarget* RT1 = SceneContext.TranslucencyLightingVolumeDirectional[VolumeCascadeIndex];
			FTextureRHIRef TargetTexture0 = RT0->GetRenderTargetItem().TargetableTexture;
			FTextureRHIRef TargetTexture1 = RT1->GetRenderTargetItem().TargetableTexture;
			RHICmdList.CopyToResolveTarget(TargetTexture0, TargetTexture0, true, FResolveParams());
			RHICmdList.CopyToResolveTarget(TargetTexture1, TargetTexture1, true, FResolveParams());
		}
#endif

		if (GUseTranslucencyVolumeBlur)
		{
			//@todo - support multiple views
			const FViewInfo& View = Views[0];

			SCOPED_DRAW_EVENTF(RHICmdList, FilterTranslucentVolume, TEXT("FilterTranslucentVolume %dx%dx%d Cascades:%d"),
				GTranslucencyLightingVolumeDim, GTranslucencyLightingVolumeDim, GTranslucencyLightingVolumeDim, TVC_MAX);

			SCOPED_GPU_STAT(RHICmdList, Stat_GPU_TranslucentLighting);

			// Filter each cascade
			for (int32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
			{
				const IPooledRenderTarget* RT0 = SceneContext.GetTranslucencyVolumeAmbient((ETranslucencyVolumeCascade)VolumeCascadeIndex);
				const IPooledRenderTarget* RT1 = SceneContext.GetTranslucencyVolumeDirectional((ETranslucencyVolumeCascade)VolumeCascadeIndex);

				const IPooledRenderTarget* Input0 = SceneContext.TranslucencyLightingVolumeAmbient[VolumeCascadeIndex];
				const IPooledRenderTarget* Input1 = SceneContext.TranslucencyLightingVolumeDirectional[VolumeCascadeIndex];					

				GRenderTargetPool.VisualizeTexture.SetCheckPoint(RHICmdList, RT0);
				GRenderTargetPool.VisualizeTexture.SetCheckPoint(RHICmdList, RT1);

				FTextureRHIParamRef RenderTargets[2];
				RenderTargets[0] = RT0->GetRenderTargetItem().TargetableTexture;
				RenderTargets[1] = RT1->GetRenderTargetItem().TargetableTexture;

				FTextureRHIParamRef Inputs[2];
				Inputs[0] = Input0->GetRenderTargetItem().TargetableTexture;
				Inputs[1] = Input1->GetRenderTargetItem().TargetableTexture;

				static_assert(TVC_MAX == 2, "Final transition logic should change");

				//the volume textures should still be writable from the injection phase on the first loop.
				if (VolumeCascadeIndex > 0)
				{
					RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, RenderTargets, 2);
				}
				RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, Inputs, 2);

				SetRenderTargets(RHICmdList, ARRAY_COUNT(RenderTargets), RenderTargets, FTextureRHIRef(), 0, NULL, true);


				const FVolumeBounds VolumeBounds(GTranslucencyLightingVolumeDim);
				TShaderMapRef<FWriteToSliceVS> VertexShader(View.ShaderMap);
				TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(View.ShaderMap);
				TShaderMapRef<FFilterTranslucentVolumePS> PixelShader(View.ShaderMap);
				
				RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
				RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
				RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());

				SetGlobalBoundShaderState(RHICmdList, FeatureLevel, FilterBoundShaderState, GScreenVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader, *GeometryShader);

				VertexShader->SetParameters(RHICmdList, VolumeBounds, GTranslucencyLightingVolumeDim);
				if(GeometryShader.IsValid())
				{
					GeometryShader->SetParameters(RHICmdList, VolumeBounds);
				}
				PixelShader->SetParameters(RHICmdList, View, VolumeCascadeIndex);

				RasterizeToVolumeTexture(RHICmdList, VolumeBounds);

				//only do readable transition on the final loop since the other ones will do this up front.
				//if (VolumeCascadeIndex == TVC_MAX - 1)
				{					
					RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, RenderTargets, 2);
				}
			}
		}
	}
}
