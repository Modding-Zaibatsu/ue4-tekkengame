// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.


#include "PersonaPrivatePCH.h"

#include "SAnimCompositeEditor.h"
#include "GraphEditor.h"
#include "GraphEditorModule.h"
#include "Editor/Kismet/Public/SKismetInspector.h"
#include "SAnimCompositePanel.h"
#include "ScopedTransaction.h"
#include "SAnimNotifyPanel.h"
#include "SAnimCurvePanel.h"

//////////////////////////////////////////////////////////////////////////
// SAnimCompositeEditor

TSharedRef<SWidget> SAnimCompositeEditor::CreateDocumentAnchor()
{
	return IDocumentation::Get()->CreateAnchor(TEXT("Engine/Animation/AnimationComposite"));
}

void SAnimCompositeEditor::Construct(const FArguments& InArgs)
{
	bIsActiveTimerRegistered = false;
	PersonaPtr = InArgs._Persona;
	CompositeObj = InArgs._Composite;
	check(CompositeObj);

	SAnimEditorBase::Construct( SAnimEditorBase::FArguments()
		.Persona(InArgs._Persona)
		);

	PersonaPtr.Pin()->RegisterOnPostUndo(FPersona::FOnPostUndo::CreateSP( this, &SAnimCompositeEditor::PostUndo ) );	

	EditorPanels->AddSlot()
		.AutoHeight()
		.Padding(0, 10)
		[
			SAssignNew( AnimCompositePanel, SAnimCompositePanel )
			.Persona(PersonaPtr)
			.Composite(CompositeObj)
			.CompositeEditor(SharedThis(this))
			.WidgetWidth(S2ColumnWidget::DEFAULT_RIGHT_COLUMN_WIDTH)
			.ViewInputMin(this, &SAnimEditorBase::GetViewMinInput)
			.ViewInputMax(this, &SAnimEditorBase::GetViewMaxInput)
			.OnSetInputViewRange(this, &SAnimEditorBase::SetInputViewRange)
		];

	EditorPanels->AddSlot()
		.AutoHeight()
		.Padding(0, 10)
		[
			SAssignNew( AnimNotifyPanel, SAnimNotifyPanel )
			.Persona(InArgs._Persona)
			.Sequence(CompositeObj)
			.WidgetWidth(S2ColumnWidget::DEFAULT_RIGHT_COLUMN_WIDTH)
			.InputMin(this, &SAnimEditorBase::GetMinInput)
			.InputMax(this, &SAnimEditorBase::GetMaxInput)
			.ViewInputMin(this, &SAnimEditorBase::GetViewMinInput)
			.ViewInputMax(this, &SAnimEditorBase::GetViewMaxInput)
			.OnSetInputViewRange(this, &SAnimEditorBase::SetInputViewRange)
			.OnGetScrubValue(this, &SAnimEditorBase::GetScrubValue)
			.OnSelectionChanged(this, &SAnimEditorBase::OnSelectionChanged)
		];

	EditorPanels->AddSlot()
		.AutoHeight()
		.Padding(0, 10)
		[
			SAssignNew( AnimCurvePanel, SAnimCurvePanel )
			.Persona(PersonaPtr)
			.Sequence(CompositeObj)
			.WidgetWidth(S2ColumnWidget::DEFAULT_RIGHT_COLUMN_WIDTH)
			.ViewInputMin(this, &SAnimEditorBase::GetViewMinInput)
			.ViewInputMax(this, &SAnimEditorBase::GetViewMaxInput)
			.InputMin(this, &SAnimEditorBase::GetMinInput)
			.InputMax(this, &SAnimEditorBase::GetMaxInput)
			.OnSetInputViewRange(this, &SAnimEditorBase::SetInputViewRange)
			.OnGetScrubValue(this, &SAnimEditorBase::GetScrubValue)
		];

	CollapseComposite();
}

SAnimCompositeEditor::~SAnimCompositeEditor()
{
	if (PersonaPtr.IsValid())
	{
		PersonaPtr.Pin()->UnregisterOnPostUndo(this);
	}
}

void SAnimCompositeEditor::PreAnimUpdate()
{
	CompositeObj->Modify();
}

void SAnimCompositeEditor::PostAnimUpdate()
{
	CompositeObj->MarkPackageDirty();
	SortAndUpdateComposite();
}

void SAnimCompositeEditor::RebuildPanel()
{
	SortAndUpdateComposite();
	AnimCompositePanel->Update();
}

void SAnimCompositeEditor::OnCompositeChange(class UObject *EditorAnimBaseObj, bool bRebuild)
{
	if ( CompositeObj != nullptr )
	{
		if(bRebuild && !bIsActiveTimerRegistered)
		{
			// sometimes crashes because the timer delay but animation still renders, so invalidating here before calling timer
			CompositeObj->InvalidateRecursiveAsset();
			bIsActiveTimerRegistered = true;
			RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SAnimCompositeEditor::TriggerRebuildPanel));
		} 
		else
		{
			CollapseComposite();
		}

		CompositeObj->MarkPackageDirty();
	}
}

void SAnimCompositeEditor::CollapseComposite()
{
	if ( CompositeObj == nullptr )
	{
		return;
	}

	CompositeObj->AnimationTrack.CollapseAnimSegments();

	RecalculateSequenceLength();
}

void SAnimCompositeEditor::PostUndo()
{
	if (!bIsActiveTimerRegistered)
	{
		bIsActiveTimerRegistered = true;
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SAnimCompositeEditor::TriggerRebuildPanel));
	}

	// when undo or redo happens, we still have to recalculate length, so we can't rely on sequence length changes or not
	if (CompositeObj->SequenceLength)
	{
		CompositeObj->SequenceLength = 0.f;
	}
}

void SAnimCompositeEditor::InitDetailsViewEditorObject(UEditorAnimBaseObj* EdObj)
{
	EdObj->InitFromAnim(CompositeObj, FOnAnimObjectChange::CreateSP( SharedThis(this), &SAnimCompositeEditor::OnCompositeChange ));
}

EActiveTimerReturnType SAnimCompositeEditor::TriggerRebuildPanel(double InCurrentTime, float InDeltaTime)
{
	// we should not update any property related within PostEditChange, 
	// so this is deferred to Tick, when it needs to rebuild, just mark it and this will update in the next tick
	RebuildPanel();

	bIsActiveTimerRegistered = false;
	return EActiveTimerReturnType::Stop;
}

float SAnimCompositeEditor::CalculateSequenceLengthOfEditorObject() const
{
	return CompositeObj->AnimationTrack.GetLength();
}

void SAnimCompositeEditor::SortAndUpdateComposite()
{
	if (CompositeObj == nullptr)
	{
		return;
	}

	CompositeObj->AnimationTrack.SortAnimSegments();

	RecalculateSequenceLength();

	// Update view (this will recreate everything)
	AnimCompositePanel->Update();
}

