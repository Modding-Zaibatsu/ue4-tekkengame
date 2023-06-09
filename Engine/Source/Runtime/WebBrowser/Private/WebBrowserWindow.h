// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_CEF3

#include "IWebBrowserWindow.h"
#include "WebBrowserHandler.h"
#include "WebJSScripting.h"
#include "SlateCore.h"
#include "SlateBasics.h"
#include "CoreUObject.h"

#if PLATFORM_WINDOWS
	#include "AllowWindowsPlatformTypes.h"
#endif

#pragma push_macro("OVERRIDE")
#undef OVERRIDE // cef headers provide their own OVERRIDE macro
#include "include/internal/cef_ptr.h"
#include "include/cef_render_handler.h"
#include "include/cef_jsdialog_handler.h"
#pragma pop_macro("OVERRIDE")

#if PLATFORM_WINDOWS
	#include "HideWindowsPlatformTypes.h"
#endif

class FBrowserBufferedVideo;
/**
 * Helper for containing items required for CEF browser window creation.
 */
struct FWebBrowserWindowInfo
{
	FWebBrowserWindowInfo(CefRefPtr<CefBrowser> InBrowser, CefRefPtr<FWebBrowserHandler> InHandler) 
		: Browser(InBrowser)
		, Handler(InHandler)
	{}
	CefRefPtr<CefBrowser> Browser;
	CefRefPtr<FWebBrowserHandler> Handler;
};

/**
 * Implementation of interface for dealing with a Web Browser window.
 */
class FWebBrowserWindow
	: public IWebBrowserWindow
	, public TSharedFromThis<FWebBrowserWindow>
{
	// Allow the Handler to access functions only it needs
	friend class FWebBrowserHandler;

	// The WebBrowserSingleton should be the only one creating instances of this class
	friend class FWebBrowserSingleton;

private:
	/**
	 * Creates and initializes a new instance.
	 * 
	 * @param InBrowser The CefBrowser object representing this browser window.
	 * @param InUrl The Initial URL that will be loaded.
	 * @param InContentsToLoad Optional string to load as a web page.
	 * @param InShowErrorMessage Whether to show an error message in case of loading errors.
	 */
	FWebBrowserWindow(CefRefPtr<CefBrowser> InBrowser, CefRefPtr<FWebBrowserHandler> InHandler, FString InUrl, TOptional<FString> InContentsToLoad, bool ShowErrorMessage, bool bThumbMouseButtonNavigation, bool bUseTransparency);

public:
	/** Virtual Destructor. */
	virtual ~FWebBrowserWindow();

	bool IsShowingErrorMessages() { return ShowErrorMessage; }
	bool IsThumbMouseButtonNavigationEnabled() { return bThumbMouseButtonNavigation; }
	bool UseTransparency() { return bUseTransparency; }

public:

	// IWebBrowserWindow Interface

	virtual void LoadURL(FString NewURL) override;
	virtual void LoadString(FString Contents, FString DummyURL) override;
	virtual void SetViewportSize(FIntPoint WindowSize, FIntPoint WindowPos) override;
	virtual FSlateShaderResource* GetTexture(bool bIsPopup = false) override;
	virtual bool IsValid() const override;
	virtual bool IsInitialized() const override;
	virtual bool IsClosing() const override;
	virtual EWebBrowserDocumentState GetDocumentLoadingState() const override;
	virtual FString GetTitle() const override;
	virtual FString GetUrl() const override;
	virtual void GetSource(TFunction<void (const FString&)> Callback) const override;
	virtual bool OnKeyDown(const FKeyEvent& InKeyEvent) override;
	virtual bool OnKeyUp(const FKeyEvent& InKeyEvent) override;
	virtual bool OnKeyChar(const FCharacterEvent& InCharacterEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup) override;
	virtual void OnFocus(bool SetFocus, bool bIsPopup) override;
	virtual void OnCaptureLost() override;
	virtual bool CanGoBack() const override;
	virtual void GoBack() override;
	virtual bool CanGoForward() const override;
	virtual void GoForward() override;
	virtual bool IsLoading() const override;
	virtual void Reload() override;
	virtual void StopLoad() override;
	virtual void ExecuteJavascript(const FString& Script) override;
	virtual void CloseBrowser(bool bForce) override;
	virtual void BindUObject(const FString& Name, UObject* Object, bool bIsPermanent = true) override;
	virtual void UnbindUObject(const FString& Name, UObject* Object = nullptr, bool bIsPermanent = true) override;
	virtual int GetLoadError() override;
	virtual void SetIsDisabled(bool bValue) override;

	DECLARE_DERIVED_EVENT(FWebBrowserWindow, IWebBrowserWindow::FOnDocumentStateChanged, FOnDocumentStateChanged);
	virtual FOnDocumentStateChanged& OnDocumentStateChanged() override
	{
		return DocumentStateChangedEvent;
	}

	DECLARE_DERIVED_EVENT(FWebBrowserWindow, IWebBrowserWindow::FOnTitleChanged, FOnTitleChanged);
	virtual FOnTitleChanged& OnTitleChanged() override
	{
		return TitleChangedEvent;
	}

	DECLARE_DERIVED_EVENT(FWebBrowserWindow, IWebBrowserWindow::FOnUrlChanged, FOnUrlChanged);
	virtual FOnUrlChanged& OnUrlChanged() override
	{
		return UrlChangedEvent;
	}

	DECLARE_DERIVED_EVENT(FWebBrowserWindow, IWebBrowserWindow::FOnToolTip, FOnToolTip);
	virtual FOnToolTip& OnToolTip() override
	{
		return ToolTipEvent;
	}

	DECLARE_DERIVED_EVENT(FWebBrowserWindow, IWebBrowserWindow::FOnNeedsRedraw, FOnNeedsRedraw);
	virtual FOnNeedsRedraw& OnNeedsRedraw() override
	{
		return NeedsRedrawEvent;
	}
	
	virtual FOnBeforeBrowse& OnBeforeBrowse() override
	{
		return BeforeBrowseDelegate;
	}
	
	virtual FOnLoadUrl& OnLoadUrl() override
	{
		return LoadUrlDelegate;
	}

	virtual FOnCreateWindow& OnCreateWindow() override
	{
		return WebBrowserHandler->OnCreateWindow();
	}

	virtual FOnCloseWindow& OnCloseWindow() override
	{
		return CloseWindowDelegate;
	}

	virtual FCursorReply OnCursorQuery( const FGeometry& MyGeometry, const FPointerEvent& CursorEvent ) override
	{
		return Cursor == EMouseCursor::Default ? FCursorReply::Unhandled() : FCursorReply::Cursor(Cursor);
	}

	virtual FOnBeforePopupDelegate& OnBeforePopup() override
	{
		return WebBrowserHandler->OnBeforePopup();
	}

	DECLARE_DERIVED_EVENT(FWebBrowserWindow, IWebBrowserWindow::FOnShowPopup, FOnShowPopup);
	virtual FOnShowPopup& OnShowPopup() override
	{
		return ShowPopupEvent;
	}

	DECLARE_DERIVED_EVENT(FWebBrowserWindow, IWebBrowserWindow::FOnDismissPopup, FOnDismissPopup);
	virtual FOnDismissPopup& OnDismissPopup() override
	{
		return DismissPopupEvent;
	}

	virtual FOnShowDialog& OnShowDialog() override
	{
		return ShowDialogDelegate;
	}

	virtual FOnDismissAllDialogs& OnDismissAllDialogs() override
	{
		return DismissAllDialogsDelegate;
	}

private:

	/**
	 * Used to obtain the internal CEF browser.
	 *
	 * @return The bound CEF browser.
	 */
	CefRefPtr<CefBrowser> GetCefBrowser();

	/**
	 * Sets the Title of this window.
	 * 
	 * @param InTitle The new title of this window.
	 */
	void SetTitle(const CefString& InTitle);

	/**
	 * Sets the url of this window.
	 * 
	 * @param InUrl The new url of this window.
	 */
	void SetUrl(const CefString& InUrl);

	/**
	 * Sets the tool tip for this window.
	 * 
	 * @param InToolTip The text to show in the ToolTip. Empty string for no tool tip.
	 */
	void SetToolTip(const CefString& InToolTip);

	/**
	 * Get the current proportions of this window.
	 *
	 * @param Rect Reference to CefRect to store sizes.
	 * @return Whether Rect was set up correctly.
	 */
	bool GetViewRect(CefRect& Rect);

	/** Notifies clients that document loading has failed. */
	void NotifyDocumentError(int ErrorCode);

	/**
	 * Notifies clients that the loading state of the document has changed.
	 *
	 * @param IsLoading Whether the document is loading (false = completed).
	 */
	void NotifyDocumentLoadingStateChange(bool IsLoading);

	/**
	 * Called when there is an update to the rendered web page.
	 *
	 * @param Type Paint type.
	 * @param DirtyRects List of image areas that have been changed.
	 * @param Buffer Pointer to the raw texture data.
	 * @param Width Width of the texture.
	 * @param Height Height of the texture.
	 */
	void OnPaint(CefRenderHandler::PaintElementType Type, const CefRenderHandler::RectList& DirtyRects, const void* Buffer, int Width, int Height);
	
	/**
	 * Called when cursor would change due to web browser interaction.
	 * 
	 * @param Cursor Handle to CEF mouse cursor.
	 */
	void OnCursorChange(CefCursorHandle Cursor, CefRenderHandler::CursorType Type, const CefCursorInfo& CustomCursorInfo);

	/**
	 * Called when a message was received from the renderer process.
	 *
	 * @param Browser The CefBrowser for this window.
	 * @param SourceProcess The process id of the sender of the message. Currently always PID_RENDERER.
	 * @param Message The actual message.
	 * @return true if the message was handled, else false.
	 */
	bool OnProcessMessageReceived(CefRefPtr<CefBrowser> Browser, CefProcessId SourceProcess, CefRefPtr<CefProcessMessage> Message);

	/**
	 * Called before browser navigation.
	 *
	 * @param Browser The CefBrowser for this window.
	 * @param Frame The CefFrame the request came from.
	 * @param Request The CefRequest containing web request info.
	 * @param bIsRedirect true if the navigation was a result of redirection, false otherwise.
	 * @return true if the navigation was handled and no further processing of the navigation request should be disabled, false if the navigation should be handled by the default CEF implementation.
	 */
	bool OnBeforeBrowse(CefRefPtr<CefBrowser> Browser, CefRefPtr<CefFrame> Frame, CefRefPtr<CefRequest> Request, bool bIsRedirect);
	
	/**
	 * Called before loading a resource to allow overriding the content for a request.
	 *
	 * @return string content representing the content to show for the URL or an unset value to fetch the URL normally.
	 */
	TOptional<FString> GetResourceContent( CefRefPtr< CefFrame > Frame, CefRefPtr< CefRequest > Request);

	/** 
	 * Called when browser reports a key event that was not handled by it
	 */
	bool OnUnhandledKeyEvent(const CefKeyEvent& CefEvent);

	/**
	 * Handle showing javascript dialogs
	 */
	bool OnJSDialog(CefJSDialogHandler::JSDialogType DialogType, const CefString& MessageText, const CefString& DefaultPromptText, CefRefPtr<CefJSDialogCallback> Callback, bool& OutSuppressMessage);

	/**
	 * Handle showing unload confirmation dialogs
	 */
	bool OnBeforeUnloadDialog(const CefString& MessageText, bool IsReload, CefRefPtr<CefJSDialogCallback> Callback);

	/**
	 * Notify when any and all pending dialogs should be canceled
	 */
	void OnResetDialogState();

	/**
	 * Called when render process was terminated abnormally.
	 */
	void OnRenderProcessTerminated(CefRequestHandler::TerminationStatus Status);


	/** Called when the browser requests a new UI window
	 *
	 * @param NewBrowserWindow The web browser window to display in the new UI window.
	 * @param BrowserPopupFeatures The popup features and settings for the browser window.
	 * @return true if the UI window was created, false otherwise.
	 */
	bool RequestCreateWindow(const TSharedRef<IWebBrowserWindow>& NewBrowserWindow, const TSharedPtr<IWebBrowserPopupFeatures>& BrowserPopupFeatures);
	
	//bool SupportsCloseWindows();
	//bool RequestCloseWindow(const TSharedRef<IWebBrowserWindow>& BrowserWindow);


	/**
	 * Called once the browser begins closing procedures.
	 */
	void OnBrowserClosing();
	
	/**
	 * Called once the browser is closed.
	 */
	void OnBrowserClosed();

	/**
	 * Called to set the popup menu location. Note that CEF also passes a size to this method, 
	 * which is ignored as the correct size is usually not known until inside OnPaint.
	 *
 	 * @param PopupSize The location of the popup widget.
	 */
	void SetPopupMenuPosition(CefRect PopupSize);

	/**
	 * Called to request that the popup widget is shown or hidden.
	 *
 	 * @param bShow true for showing the popup, false for hiding it.
	 */
	void ShowPopupMenu(bool bShow);

public:

	/**
	 * Gets the Cef Keyboard Modifiers based on a Key Event.
	 * 
	 * @param KeyEvent The Key event.
	 * @return Bits representing keyboard modifiers.
	 */
	static int32 GetCefKeyboardModifiers(const FKeyEvent& KeyEvent);

	/**
	 * Gets the Cef Mouse Modifiers based on a Mouse Event.
	 * 
	 * @param InMouseEvent The Mouse event.
	 * @return Bits representing mouse modifiers.
	 */
	static int32 GetCefMouseModifiers(const FPointerEvent& InMouseEvent);

	/**
	 * Gets the Cef Input Modifiers based on an Input Event.
	 * 
	 * @param InputEvent The Input event.
	 * @return Bits representing input modifiers.
	 */
	static int32 GetCefInputModifiers(const FInputEvent& InputEvent);

public:

	/**
	 * Called from the WebBrowserSingleton tick event. Should test wether the widget got a tick from Slate last frame and set the state to hidden if not.
	 */
	void CheckTickActivity();

	/**
	* Called from the engine tick.
	*/
	void UpdateVideoBuffering();

	/**
	 * Called on every browser window when CEF launches a new render process.
	 * Used to ensure global JS objects are registered as soon as possible.
	 */
	CefRefPtr<CefDictionaryValue> GetProcessInfo();

private:
	/** Helper that calls WasHidden on the CEF host object when the value changes */
	void SetIsHidden(bool bValue);

	/** Used by the key down and up handlers to convert Slate key events to the CEF equivalent. */
	void PopulateCefKeyEvent(const FKeyEvent& InKeyEvent, CefKeyEvent& OutKeyEvent);

	/** Used to convert a FPointerEvent to a CefMouseEvent */
	CefMouseEvent GetCefMouseEvent(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsPopup);

private:

	/** Current state of the document being loaded. */
	EWebBrowserDocumentState DocumentState;

	/** Interface to the texture we are rendering to. */
	FSlateUpdatableTexture* UpdatableTextures[2];

	/** Pointer to the CEF Browser for this window. */
	CefRefPtr<CefBrowser> InternalCefBrowser;

	/** Pointer to the CEF handler for this window. */
	CefRefPtr<FWebBrowserHandler> WebBrowserHandler;

	/** Current title of this window. */
	FString Title;

	/** Current Url of this window. */
	FString CurrentUrl;

	/** Current tool tip. */
	FString ToolTipText;

	/** Current size of this window. */
	FIntPoint ViewportSize;

	/** Whether this window is closing. */
	bool bIsClosing;

	/** Whether this window has been painted at least once. */
	bool bIsInitialized;

	/** Optional text to load as a web page. */
	TOptional<FString> ContentsToLoad;

	/** Delegate for broadcasting load state changes. */
	FOnDocumentStateChanged DocumentStateChangedEvent;

	/** Whether to show an error message in case of loading errors. */
	bool ShowErrorMessage;

	/** Whether to allow forward and back navigation via the mouse thumb buttons. */
	bool bThumbMouseButtonNavigation;

	/** Whether transparency is enabled. */
	bool bUseTransparency;

	/** Delegate for broadcasting title changes. */
	FOnTitleChanged TitleChangedEvent;

	/** Delegate for broadcasting address changes. */
	FOnUrlChanged UrlChangedEvent;

	/** Delegate for showing or hiding tool tips. */
	FOnToolTip ToolTipEvent;

	/** Delegate for notifying that the window needs refreshing. */
	FOnNeedsRedraw NeedsRedrawEvent;

	/** Delegate that is executed prior to browser navigation. */
	FOnBeforeBrowse BeforeBrowseDelegate;

	/** Delegate for overriding Url contents. */
	FOnLoadUrl LoadUrlDelegate;

	/** Delegate for handling requests to close new windows that were created. */
	FOnCloseWindow CloseWindowDelegate;

	/** Delegate for handling requests to show the popup menu. */
	FOnShowPopup ShowPopupEvent;

	/** Delegate for handling requests to dismiss the current popup menu. */
	FOnDismissPopup DismissPopupEvent;

	/** Delegate for showing dialogs. */
	FOnShowDialog ShowDialogDelegate;

	/** Delegate for dismissing all dialogs. */
	FOnDismissAllDialogs DismissAllDialogsDelegate;

	/** Tracks the current mouse cursor */
	EMouseCursor::Type Cursor;

	/** Tracks wether the widget is currently disabled or not*/
	bool bIsDisabled;

	/** Tracks wether the widget is currently hidden or not*/
	bool bIsHidden;

	/** Used to detect when the widget is hidden*/
	bool bTickedLastFrame;

	/** Used for unhandled key events forwarding*/
	TOptional<FKeyEvent> PreviousKeyDownEvent;
	TOptional<FKeyEvent> PreviousKeyUpEvent;
	TOptional<FCharacterEvent> PreviousCharacterEvent;
	bool bIgnoreKeyDownEvent;
	bool bIgnoreKeyUpEvent;
	bool bIgnoreCharacterEvent;

	/** Used to ignore any popup menus when forwarding focus gained/lost events*/
	bool bMainHasFocus;
	bool bPopupHasFocus;

	FIntPoint PopupPosition;
	bool bShowPopupRequested;

	/** This is set to true when reloading after render process crash. */
	bool bRecoverFromRenderProcessCrash;

	int ErrorCode;

	TUniquePtr<FBrowserBufferedVideo> BufferedVideo;

	/** Handling of passing and marshalling messages for JS integration is delegated to a helper class*/
	TSharedPtr<FWebJSScripting> Scripting;
};

#endif
