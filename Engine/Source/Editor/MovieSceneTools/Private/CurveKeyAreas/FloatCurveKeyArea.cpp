// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "MovieSceneToolsPrivatePCH.h"
#include "FloatCurveKeyArea.h"
#include "SFloatCurveKeyEditor.h"
#include "MovieSceneClipboard.h"
#include "SequencerClipboardReconciler.h"

/* IKeyArea interface
 *****************************************************************************/

TArray<FKeyHandle> FFloatCurveKeyArea::AddKeyUnique(float Time, EMovieSceneKeyInterpolation InKeyInterpolation, float TimeToCopyFrom)
{
	TArray<FKeyHandle> AddedKeyHandles;
	FKeyHandle CurrentKeyHandle = Curve->FindKey(Time);

	if (Curve->IsKeyHandleValid(CurrentKeyHandle) == false)
	{
		if (OwningSection->GetStartTime() > Time)
		{
			OwningSection->SetStartTime(Time);
		}

		if (OwningSection->GetEndTime() < Time)
		{
			OwningSection->SetEndTime(Time);
		}

		float Value = Curve->Eval(Time);

		if (TimeToCopyFrom != FLT_MAX)
		{
			Value = Curve->Eval(TimeToCopyFrom);
		}
		else if ( IntermediateValue.IsSet() )
		{
			Value = IntermediateValue.GetValue();
		}

		Curve->AddKey(Time, Value, false, CurrentKeyHandle);
		AddedKeyHandles.Add(CurrentKeyHandle);
		
		MovieSceneHelpers::SetKeyInterpolation(*Curve, CurrentKeyHandle, InKeyInterpolation);		
		
		// Copy the properties from the key if it exists
		FKeyHandle KeyHandleToCopy = Curve->FindKey(TimeToCopyFrom);

		if (Curve->IsKeyHandleValid(KeyHandleToCopy))
		{
			FRichCurveKey& CurrentKey = Curve->GetKey(CurrentKeyHandle);
			FRichCurveKey& KeyToCopy = Curve->GetKey(KeyHandleToCopy);
			CurrentKey.InterpMode = KeyToCopy.InterpMode;
			CurrentKey.TangentMode = KeyToCopy.TangentMode;
			CurrentKey.TangentWeightMode = KeyToCopy.TangentWeightMode;
			CurrentKey.ArriveTangent = KeyToCopy.ArriveTangent;
			CurrentKey.LeaveTangent = KeyToCopy.LeaveTangent;
			CurrentKey.ArriveTangentWeight = KeyToCopy.ArriveTangentWeight;
			CurrentKey.LeaveTangentWeight = KeyToCopy.LeaveTangentWeight;
		}
	}
	else if ( IntermediateValue.IsSet() )
	{
		float Value = IntermediateValue.GetValue();
		Curve->UpdateOrAddKey(Time,Value);
	}

	return AddedKeyHandles;
}

TOptional<FKeyHandle> FFloatCurveKeyArea::DuplicateKey(FKeyHandle KeyToDuplicate)
{
	if (!Curve->IsKeyHandleValid(KeyToDuplicate))
	{
		return TOptional<FKeyHandle>();
	}

	FRichCurveKey ThisKey = Curve->GetKey(KeyToDuplicate);

	FKeyHandle KeyHandle = Curve->AddKey(GetKeyTime(KeyToDuplicate), ThisKey.Value);
	// Ensure the rest of the key properties are added
	Curve->GetKey(KeyHandle) = ThisKey;

	return KeyHandle;
}

bool FFloatCurveKeyArea::CanCreateKeyEditor()
{
	return true;
}


TSharedRef<SWidget> FFloatCurveKeyArea::CreateKeyEditor(ISequencer* Sequencer)
{
	return SNew(SFloatCurveKeyEditor)
		.Sequencer(Sequencer)
		.OwningSection(OwningSection)
		.Curve(Curve)
		.OnValueChanged(this, &FFloatCurveKeyArea::OnValueChanged)
		.IntermediateValue_Lambda([this] {
			return IntermediateValue;
		});
};


void FFloatCurveKeyArea::DeleteKey(FKeyHandle KeyHandle)
{
	if (Curve->IsKeyHandleValid(KeyHandle))
	{
		Curve->DeleteKey(KeyHandle);
	}
}


TOptional<FLinearColor> FFloatCurveKeyArea::GetColor()
{
	return Color;
}


ERichCurveExtrapolation FFloatCurveKeyArea::GetExtrapolationMode(bool bPreInfinity) const
{
	if (bPreInfinity)
	{
		return Curve->PreInfinityExtrap;
	}

	return Curve->PostInfinityExtrap;
}


ERichCurveInterpMode FFloatCurveKeyArea::GetKeyInterpMode(FKeyHandle KeyHandle) const
{
	if (Curve->IsKeyHandleValid(KeyHandle))
	{
		return Curve->GetKeyInterpMode(KeyHandle);
	}

	return RCIM_None;
}


TSharedPtr<FStructOnScope> FFloatCurveKeyArea::GetKeyStruct(FKeyHandle KeyHandle)
{
	return MakeShareable(new FStructOnScope(FRichCurveKey::StaticStruct(), (uint8*)&Curve->GetKey(KeyHandle)));
}


ERichCurveTangentMode FFloatCurveKeyArea::GetKeyTangentMode(FKeyHandle KeyHandle) const
{
	if (Curve->IsKeyHandleValid(KeyHandle))
	{
		return Curve->GetKeyTangentMode(KeyHandle);
	}

	return RCTM_None;
}


float FFloatCurveKeyArea::GetKeyTime(FKeyHandle KeyHandle) const
{
	return Curve->GetKeyTime(KeyHandle);
}


UMovieSceneSection* FFloatCurveKeyArea::GetOwningSection()
{
	return OwningSection;
}


FRichCurve* FFloatCurveKeyArea::GetRichCurve()
{
	return Curve;
}


TArray<FKeyHandle> FFloatCurveKeyArea::GetUnsortedKeyHandles() const
{
	TArray<FKeyHandle> OutKeyHandles;

	for (auto It(Curve->GetKeyHandleIterator()); It; ++It)
	{
		OutKeyHandles.Add(It.Key());
	}

	return OutKeyHandles;
}


FKeyHandle FFloatCurveKeyArea::MoveKey(FKeyHandle KeyHandle, float DeltaPosition)
{
	return Curve->SetKeyTime(KeyHandle, Curve->GetKeyTime(KeyHandle) + DeltaPosition);
}


void FFloatCurveKeyArea::SetExtrapolationMode(ERichCurveExtrapolation ExtrapMode, bool bPreInfinity)
{
	if (bPreInfinity)
	{
		Curve->PreInfinityExtrap = ExtrapMode;
	}
	else
	{
		Curve->PostInfinityExtrap = ExtrapMode;
	}
}

bool FFloatCurveKeyArea::CanSetExtrapolationMode() const
{
	return true;
}

void FFloatCurveKeyArea::SetKeyInterpMode(FKeyHandle KeyHandle, ERichCurveInterpMode InterpMode)
{
	if (Curve->IsKeyHandleValid(KeyHandle))
	{
		Curve->SetKeyInterpMode(KeyHandle, InterpMode);
	}
}


void FFloatCurveKeyArea::SetKeyTangentMode(FKeyHandle KeyHandle, ERichCurveTangentMode TangentMode)
{
	if (Curve->IsKeyHandleValid(KeyHandle))
	{
		Curve->SetKeyTangentMode(KeyHandle, TangentMode);
	}
}


void FFloatCurveKeyArea::SetKeyTime(FKeyHandle KeyHandle, float NewKeyTime) const
{
	Curve->SetKeyTime(KeyHandle, NewKeyTime);
}

void FFloatCurveKeyArea::CopyKeys(FMovieSceneClipboardBuilder& ClipboardBuilder, const TFunctionRef<bool(FKeyHandle, const IKeyArea&)>& KeyMask) const
{
	const UMovieSceneSection* Section = const_cast<FFloatCurveKeyArea*>(this)->GetOwningSection();
	UMovieSceneTrack* Track = Section ? Section->GetTypedOuter<UMovieSceneTrack>() : nullptr;
	if (!Track)
	{
		return;
	}

	FMovieSceneClipboardKeyTrack* KeyTrack = nullptr;

	for (auto It(Curve->GetKeyHandleIterator()); It; ++It)
	{
		FKeyHandle Handle = It.Key();
		if (KeyMask(Handle, *this))
		{
			if (!KeyTrack)
			{
				KeyTrack = &ClipboardBuilder.FindOrAddKeyTrack<FRichCurveKey>(GetName(), *Track);
			}

			FRichCurveKey Key = Curve->GetKey(Handle);
			KeyTrack->AddKey(Key.Time, Key);
		}
	}
}

void FFloatCurveKeyArea::PasteKeys(const FMovieSceneClipboardKeyTrack& KeyTrack, const FMovieSceneClipboardEnvironment& SrcEnvironment, const FSequencerPasteEnvironment& DstEnvironment)
{
	float PasteAt = DstEnvironment.CardinalTime;

	KeyTrack.IterateKeys([&](const FMovieSceneClipboardKey& Key){
		UMovieSceneSection* Section = GetOwningSection();
		if (!Section)
		{
			return true;
		}

		if (Section->TryModify())
		{
			float Time = PasteAt + Key.GetTime();
			if (Section->GetStartTime() > Time)
			{
				Section->SetStartTime(Time);
			}
			if (Section->GetEndTime() < Time)
			{
				Section->SetEndTime(Time);
			}

			FRichCurveKey NewKey = Key.GetValue<FRichCurveKey>();

			// Rich curve keys store their time internally, so we need to ensure they are set up correctly here (if there's been some conversion, for instance)
			NewKey.Time = Time;

			FKeyHandle KeyHandle = Curve->UpdateOrAddKey(Time, NewKey.Value);
			// Ensure the rest of the key properties are added
			Curve->GetKey(KeyHandle) = NewKey;

			DstEnvironment.ReportPastedKey(KeyHandle, *this);
		}

		return true;
	});
}

void FFloatCurveKeyArea::OnValueChanged(float InValue)
{
	ClearIntermediateValue();
}
