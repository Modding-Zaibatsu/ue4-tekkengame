// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "EnginePrivate.h"
#include "AnimCurveTypes.h"
#include "Animation/AnimInstance.h"
#include "AnimationRuntime.h"
#include "FrameworkObjectVersion.h"

DECLARE_CYCLE_STAT(TEXT("AnimSeq EvalCurveData"), STAT_AnimSeq_EvalCurveData, STATGROUP_Anim);

/////////////////////////////////////////////////////
// FFloatCurve

void FAnimCurveBase::PostSerialize(FArchive& Ar)
{
	FSmartNameMapping::UID CurveUid = FSmartNameMapping::MaxUID;
	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	if ((Ar.IsLoading()))
	{
		if (Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::SmartNameRefactor)
		{
			if (Ar.UE4Ver() >= VER_UE4_SKELETON_ADD_SMARTNAMES)
			{
				Ar << CurveUid;

				Name.UID = CurveUid;
				Name.DisplayName = LastObservedName_DEPRECATED;
			}
			else
			{
				Name.DisplayName = LastObservedName_DEPRECATED;
			}
		}
	}
}

void FAnimCurveBase::SetCurveTypeFlag(EAnimCurveFlags InFlag, bool bValue)
{
	if (bValue)
	{
		CurveTypeFlags |= InFlag;
	}
	else
	{
		CurveTypeFlags &= ~InFlag;
	}
}

void FAnimCurveBase::ToggleCurveTypeFlag(EAnimCurveFlags InFlag)
{
	bool Current = GetCurveTypeFlag(InFlag);
	SetCurveTypeFlag(InFlag, !Current);
}

bool FAnimCurveBase::GetCurveTypeFlag(EAnimCurveFlags InFlag) const
{
	return (CurveTypeFlags & InFlag) != 0;
}


void FAnimCurveBase::SetCurveTypeFlags(int32 NewCurveTypeFlags)
{
	CurveTypeFlags = NewCurveTypeFlags;
}

int32 FAnimCurveBase::GetCurveTypeFlags() const
{
	return CurveTypeFlags;
}

////////////////////////////////////////////////////
//  FFloatCurve

// we don't want to have = operator. This only copies curves, but leaving naming and everything else intact. 
void FFloatCurve::CopyCurve(FFloatCurve& SourceCurve)
{
	FloatCurve = SourceCurve.FloatCurve;
}

float FFloatCurve::Evaluate(float CurrentTime) const
{
	return FloatCurve.Eval(CurrentTime);
}

void FFloatCurve::UpdateOrAddKey(float NewKey, float CurrentTime)
{
	FloatCurve.UpdateOrAddKey(CurrentTime, NewKey);
}

void FFloatCurve::Resize(float NewLength, bool bInsert/* whether insert or remove*/, float OldStartTime, float OldEndTime)
{
	FloatCurve.ReadjustTimeRange(0, NewLength, bInsert, OldStartTime, OldEndTime);
}
////////////////////////////////////////////////////
//  FVectorCurve

// we don't want to have = operator. This only copies curves, but leaving naming and everything else intact. 
void FVectorCurve::CopyCurve(FVectorCurve& SourceCurve)
{
	FMemory::Memcpy(FloatCurves, SourceCurve.FloatCurves);
}

FVector FVectorCurve::Evaluate(float CurrentTime, float BlendWeight) const
{
	FVector Value;

	Value.X = FloatCurves[X].Eval(CurrentTime)*BlendWeight;
	Value.Y = FloatCurves[Y].Eval(CurrentTime)*BlendWeight;
	Value.Z = FloatCurves[Z].Eval(CurrentTime)*BlendWeight;

	return Value;
}

void FVectorCurve::UpdateOrAddKey(const FVector& NewKey, float CurrentTime)
{
	FloatCurves[X].UpdateOrAddKey(CurrentTime, NewKey.X);
	FloatCurves[Y].UpdateOrAddKey(CurrentTime, NewKey.Y);
	FloatCurves[Z].UpdateOrAddKey(CurrentTime, NewKey.Z);
}

void FVectorCurve::Resize(float NewLength, bool bInsert/* whether insert or remove*/, float OldStartTime, float OldEndTime)
{
	FloatCurves[X].ReadjustTimeRange(0, NewLength, bInsert, OldStartTime, OldEndTime);
	FloatCurves[Y].ReadjustTimeRange(0, NewLength, bInsert, OldStartTime, OldEndTime);
	FloatCurves[Z].ReadjustTimeRange(0, NewLength, bInsert, OldStartTime, OldEndTime);
}
////////////////////////////////////////////////////
//  FTransformCurve

// we don't want to have = operator. This only copies curves, but leaving naming and everything else intact. 
void FTransformCurve::CopyCurve(FTransformCurve& SourceCurve)
{
	TranslationCurve.CopyCurve(SourceCurve.TranslationCurve);
	RotationCurve.CopyCurve(SourceCurve.RotationCurve);
	ScaleCurve.CopyCurve(SourceCurve.ScaleCurve);
}

FTransform FTransformCurve::Evaluate(float CurrentTime, float BlendWeight) const
{
	FTransform Value;
	Value.SetTranslation(TranslationCurve.Evaluate(CurrentTime, BlendWeight));
	if (ScaleCurve.DoesContainKey())
	{
		Value.SetScale3D(ScaleCurve.Evaluate(CurrentTime, BlendWeight));
	}
	else
	{
		Value.SetScale3D(FVector(1.f));
	}

	// blend rotation float curve
	FVector RotationAsVector = RotationCurve.Evaluate(CurrentTime, BlendWeight);
	// pitch, yaw, roll order - please check AddKey function
	FRotator Rotator(RotationAsVector.Y, RotationAsVector.Z, RotationAsVector.X);
	Value.SetRotation(FQuat(Rotator));

	return Value;
}

void FTransformCurve::UpdateOrAddKey(const FTransform& NewKey, float CurrentTime)
{
	TranslationCurve.UpdateOrAddKey(NewKey.GetTranslation(), CurrentTime);
	// pitch, yaw, roll order - please check Evaluate function
	FVector RotationAsVector;
	FRotator Rotator = NewKey.GetRotation().Rotator();
	RotationAsVector.X = Rotator.Roll;
	RotationAsVector.Y = Rotator.Pitch;
	RotationAsVector.Z = Rotator.Yaw;

	RotationCurve.UpdateOrAddKey(RotationAsVector, CurrentTime);
	ScaleCurve.UpdateOrAddKey(NewKey.GetScale3D(), CurrentTime);
}

void FTransformCurve::Resize(float NewLength, bool bInsert/* whether insert or remove*/, float OldStartTime, float OldEndTime)
{
	TranslationCurve.Resize(NewLength, bInsert, OldStartTime, OldEndTime);
	RotationCurve.Resize(NewLength, bInsert, OldStartTime, OldEndTime);
	ScaleCurve.Resize(NewLength, bInsert, OldStartTime, OldEndTime);
}

/////////////////////////////////////////////////////
// FRawCurveTracks

void FRawCurveTracks::EvaluateCurveData( FBlendedCurve& Curves, float CurrentTime ) const
{
	SCOPE_CYCLE_COUNTER(STAT_AnimSeq_EvalCurveData);
	// evaluate the curve data at the CurrentTime and add to Instance
	for(auto CurveIter = FloatCurves.CreateConstIterator(); CurveIter; ++CurveIter)
	{
		const FFloatCurve& Curve = *CurveIter;
		Curves.Set(Curve.Name.UID, Curve.Evaluate(CurrentTime), Curve.GetCurveTypeFlags());
	}
}

#if WITH_EDITOR
/**
 * Since we don't care about blending, we just change this decoration to OutCurves
 * @TODO : Fix this if we're saving vectorcurves and blending
 */
void FRawCurveTracks::EvaluateTransformCurveData(USkeleton * Skeleton, TMap<FName, FTransform>&OutCurves, float CurrentTime, float BlendWeight) const
{
	check (Skeleton);
	// evaluate the curve data at the CurrentTime and add to Instance
	for(auto CurveIter = TransformCurves.CreateConstIterator(); CurveIter; ++CurveIter)
	{
		const FTransformCurve& Curve = *CurveIter;

		// if disabled, do not handle
		if (Curve.GetCurveTypeFlag(ACF_Disabled))
		{
			continue;
		}

		// Add or retrieve curve
		FName CurveName = Curve.Name.DisplayName;
		
		// note we're not checking Curve.GetCurveTypeFlags() yet
		FTransform & Value = OutCurves.FindOrAdd(CurveName);
		Value = Curve.Evaluate(CurrentTime, BlendWeight);
	}
}
#endif
FAnimCurveBase * FRawCurveTracks::GetCurveData(USkeleton::AnimCurveUID Uid, ESupportedCurveType SupportedCurveType /*= FloatType*/)
{
	switch (SupportedCurveType)
	{
#if WITH_EDITOR
	case VectorType:
		return GetCurveDataImpl<FVectorCurve>(VectorCurves, Uid);
	case TransformType:
		return GetCurveDataImpl<FTransformCurve>(TransformCurves, Uid);
#endif // WITH_EDITOR
	case FloatType:
	default:
		return GetCurveDataImpl<FFloatCurve>(FloatCurves, Uid);
	}
}

bool FRawCurveTracks::DeleteCurveData(const FSmartName& CurveToDelete, ESupportedCurveType SupportedCurveType /*= FloatType*/)
{
	switch(SupportedCurveType)
	{
#if WITH_EDITOR
	case VectorType:
		return DeleteCurveDataImpl<FVectorCurve>(VectorCurves, CurveToDelete);
	case TransformType:
		return DeleteCurveDataImpl<FTransformCurve>(TransformCurves, CurveToDelete);
#endif // WITH_EDITOR
	case FloatType:
	default:
		return DeleteCurveDataImpl<FFloatCurve>(FloatCurves, CurveToDelete);
	}
}

void FRawCurveTracks::DeleteAllCurveData(ESupportedCurveType SupportedCurveType /*= FloatType*/)
{
	switch(SupportedCurveType)
	{
#if WITH_EDITOR
	case VectorType:
		VectorCurves.Empty();
		break;
	case TransformType:
		TransformCurves.Empty();
		break;
#endif // WITH_EDITOR
	case FloatType:
	default:
		FloatCurves.Empty();
		break;
	}
}

#if WITH_EDITOR
void FRawCurveTracks::AddFloatCurveKey(const FSmartName& NewCurve, int32 CurveFlags, float Time, float Value)
{
	FFloatCurve* FloatCurve = GetCurveDataImpl<FFloatCurve>(FloatCurves, NewCurve.UID);
	if (FloatCurve == nullptr)
	{
		AddCurveData(NewCurve, CurveFlags, FloatType);
		FloatCurve = GetCurveDataImpl<FFloatCurve>(FloatCurves, NewCurve.UID);
	}

	if (FloatCurve->GetCurveTypeFlags() != CurveFlags)
	{
		FloatCurve->SetCurveTypeFlags(FloatCurve->GetCurveTypeFlags() | CurveFlags);
	}

	FloatCurve->UpdateOrAddKey(Value, Time);
}

void FRawCurveTracks::RemoveRedundantKeys()
{
	for (auto CurveIter = FloatCurves.CreateIterator(); CurveIter; ++CurveIter)
	{
		FFloatCurve& Curve = *CurveIter;
		Curve.FloatCurve.RemoveRedundantKeys(SMALL_NUMBER);
	}
}
#endif

bool FRawCurveTracks::AddCurveData(const FSmartName& NewCurve, int32 CurveFlags /*= ACF_DefaultCurve*/, ESupportedCurveType SupportedCurveType /*= FloatType*/)
{
	switch(SupportedCurveType)
	{
#if WITH_EDITOR
	case VectorType:
		return AddCurveDataImpl<FVectorCurve>(VectorCurves, NewCurve, CurveFlags);
	case TransformType:
		return AddCurveDataImpl<FTransformCurve>(TransformCurves, NewCurve, CurveFlags);
#endif // WITH_EDITOR
	case FloatType:
	default:
		return AddCurveDataImpl<FFloatCurve>(FloatCurves, NewCurve, CurveFlags);
	}
}

void FRawCurveTracks::Resize(float TotalLength, bool bInsert/* whether insert or remove*/, float OldStartTime, float OldEndTime)
{
	for (auto& Curve: FloatCurves)
	{
		Curve.Resize(TotalLength, bInsert, OldStartTime, OldEndTime);
	}

#if WITH_EDITORONLY_DATA
	for(auto& Curve: VectorCurves)
	{
		Curve.Resize(TotalLength, bInsert, OldStartTime, OldEndTime);
	}

	for(auto& Curve: TransformCurves)
	{
		Curve.Resize(TotalLength, bInsert, OldStartTime, OldEndTime);
	}
#endif
}

void FRawCurveTracks::PostSerialize(FArchive& Ar)
{
	// @TODO: If we're about to serialize vector curve, add here
	for(FFloatCurve& Curve : FloatCurves)
	{
		Curve.PostSerialize(Ar);
	}
#if WITH_EDITORONLY_DATA
	if( !Ar.IsCooking() )
	{
		if( Ar.UE4Ver() >= VER_UE4_ANIMATION_ADD_TRACKCURVES )
		{
			for( FTransformCurve& Curve : TransformCurves )
			{
				Curve.PostSerialize( Ar );
			}

		}
	}
#endif // WITH_EDITORONLY_DATA
}

void FRawCurveTracks::RefreshName(const FSmartNameMapping* NameMapping, ESupportedCurveType SupportedCurveType /*= FloatType*/)
{
	switch(SupportedCurveType)
	{
#if WITH_EDITOR
	case VectorType:
		UpdateLastObservedNamesImpl<FVectorCurve>(VectorCurves, NameMapping);
		break;
	case TransformType:
		UpdateLastObservedNamesImpl<FTransformCurve>(TransformCurves, NameMapping);
		break;
#endif // WITH_EDITOR
	case FloatType:
	default:
		UpdateLastObservedNamesImpl<FFloatCurve>(FloatCurves, NameMapping);
	}
}

bool FRawCurveTracks::DuplicateCurveData(const FSmartName& CurveToCopy, const FSmartName& NewCurve, ESupportedCurveType SupportedCurveType /*= FloatType*/)
{
	switch(SupportedCurveType)
	{
#if WITH_EDITOR
	case VectorType:
		return DuplicateCurveDataImpl<FVectorCurve>(VectorCurves, CurveToCopy, NewCurve);
	case TransformType:
		return DuplicateCurveDataImpl<FTransformCurve>(TransformCurves, CurveToCopy, NewCurve);
#endif // WITH_EDITOR
	case FloatType:
	default:
		return DuplicateCurveDataImpl<FFloatCurve>(FloatCurves, CurveToCopy, NewCurve);
	}
}

///////////////////////////////////
// @TODO: REFACTOR THIS IF WE'RE SERIALIZING VECTOR CURVES
//
// implementation template functions to accomodate FloatCurve and VectorCurve
// for now vector curve isn't used in run-time, so it's useless outside of editor
// so just to reduce cost of run-time, functionality is split. 
// this split worries me a bit because if the name conflict happens this will break down w.r.t. smart naming
// currently vector curve is not saved and not evaluated, so it will be okay since the name doesn't matter much, 
// but this has to be refactored once we'd like to move onto serialize
///////////////////////////////////
template <typename DataType>
DataType * FRawCurveTracks::GetCurveDataImpl(TArray<DataType> & Curves, USkeleton::AnimCurveUID Uid)
{
	for(DataType& Curve : Curves)
	{
		if(Curve.Name.UID == Uid)
		{
			return &Curve;
		}
	}

	return NULL;
}

template <typename DataType>
bool FRawCurveTracks::DeleteCurveDataImpl(TArray<DataType> & Curves, const FSmartName& CurveToDelete)
{
	for(int32 Idx = 0; Idx < Curves.Num(); ++Idx)
	{
		if(Curves[Idx].Name.UID == CurveToDelete.UID)
		{
			Curves.RemoveAt(Idx);
			return true;
		}
	}

	return false;
}

template <typename DataType>
bool FRawCurveTracks::AddCurveDataImpl(TArray<DataType> & Curves, const FSmartName& NewCurve, int32 CurveFlags)
{
	if(GetCurveDataImpl<DataType>(Curves, NewCurve.UID) == NULL)
	{
		Curves.Add(DataType(NewCurve, CurveFlags));
		return true;
	}
	return false;
}

template <typename DataType>
void FRawCurveTracks::UpdateLastObservedNamesImpl(TArray<DataType> & Curves, const FSmartNameMapping* NameMapping)
{
	if(NameMapping)
	{
		for(DataType& Curve : Curves)
		{
			NameMapping->GetName(Curve.Name.UID, Curve.Name.DisplayName);
		}
	}
}

template <typename DataType>
bool FRawCurveTracks::DuplicateCurveDataImpl(TArray<DataType> & Curves, const FSmartName& CurveToCopy, const FSmartName& NewCurve)
{
	DataType* ExistingCurve = GetCurveDataImpl<DataType>(Curves, CurveToCopy.UID);
	if(ExistingCurve && GetCurveDataImpl<DataType>(Curves, NewCurve.UID) == NULL)
	{
		// Add the curve to the track and set its data to the existing curve
		Curves.Add(DataType(NewCurve, ExistingCurve->GetCurveTypeFlags()));
		Curves.Last().CopyCurve(*ExistingCurve);

		return true;
	}
	return false;
}

FArchive& operator<<(FArchive& Ar, FRawCurveTracks& D)
{
	UScriptStruct* StaticStruct = FRawCurveTracks::StaticStruct();
	StaticStruct->SerializeTaggedProperties(Ar, (uint8*)&D, StaticStruct, nullptr);
	// do not call custom serialize that relies on version number. The Archive version doesn't exists on this. 
	return Ar;
}

///////////////////////////////////////////////////////////////////////
// FAnimCurveParam

void FAnimCurveParam::Initialize(USkeleton* Skeleton)
{
	// Initialize for curve UID
	if (Name != NAME_None)
	{
		UID = Skeleton->GetUIDByName(USkeleton::AnimCurveMappingName, Name);
	}
	else
	{
		// invalidate current UID
		UID = FSmartNameMapping::MaxUID;
	}
}