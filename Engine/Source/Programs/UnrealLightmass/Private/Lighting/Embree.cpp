// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "LightmassPCH.h"
#include "LightingSystem.h"
 
// #pragma optimize( "", off )
// #define EMBREE_INLINE

#define EMBREE_INLINE FORCEINLINE

#if USE_EMBREE

namespace Lightmass
{

volatile int64 GEmbreeAllocatedSpace = 0;

void FEmbreeTransmissionAccumulator::Resolve(FLinearColor& FinalColor, float tCollide)
{
	FinalColor = FLinearColor::White;

	for (int32 i = 0; i < Colors.Num(); ++i)
	{
		const FLinearColor& Color  = Colors[i];
		if (Color.A < tCollide)
		{
			FinalColor.R *= Color.R;
			FinalColor.G *= Color.G;
			FinalColor.B *= Color.B;
		}
	}
}

void FEmbreeTransmissionAccumulator::Resolve(FLinearColor& FinalColor)
{
	FinalColor = FLinearColor::White;

	for (int32 i = 0; i < Colors.Num(); ++i)
	{
		const FLinearColor& Color  = Colors[i];
		FinalColor.R *= Color.R;
		FinalColor.G *= Color.G;
		FinalColor.B *= Color.B;
	}
}

FEmbreeRay::FEmbreeRay(
	const FStaticLightingMesh* InShadowMesh, 
	const FStaticLightingMesh* InMappingMesh, 
	uint32 InTraceFlags,
	bool InFindClosestIntersection, 
	bool InCalculateTransmission, 
	bool InDirectShadowingRay
	) :
	ShadowMesh(InShadowMesh),
	MappingMesh(InMappingMesh),
	TraceFlags(InTraceFlags),
	bFindClosestIntersection(InFindClosestIntersection),
	bCalculateTransmission(InCalculateTransmission),
	bDirectShadowingRay(InDirectShadowingRay),
	bStaticAndOpaqueOnly((TraceFlags & LIGHTRAY_STATIC_AND_OPAQUEONLY) != 0),
	bTwoSidedCollision(!InDirectShadowingRay),
	bFlipSidedness((TraceFlags & LIGHTRAY_FLIP_SIDEDNESS) != 0),
	ElementIndex(-1),
	RelativeVertexIndex(-1),
	TextureCoordinates(0, 0),
	LightmapCoordinates(0, 0)
{
	u = v = 0;
	time = 0;
	mask = 0xFFFFFFFF;
	geomID = -1;
	instID = -1;
	primID = -1;
}

struct FEmbreeFilterProcessor
{
	FEmbreeFilterProcessor(FEmbreeRay& InRay, const FEmbreeGeometry& InGeo) : Ray(InRay), Geo(InGeo), bCoordsDirty(true) 
	{
		Desc = Geo.TriangleDescs[Ray.primID];
		s = 1 - Ray.u - Ray.v;
	}

	FEmbreeRay& Ray;
	const FEmbreeGeometry& Geo;
	FEmbreeTriangleDesc Desc;

	float s; // (s,u,v) : Barycentric Weights
	int32 Index0;
	int32 Index1;
	int32 Index2;

	int32 RelativeVertexIndex; // Which index is closer.
	FVector2D TextureCoordinates; // Material Coordinates

	bool bCoordsDirty;

	void UpdateCoordinates();

	// This is called when everything succeeds and the ray is the final collision.
	void UpdateRay();

	EMBREE_INLINE void Invalidate()
	{
		Ray.geomID = -1;
	}

	EMBREE_INLINE bool IsBackFace() const
	{
		return Ray.dir[0] * Ray.Ng[0] +  Ray.dir[1] * Ray.Ng[1] + Ray.dir[2] * Ray.Ng[2] < 0;
	}


	EMBREE_INLINE bool HitRejectedByStaticAndOpaqueOnlyTest() const
	{
		return Ray.bStaticAndOpaqueOnly && !Desc.StaticAndOpaqueMask;
	}

	EMBREE_INLINE bool HitRejectedByBackFaceCullingTest() const
	{
		if (!Ray.bTwoSidedCollision && !Desc.TwoSidedMask)
		{
			bool bIsBackFace = IsBackFace();
			if (Ray.bFlipSidedness ? !bIsBackFace : bIsBackFace)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Test if the ray comes from an HLOD (massive LOD) ignore intersection from other LODs of this HLOD.
	 *
	 * @return		true if the ray is rejected.
     */
	EMBREE_INLINE bool HitRejectedByHLODTest() const
	{
		const uint32 InvalidIndex = 0xFFFF;
		uint32 GeoHLODTreeIndex = (Geo.Mesh->GetLODIndices() & 0xFFFF0000) >> 16;
		uint32 RayHLODTreeIndex = Ray.MappingMesh ? (Ray.MappingMesh->GetLODIndices() & 0xFFFF0000) >> 16 : InvalidIndex;

		// If either Geo or Ray is a HLOD (0xFFFF being invalid HLOD).
		if (GeoHLODTreeIndex != InvalidIndex || RayHLODTreeIndex != InvalidIndex)
		{
			uint32 GeoHLODRange = Geo.Mesh->GetHLODRange();
			uint32 GeoHLODRangeStart = GeoHLODRange & 0xFFFF;
			uint32 GeoHLODRangeEnd = (GeoHLODRange & 0xFFFF0000) >> 16;

			uint32 RayHLODRange = Ray.MappingMesh ? Ray.MappingMesh->GetHLODRange() : 0;
			uint32 RayHLODRangeStart = RayHLODRange & 0xFFFF;
			uint32 RayHLODRangeEnd = (RayHLODRange & 0xFFFF0000) >> 16;

			// Different rules apply if we're dealing with the same cluster.
			if (GeoHLODTreeIndex != RayHLODTreeIndex)
			{
				// Allow LOD0 nodes to interact with other meshes, else reject.
				// Note: If the Start == End, it's an HLOD leaf which means LOD0 (inverted tree)
				if (GeoHLODRangeStart != GeoHLODRangeEnd)
				{
					return true;
				}
			}
			else
			{
				// Allow HLOD nodes to self-shadow (identical ranges), else reject where range intersects.
				if (GeoHLODRange != RayHLODRange && 
					GeoHLODRangeStart <= RayHLODRangeEnd && RayHLODRangeStart <= GeoHLODRangeEnd)
				{
					return true;
				}
			}
		}

		return false;
	}

	EMBREE_INLINE bool HitRejectedByLODIndexTest() const
	{
		uint32 GeoMeshLODIndex = Geo.Mesh->GetLODIndices() & 0xFFFF;

		// Only shadows from appropriate mesh LODs.
		if (Ray.MappingMesh)
		{
			// If it is not from the same mesh, then only LOD 0 can cast shadow.
			if (Ray.MappingMesh->MeshIndex != Geo.Mesh->MeshIndex)
			{
				return GeoMeshLODIndex != 0;
			}
			else
			{
				// If it is from the same mesh, then only same LOD can cast shadow.
				return (Ray.MappingMesh->GetLODIndices() & 0xFFFF) != GeoMeshLODIndex;
			}
		}

		// If the ray didn't originate from a mesh, only intersect against LOD0
		return GeoMeshLODIndex != 0;
	}

	EMBREE_INLINE bool HitRejectedBySelfShadowTest() const
	{
		// No self shadows, or only self shadow
		if ((Geo.Mesh == Ray.ShadowMesh && ((Geo.Mesh->LightingFlags & GI_INSTANCE_SELFSHADOWDISABLE) || (Ray.TraceFlags & LIGHTRAY_SELFSHADOWDISABLE)))
		|| (Ray.bDirectShadowingRay && Desc.IndirectlyShadowedOnly)
		|| (Geo.Mesh != Ray.ShadowMesh && (Geo.Mesh->LightingFlags & GI_INSTANCE_SELFSHADOWONLY)))
		{
			return true;
		}
		return false;
	}

	EMBREE_INLINE bool HitRejectedByAlphaTestTest()
	{
		UpdateCoordinates();

		if (Desc.Masked || (Ray.bDirectShadowingRay && Desc.CastShadowAsMasked))
		{
 			return !Geo.Mesh->EvaluateMaskedCollision(TextureCoordinates, Desc.ElementIndex);
		}
		return false;
	}
};

void FEmbreeFilterProcessor::UpdateCoordinates()
{
	if (bCoordsDirty)
	{
		Geo.Mesh->GetTriangleIndices(Ray.primID, Index0, Index1, Index2);

		const FVector2D& UV1 = Geo.UVs[Index0];
		const FVector2D& UV2 = Geo.UVs[Index1];
		const FVector2D& UV3 = Geo.UVs[Index2];
		TextureCoordinates = UV1 * s + UV2 * Ray.u + UV3 * Ray.v;

		bCoordsDirty = false;
	}
}

// This is called when everything succeeds and the ray is the final collision.
void FEmbreeFilterProcessor::UpdateRay()
{
	// ElementIndex
	Ray.ElementIndex = Desc.ElementIndex;

	if (Ray.bFindClosestIntersection)
	{
		//@todo - only lookup and interpolate UV's if needed
		UpdateCoordinates();

		// TextureCoordinates
		Ray.TextureCoordinates = TextureCoordinates;

		// LightmapCoordinates
		const FVector2D& LightmapUV1 = Geo.LightmapUVs[Index0];
		const FVector2D& LightmapUV2 = Geo.LightmapUVs[Index1];
		const FVector2D& LightmapUV3 = Geo.LightmapUVs[Index2];
		Ray.LightmapCoordinates = LightmapUV1 * s + LightmapUV2 * Ray.u + LightmapUV3 * Ray.v;
	}
	else if (bCoordsDirty)
	{
		Geo.Mesh->GetTriangleIndices(Ray.primID, Index0, Index1, Index2);
	}

	// RelativeVertexIndex
	Ray.RelativeVertexIndex = (Ray.u > s) ? (Ray.v > Ray.u ? Index2 : Index1) : (Ray.v > s ? Index2 : Index0);


	// Transmission : updated outside of this scope.
}

void EmbreeFilterFunc(void* UserPtr, RTCRay& InRay)
{
	FEmbreeFilterProcessor Proc((FEmbreeRay&)InRay, *(FEmbreeGeometry*)UserPtr);
	checkSlow(Proc.Geo.GeomID == Proc.Ray.geomID);

	if (!Proc.Desc.CastShadow)
	{
		Proc.Invalidate();
		return;
	}

	// appLineCheckTriangleSOA
	if (Proc.HitRejectedByStaticAndOpaqueOnlyTest() || 
		Proc.HitRejectedByBackFaceCullingTest() ||
		Proc.HitRejectedByLODIndexTest() ||
		Proc.HitRejectedByHLODTest())
	{
		Proc.Invalidate();
		return;
	}

	// No collision with translucent primitives.
	if (Proc.Desc.Translucent && !(Proc.Ray.bDirectShadowingRay && Proc.Desc.CastShadowAsMasked))
	{
		if (Proc.Ray.bCalculateTransmission)
		{
			Proc.UpdateCoordinates();

			// Accumulate the total transmission along the ray
			// The result is order independent so the intersections don't have to be strictly front to back
			Proc.Ray.TransmissionAcc.Push(Proc.Geo.Mesh->EvaluateTransmission(Proc.TextureCoordinates, Proc.Desc.ElementIndex), Proc.Ray.tfar);

		}
		Proc.Invalidate();
		return;
	}

	// No self shadows, or only self shadow
	if (Proc.HitRejectedBySelfShadowTest())
	{
		Proc.Invalidate();
		return;
	}

	if (Proc.Ray.bFindClosestIntersection)
	{
		if (Proc.HitRejectedByAlphaTestTest())
		{
			Proc.Invalidate();
			return;
		}
	}

	// Ray Properties need to be updated only once everything has been validated.
	// Otherwise, after a valid collision, a failed collision could be tested which must not change any property.
	Proc.UpdateRay();
}

FEmbreeGeometry::FEmbreeGeometry(
	RTCDevice EmbreeDevice, 
	RTCScene EmbreeScene, 
	const FBoxSphereBounds& ImportanceBounds,
	const FStaticLightingMesh* InMesh,
	const FStaticLightingMapping* InMapping
	) :
	Mesh(InMesh),
	Mapping(InMapping),
	SurfaceArea(0),
	SurfaceAreaWithinImportanceVolume(0),
	bHasShadowCastingPrimitives(false)
{
	const FStaticLightingTextureMapping* TextureMapping = Mapping ? Mapping->GetTextureMapping() : NULL;

	GeomID = rtcNewTriangleMesh(EmbreeScene, RTC_GEOMETRY_STATIC, Mesh->NumTriangles, Mesh->NumVertices);
	TriangleDescs.AddZeroed(Mesh->NumTriangles);
	UVs.AddZeroed(Mesh->NumVertices);
	LightmapUVs.AddZeroed(Mesh->NumVertices);

	FVector4* Vertices = (FVector4*) rtcMapBuffer(EmbreeScene, GeomID, RTC_VERTEX_BUFFER);
	int32* Indices = (int32*) rtcMapBuffer(EmbreeScene, GeomID, RTC_INDEX_BUFFER);

	for (int32 TriangleIndex = 0;TriangleIndex < Mesh->NumTriangles;TriangleIndex++)
	{
		int32 I0 = 0, I1 = 0, I2 = 0;
		FStaticLightingVertex V0, V1, V2;
		int32 ElementIndex;

		Mesh->GetTriangleIndices(TriangleIndex,I0,I1,I2);
		Mesh->GetTriangle(TriangleIndex,V0,V1,V2,ElementIndex);

		// Compute the triangle's normal.
		const FVector4 TriangleNormal = (V2.WorldPosition - V0.WorldPosition) ^ (V1.WorldPosition - V0.WorldPosition);
		// Compute the triangle area.
		const float TriangleArea = TriangleNormal.Size3() * 0.5f;

		FEmbreeTriangleDesc& Desc = TriangleDescs[TriangleIndex];
		Desc.ElementIndex = ElementIndex;
		Desc.CastShadow = TriangleArea > TRIANGLE_AREA_THRESHOLD && Mesh->IsElementCastingShadow(ElementIndex);
		Desc.StaticAndOpaqueMask = !Mesh->IsMasked(ElementIndex) && !Mesh->IsTranslucent(ElementIndex) && !Mesh->bMovable;;
		Desc.TwoSidedMask = Mesh->IsTwoSided(ElementIndex) || Mesh->IsCastingShadowAsTwoSided();
		Desc.Translucent = Mesh->IsTranslucent(ElementIndex);
		Desc.IndirectlyShadowedOnly = Mesh->IsIndirectlyShadowedOnly(ElementIndex);
		Desc.Masked = Mesh->IsMasked(ElementIndex);
		Desc.CastShadowAsMasked = Mesh->IsCastingShadowsAsMasked(ElementIndex);

		if (Desc.CastShadow)
		{
			Indices[TriangleIndex * 3 + 0] = I0;
			Indices[TriangleIndex * 3 + 1] = I1;
			Indices[TriangleIndex * 3 + 2] = I2;
			bHasShadowCastingPrimitives = true;
		}
		else // Otherwise map an degenerated triangle to reduce intersections
		{
			Indices[TriangleIndex * 3 + 0] = I0;
			Indices[TriangleIndex * 3 + 1] = I0;
			Indices[TriangleIndex * 3 + 2] = I0;
		}

		Vertices[I0] = V0.WorldPosition;
		Vertices[I1] = V1.WorldPosition;
		Vertices[I2] = V2.WorldPosition;

		UVs[I0] = V0.TextureCoordinates[Mesh->TextureCoordinateIndex];
		UVs[I1] = V1.TextureCoordinates[Mesh->TextureCoordinateIndex];
		UVs[I2] = V2.TextureCoordinates[Mesh->TextureCoordinateIndex];
		if (TextureMapping)
		{
			LightmapUVs[I0] = V0.TextureCoordinates[TextureMapping->LightmapTextureCoordinateIndex];
			LightmapUVs[I1] = V1.TextureCoordinates[TextureMapping->LightmapTextureCoordinateIndex];
			LightmapUVs[I2] = V2.TextureCoordinates[TextureMapping->LightmapTextureCoordinateIndex];
		}

		SurfaceArea += TriangleArea;

		// Sum the total triangle area of everything in the aggregate mesh within the importance volume,
		// if any vertex is contained or if there is no importance volume at all
		if( ImportanceBounds.SphereRadius < DELTA ||
			ImportanceBounds.GetBox().IsInside( V0.WorldPosition ) ||
			ImportanceBounds.GetBox().IsInside( V1.WorldPosition ) ||
			ImportanceBounds.GetBox().IsInside( V2.WorldPosition ) )
		{
			SurfaceAreaWithinImportanceVolume += TriangleArea;
		}

	}
	rtcUnmapBuffer(EmbreeScene, GeomID, RTC_VERTEX_BUFFER);
	rtcUnmapBuffer(EmbreeScene, GeomID, RTC_INDEX_BUFFER);

	check(rtcDeviceGetError(EmbreeDevice) == RTC_NO_ERROR);
}

bool EmbreeMemoryMonitor(const ssize_t bytes, const bool post)
{
	FPlatformAtomics::InterlockedAdd(&GEmbreeAllocatedSpace, bytes);
	return true;
}

FEmbreeAggregateMesh::FEmbreeAggregateMesh(const FScene& InScene):
	FStaticLightingAggregateMesh(InScene),
	EmbreeDevice(NULL),
	EmbreeScene(NULL),
	TotalNumTriangles(0)
{
	rtcDeviceSetMemoryMonitorFunction(InScene.EmbreeDevice, EmbreeMemoryMonitor);

	EmbreeDevice = InScene.EmbreeDevice;
	EmbreeScene = rtcDeviceNewScene(InScene.EmbreeDevice, RTC_SCENE_STATIC, RTC_INTERSECT1);
	check(rtcDeviceGetError(EmbreeDevice) == RTC_NO_ERROR);
}

FEmbreeAggregateMesh::~FEmbreeAggregateMesh()
{
	for (int32 i = 0; i < MeshInfos.Num(); i++)
	{
		delete MeshInfos[i];
	}
	rtcDeleteScene(EmbreeScene);
}

/**
* Merges a mesh into the shadow mesh.
* @param Mesh - The mesh the triangle comes from.
*/
void FEmbreeAggregateMesh::AddMesh(const FStaticLightingMesh* Mesh, const FStaticLightingMapping* Mapping)
{
	// Only use shadow casting meshes.
	if( Mesh->LightingFlags&GI_INSTANCE_CASTSHADOW )
	{
		SceneBounds = SceneBounds + Mesh->BoundingBox;

		FEmbreeGeometry* Geo = new FEmbreeGeometry(EmbreeDevice, EmbreeScene, Scene.GetImportanceBounds(), Mesh, Mapping);
		MeshInfos.Add(Geo);
		
		rtcSetUserData(EmbreeScene, Geo->GeomID, Geo);
		rtcSetIntersectionFilterFunction(EmbreeScene, Geo->GeomID, EmbreeFilterFunc);
		rtcSetOcclusionFilterFunction(EmbreeScene, Geo->GeomID, EmbreeFilterFunc);

		bHasShadowCastingPrimitives |= Geo->bHasShadowCastingPrimitives;

		// Sum the total triangle area of everything in the aggregate mesh
		SceneSurfaceArea += Geo->SurfaceArea;
		SceneSurfaceAreaWithinImportanceVolume += Geo->SurfaceAreaWithinImportanceVolume;

		TotalNumTriangles += Mesh->NumTriangles;
	}
}

void FEmbreeAggregateMesh::PrepareForRaytracing()
{
	rtcCommit(EmbreeScene);
	check(rtcDeviceGetError(EmbreeDevice) == RTC_NO_ERROR);
}

void FEmbreeAggregateMesh::DumpStats() const 
{
	int64 MeshInfoSize = sizeof(FEmbreeGeometry) * MeshInfos.Num();
	int64 UVSize = 0;
	int64 LightmapUV = 0;

	for (int32 i = 0; i < MeshInfos.Num(); i++)
	{
		const FEmbreeGeometry* Geo = MeshInfos[i];
		UVSize += Geo->UVs.GetAllocatedSize();
		LightmapUV += Geo->LightmapUVs.GetAllocatedSize();
	}

	UE_LOG(LogLightmass, Log, TEXT("\n"));
	UE_LOG(LogLightmass, Log, TEXT("Collision Mesh Overview:"));
	UE_LOG(LogLightmass, Log, TEXT("Num Triangles         : %d"), TotalNumTriangles);
	UE_LOG(LogLightmass, Log, TEXT("MeshInfos             : %7.1fMb"), MeshInfoSize / 1048576.0f);
	UE_LOG(LogLightmass, Log, TEXT("UVs                   : %7.1fMb"), UVSize / 1048576.0f);
	UE_LOG(LogLightmass, Log, TEXT("LightmapUVs           : %7.1fMb"), LightmapUV / 1048576.0f);
	UE_LOG(LogLightmass, Log, TEXT("Embree Used Memory    : %7.1fMb"), GEmbreeAllocatedSpace / 1048576.0f);
	UE_LOG(LogLightmass, Log, TEXT("\n"));
}

bool FEmbreeAggregateMesh::IntersectLightRay(
	const FLightRay& LightRay,
	bool bFindClosestIntersection,
	bool bCalculateTransmission,
	bool bDirectShadowingRay,
	FCoherentRayCache& CoherentRayCache,
	FLightRayIntersection& ClosestIntersection) const
{
	LIGHTINGSTAT(FScopedRDTSCTimer RayTraceTimer(bFindClosestIntersection ? CoherentRayCache.FirstHitRayTraceTime : CoherentRayCache.BooleanRayTraceTime);)
	bFindClosestIntersection ? CoherentRayCache.NumFirstHitRaysTraced++ : CoherentRayCache.NumBooleanRaysTraced++;
	// Calculating transmission requires finding the closest intersection for now
	//@todo - allow boolean visibility tests while calculating transmission
	checkSlow(!bCalculateTransmission || bFindClosestIntersection);

	ClosestIntersection.bIntersects = false;

	FEmbreeRay EmbreeRay(
		LightRay.Mesh, 
		LightRay.Mapping ? LightRay.Mapping->Mesh : NULL,
		LightRay.TraceFlags, 
		bFindClosestIntersection, 
		bCalculateTransmission, 
		bDirectShadowingRay);

	EmbreeRay.org[0] = LightRay.Start.X;
	EmbreeRay.org[1] = LightRay.Start.Y;
	EmbreeRay.org[2] = LightRay.Start.Z;
	EmbreeRay.dir[0] = LightRay.Direction.X;
	EmbreeRay.dir[1] = LightRay.Direction.Y;
	EmbreeRay.dir[2] = LightRay.Direction.Z;
	EmbreeRay.tnear = 0;
	EmbreeRay.tfar = LightRay.Length;

	if (bFindClosestIntersection)
	{
		rtcIntersect(EmbreeScene, EmbreeRay);
	}
	else
	{
		rtcOccluded(EmbreeScene, EmbreeRay);
	}

	if (EmbreeRay.geomID != -1 && EmbreeRay.primID != -1)
	{
		const FEmbreeGeometry& Geo = *(FEmbreeGeometry*)rtcGetUserData(EmbreeScene, EmbreeRay.geomID);

		FStaticLightingVertex EmbreeVertex;
		EmbreeVertex.WorldPosition = LightRay.Start + LightRay.Direction * EmbreeRay.tfar;
		EmbreeVertex.WorldTangentZ = FVector(EmbreeRay.Ng[0], EmbreeRay.Ng[1], EmbreeRay.Ng[2]).GetSafeNormal();

		EmbreeVertex.TextureCoordinates[0] = EmbreeRay.TextureCoordinates;
		EmbreeVertex.TextureCoordinates[1] = EmbreeRay.LightmapCoordinates;

		ClosestIntersection = FLightRayIntersection(true, EmbreeVertex, Geo.Mesh, Geo.Mapping, EmbreeRay.RelativeVertexIndex, EmbreeRay.ElementIndex);

		EmbreeRay.TransmissionAcc.Resolve(ClosestIntersection.Transmission, EmbreeRay.tfar);
	}
	else
	{
		EmbreeRay.TransmissionAcc.Resolve(ClosestIntersection.Transmission);
	}

	return ClosestIntersection.bIntersects;
}

FEmbreeVerifyAggregateMesh::FEmbreeVerifyAggregateMesh(const FScene& InScene) :
	Super(InScene),
	DefaultAggregate(InScene),
	EmbreeAggregate(InScene),
	TransmissionMismatchCount(0),
	TransmissionEqualCount(0),
	CheckEqualCount(0),
	CheckMismatchCount(0)
{
}

void FEmbreeVerifyAggregateMesh::AddMesh(const FStaticLightingMesh* Mesh, const FStaticLightingMapping* Mapping)
{
	DefaultAggregate.AddMesh(Mesh, Mapping);
	EmbreeAggregate.AddMesh(Mesh, Mapping);

	// Update properties affected by AddMesh
	bHasShadowCastingPrimitives = DefaultAggregate.bHasShadowCastingPrimitives;
	SceneBounds = DefaultAggregate.SceneBounds;
	SceneSurfaceArea = DefaultAggregate.SceneSurfaceArea;
	SceneSurfaceAreaWithinImportanceVolume = DefaultAggregate.SceneSurfaceAreaWithinImportanceVolume;
}

void FEmbreeVerifyAggregateMesh::ReserveMemory(int32 NumMeshes, int32 NumVertices, int32 NumTriangles)
{
	DefaultAggregate.ReserveMemory(NumMeshes, NumVertices, NumTriangles);
	EmbreeAggregate.ReserveMemory(NumMeshes, NumVertices, NumTriangles);
}

void FEmbreeVerifyAggregateMesh::PrepareForRaytracing()
{
	DefaultAggregate.PrepareForRaytracing();
	EmbreeAggregate.PrepareForRaytracing();
}

void FEmbreeVerifyAggregateMesh::DumpStats() const 
{
	DefaultAggregate.DumpStats();
	EmbreeAggregate.DumpStats();
}

void FEmbreeVerifyAggregateMesh::DumpCheckStats() const
{
	DefaultAggregate.DumpCheckStats();
	EmbreeAggregate.DumpCheckStats();

	UE_LOG(LogLightmass, Display, TEXT("\n\n"));
	UE_LOG(LogLightmass, Display, TEXT("============================================================"));

	float r = TransmissionMismatchCount > 0 ? (TransmissionMismatchCount / (float)(TransmissionEqualCount + TransmissionMismatchCount)) : 0;
	UE_LOG(LogLightmass, Log, TEXT("Embree transmission divergence : %d / %d [%.7f]"), TransmissionMismatchCount, TransmissionEqualCount + TransmissionMismatchCount, r);

	r = CheckMismatchCount > 0 ? (CheckMismatchCount / (float)(CheckEqualCount + CheckMismatchCount)) : 0;
	UE_LOG(LogLightmass, Log, TEXT("Embree check divergence : %d / %d [%.7f]"), CheckMismatchCount, CheckEqualCount + CheckMismatchCount, r);

	UE_LOG(LogLightmass, Display, TEXT("============================================================"));
	UE_LOG(LogLightmass, Display, TEXT("\n\n"));
}

bool FEmbreeVerifyAggregateMesh::VerifyTransmissions(const FLightRayIntersection& EmbreeIntersection, const FLightRayIntersection& ClosestIntersection)
{
	const_cast<float&>(ClosestIntersection.Transmission.A) = 1.f;
	return EmbreeIntersection.Transmission.Equals(ClosestIntersection.Transmission, 0.01f);
}

bool FEmbreeVerifyAggregateMesh::VerifyChecks(const FLightRayIntersection& EmbreeIntersection, const FLightRayIntersection& ClosestIntersection, bool bFindClosestIntersection)
{
	if (EmbreeIntersection.bIntersects != ClosestIntersection.bIntersects)
	{
		return false;
	}

	if (bFindClosestIntersection && EmbreeIntersection.bIntersects)
	{
		if (EmbreeIntersection.ElementIndex != ClosestIntersection.ElementIndex)
		{
			return false;
		}

		if (EmbreeIntersection.VertexIndex != ClosestIntersection.VertexIndex)
		{
			return false;
		}

		const_cast<float&>(EmbreeIntersection.IntersectionVertex.WorldPosition.W) = const_cast<float&>(ClosestIntersection.IntersectionVertex.WorldPosition.W) = 1;
		if (!EmbreeIntersection.IntersectionVertex.WorldPosition.Equals(ClosestIntersection.IntersectionVertex.WorldPosition, .1f))
		{
			return false;
		}

		const_cast<float&>(EmbreeIntersection.IntersectionVertex.WorldTangentZ.W) = const_cast<float&>(ClosestIntersection.IntersectionVertex.WorldTangentZ.W) = 0;
		if (!EmbreeIntersection.IntersectionVertex.WorldTangentZ.Equals(ClosestIntersection.IntersectionVertex.WorldTangentZ, .01f))
		{
			return false;
		}

		FVector4 EmbreeCoord = FVector4(EmbreeIntersection.IntersectionVertex.TextureCoordinates[0].X, EmbreeIntersection.IntersectionVertex.TextureCoordinates[0].Y,
								EmbreeIntersection.IntersectionVertex.TextureCoordinates[1].X, EmbreeIntersection.IntersectionVertex.TextureCoordinates[1].Y);
		FVector4 ClosestCoord = FVector4(ClosestIntersection.IntersectionVertex.TextureCoordinates[0].X, ClosestIntersection.IntersectionVertex.TextureCoordinates[0].Y,
								ClosestIntersection.IntersectionVertex.TextureCoordinates[1].X, ClosestIntersection.IntersectionVertex.TextureCoordinates[1].Y);

		if (!EmbreeCoord.Equals(ClosestCoord, .01f))
		{
			return false;
		}
	}

	return true;
}


bool FEmbreeVerifyAggregateMesh::IntersectLightRay(
	const FLightRay& LightRay,
	bool bFindClosestIntersection,
	bool bCalculateTransmission,
	bool bDirectShadowingRay,
	FCoherentRayCache& CoherentRayCache,
	FLightRayIntersection& ClosestIntersection) const
{
	DefaultAggregate.IntersectLightRay(LightRay, bFindClosestIntersection, bCalculateTransmission, bDirectShadowingRay, CoherentRayCache, ClosestIntersection);

	FLightRayIntersection EmbreeIntersection;
	EmbreeAggregate.IntersectLightRay(LightRay, bFindClosestIntersection, bCalculateTransmission, bDirectShadowingRay, CoherentRayCache, EmbreeIntersection);

	if (bCalculateTransmission)
	{
		if (VerifyTransmissions(EmbreeIntersection, ClosestIntersection))
		{
			FPlatformAtomics::InterlockedIncrement(&TransmissionEqualCount);
		}
		else
		{
			FPlatformAtomics::InterlockedIncrement(&TransmissionMismatchCount);
		}
	}

	if (VerifyChecks(EmbreeIntersection, ClosestIntersection, bFindClosestIntersection))
	{
		FPlatformAtomics::InterlockedIncrement(&CheckEqualCount);
	}
	else
	{
		FPlatformAtomics::InterlockedIncrement(&CheckMismatchCount);
	}

	return ClosestIntersection.bIntersects;
}

} // namespace
#endif // USE_EMBREE