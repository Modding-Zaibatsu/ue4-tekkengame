// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.
 
#include "SlatePrivatePCH.h"
#include "LayoutUtils.h"
#include "Menu.h"

static FVector2D GetMenuOffsetForPlacement(const FGeometry& AllottedGeometry, EMenuPlacement PlacementMode, FVector2D PopupSizeLocalSpace)
{
	switch (PlacementMode)
	{
		case MenuPlacement_BelowAnchor:
			return FVector2D(0.0f, AllottedGeometry.GetLocalSize().Y);
			break;
		case MenuPlacement_CenteredBelowAnchor:
			return FVector2D(-((PopupSizeLocalSpace.X / 2) - (AllottedGeometry.GetLocalSize().X / 2)), AllottedGeometry.GetLocalSize().Y);
			break;
		case MenuPlacement_BelowRightAnchor:
			return FVector2D( -( PopupSizeLocalSpace.X ) - ( AllottedGeometry.GetLocalSize().X ), AllottedGeometry.GetLocalSize().Y );
			break;
		case MenuPlacement_ComboBox:
			return FVector2D(0.0f, AllottedGeometry.GetLocalSize().Y);
			break;
		case MenuPlacement_ComboBoxRight:
			return FVector2D(AllottedGeometry.GetLocalSize().X - PopupSizeLocalSpace.X, AllottedGeometry.GetLocalSize().Y);
			break;
		case MenuPlacement_MenuRight:
			return FVector2D(AllottedGeometry.GetLocalSize().X, 0.0f);
			break;
		case MenuPlacement_AboveAnchor:
			return FVector2D(0.0f, -PopupSizeLocalSpace.Y);
			break;
		case MenuPlacement_CenteredAboveAnchor:
			return FVector2D(-((PopupSizeLocalSpace.X / 2) - (AllottedGeometry.GetLocalSize().X / 2)), -PopupSizeLocalSpace.Y);
			break;
		case MenuPlacement_AboveRightAnchor:
			return FVector2D( -( PopupSizeLocalSpace.X ) - ( AllottedGeometry.GetLocalSize().X ), -PopupSizeLocalSpace.Y );
			break;
		case MenuPlacement_MenuLeft:
			return FVector2D(-PopupSizeLocalSpace.X, 0.0f);
			break;
		case MenuPlacement_Center:
			return FVector2D( -( ( PopupSizeLocalSpace.X / 2 ) - ( AllottedGeometry.GetLocalSize().X / 2 ) ), -( ( PopupSizeLocalSpace.Y / 2 ) - ( AllottedGeometry.GetLocalSize().Y / 2 ) ) );
			break;
		case MenuPlacement_RightLeftCenter:
			return FVector2D( AllottedGeometry.GetLocalSize().X, - ( ( PopupSizeLocalSpace.Y / 2 ) - ( AllottedGeometry.GetLocalSize().Y / 2 ) ) );
			break;
		default:
			ensureMsgf( false, TEXT("Unhandled placement mode: %d"), PlacementMode );
			return FVector2D::ZeroVector;
	}
}

/** Compute the popup size, offset, and anchor rect in local space. */
struct FPopupPlacement
{
	FPopupPlacement(const FGeometry& AllottedGeometry, const FVector2D& PopupDesiredSize, EMenuPlacement PlacementMode)
	{
		// Compute the popup size, offset, and anchor rect  in local space
		LocalPopupSize = (PlacementMode == MenuPlacement_ComboBox || PlacementMode == MenuPlacement_ComboBoxRight)
			? FVector2D(FMath::Max(AllottedGeometry.Size.X, PopupDesiredSize.X), PopupDesiredSize.Y)
			: PopupDesiredSize;
		LocalPopupOffset = GetMenuOffsetForPlacement(AllottedGeometry, PlacementMode, LocalPopupSize);
		AnchorLocalSpace = FSlateRect::FromPointAndExtent(FVector2D::ZeroVector, AllottedGeometry.Size);
		Orientation = (PlacementMode == MenuPlacement_MenuRight || PlacementMode == MenuPlacement_MenuLeft) ? Orient_Horizontal : Orient_Vertical;
	}

	FVector2D LocalPopupSize;
	FVector2D LocalPopupOffset;
	FSlateRect AnchorLocalSpace;
	EOrientation Orientation;
};

FGeometry ComputeMenuPlacement(const FGeometry& AllottedGeometry, const FVector2D& PopupDesiredSize, EMenuPlacement PlacementMode)
{
	// Compute the popup size, offset, and anchor rect  in local space
	const FPopupPlacement Placement(AllottedGeometry, PopupDesiredSize, PlacementMode);

	// ask the application to compute the proper desktop offset for the anchor. This requires the offsets to be in desktop space.
	const FVector2D NewPositionDesktopSpace = FSlateApplication::Get().CalculatePopupWindowPosition(
		TransformRect(AllottedGeometry.GetAccumulatedLayoutTransform(), Placement.AnchorLocalSpace),
		TransformVector(AllottedGeometry.GetAccumulatedLayoutTransform(), Placement.LocalPopupSize),
		Placement.Orientation);

	// transform the desktop offset into local space and use that as the layout transform for the child content.
	return AllottedGeometry.MakeChild(
		Placement.LocalPopupSize,
		FSlateLayoutTransform(TransformPoint(Inverse(AllottedGeometry.GetAccumulatedLayoutTransform()), NewPositionDesktopSpace)));
}

/*static*/ TArray<TWeakPtr<IMenu>> SMenuAnchor::OpenApplicationMenus;

/**
 * Construct this widget
 *
 * @param	InArgs	The declaration data for this widget
 */
void SMenuAnchor::Construct( const FArguments& InArgs )
{
	Children.Add( new FSimpleSlot() );
	Children.Add( new FSimpleSlot() );
	

	Children[0]
		.Padding(0)
		[
			InArgs._Content.Widget
		];

	MenuContent                            = InArgs._MenuContent;
	WrappedContent                         = InArgs._MenuContent;
	OnGetMenuContent                       = InArgs._OnGetMenuContent;
	OnMenuOpenChanged                      = InArgs._OnMenuOpenChanged;
	Placement                              = InArgs._Placement;
	Method                                 = InArgs._Method;
	bShouldDeferPaintingAfterWindowContent = InArgs._ShouldDeferPaintingAfterWindowContent;
	bUseApplicationMenuStack               = InArgs._UseApplicationMenuStack;
	bIsCollapsedByParent                   = InArgs._IsCollapsedByParent;
}


void SMenuAnchor::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	TSharedPtr<SWindow> PopupWindow = PopupWindowPtr.Pin();
	if ( PopupWindow.IsValid() && IsOpenViaCreatedWindow() )
	{
		// Figure out where our attached pop-up window should be placed.
		const FVector2D PopupContentDesiredSize = PopupWindow->GetContent()->GetDesiredSize();
		FGeometry PopupGeometry = ComputeMenuPlacement( AllottedGeometry, PopupContentDesiredSize, Placement.Get() );
		const FVector2D NewPosition = PopupGeometry.LocalToAbsolute(FVector2D::ZeroVector);
		// For the CreateWindow case, don't transform the size; it will always use the ApplicationScale
		const FVector2D NewSize = PopupGeometry.GetLocalSize();

		const FSlateRect NewShape = FSlateRect( NewPosition.X, NewPosition.Y, NewPosition.X + NewSize.X, NewPosition.Y + NewSize.Y );

		// We made a window for showing the popup.
		// Update the window's position!
		if (false /*PopupWindow->IsMorphing()*/ )
		{
			if( NewShape != PopupWindow->GetMorphTargetShape() )
			{
				// Update the target shape
				PopupWindow->UpdateMorphTargetShape( NewShape );
				// Set size immediately if not morphing size
				if(!PopupWindow->IsMorphingSize())
				{
					PopupWindow->ReshapeWindow( PopupWindow->GetPositionInScreen(), NewSize );
				}
			}
		}
		else
		{
			const FVector2D WindowPosition = PopupWindow->GetPositionInScreen();
			const FVector2D WindowSize = PopupWindow->GetSizeInScreen();
			if ( NewPosition != WindowPosition || NewSize != WindowSize )
			{
#if PLATFORM_LINUX
				// @FIXME: for some reason, popups reshaped here do not trigger OnWindowMoved() callback,
				// so we manually set cached position to where we expect it to be. Note the order of operations - (before Reshape) - 
				// still giving the callback a chance to change it.
				// This needs to be investigated (tracked as TTP #347674).
				PopupWindow->SetCachedScreenPosition( NewPosition );
#endif // PLATFORM_LINUX
				PopupWindow->ReshapeWindow( NewShape );
			}
		}
	}
	else if (PopupWindow.IsValid() && IsOpenAndReusingWindow())
	{
		// Ideally, do this in OnArrangeChildren(); currently not possible because OnArrangeChildren()
		// can be called in DesktopSpace or WindowSpace, and we will not know which version of the Window
		// geometry to use. Tick() is always in DesktopSpace, so cache the solution here and just use
		// it in OnArrangeChildren().
		const FPopupPlacement LocalPlacement(AllottedGeometry, Children[1].GetWidget()->GetDesiredSize(), Placement.Get());
		const FSlateRect WindowRectLocalSpace = TransformRect(Inverse(AllottedGeometry.GetAccumulatedLayoutTransform()), PopupWindow->GetClientRectInScreen());
		const FVector2D FittedPlacement = ComputePopupFitInRect(
			LocalPlacement.AnchorLocalSpace,
			FSlateRect(LocalPlacement.LocalPopupOffset, LocalPlacement.LocalPopupOffset + LocalPlacement.LocalPopupSize),
			LocalPlacement.Orientation, WindowRectLocalSpace);

		LocalPopupPosition = FittedPlacement;
		ScreenPopupPosition = AllottedGeometry.GetAccumulatedLayoutTransform().TransformPoint(LocalPopupPosition);
	}

	/** The tick is ending, so the window was not dismissed this tick. */
	bDismissedThisTick = false;
}

void SMenuAnchor::OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const
{
	ArrangeSingleChild( AllottedGeometry, ArrangedChildren, Children[0], FVector2D::UnitVector );
	const TSharedPtr<SWindow> PresentingWindow = PopupWindowPtr.Pin();
	if (IsOpenAndReusingWindow() && PresentingWindow.IsValid())
	{
		const FPopupPlacement LocalPlacement(AllottedGeometry, Children[1].GetWidget()->GetDesiredSize(), Placement.Get());
		ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(Children[1].GetWidget(), LocalPlacement.LocalPopupSize, FSlateLayoutTransform(LocalPopupPosition)));
	}
}

FVector2D SMenuAnchor::ComputeDesiredSize( float ) const
{
	const FVector2D DesiredWidgetSize = Children[0].GetWidget()->GetDesiredSize();

	// Menu anchors might be created with null content, in which case they must still get drawn in order to 
	// draw pop-up content, therefore it must lie and always request a desired size of at least 1,1, otherwise 
	// a panel may filter it from drawing thinking the it doesn't have anything to draw.
	return FVector2D(FMath::Max(DesiredWidgetSize.X, 1.0f), FMath::Max(DesiredWidgetSize.Y, 1.0f));
}

FChildren* SMenuAnchor::GetChildren()
{
	return &Children;
}

int32 SMenuAnchor::OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	FArrangedChildren ArrangedChildren( EVisibility::Visible );
	ArrangeChildren( AllottedGeometry, ArrangedChildren );
	
	// There may be zero elements in this array if our child collapsed/hidden
	if ( ArrangedChildren.Num() > 0 )
	{
		const FArrangedWidget& FirstChild = ArrangedChildren[0];

		// In the case where the user doesn't provide content to the menu anchor, the null widget
		// wont appear in the visible set of arranged children, so only immediately paint the first child,
		// if it's visible and matches the first slot content.
		const bool bHasArrangedAnchorContent = FirstChild.Widget == Children[0].GetWidget();
		if ( bHasArrangedAnchorContent )
		{
			const FSlateRect ChildClippingRect = AllottedGeometry.GetClippingRect().IntersectionWith(MyClippingRect);
			LayerId = FirstChild.Widget->Paint(Args.WithNewParent(this), FirstChild.Geometry, ChildClippingRect, OutDrawElements, LayerId + 1, InWidgetStyle, ShouldBeEnabled(bParentEnabled));
		}

		const bool bIsOpen = IsOpen();

		if ( bIsOpen )
		{
			// In the case where the anchor content is present and visible, it's the 1 index child, in the case
			// where the anchor content is invisible, it's the 0 index child.
			FArrangedWidget* PopupChild = nullptr;
			if ( bHasArrangedAnchorContent && ArrangedChildren.Num() > 1 )
			{
				PopupChild = &ArrangedChildren[1];
			}
			else if ( !bHasArrangedAnchorContent && ArrangedChildren.Num() == 1 )
			{
				PopupChild = &ArrangedChildren[0];
			}

			if ( PopupChild != nullptr )
			{
				if (bShouldDeferPaintingAfterWindowContent)
				{
					OutDrawElements.QueueDeferredPainting(
						FSlateWindowElementList::FDeferredPaint(PopupChild->Widget, Args, PopupChild->Geometry, MyClippingRect, InWidgetStyle, bParentEnabled));
				}
				else
				{
					const TSharedPtr<SWindow> PresentingWindow = PopupWindowPtr.Pin();
					if (PresentingWindow.IsValid())
					{
						PopupChild->Widget->Paint(Args.WithNewParent(this), PopupChild->Geometry, PresentingWindow->GetClippingRectangleInWindow(), OutDrawElements, LayerId + 1, InWidgetStyle, ShouldBeEnabled(bParentEnabled));
					}
				}
			}
		} 
	}

	return LayerId;
}

bool SMenuAnchor::IsOpenAndReusingWindow() const
{
	return MethodInUse.IsSet() && MethodInUse.GetPopupMethod() == EPopupMethod::UseCurrentWindow;
}

bool SMenuAnchor::IsOpenViaCreatedWindow() const
{
	return MethodInUse.IsSet() && MethodInUse.GetPopupMethod() == EPopupMethod::CreateNewWindow;
}

void SMenuAnchor::SetContent(TSharedRef<SWidget> InContent)
{
	Children[0]
	.Padding(0)
	[
		InContent
	];
}

void SMenuAnchor::SetMenuContent(TSharedRef<SWidget> InMenuContent)
{
	MenuContent = InMenuContent;
	WrappedContent = InMenuContent;	// wrapping, if any will happen when the menu is opened
}

FPopupMethodReply QueryPopupMethod(const FWidgetPath& PathToQuery)
{
	for (int32 WidgetIndex = PathToQuery.Widgets.Num() - 1; WidgetIndex >= 0; --WidgetIndex)
	{
		FPopupMethodReply PopupMethod = PathToQuery.Widgets[WidgetIndex].Widget->OnQueryPopupMethod();
		if (PopupMethod.IsEventHandled())
		{
			return PopupMethod;
		}
	}

	return FPopupMethodReply::UseMethod(EPopupMethod::CreateNewWindow);
}


/**
 * Open or close the popup
 *
 * @param InIsOpen  If true, open the popup. Otherwise close it.
 */
void SMenuAnchor::SetIsOpen( bool InIsOpen, const bool bFocusMenu, const int32 FocusUserIndex )
{
	// Prevent redundant opens/closes
	if ( IsOpen() != InIsOpen )
	{
		if ( InIsOpen )
		{
			if ( OnGetMenuContent.IsBound() )
			{
				SetMenuContent(OnGetMenuContent.Execute());
			}

			if ( MenuContent.IsValid() )
			{
				// OPEN POPUP
				if ( OnMenuOpenChanged.IsBound() )
				{
					OnMenuOpenChanged.Execute(true);
				}

				// Figure out where the menu anchor is on the screen, so we can set the initial position of our pop-up window
				// This can be called at any time so we use the push menu override that explicitly allows us to specify our parent
				// NOTE: Careful, GeneratePathToWidget can be reentrant in that it can call visibility delegates and such
				FWidgetPath MyWidgetPath;
				FSlateApplication::Get().GeneratePathToWidgetUnchecked(AsShared(), MyWidgetPath);
				if (MyWidgetPath.IsValid())
				{
					const FGeometry& MyGeometry = MyWidgetPath.Widgets.Last().Geometry;
					const float LayoutScaleMultiplier =  MyGeometry.GetAccumulatedLayoutTransform().GetScale();

					SlatePrepass(LayoutScaleMultiplier);

					// Figure out how big the content widget is so we can set the window's initial size properly
					TSharedRef< SWidget > MenuContentRef = MenuContent.ToSharedRef();
					MenuContentRef->SlatePrepass(LayoutScaleMultiplier);

					// Combo-boxes never size down smaller than the widget that spawned them, but all
					// other pop-up menus are currently auto-sized
					const FVector2D DesiredContentSize = MenuContentRef->GetDesiredSize();  // @todo slate: This is ignoring any window border size!
					const EMenuPlacement PlacementMode = Placement.Get();

					const FVector2D NewPosition = MyGeometry.AbsolutePosition;
					FVector2D NewWindowSize = DesiredContentSize;
					const FVector2D SummonLocationSize = MyGeometry.Size;

					FPopupTransitionEffect TransitionEffect( FPopupTransitionEffect::None );
					if ( PlacementMode == MenuPlacement_ComboBox || PlacementMode == MenuPlacement_ComboBoxRight )
					{
						TransitionEffect = FPopupTransitionEffect( FPopupTransitionEffect::ComboButton );
						NewWindowSize = FVector2D( FMath::Max( MyGeometry.Size.X, DesiredContentSize.X ), DesiredContentSize.Y );
					}
					else if ( PlacementMode == MenuPlacement_BelowAnchor )
					{
						TransitionEffect = FPopupTransitionEffect( FPopupTransitionEffect::TopMenu );
					}
					else if ( PlacementMode == MenuPlacement_MenuRight )
					{
						TransitionEffect = FPopupTransitionEffect( FPopupTransitionEffect::SubMenu );
					}

					MethodInUse = Method.IsSet()
						? FPopupMethodReply::UseMethod(Method.GetValue())
						: QueryPopupMethod(MyWidgetPath);

					// "Normal" menus are created and managed by the application's menu stack functions
					if (bUseApplicationMenuStack)
					{
						if (MethodInUse.GetPopupMethod() == EPopupMethod::CreateNewWindow)
						{
							// Open the pop-up
							TSharedPtr<IMenu> NewMenu = FSlateApplication::Get().PushMenu(AsShared(), MyWidgetPath, MenuContentRef, NewPosition, TransitionEffect, bFocusMenu, SummonLocationSize, MethodInUse.GetPopupMethod(), bIsCollapsedByParent);

							PopupMenuPtr = NewMenu;
							check(NewMenu.IsValid() && NewMenu->GetOwnedWindow().IsValid());
							NewMenu->GetOnMenuDismissed().AddSP(this, &SMenuAnchor::OnMenuClosed);
							PopupWindowPtr = NewMenu->GetOwnedWindow();
						}
						else
						{
							// We are re-using the current window instead of creating a new one.
							// The popup will be presented as a child of this widget.
							ensure(MethodInUse.GetPopupMethod() == EPopupMethod::UseCurrentWindow);
							PopupWindowPtr = MyWidgetPath.GetWindow();

							if (bFocusMenu)
							{
								FSlateApplication::Get().ReleaseMouseCaptureForUser(FocusUserIndex);
							}

							TSharedRef<SMenuAnchor> SharedThis = StaticCastSharedRef<SMenuAnchor>(AsShared());

							TSharedPtr<IMenu> NewMenu = FSlateApplication::Get().PushHostedMenu(
								SharedThis, MyWidgetPath, SharedThis, MenuContentRef, WrappedContent, TransitionEffect, MethodInUse.GetShouldThrottle(), bIsCollapsedByParent);

							PopupMenuPtr = NewMenu;
							check(NewMenu.IsValid());
							//check(NewMenu->GetParentWindow().ToSharedRef() == PopupWindow);
							check(WrappedContent.IsValid());

							Children[1]
							[
								WrappedContent.ToSharedRef()
							];

							if (bFocusMenu)
							{
								FSlateApplication::Get().SetUserFocus(FocusUserIndex, MenuContentRef, EFocusCause::SetDirectly);
							}
						}
					}
					else // !bUseApplicationMenuStack
					{
						// Anchor's menu doesn't participate in the application's menu stack.
						// Lifetime is managed by this anchor
						if (MethodInUse.GetPopupMethod() == EPopupMethod::CreateNewWindow)
						{
							// Start pop-up windows out transparent, then fade them in over time
#if ALPHA_BLENDED_WINDOWS
							const EWindowTransparency Transparency(EWindowTransparency::PerPixel);
#else
							const EWindowTransparency Transparency(EWindowTransparency::None);
#endif
							const float TargetWindowOpacity = 1.0f;
							FSlateRect Anchor(NewPosition, NewPosition + SummonLocationSize);
							EOrientation Orientation = (TransitionEffect.SlideDirection == FPopupTransitionEffect::SubMenu) ? Orient_Horizontal : Orient_Vertical;
		
							// @todo slate: Assumes that popup is not Scaled up or down from application scale.
							MenuContentRef->SlatePrepass(FSlateApplication::Get().GetApplicationScale());
							// @todo slate: Doesn't take into account potential window border size
							FVector2D ExpectedSize = MenuContentRef->GetDesiredSize();
							const FVector2D ScreenPosition = FSlateApplication::Get().CalculatePopupWindowPosition(Anchor, ExpectedSize, Orientation);

							// Release the mouse so that context can be properly restored upon closing menus.  See CL 1411833 before changing this.
							if (bFocusMenu)
							{
								FSlateApplication::Get().ReleaseMouseCaptureForUser(FocusUserIndex);
							}

							// Create a new window for the menu
							TSharedRef<SWindow> NewMenuWindow = SNew(SWindow)
								.Type(EWindowType::Menu)
								.IsPopupWindow(true)
								.SizingRule(ESizingRule::Autosized)
								.ScreenPosition(ScreenPosition)
								.AutoCenter(EAutoCenter::None)
								.ClientSize(ExpectedSize)
								.InitialOpacity(1.0f)
								.SupportsTransparency(Transparency)
								.FocusWhenFirstShown(bFocusMenu)
								.ActivateWhenFirstShown(bFocusMenu)
								[
									MenuContentRef
								];

							if (bFocusMenu)
							{
								// Focus the unwrapped content rather than just the window
								NewMenuWindow->SetWidgetToFocusOnActivate(MenuContentRef);
							}

							TSharedPtr<IMenu> NewMenu = MakeShareable(new FMenuInWindow(NewMenuWindow, MenuContentRef, bIsCollapsedByParent));
							FSlateApplication::Get().AddWindowAsNativeChild(NewMenuWindow, MyWidgetPath.GetWindow(), true);

							PopupMenuPtr = OwnedMenuPtr = NewMenu;
							check(NewMenu.IsValid());
							NewMenu->GetOnMenuDismissed().AddSP(this, &SMenuAnchor::OnMenuClosed);
							PopupWindowPtr = NewMenuWindow;
						}
						else
						{
							// We are re-using the current window instead of creating a new one.
							// The popup will be presented as a child of this widget.
							ensure(MethodInUse.GetPopupMethod() == EPopupMethod::UseCurrentWindow);
							PopupWindowPtr = MyWidgetPath.GetWindow();

							if (bFocusMenu)
							{
								FSlateApplication::Get().ReleaseMouseCaptureForUser(FocusUserIndex);
							}

							TSharedRef<SMenuAnchor> SharedThis = StaticCastSharedRef<SMenuAnchor>(AsShared());
							TSharedPtr<IMenu> NewMenu = MakeShareable(new FMenuInHostWidget(SharedThis, MenuContentRef, bIsCollapsedByParent));

							PopupMenuPtr = OwnedMenuPtr = NewMenu;
							check(NewMenu.IsValid());
							//check(NewMenu->GetParentWindow().ToSharedRef() == PopupWindow);

							Children[1]
							[
								MenuContentRef
							];

							if (bFocusMenu)
							{
								FSlateApplication::Get().SetUserFocus(FocusUserIndex, MenuContentRef, EFocusCause::SetDirectly);
							}

							OpenApplicationMenus.Add(NewMenu);
						}
					}
				}
			}
		}
		else
		{
			// CLOSE POPUP
			if (PopupMenuPtr.IsValid())
			{
				PopupMenuPtr.Pin()->Dismiss();
			}
			else
			{
				PopupWindowPtr.Reset();
				OwnedMenuPtr.Reset();
				MethodInUse = FPopupMethodReply::Unhandled();
			}

			// Always clear out the menu content children slot to prevent prepass and other hierarchy queries from considering the
			// hidden menu content as content they should be concerned with.
			Children[1]
			[
				SNullWidget::NullWidget
			];
		}
	}
}

void SMenuAnchor::OnMenuClosed(TSharedRef<IMenu> InMenu)
{
	bDismissedThisTick = true;
	MethodInUse = FPopupMethodReply::Unhandled();
	PopupMenuPtr.Reset();
	OwnedMenuPtr.Reset();
	PopupWindowPtr.Reset();

	if (OnMenuOpenChanged.IsBound())
	{
		OnMenuOpenChanged.Execute(false);
	}
}

bool SMenuAnchor::IsOpen() const
{
	return MethodInUse.IsSet() && PopupMenuPtr.IsValid();
}

bool SMenuAnchor::ShouldOpenDueToClick() const
{
	return !IsOpen() && !bDismissedThisTick;
}

FVector2D SMenuAnchor::GetMenuPosition() const
{
	FVector2D Pos(0,0);

	if (IsOpenViaCreatedWindow() && PopupWindowPtr.IsValid())
	{
		Pos = PopupWindowPtr.Pin()->GetPositionInScreen();
	}
	else if (IsOpenAndReusingWindow() && PopupMenuPtr.IsValid())
	{
		Pos = ScreenPopupPosition;
	}
	
	return Pos;
}

bool SMenuAnchor::HasOpenSubMenus() const
{
	bool Result = false;
	if (PopupMenuPtr.IsValid())
	{
		Result = FSlateApplication::Get().HasOpenSubMenus(PopupMenuPtr.Pin());
	}
	return Result;
}

TSharedPtr<SWindow> SMenuAnchor::GetMenuWindow() const
{
	return IsOpen() ? PopupWindowPtr.Pin() : TSharedPtr<SWindow>();
}

void SMenuAnchor::OnMenuDismissed()
{
	if (PopupMenuPtr.IsValid())
	{
		OnMenuClosed(PopupMenuPtr.Pin().ToSharedRef());
	}
}

/*static*/ void SMenuAnchor::DismissAllApplicationMenus()
{
	for (TWeakPtr<IMenu>& OpenMenu : OpenApplicationMenus)
	{
		TSharedPtr<IMenu> Iter = OpenMenu.IsValid() ? OpenMenu.Pin() : nullptr;
		if (Iter.IsValid())
		{
			Iter->Dismiss();
		}
	}

	OpenApplicationMenus.Empty();
}

SMenuAnchor::SMenuAnchor()
	: MenuContent( SNullWidget::NullWidget )
	, WrappedContent(SNullWidget::NullWidget)
	, bDismissedThisTick( false )
	, Method()
	, MethodInUse()
	, LocalPopupPosition( FVector2D::ZeroVector )
{
}

SMenuAnchor::~SMenuAnchor()
{
	if (PopupMenuPtr.IsValid())
	{
		PopupMenuPtr.Pin()->Dismiss();
	}
		
	// We no longer have a popup open, so reset all the tracking state associated.
	PopupMenuPtr.Reset();
	OwnedMenuPtr.Reset();
	PopupWindowPtr.Reset();
	MethodInUse = FPopupMethodReply::Unhandled();
}
