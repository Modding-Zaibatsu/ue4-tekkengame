// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "SequencerPrivatePCH.h"
#include "SequencerEditTool_Movement.h"
#include "Sequencer.h"
#include "VirtualTrackArea.h"
#include "SequencerHotspots.h"
#include "EditToolDragOperations.h"


const FName FSequencerEditTool_Movement::Identifier = "Movement";


FSequencerEditTool_Movement::FSequencerEditTool_Movement(FSequencer& InSequencer)
	: FSequencerEditTool(InSequencer)
	, SequencerWidget(StaticCastSharedRef<SSequencer>(InSequencer.GetSequencerWidget()))
{ }


FReply FSequencerEditTool_Movement::OnMouseButtonDown(SWidget& OwnerWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TSharedPtr<ISequencerHotspot> Hotspot = Sequencer.GetHotspot();

	DelayedDrag.Reset();

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton || MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		const FVirtualTrackArea VirtualTrackArea = SequencerWidget.Pin()->GetVirtualTrackArea();

		DelayedDrag = FDelayedDrag_Hotspot(VirtualTrackArea.CachedTrackAreaGeometry().AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), MouseEvent.GetEffectingButton(), Hotspot);

 		if (Sequencer.GetSettings()->GetSnapPlayTimeToDraggedKey() || (MouseEvent.IsShiftDown() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) )
		{
			if (DelayedDrag->Hotspot.IsValid())
			{
				if (DelayedDrag->Hotspot->GetType() == ESequencerHotspot::Key)
				{
					FSequencerSelectedKey& ThisKey = StaticCastSharedPtr<FKeyHotspot>(DelayedDrag->Hotspot)->Key;
					Sequencer.SetGlobalTime(ThisKey.KeyArea->GetKeyTime(ThisKey.KeyHandle.GetValue()));
				}
			}
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}


FReply FSequencerEditTool_Movement::OnMouseMove(SWidget& OwnerWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (DelayedDrag.IsSet())
	{
		const FVirtualTrackArea VirtualTrackArea = SequencerWidget.Pin()->GetVirtualTrackArea();

		FReply Reply = FReply::Handled();

		if (DelayedDrag->IsDragging())
		{
			// If we're already dragging, just update the drag op if it exists
			if (DragOperation.IsValid())
			{
				DragPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
				DragOperation->OnDrag(MouseEvent, DragPosition, VirtualTrackArea);
			}
		}
		// Otherwise we can attempt a new drag
		else if (DelayedDrag->AttemptDragStart(MouseEvent))
		{
			DragOperation = CreateDrag(MouseEvent);

			if (DragOperation.IsValid())
			{
				DragOperation->OnBeginDrag(MouseEvent, DelayedDrag->GetInitialPosition(), VirtualTrackArea);

				// Steal the capture, as we're now the authoritative widget in charge of a mouse-drag operation
				Reply.CaptureMouse(OwnerWidget.AsShared());
			}
		}

		return Reply;
	}
	return FReply::Unhandled();
}

bool FSequencerEditTool_Movement::GetHotspotTime(float& HotspotTime) const
{
	if (DelayedDrag->Hotspot.IsValid())
	{
		auto HotspotType = DelayedDrag->Hotspot->GetType();
		if (HotspotType == ESequencerHotspot::Key)
		{
			FSequencerSelectedKey& ThisKey = StaticCastSharedPtr<FKeyHotspot>(DelayedDrag->Hotspot)->Key;
			HotspotTime = ThisKey.KeyArea->GetKeyTime(ThisKey.KeyHandle.GetValue());
			return true;
		}
		else if (HotspotType == ESequencerHotspot::Section || HotspotType == ESequencerHotspot::SectionResize_L)
		{
			FSectionHandle& ThisHandle = StaticCastSharedPtr<FSectionHotspot>(DelayedDrag->Hotspot)->Section;
			UMovieSceneSection* ThisSection = ThisHandle.GetSectionObject();
			if (ThisSection)
			{
				HotspotTime = ThisSection->GetStartTime();
				return true;
			}
		}
		else if (HotspotType == ESequencerHotspot::SectionResize_R)
		{
			FSectionHandle& ThisHandle = StaticCastSharedPtr<FSectionHotspot>(DelayedDrag->Hotspot)->Section;
			UMovieSceneSection* ThisSection = ThisHandle.GetSectionObject();
			if (ThisSection)
			{
				HotspotTime = ThisSection->GetEndTime();
				return true;
			}
		}
	}
	return false;
}

TSharedPtr<ISequencerEditToolDragOperation> FSequencerEditTool_Movement::CreateDrag(const FPointerEvent& MouseEvent)
{
	FSequencerSelection& Selection = Sequencer.GetSelection();

	GetHotspotTime(OriginalHotspotTime);

	if (DelayedDrag->Hotspot.IsValid())
	{
		// Let the hotspot start a drag first, if it wants to
		auto HotspotDrag = DelayedDrag->Hotspot->InitiateDrag(Sequencer);
		if (HotspotDrag.IsValid())
		{
			return HotspotDrag;
		}

		// Ok, the hotspot doesn't know how to drag - let's decide for ourselves
		auto HotspotType = DelayedDrag->Hotspot->GetType();

		// Moving section(s)?
		if (HotspotType == ESequencerHotspot::Section)
		{
			TArray<FSectionHandle> SectionHandles;

			FSectionHandle& ThisHandle = StaticCastSharedPtr<FSectionHotspot>(DelayedDrag->Hotspot)->Section;
			UMovieSceneSection* ThisSection = ThisHandle.GetSectionObject();
			if (Selection.IsSelected(ThisSection))
			{
				SectionHandles = SequencerWidget.Pin()->GetSectionHandles(Selection.GetSelectedSections());
			}
			else
			{
				Selection.EmptySelectedKeys();
				Selection.EmptySelectedSections();
				Selection.EmptyNodesWithSelectedKeysOrSections();
				Selection.AddToSelection(ThisSection);
				SequencerHelpers::UpdateHoveredNodeFromSelectedSections(Sequencer);
				SectionHandles.Add(ThisHandle);
			}
			return MakeShareable( new FMoveSection( Sequencer, SectionHandles ) );
		}
		// Moving key(s)?
		else if (HotspotType == ESequencerHotspot::Key)
		{
			FSequencerSelectedKey& ThisKey = StaticCastSharedPtr<FKeyHotspot>(DelayedDrag->Hotspot)->Key;

			// If it's not selected, we'll treat this as a unique drag
			if (!Selection.IsSelected(ThisKey))
			{
				Selection.EmptySelectedKeys();
				Selection.EmptySelectedSections();
				Selection.EmptyNodesWithSelectedKeysOrSections();
				Selection.AddToSelection(ThisKey);
				SequencerHelpers::UpdateHoveredNodeFromSelectedKeys(Sequencer);
			}

			// @todo sequencer: Make this a customizable UI command modifier?
			if (MouseEvent.IsAltDown())
			{
				return MakeShareable( new FDuplicateKeys( Sequencer, Selection.GetSelectedKeys() ) );
			}

			return MakeShareable( new FMoveKeys( Sequencer, Selection.GetSelectedKeys() ) );
		}
	}
	// If we're not dragging a hotspot, sections take precedence over keys
	else if (Selection.GetSelectedSections().Num())
	{
		return MakeShareable( new FMoveSection( Sequencer, SequencerWidget.Pin()->GetSectionHandles(Selection.GetSelectedSections()) ) );
	}
	else if (Selection.GetSelectedKeys().Num())
	{
		return MakeShareable( new FMoveKeys( Sequencer, Selection.GetSelectedKeys() ) );
	}

	return nullptr;
}


FReply FSequencerEditTool_Movement::OnMouseButtonUp(SWidget& OwnerWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	DelayedDrag.Reset();

	if (DragOperation.IsValid())
	{
		DragOperation->OnEndDrag(MouseEvent, MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), SequencerWidget.Pin()->GetVirtualTrackArea());
		DragOperation = nullptr;

		// Only return handled if we actually started a drag
		return FReply::Handled().ReleaseMouseCapture();
	}

	SequencerHelpers::PerformDefaultSelection(Sequencer, MouseEvent);

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		TSharedPtr<SWidget> MenuContent = SequencerHelpers::SummonContextMenu( Sequencer, MyGeometry, MouseEvent );
		if (MenuContent.IsValid())
		{
			FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();

			FSlateApplication::Get().PushMenu(
				OwnerWidget.AsShared(),
				WidgetPath,
				MenuContent.ToSharedRef(),
				MouseEvent.GetScreenSpacePosition(),
				FPopupTransitionEffect( FPopupTransitionEffect::ContextMenu )
				);

			return FReply::Handled().SetUserFocus(MenuContent.ToSharedRef(), EFocusCause::SetDirectly).ReleaseMouseCapture();
		}
	}

	return FReply::Handled();
}


void FSequencerEditTool_Movement::OnMouseCaptureLost()
{
	DelayedDrag.Reset();
	DragOperation = nullptr;
}


int32 FSequencerEditTool_Movement::OnPaint(const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (DelayedDrag.IsSet() && DelayedDrag->IsDragging())
	{
		const TSharedPtr<ISequencerHotspot>& Hotspot = DelayedDrag->Hotspot;

		if (Hotspot.IsValid())
		{
			float CurrentTime = 0.0f;

			if (GetHotspotTime(CurrentTime))
			{
				const FSlateFontInfo SmallLayoutFont(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"), 10);			
				const TSharedRef< FSlateFontMeasure > FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
				const FLinearColor DrawColor = FEditorStyle::GetSlateColor("SelectionColor").GetColor(FWidgetStyle());
				const FVector2D BoxPadding = FVector2D(4.0f, 2.0f);
				const float MousePadding = 20.0f;

				// calculate draw position
				const FVirtualTrackArea VirtualTrackArea = SequencerWidget.Pin()->GetVirtualTrackArea();
				const float HorizontalDelta = DragPosition.X - DelayedDrag->GetInitialPosition().X;
				const float InitialY = DelayedDrag->GetInitialPosition().Y;

				const FVector2D OldPos = FVector2D(VirtualTrackArea.TimeToPixel(OriginalHotspotTime), InitialY);
				const FVector2D NewPos = FVector2D(VirtualTrackArea.TimeToPixel(CurrentTime), InitialY);

				TArray<FVector2D> LinePoints;
				{
					LinePoints.AddUninitialized(2);
					LinePoints[0] = FVector2D(0.0f, 0.0f);
					LinePoints[1] = FVector2D(0.0f, VirtualTrackArea.GetPhysicalSize().Y);
				}

				// draw old position vertical
				FSlateDrawElement::MakeLines(
					OutDrawElements,
					LayerId + 1,
					AllottedGeometry.ToPaintGeometry(FVector2D(OldPos.X, 0.0f), FVector2D(1.0f, 1.0f)),
					LinePoints,
					MyClippingRect,
					ESlateDrawEffect::None,
					FLinearColor::White.CopyWithNewOpacity(0.5f),
					false
				);

				// draw new position vertical
				FSlateDrawElement::MakeLines(
					OutDrawElements,
					LayerId + 1,
					AllottedGeometry.ToPaintGeometry(FVector2D(NewPos.X, 0.0f), FVector2D(1.0f, 1.0f)),
					LinePoints,
					MyClippingRect,
					ESlateDrawEffect::None,
					DrawColor,
					false
				);

				// draw time string
				const FString TimeString = TimeToString(CurrentTime, false);
				const FVector2D TimeStringSize = FontMeasureService->Measure(TimeString, SmallLayoutFont);
				const FVector2D TimePos = FVector2D(NewPos.X - MousePadding - TimeStringSize.X, NewPos.Y - 0.5f * TimeStringSize.Y);

				FSlateDrawElement::MakeBox( 
					OutDrawElements,
					LayerId + 2, 
					AllottedGeometry.ToPaintGeometry(TimePos - BoxPadding, TimeStringSize + 2.0f * BoxPadding),
					FEditorStyle::GetBrush("WhiteBrush"),
					MyClippingRect, 
					ESlateDrawEffect::None, 
					FLinearColor::Black.CopyWithNewOpacity(0.5f)
				);

				FSlateDrawElement::MakeText(
					OutDrawElements,
					LayerId + 3,
					AllottedGeometry.ToPaintGeometry(TimePos, TimeStringSize),
					TimeString,
					SmallLayoutFont,
					MyClippingRect,
					ESlateDrawEffect::None,
					DrawColor
				);

				// draw offset string
				const FString OffsetString = TimeToString(CurrentTime - OriginalHotspotTime, true);
				const FVector2D OffsetStringSize = FontMeasureService->Measure(OffsetString, SmallLayoutFont);
				const FVector2D OffsetPos = FVector2D(NewPos.X + MousePadding, NewPos.Y - 0.5f * OffsetStringSize.Y);

				FSlateDrawElement::MakeBox( 
					OutDrawElements,
					LayerId + 2, 
					AllottedGeometry.ToPaintGeometry(OffsetPos - BoxPadding, OffsetStringSize + 2.0f * BoxPadding),
					FEditorStyle::GetBrush("WhiteBrush"),
					MyClippingRect, 
					ESlateDrawEffect::None, 
					FLinearColor::Black.CopyWithNewOpacity(0.5f)
				);

				FSlateDrawElement::MakeText(
					OutDrawElements,
					LayerId + 3,
					AllottedGeometry.ToPaintGeometry(OffsetPos, TimeStringSize),
					OffsetString,
					SmallLayoutFont,
					MyClippingRect,
					ESlateDrawEffect::None,
					DrawColor
				);
			}
		}
	}

	return LayerId;
}


FCursorReply FSequencerEditTool_Movement::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	TSharedPtr<ISequencerHotspot> Hotspot = DelayedDrag.IsSet()
		? DelayedDrag->Hotspot
		: Sequencer.GetHotspot();

	if (Hotspot.IsValid())
	{
		FCursorReply Reply = Hotspot->GetCursor();
		if (Reply.IsEventHandled())
		{
			return Reply;
		}
	}

	return FCursorReply::Cursor(EMouseCursor::CardinalCross);
}


FName FSequencerEditTool_Movement::GetIdentifier() const
{
	return Identifier;
}


bool FSequencerEditTool_Movement::CanDeactivate() const
{
	return !DelayedDrag.IsSet();
}


FString FSequencerEditTool_Movement::TimeToString(float Time, bool IsDelta) const
{
	USequencerSettings* Settings = Sequencer.GetSettings();

	if ((Settings != nullptr) && Settings->GetShowFrameNumbers())
	{
		if (SequencerSnapValues::IsTimeSnapIntervalFrameRate(Settings->GetTimeSnapInterval()))
		{
			const float FrameRate = 1.0f / Settings->GetTimeSnapInterval();
			const int32 Frame = SequencerHelpers::TimeToFrame(Time, FrameRate);

			return FString::Printf(IsDelta ? TEXT("[%+d]") : TEXT("%d"), Frame);
		}
	}

	return FString::Printf(IsDelta ? TEXT("[%+.3f]") : TEXT("%.3f"), Time);
}

const ISequencerHotspot* FSequencerEditTool_Movement::GetDragHotspot() const
{
	return DelayedDrag.IsSet() ? DelayedDrag->Hotspot.Get() : nullptr;
}