// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "UMGEditorPrivatePCH.h"

#include "SKismetInspector.h"
#include "WidgetBlueprintEditor.h"
#include "MovieScene.h"
#include "MovieSceneSequenceInstance.h"
#include "MovieScene2DTransformTrack.h"
#include "Editor/Sequencer/Public/ISequencerModule.h"
#include "ObjectEditorUtils.h"

#include "PropertyCustomizationHelpers.h"

#include "WidgetBlueprintApplicationModes.h"
#include "WidgetBlueprintEditorUtils.h"
#include "WidgetDesignerApplicationMode.h"
#include "WidgetGraphApplicationMode.h"

#include "WidgetBlueprintEditorToolbar.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "GenericCommands.h"
#include "WidgetBlueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/CompilerResultsLog.h"
#include "IMessageLogListing.h"

#include "MovieSceneWidgetMaterialTrack.h"
#include "WidgetMaterialTrackUtilities.h"

#include "ScopedTransaction.h"

#include "NotificationManager.h"
#include "SNotificationList.h"

#define LOCTEXT_NAMESPACE "UMG"

FWidgetBlueprintEditor::FWidgetBlueprintEditor()
	: PreviewScene(FPreviewScene::ConstructionValues().AllowAudioPlayback(true).ShouldSimulatePhysics(true))
	, PreviewBlueprint(nullptr)
	, bIsSimulateEnabled(false)
	, bIsRealTime(true)
	, bRefreshGeneratedClassAnimations(false)
{
	PreviewScene.GetWorld()->bBegunPlay = false;

	// Register sequencer menu extenders.
	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>( "Sequencer" );
	{
		int32 NewIndex = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().Add(
			FAssetEditorExtender::CreateRaw(this, &FWidgetBlueprintEditor::GetAddTrackSequencerExtender));
		SequencerAddTrackExtenderHandle = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates()[NewIndex].GetHandle();
	}

	{
		int32 NewIndex = SequencerModule.GetObjectBindingContextMenuExtensibilityManager()->GetExtenderDelegates().Add(
			FAssetEditorExtender::CreateRaw(this, &FWidgetBlueprintEditor::GetObjectBindingContextMenuExtender));
		SequencerObjectBindingExtenderHandle = SequencerModule.GetObjectBindingContextMenuExtensibilityManager()->GetExtenderDelegates()[NewIndex].GetHandle();
	}
}

FWidgetBlueprintEditor::~FWidgetBlueprintEditor()
{
	UWidgetBlueprint* Blueprint = GetWidgetBlueprintObj();
	if ( Blueprint )
	{
		Blueprint->OnChanged().RemoveAll(this);
		Blueprint->OnCompiled().RemoveAll(this);
	}

	GEditor->OnObjectsReplaced().RemoveAll(this);
	
	if ( Sequencer.IsValid() )
	{
		Sequencer->OnMovieSceneDataChanged().RemoveAll( this );
		Sequencer.Reset();
	}

	// Un-Register sequencer menu extenders.
	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().RemoveAll([this](const FAssetEditorExtender& Extender)
	{
		return SequencerAddTrackExtenderHandle == Extender.GetHandle();
	});

	SequencerModule.GetObjectBindingContextMenuExtensibilityManager()->GetExtenderDelegates().RemoveAll([this](const FAssetEditorExtender& Extender)
	{
		return SequencerObjectBindingExtenderHandle == Extender.GetHandle();
	});
}

void FWidgetBlueprintEditor::InitWidgetBlueprintEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, const TArray<UBlueprint*>& InBlueprints, bool bShouldOpenInDefaultsMode)
{
	TSharedPtr<FWidgetBlueprintEditor> ThisPtr(SharedThis(this));
	WidgetToolbar = MakeShareable(new FWidgetBlueprintEditorToolbar(ThisPtr));

	InitBlueprintEditor(Mode, InitToolkitHost, InBlueprints, bShouldOpenInDefaultsMode);

	// register for any objects replaced
	GEditor->OnObjectsReplaced().AddSP(this, &FWidgetBlueprintEditor::OnObjectsReplaced);

	UWidgetBlueprint* Blueprint = GetWidgetBlueprintObj();

	// If this blueprint is empty, add a canvas panel as the root widget.
	if ( Blueprint->WidgetTree->RootWidget == nullptr )
	{
		UWidget* RootWidget = Blueprint->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		RootWidget->SetDesignerFlags(GetCurrentDesignerFlags());
		Blueprint->WidgetTree->RootWidget = RootWidget;
	}

	UpdatePreview(GetWidgetBlueprintObj(), true);

	// If the user has close the sequencer tab, this will not be initialized.
	if (CurrentAnimation.IsValid())
	{
		CurrentAnimation->Initialize(PreviewWidgetPtr.Get());
	}

	DesignerCommandList = MakeShareable(new FUICommandList);

	DesignerCommandList->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::DeleteSelectedWidgets),
		FCanExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::CanDeleteSelectedWidgets)
		);

	DesignerCommandList->MapAction(FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::CopySelectedWidgets),
		FCanExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::CanCopySelectedWidgets)
		);

	DesignerCommandList->MapAction(FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::CutSelectedWidgets),
		FCanExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::CanCutSelectedWidgets)
		);

	DesignerCommandList->MapAction(FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::PasteWidgets),
		FCanExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::CanPasteWidgets)
		);
}

void FWidgetBlueprintEditor::RegisterApplicationModes(const TArray<UBlueprint*>& InBlueprints, bool bShouldOpenInDefaultsMode, bool bNewlyCreated/* = false*/)
{
	//FBlueprintEditor::RegisterApplicationModes(InBlueprints, bShouldOpenInDefaultsMode);

	if ( InBlueprints.Num() == 1 )
	{
		TSharedPtr<FWidgetBlueprintEditor> ThisPtr(SharedThis(this));

		// Create the modes and activate one (which will populate with a real layout)
		TArray< TSharedRef<FApplicationMode> > TempModeList;
		TempModeList.Add(MakeShareable(new FWidgetDesignerApplicationMode(ThisPtr)));
		TempModeList.Add(MakeShareable(new FWidgetGraphApplicationMode(ThisPtr)));

		for ( TSharedRef<FApplicationMode>& AppMode : TempModeList )
		{
			AddApplicationMode(AppMode->GetModeName(), AppMode);
		}

		SetCurrentMode(FWidgetBlueprintApplicationModes::DesignerMode);
	}
	else
	{
		//// We either have no blueprints or many, open in the defaults mode for multi-editing
		//AddApplicationMode(
		//	FBlueprintEditorApplicationModes::BlueprintDefaultsMode,
		//	MakeShareable(new FBlueprintDefaultsApplicationMode(SharedThis(this))));
		//SetCurrentMode(FBlueprintEditorApplicationModes::BlueprintDefaultsMode);
	}
}

void FWidgetBlueprintEditor::SelectWidgets(const TSet<FWidgetReference>& Widgets, bool bAppendOrToggle)
{
	TSet<FWidgetReference> TempSelection;
	for ( const FWidgetReference& Widget : Widgets )
	{
		if ( Widget.IsValid() )
		{
			TempSelection.Add(Widget);
		}
	}

	OnSelectedWidgetsChanging.Broadcast();

	// Finally change the selected widgets after we've updated the details panel 
	// to ensure values that are pending are committed on focus loss, and migrated properly
	// to the old selected widgets.
	if ( !bAppendOrToggle )
	{
		SelectedWidgets.Empty();
	}
	SelectedObjects.Empty();
	SelectedNamedSlot.Reset();

	for ( const FWidgetReference& Widget : TempSelection )
	{
		if ( bAppendOrToggle && SelectedWidgets.Contains(Widget) )
		{
			SelectedWidgets.Remove(Widget);
		}
		else
		{
			SelectedWidgets.Add(Widget);
		}
	}

	OnSelectedWidgetsChanged.Broadcast();
}

void FWidgetBlueprintEditor::SelectObjects(const TSet<UObject*>& Objects)
{
	OnSelectedWidgetsChanging.Broadcast();

	SelectedWidgets.Empty();
	SelectedObjects.Empty();
	SelectedNamedSlot.Reset();

	for ( UObject* Obj : Objects )
	{
		SelectedObjects.Add(Obj);
	}

	OnSelectedWidgetsChanged.Broadcast();
}

void FWidgetBlueprintEditor::SetSelectedNamedSlot(TOptional<FNamedSlotSelection> InSelectedNamedSlot)
{
	OnSelectedWidgetsChanging.Broadcast();

	SelectedWidgets.Empty();
	SelectedObjects.Empty();
	SelectedNamedSlot.Reset();

	SelectedNamedSlot = InSelectedNamedSlot;

	OnSelectedWidgetsChanged.Broadcast();
}

void FWidgetBlueprintEditor::CleanSelection()
{
	TSet<FWidgetReference> TempSelection;

	TArray<UWidget*> WidgetsInTree;
	GetWidgetBlueprintObj()->WidgetTree->GetAllWidgets(WidgetsInTree);
	TSet<UWidget*> TreeWidgetSet(WidgetsInTree);

	for ( FWidgetReference& WidgetRef : SelectedWidgets )
	{
		if ( WidgetRef.IsValid() )
		{
			if ( TreeWidgetSet.Contains(WidgetRef.GetTemplate()) )
			{
				TempSelection.Add(WidgetRef);
			}
		}
	}

	if ( TempSelection.Num() != SelectedWidgets.Num() )
	{
		SelectWidgets(TempSelection, false);
	}
}

const TSet<FWidgetReference>& FWidgetBlueprintEditor::GetSelectedWidgets() const
{
	return SelectedWidgets;
}

const TSet< TWeakObjectPtr<UObject> >& FWidgetBlueprintEditor::GetSelectedObjects() const
{
	return SelectedObjects;
}

TOptional<FNamedSlotSelection> FWidgetBlueprintEditor::GetSelectedNamedSlot() const
{
	return SelectedNamedSlot;
}

void FWidgetBlueprintEditor::InvalidatePreview(bool bViewOnly)
{
	if ( bViewOnly )
	{
		OnWidgetPreviewUpdated.Broadcast();
	}
	else
	{
		bPreviewInvalidated = true;
	}
}

void FWidgetBlueprintEditor::OnBlueprintChangedImpl(UBlueprint* InBlueprint, bool bIsJustBeingCompiled )
{
	DestroyPreview();

	FBlueprintEditor::OnBlueprintChangedImpl(InBlueprint, bIsJustBeingCompiled);

	if ( InBlueprint )
	{
		RefreshPreview();
	}
}

void FWidgetBlueprintEditor::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	// Remove dead references and update references
	for ( int32 HandleIndex = WidgetHandlePool.Num() - 1; HandleIndex >= 0; HandleIndex-- )
	{
		TSharedPtr<FWidgetHandle> Ref = WidgetHandlePool[HandleIndex].Pin();

		if ( Ref.IsValid() )
		{
			UObject* const* NewObject = ReplacementMap.Find(Ref->Widget.Get());
			if ( NewObject )
			{
				Ref->Widget = Cast<UWidget>(*NewObject);
			}
		}
		else
		{
			WidgetHandlePool.RemoveAtSwap(HandleIndex);
		}
	}
}

bool FWidgetBlueprintEditor::CanDeleteSelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	return Widgets.Num() > 0;
}

void FWidgetBlueprintEditor::DeleteSelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	FWidgetBlueprintEditorUtils::DeleteWidgets(GetWidgetBlueprintObj(), Widgets);

	// Clear the selection now that the widget has been deleted.
	TSet<FWidgetReference> Empty;
	SelectWidgets(Empty, false);
}

bool FWidgetBlueprintEditor::CanCopySelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	return Widgets.Num() > 0;
}

void FWidgetBlueprintEditor::CopySelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	FWidgetBlueprintEditorUtils::CopyWidgets(GetWidgetBlueprintObj(), Widgets);
}

bool FWidgetBlueprintEditor::CanCutSelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	return Widgets.Num() > 0;
}

void FWidgetBlueprintEditor::CutSelectedWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	FWidgetBlueprintEditorUtils::CutWidgets(GetWidgetBlueprintObj(), Widgets);
}

const UWidgetAnimation* FWidgetBlueprintEditor::RefreshCurrentAnimation()
{
	return CurrentAnimation.Get();
}

bool FWidgetBlueprintEditor::CanPasteWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	if ( Widgets.Num() == 1 )
	{
		FWidgetReference Target = *Widgets.CreateIterator();
		const bool bIsPanel = Cast<UPanelWidget>(Target.GetTemplate()) != nullptr;
		return bIsPanel;
	}
	else if ( GetWidgetBlueprintObj()->WidgetTree->RootWidget == nullptr )
	{
		return true;
	}
	else
	{
		TOptional<FNamedSlotSelection> NamedSlotSelection = GetSelectedNamedSlot();
		if ( NamedSlotSelection.IsSet() )
		{
			INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(NamedSlotSelection->NamedSlotHostWidget.GetTemplate());
			if ( NamedSlotHost == nullptr )
			{
				return false;
			}
			else if ( NamedSlotHost->GetContentForSlot(NamedSlotSelection->SlotName) != nullptr )
			{
				return false;
			}

			return true;
		}
	}

	return false;
}

void FWidgetBlueprintEditor::PasteWidgets()
{
	TSet<FWidgetReference> Widgets = GetSelectedWidgets();
	FWidgetReference Target = Widgets.Num() > 0 ? *Widgets.CreateIterator() : FWidgetReference();
	FName SlotName = NAME_None;

	TOptional<FNamedSlotSelection> NamedSlotSelection = GetSelectedNamedSlot();
	if ( NamedSlotSelection.IsSet() )
	{
		Target = NamedSlotSelection->NamedSlotHostWidget;
		SlotName = NamedSlotSelection->SlotName;
	}

	FWidgetBlueprintEditorUtils::PasteWidgets(SharedThis(this), GetWidgetBlueprintObj(), Target, SlotName, PasteDropLocation);

	//TODO UMG - Select the newly selected pasted widgets.
}

void FWidgetBlueprintEditor::Tick(float DeltaTime)
{
	FBlueprintEditor::Tick(DeltaTime);

	// Tick the preview scene world.
	if ( !GIntraFrameDebuggingGameThread )
	{
		// Allow full tick only if preview simulation is enabled and we're not currently in an active SIE or PIE session
		if ( bIsSimulateEnabled && GEditor->PlayWorld == nullptr && !GEditor->bIsSimulatingInEditor )
		{
			PreviewScene.GetWorld()->Tick(bIsRealTime ? LEVELTICK_All : LEVELTICK_TimeOnly, DeltaTime);
		}
		else
		{
			PreviewScene.GetWorld()->Tick(bIsRealTime ? LEVELTICK_ViewportsOnly : LEVELTICK_TimeOnly, DeltaTime);
		}
	}

	// Whenever animations change the generated class animations need to be updated since they are copied on compile.  This
	// update is deferred to tick since some edit operations (e.g. drag/drop) cause large numbers of changes to the data.
	if ( bRefreshGeneratedClassAnimations )
	{
		TArray<UWidgetAnimation*>& PreviewAnimations = Cast<UWidgetBlueprintGeneratedClass>( PreviewBlueprint->GeneratedClass )->Animations;
		PreviewAnimations.Empty();
		for ( UWidgetAnimation* WidgetAnimation : PreviewBlueprint->Animations )
		{
			PreviewAnimations.Add( DuplicateObject<UWidgetAnimation>( WidgetAnimation, PreviewBlueprint->GeneratedClass ) );
		}
		bRefreshGeneratedClassAnimations = false;
	}

	// Note: The weak ptr can become stale if the actor is reinstanced due to a Blueprint change, etc. In that case we 
	//       look to see if we can find the new instance in the preview world and then update the weak ptr.
	if ( PreviewWidgetPtr.IsStale(true) || bPreviewInvalidated )
	{
		bPreviewInvalidated = false;
		RefreshPreview();
	}
}

static bool MigratePropertyValue(UObject* SourceObject, UObject* DestinationObject, FEditPropertyChain::TDoubleLinkedListNode* PropertyChainNode, UProperty* MemberProperty, bool bIsModify)
{
	UProperty* CurrentProperty = PropertyChainNode->GetValue();

	if ( PropertyChainNode->GetNextNode() == nullptr )
	{
		if (bIsModify)
		{
			if (DestinationObject)
			{
				DestinationObject->Modify();
			}
			return true;
		}
		else
		{
			// Check to see if there's an edit condition property we also need to migrate.
			bool bDummyNegate = false;
			UBoolProperty* EditConditionProperty = PropertyCustomizationHelpers::GetEditConditionProperty(MemberProperty, bDummyNegate);
			if ( EditConditionProperty != nullptr )
			{
				FObjectEditorUtils::MigratePropertyValue(SourceObject, EditConditionProperty, DestinationObject, EditConditionProperty);
			}

			return FObjectEditorUtils::MigratePropertyValue(SourceObject, MemberProperty, DestinationObject, MemberProperty);
		}
	}
	
	if ( UObjectProperty* CurrentObjectProperty = Cast<UObjectProperty>(CurrentProperty) )
	{
		// Get the property addresses for the source and destination objects.
		UObject* SourceObjectProperty = CurrentObjectProperty->GetObjectPropertyValue(CurrentObjectProperty->ContainerPtrToValuePtr<void>(SourceObject));
		UObject* DestionationObjectProperty = CurrentObjectProperty->GetObjectPropertyValue(CurrentObjectProperty->ContainerPtrToValuePtr<void>(DestinationObject));

		return MigratePropertyValue(SourceObjectProperty, DestionationObjectProperty, PropertyChainNode->GetNextNode(), PropertyChainNode->GetNextNode()->GetValue(), bIsModify);
	}
	else if ( UArrayProperty* CurrentArrayProperty = Cast<UArrayProperty>(CurrentProperty) )
	{
		// Arrays!
	}

	return MigratePropertyValue(SourceObject, DestinationObject, PropertyChainNode->GetNextNode(), MemberProperty, bIsModify);
}

void FWidgetBlueprintEditor::AddReferencedObjects( FReferenceCollector& Collector )
{
	FBlueprintEditor::AddReferencedObjects( Collector );

	UUserWidget* Preview = GetPreview();
	Collector.AddReferencedObject( Preview );
}

void FWidgetBlueprintEditor::MigrateFromChain(FEditPropertyChain* PropertyThatChanged, bool bIsModify)
{
	UWidgetBlueprint* Blueprint = GetWidgetBlueprintObj();

	UUserWidget* PreviewActor = GetPreview();
	if ( PreviewActor != nullptr )
	{
		for ( TWeakObjectPtr<UObject> ObjectRef : SelectedObjects )
		{
			// dealing with root widget here
			FEditPropertyChain::TDoubleLinkedListNode* PropertyChainNode = PropertyThatChanged->GetHead();
			UObject* WidgetCDO = ObjectRef.Get()->GetClass()->GetDefaultObject(true);
			MigratePropertyValue(ObjectRef.Get(), WidgetCDO, PropertyChainNode, PropertyChainNode->GetValue(), bIsModify);
		}

		for ( FWidgetReference& WidgetRef : SelectedWidgets )
		{
			UWidget* PreviewWidget = WidgetRef.GetPreview();

			if ( PreviewWidget )
			{
				FName PreviewWidgetName = PreviewWidget->GetFName();
				UWidget* TemplateWidget = Blueprint->WidgetTree->FindWidget(PreviewWidgetName);

				if ( TemplateWidget )
				{
					FEditPropertyChain::TDoubleLinkedListNode* PropertyChainNode = PropertyThatChanged->GetHead();
					MigratePropertyValue(PreviewWidget, TemplateWidget, PropertyChainNode, PropertyChainNode->GetValue(), bIsModify);
				}
			}
		}
	}
}

void FWidgetBlueprintEditor::PostUndo(bool bSuccessful)
{
	FBlueprintEditor::PostUndo(bSuccessful);

	OnWidgetBlueprintTransaction.Broadcast();
}

void FWidgetBlueprintEditor::PostRedo(bool bSuccessful)
{
	FBlueprintEditor::PostRedo(bSuccessful);

	OnWidgetBlueprintTransaction.Broadcast();
}

TSharedRef<SWidget> FWidgetBlueprintEditor::CreateSequencerWidget()
{
	TSharedRef<SOverlay> SequencerOverlayRef =
		SNew(SOverlay)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Sequencer")));
	SequencerOverlay = SequencerOverlayRef;

	TSharedRef<STextBlock> NoAnimationTextBlockRef = 
		SNew(STextBlock)
		.TextStyle(FEditorStyle::Get(), "UMGEditor.NoAnimationFont")
		.Text(LOCTEXT("NoAnimationSelected", "No Animation Selected"));
	NoAnimationTextBlock = NoAnimationTextBlockRef;

	SequencerOverlayRef->AddSlot(0)
	[
		GetSequencer()->GetSequencerWidget()
	];

	SequencerOverlayRef->AddSlot(1)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
	[
		NoAnimationTextBlockRef
	];

	return SequencerOverlayRef;
}

UWidgetBlueprint* FWidgetBlueprintEditor::GetWidgetBlueprintObj() const
{
	return Cast<UWidgetBlueprint>(GetBlueprintObj());
}

UUserWidget* FWidgetBlueprintEditor::GetPreview() const
{
	if ( PreviewWidgetPtr.IsStale(true) )
	{
		return nullptr;
	}

	return PreviewWidgetPtr.Get();
}

FPreviewScene* FWidgetBlueprintEditor::GetPreviewScene()
{
	return &PreviewScene;
}

bool FWidgetBlueprintEditor::IsSimulating() const
{
	return bIsSimulateEnabled;
}

void FWidgetBlueprintEditor::SetIsSimulating(bool bSimulating)
{
	bIsSimulateEnabled = bSimulating;
}

FWidgetReference FWidgetBlueprintEditor::GetReferenceFromTemplate(UWidget* TemplateWidget)
{
	TSharedRef<FWidgetHandle> Reference = MakeShareable(new FWidgetHandle(TemplateWidget));
	WidgetHandlePool.Add(Reference);

	return FWidgetReference(SharedThis(this), Reference);
}

FWidgetReference FWidgetBlueprintEditor::GetReferenceFromPreview(UWidget* PreviewWidget)
{
	UUserWidget* PreviewRoot = GetPreview();
	if ( PreviewRoot )
	{
		UWidgetBlueprint* Blueprint = GetWidgetBlueprintObj();

		if ( PreviewWidget )
		{
			FName Name = PreviewWidget->GetFName();
			return GetReferenceFromTemplate(Blueprint->WidgetTree->FindWidget(Name));
		}
	}

	return FWidgetReference(SharedThis(this), TSharedPtr<FWidgetHandle>());
}

TSharedPtr<ISequencer>& FWidgetBlueprintEditor::GetSequencer()
{
	if(!Sequencer.IsValid())
	{
		const float InTime = 0.f;
		const float OutTime = 5.0f;

		FSequencerViewParams ViewParams(TEXT("UMGSequencerSettings"));
		{
			ViewParams.InitialScrubPosition = 0;
			ViewParams.OnGetAddMenuContent = FOnGetAddMenuContent::CreateSP(this, &FWidgetBlueprintEditor::OnGetAnimationAddMenuContent);
		}

		FSequencerInitParams SequencerInitParams;
		{
			UWidgetAnimation* NullAnimation = UWidgetAnimation::GetNullAnimation();
			NullAnimation->MovieScene->SetPlaybackRange(InTime, OutTime);
			NullAnimation->MovieScene->GetEditorData().WorkingRange = TRange<float>(InTime, OutTime);

			SequencerInitParams.ViewParams = ViewParams;
			SequencerInitParams.RootSequence = NullAnimation;
			SequencerInitParams.bEditWithinLevelEditor = false;
			SequencerInitParams.ToolkitHost = GetToolkitHost();
		};

		Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(SequencerInitParams);
		Sequencer->OnMovieSceneDataChanged().AddSP( this, &FWidgetBlueprintEditor::OnMovieSceneDataChanged );
		ChangeViewedAnimation(*UWidgetAnimation::GetNullAnimation());
	}

	return Sequencer;
}

void FWidgetBlueprintEditor::ChangeViewedAnimation( UWidgetAnimation& InAnimationToView )
{
	if (CurrentAnimation.IsValid())
	{
		CurrentAnimation->Initialize(nullptr);
	}

	CurrentAnimation = &InAnimationToView;
	if (CurrentAnimation.IsValid())
	{
		CurrentAnimation->Initialize(PreviewWidgetPtr.Get());
	}

	if (Sequencer.IsValid())
	{
		Sequencer->ResetToNewRootSequence(InAnimationToView);
	}

	TSharedPtr<SOverlay> SequencerOverlayPin = SequencerOverlay.Pin();
	if (SequencerOverlayPin.IsValid())
	{
		TSharedPtr<STextBlock> NoAnimationTextBlockPin = NoAnimationTextBlock.Pin();
		if( &InAnimationToView == UWidgetAnimation::GetNullAnimation())
		{
			// Disable sequencer from interaction
			Sequencer->GetSequencerWidget()->SetEnabled(false);
			Sequencer->SetAutoKeyMode(EAutoKeyMode::KeyNone);
			NoAnimationTextBlockPin->SetVisibility(EVisibility::Visible);
			SequencerOverlayPin->SetVisibility( EVisibility::HitTestInvisible );
		}
		else
		{
			// Allow sequencer to be interacted with
			Sequencer->GetSequencerWidget()->SetEnabled(true);
			NoAnimationTextBlockPin->SetVisibility(EVisibility::Collapsed);
			SequencerOverlayPin->SetVisibility( EVisibility::SelfHitTestInvisible );
		}
	}
	InvalidatePreview();
}

void FWidgetBlueprintEditor::RefreshPreview()
{
	// Rebuilding the preview can force objects to be recreated, so the selection may need to be updated.
	OnSelectedWidgetsChanging.Broadcast();

	UpdatePreview(GetWidgetBlueprintObj(), true);

	CleanSelection();

	// Fire the selection updated event to ensure everyone is watching the same widgets.
	OnSelectedWidgetsChanged.Broadcast();
}

void FWidgetBlueprintEditor::Compile()
{
	DestroyPreview();

	FBlueprintEditor::Compile();
}

void FWidgetBlueprintEditor::DestroyPreview()
{
	UUserWidget* PreviewActor = GetPreview();
	if ( PreviewActor != nullptr )
	{
		check(PreviewScene.GetWorld());

		// Immediately release the preview ptr to let people know it's gone.
		PreviewWidgetPtr.Reset();

		// Immediately notify anyone with a preview out there they need to dispose of it right now,
		// otherwise the leak detection can't be trusted.
		OnWidgetPreviewUpdated.Broadcast();

		TWeakPtr<SWidget> PreviewSlateWidgetWeak = PreviewActor->GetCachedWidget();

		PreviewActor->MarkPendingKill();
		PreviewActor->ReleaseSlateResources(true);

		FCompilerResultsLog LogResults;
		LogResults.bAnnotateMentionedNodes = false;

		ensure(!PreviewSlateWidgetWeak.IsValid());

		bool bFoundLeak = false;
		
		// NOTE: This doesn't explore sub UUserWidget trees, searching for leaks there.

		// Verify everything is going to be garbage collected.
		PreviewActor->WidgetTree->ForEachWidget([&LogResults, &bFoundLeak] (UWidget* Widget) {
			if ( !bFoundLeak )
			{
				TWeakPtr<SWidget> PreviewChildWidget = Widget->GetCachedWidget();
				if ( PreviewChildWidget.IsValid() )
				{
					bFoundLeak = true;
					if ( UPanelWidget* ParentWidget = Widget->GetParent() )
					{
						LogResults.Warning(*FString::Printf(*LOCTEXT("LeakingWidgetsWithParent_Warning", "Leak Detected!  %s (@@) still has living Slate widgets, it or the parent %s (@@) is keeping them in memory.  Release all Slate resources in ReleaseSlateResources().").ToString(), *Widget->GetName(), *( ParentWidget->GetName() )), Widget->GetClass(), ParentWidget->GetClass());
					}
					else
					{
						LogResults.Warning(*FString::Printf(*LOCTEXT("LeakingWidgetsWithoutParent_Warning", "Leak Detected!  %s (@@) still has living Slate widgets, it or the parent widget is keeping them in memory.  Release all Slate resources in ReleaseSlateResources().").ToString(), *Widget->GetName()), Widget->GetClass());
					}
				}
			}
		});

		DesignerCompilerMessages = LogResults.Messages;
	}
}

void FWidgetBlueprintEditor::AppendExtraCompilerResults(TSharedPtr<class IMessageLogListing> ResultsListing)
{
	FBlueprintEditor::AppendExtraCompilerResults(ResultsListing);

	ResultsListing->AddMessages(DesignerCompilerMessages);
}

void FWidgetBlueprintEditor::UpdatePreview(UBlueprint* InBlueprint, bool bInForceFullUpdate)
{
	UUserWidget* PreviewActor = GetPreview();

	// Signal that we're going to be constructing editor components
	if ( InBlueprint != nullptr && InBlueprint->SimpleConstructionScript != nullptr )
	{
		InBlueprint->SimpleConstructionScript->BeginEditorComponentConstruction();
	}

	// If the Blueprint is changing
	if ( InBlueprint != PreviewBlueprint || bInForceFullUpdate )
	{
		// Destroy the previous actor instance
		DestroyPreview();

		// Save the Blueprint we're creating a preview for
		PreviewBlueprint = Cast<UWidgetBlueprint>(InBlueprint);

		// Create the Widget, we have to do special swapping out of the widget tree.
		{
			// Assign the outer to the game instance if it exists, otherwise use the world
			PreviewActor = NewObject<UUserWidget>(PreviewScene.GetWorld(), PreviewBlueprint->GeneratedClass);

			// The preview widget should not be transactional.
			PreviewActor->ClearFlags(RF_Transactional);

			UWidgetTree* LatestWidgetTree = PreviewBlueprint->WidgetTree;

			// HACK NickD: Doing this to match the hack in UUserWidget::Initialize(), to permit some semblance of widgettree
			// inheritance.  This will correctly show the parent widget tree provided your class does not specify a root.
			UWidgetBlueprintGeneratedClass* SuperBGClass = Cast<UWidgetBlueprintGeneratedClass>(PreviewBlueprint->GeneratedClass->GetSuperClass());
			if ( SuperBGClass )
			{
				UWidgetBlueprint* SuperWidgetBlueprint = Cast<UWidgetBlueprint>(SuperBGClass->ClassGeneratedBy);
				if ( SuperWidgetBlueprint && (LatestWidgetTree->RootWidget == nullptr) )
				{
					LatestWidgetTree = SuperWidgetBlueprint->WidgetTree;
				}
			}

			// Update the widget tree directly to match the blueprint tree.  That way the preview can update
			// without needing to do a full recompile.
			PreviewActor->WidgetTree = (UWidgetTree*)StaticDuplicateObject(LatestWidgetTree, PreviewActor, NAME_None, RF_Transactional);

			if ( ULocalPlayer* Player = PreviewScene.GetWorld()->GetFirstLocalPlayerFromController() )
			{
				PreviewActor->SetPlayerContext(FLocalPlayerContext(Player));
			}

			PreviewActor->Initialize();

			// Configure all the widgets to be set to design time.
			PreviewActor->SetDesignerFlags(GetCurrentDesignerFlags());
		}

		// Store a reference to the preview actor.
		PreviewWidgetPtr = PreviewActor;
	}

	if (CurrentAnimation.IsValid())
	{
		CurrentAnimation->Initialize(PreviewActor);
	}

	OnWidgetPreviewUpdated.Broadcast();
	GetSequencer()->UpdateRuntimeInstances();
}

FGraphAppearanceInfo FWidgetBlueprintEditor::GetGraphAppearance(UEdGraph* InGraph) const
{
	FGraphAppearanceInfo AppearanceInfo = FBlueprintEditor::GetGraphAppearance(InGraph);

	if ( GetBlueprintObj()->IsA(UWidgetBlueprint::StaticClass()) )
	{
		AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText", "WIDGET BLUEPRINT");
	}

	return AppearanceInfo;
}

void FWidgetBlueprintEditor::ClearHoveredWidget()
{
	HoveredWidget = FWidgetReference();
	OnHoveredWidgetCleared.Broadcast();
}

void FWidgetBlueprintEditor::SetHoveredWidget(FWidgetReference& InHoveredWidget)
{
	if (InHoveredWidget != HoveredWidget)
	{
		HoveredWidget = InHoveredWidget;
		OnHoveredWidgetSet.Broadcast(InHoveredWidget);
	}
}

const FWidgetReference& FWidgetBlueprintEditor::GetHoveredWidget() const
{
	return HoveredWidget;
}

void FWidgetBlueprintEditor::AddPostDesignerLayoutAction(TFunction<void()> Action)
{
	QueuedDesignerActions.Add(MoveTemp(Action));
}

void FWidgetBlueprintEditor::OnEnteringDesigner()
{
	OnEnterWidgetDesigner.Broadcast();
}

TArray< TFunction<void()> >& FWidgetBlueprintEditor::GetQueuedDesignerActions()
{
	return QueuedDesignerActions;
}

EWidgetDesignFlags::Type FWidgetBlueprintEditor::GetCurrentDesignerFlags() const
{
	EWidgetDesignFlags::Type Flags = EWidgetDesignFlags::Designing;
	
	if ( GetDefault<UWidgetDesignerSettings>()->bShowOutlines )
	{
		Flags = ( EWidgetDesignFlags::Type )(Flags | EWidgetDesignFlags::ShowOutline);
	}

	return Flags;
}

class FObjectAndDisplayName
{
public:
	FObjectAndDisplayName(FText InDisplayName, UObject* InObject)
	{
		DisplayName = InDisplayName;
		Object = InObject;
	}

	bool operator<(FObjectAndDisplayName const& Other) const
	{
		return DisplayName.CompareTo(Other.DisplayName) < 0;
	}

	FText DisplayName;
	UObject* Object;

};

void GetBindableObjects(UWidget* RootWidget, TArray<FObjectAndDisplayName>& BindableObjects)
{
	TArray<UWidget*> ToTraverse;
	ToTraverse.Add(RootWidget);
	while (ToTraverse.Num() > 0)
	{
		UWidget* Widget = ToTraverse[0];
		ToTraverse.RemoveAt(0);
		BindableObjects.Add(FObjectAndDisplayName(FText::FromString(Widget->GetName()), Widget));

		UUserWidget* UserWidget = Cast<UUserWidget>(Widget);
		if (UserWidget != nullptr)
		{
			TArray<FName> SlotNames;
			UserWidget->GetSlotNames(SlotNames);
			for (FName SlotName : SlotNames)
			{
				UWidget* Content = UserWidget->GetContentForSlot(SlotName);
				if (Content != nullptr)
				{
					ToTraverse.Add(Content);
				}
			}
		}

		UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
		if (PanelWidget != nullptr)
		{
			for (UPanelSlot* Slot : PanelWidget->GetSlots())
			{
				if (Slot->Content != nullptr)
				{
					FText SlotDisplayName = FText::Format(LOCTEXT("AddMenuSlotFormat", "{0} ({1} Slot)"), FText::FromString(Slot->Content->GetName()), FText::FromString(PanelWidget->GetName()));
					BindableObjects.Add(FObjectAndDisplayName(SlotDisplayName, Slot));
					ToTraverse.Add(Slot->Content);
				}
			}
		}
	}
}

void FWidgetBlueprintEditor::OnGetAnimationAddMenuContent(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> InSequencer)
{
	TArray<FObjectAndDisplayName> BindableObjects;
	{
		GetBindableObjects(GetPreview()->GetRootWidget(), BindableObjects);
		BindableObjects.Sort();
	}

	if (CurrentAnimation.IsValid())
	{
		for (FObjectAndDisplayName& BindableObject : BindableObjects)
		{
			FGuid BoundObjectGuid = Sequencer->GetFocusedMovieSceneSequenceInstance()->FindObjectId(*BindableObject.Object);
			if (BoundObjectGuid.IsValid() == false)
			{
				FUIAction AddMenuAction(FExecuteAction::CreateSP(this, &FWidgetBlueprintEditor::AddObjectToAnimation, BindableObject.Object));
				MenuBuilder.AddMenuEntry(BindableObject.DisplayName, FText(), FSlateIcon(), AddMenuAction);
			}
		}
	}
}

void FWidgetBlueprintEditor::AddObjectToAnimation(UObject* ObjectToAnimate)
{
	const FScopedTransaction Transaction( LOCTEXT( "AddWidgetToAnimation", "Add widget to animation" ) );
	Sequencer->GetFocusedMovieSceneSequence()->Modify();

	// @todo Sequencer - Make this kind of adding more explicit, this current setup seem a bit brittle.
	Sequencer->GetHandleToObject(ObjectToAnimate);
}

TSharedRef<FExtender> FWidgetBlueprintEditor::GetAddTrackSequencerExtender( const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> ContextSensitiveObjects )
{
	TSharedRef<FExtender> AddTrackMenuExtender( new FExtender() );
	AddTrackMenuExtender->AddMenuExtension(
		SequencerMenuExtensionPoints::AddTrackMenu_PropertiesSection,
		EExtensionHook::Before,
		CommandList,
		FMenuExtensionDelegate::CreateRaw( this, &FWidgetBlueprintEditor::ExtendSequencerAddTrackMenu, ContextSensitiveObjects ) );
	return AddTrackMenuExtender;
}

TSharedRef<FExtender> FWidgetBlueprintEditor::GetObjectBindingContextMenuExtender(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> ContextSensitiveObjects)
{
	TSharedRef<FExtender> ObjectBindingMenuExtender(new FExtender());

	ObjectBindingMenuExtender->AddMenuExtension(
		"Edit",
		EExtensionHook::First,
		CommandList,
		FMenuExtensionDelegate::CreateRaw(this, &FWidgetBlueprintEditor::ExtendSequencerObjectBindingMenu, ContextSensitiveObjects));
	return ObjectBindingMenuExtender;
}

void FWidgetBlueprintEditor::ExtendSequencerAddTrackMenu( FMenuBuilder& AddTrackMenuBuilder, const TArray<UObject*> ContextObjects )
{
	if ( ContextObjects.Num() == 1 )
	{
		UWidget* Widget = Cast<UWidget>( ContextObjects[0] );
		if ( Widget != nullptr )
		{
			if( Widget->GetParent() != nullptr && Widget->Slot != nullptr )
			{
				AddTrackMenuBuilder.BeginSection( "Slot", LOCTEXT( "SlotSection", "Slot" ) );
				{
					FUIAction AddSlotAction( FExecuteAction::CreateRaw( this, &FWidgetBlueprintEditor::AddSlotTrack, Widget->Slot ) );
					FText AddSlotLabel = FText::Format(LOCTEXT("SlotLabelFormat", "{0} Slot"), FText::FromString(Widget->GetParent()->GetName()));
					FText AddSlotToolTip = FText::Format(LOCTEXT("SlotToolTipFormat", "Add {0} slot"), FText::FromString( Widget->GetParent()->GetName()));
					AddTrackMenuBuilder.AddMenuEntry(AddSlotLabel, AddSlotToolTip, FSlateIcon(), AddSlotAction);
				}
				AddTrackMenuBuilder.EndSection();
			}

			TArray<TArray<UProperty*>> MaterialBrushPropertyPaths;
			WidgetMaterialTrackUtilities::GetMaterialBrushPropertyPaths( Widget, MaterialBrushPropertyPaths );
			if ( MaterialBrushPropertyPaths.Num() > 0 )
			{
				AddTrackMenuBuilder.BeginSection( "Materials", LOCTEXT( "MaterialsSection", "Materials" ) );
				{
					for ( TArray<UProperty*>& MaterialBrushPropertyPath : MaterialBrushPropertyPaths )
					{
						FString DisplayName = MaterialBrushPropertyPath[0]->GetDisplayNameText().ToString();
						for ( int32 i = 1; i < MaterialBrushPropertyPath.Num(); i++)
						{
							DisplayName.AppendChar( '.' );
							DisplayName.Append( MaterialBrushPropertyPath[i]->GetDisplayNameText().ToString() );
						}
						FText DisplayNameText = FText::FromString( DisplayName );
						FUIAction AddMaterialAction( FExecuteAction::CreateRaw( this, &FWidgetBlueprintEditor::AddMaterialTrack, Widget, MaterialBrushPropertyPath, DisplayNameText ) );
						FText AddMaterialLabel = DisplayNameText;
						FText AddMaterialToolTip = FText::Format( LOCTEXT( "MaterialToolTipFormat", "Add a material track for the {0} property." ), DisplayNameText );
						AddTrackMenuBuilder.AddMenuEntry( AddMaterialLabel, AddMaterialToolTip, FSlateIcon(), AddMaterialAction );
					}
				}
				AddTrackMenuBuilder.EndSection();
			}
		}
	}
}

void FWidgetBlueprintEditor::ReplaceTrackWithSelectedWidget(FWidgetReference SelectedWidget, UWidget* BoundWidget)
{
	const FScopedTransaction Transaction( LOCTEXT( "ReplaceTrackWithSelectedWidget", "Replace Track with Selected Widget" ) );

	UWidgetAnimation* WidgetAnimation = Cast<UWidgetAnimation> (GetSequencer().Get()->GetFocusedMovieSceneSequence());
	UMovieScene* MovieScene = WidgetAnimation->GetMovieScene();
	UWidget* PreviewWidget = SelectedWidget.GetPreview();
	UWidget* TemplateWidget = SelectedWidget.GetTemplate();
	FGuid ObjectId = WidgetAnimation->FindPossessableObjectId(*BoundWidget);

	// Try find if the SelectedWidget is already bound, if so return
	FGuid SelectedWidgetId = WidgetAnimation->FindPossessableObjectId(*PreviewWidget);
	if (SelectedWidgetId.IsValid())
	{
		FNotificationInfo Info(LOCTEXT("SelectedWidgetalreadybound", "Selected Widget already bound"));
		Info.FadeInDuration = 0.1f;
		Info.FadeOutDuration = 0.5f;
		Info.ExpireDuration = 2.5f;
		auto NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

		NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		NotificationItem->ExpireAndFadeout();
		return;
	}

	if (TemplateWidget->GetClass() != BoundWidget->GetClass())
	{
		TArray<FMovieSceneBinding> MovieSceneBindings = MovieScene->GetBindings();
		for (FMovieSceneBinding Binding : MovieSceneBindings)
		{
			if(ObjectId == Binding.GetObjectGuid())
			{
				TArray<UMovieSceneTrack*> MovieSceneTracks = Binding.GetTracks();
				for (UMovieSceneTrack* Track : MovieSceneTracks)
				{
					UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track);
					if (PropertyTrack)
					{
						FString NameString = "Set" + PropertyTrack->GetPropertyName().ToString();
						FName FunctionName = FName(*NameString);
						if (!SelectedWidget.GetTemplate()->FindFunction(FunctionName))
						{
							// Exists a track that's not compatible 
							FNotificationInfo Info(LOCTEXT("IncompatibleTrackToReplaceWith", "Selected Widget doesn't match to a Property this track is bound to"));
							Info.FadeInDuration = 0.1f;
							Info.FadeOutDuration = 0.5f;
							Info.ExpireDuration = 2.5f;
							auto NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

							NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
							NotificationItem->ExpireAndFadeout();
							return;
						}
					}
				}
			}
		}
	}

	// else it's safe to modify
	MovieScene->Modify();
	MovieScene->SetObjectDisplayName(ObjectId, FText::FromString(PreviewWidget->GetName()));
	
	FMovieScenePossessable NewPossessable(PreviewWidget->GetName(), PreviewWidget->GetClass());
	FGuid PossessableGuid = NewPossessable.GetGuid();

	MovieScene->ReplacePossessable(ObjectId, PossessableGuid, PreviewWidget->GetName());

	UObject* BindingContext = GetSequencer().Get()->GetPlaybackContext();

	// Replace bindings in WidgetAnimation
	WidgetAnimation->Modify();
	{
		TArray<FWidgetAnimationBinding> NewBindings;
		for (FWidgetAnimationBinding Binding : WidgetAnimation->AnimationBindings)
		{
			if (Binding.WidgetName == BoundWidget->GetFName())
			{
				FWidgetAnimationBinding NewBinding;
				{
					if (Binding.SlotWidgetName == NAME_None)
					{
						NewBinding.AnimationGuid = PossessableGuid;
					}
					else
					{
						NewBinding.AnimationGuid = Binding.AnimationGuid;
					}
					NewBinding.WidgetName = PreviewWidget->GetFName();
					NewBinding.SlotWidgetName = Binding.SlotWidgetName;
					NewBinding.bIsRootWidget = Binding.bIsRootWidget;
				}
				NewBindings.Add(NewBinding);
			}
		}
		WidgetAnimation->AnimationBindings.RemoveAll([&](const FWidgetAnimationBinding& Binding) {
			return Binding.WidgetName == BoundWidget->GetFName();
		});
		for (FWidgetAnimationBinding Binding : NewBindings)
		{
			WidgetAnimation->AnimationBindings.Add(Binding);
		}
	}
	WidgetAnimation->ReplacePossessableObject(ObjectId, PossessableGuid, *BoundWidget, *PreviewWidget);

	GetSequencer().Get()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
}

void FWidgetBlueprintEditor::ExtendSequencerObjectBindingMenu(FMenuBuilder& ObjectBindingMenuBuilder, const TArray<UObject*> ContextObjects)
{
	FWidgetReference SelectedWidget;
	if (SelectedWidgets.Num() == 1)
	{
		for (FWidgetReference Widget : SelectedWidgets)
		{
			SelectedWidget = Widget;
		}
	}
	if (SelectedWidget.IsValid())
	{
		UWidget* BoundWidget = Cast<UWidget>(ContextObjects[0]);
		if (BoundWidget)
		{
			FUIAction ReplaceWithMenuAction(FExecuteAction::CreateRaw(this, &FWidgetBlueprintEditor::ReplaceTrackWithSelectedWidget, SelectedWidget, BoundWidget));

			FText ReplaceWithLabel = FText::Format(LOCTEXT("ReplaceObject", "Replace with {0}"), FText::FromString(SelectedWidget.GetPreview()->GetName()));
			FText ReplaceWithToolTip = FText::Format(LOCTEXT("ReplaceObjectToolTip", "Replace the widget in this animation with selected"), FText::FromString(SelectedWidget.GetPreview()->GetName()));

			ObjectBindingMenuBuilder.AddMenuEntry(ReplaceWithLabel, ReplaceWithToolTip, FSlateIcon(), ReplaceWithMenuAction);
			ObjectBindingMenuBuilder.AddMenuSeparator();
		}
	}
}

void FWidgetBlueprintEditor::AddSlotTrack( UPanelSlot* Slot )
{
	GetSequencer()->GetHandleToObject( Slot );
}

void FWidgetBlueprintEditor::AddMaterialTrack( UWidget* Widget, TArray<UProperty*> MaterialPropertyPath, FText MaterialPropertyDisplayName )
{
	FGuid WidgetHandle = Sequencer->GetHandleToObject( Widget );
	if ( WidgetHandle.IsValid() )
	{
		UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
		TArray<FName> MaterialPropertyNamePath;
		for ( UProperty* Property : MaterialPropertyPath )
		{
			MaterialPropertyNamePath.Add( Property->GetFName() );
		}
		if( MovieScene->FindTrack( UMovieSceneWidgetMaterialTrack::StaticClass(), WidgetHandle, WidgetMaterialTrackUtilities::GetTrackNameFromPropertyNamePath( MaterialPropertyNamePath ) ) == nullptr)
		{
			const FScopedTransaction Transaction( LOCTEXT( "AddWidgetMaterialTrack", "Add widget material track" ) );

			MovieScene->Modify();

			UMovieSceneWidgetMaterialTrack* NewTrack = Cast<UMovieSceneWidgetMaterialTrack>( MovieScene->AddTrack( UMovieSceneWidgetMaterialTrack::StaticClass(), WidgetHandle ) );
			NewTrack->Modify();
			NewTrack->SetBrushPropertyNamePath( MaterialPropertyNamePath );
			NewTrack->SetDisplayName( FText::Format( LOCTEXT( "TrackDisplayNameFormat", "{0} Material"), MaterialPropertyDisplayName ) );

			Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded );
		}
	}
}

void FWidgetBlueprintEditor::OnMovieSceneDataChanged()
{
	bRefreshGeneratedClassAnimations = true;
}

#undef LOCTEXT_NAMESPACE
