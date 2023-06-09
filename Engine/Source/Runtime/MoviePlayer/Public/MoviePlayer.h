// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine.h"
#include "ModuleInterface.h"
#include "SlateBasics.h"
#include "Slate/SlateTextures.h"

UENUM()
enum EMoviePlaybackType
{
	MT_Normal,				// Normal playback mode.  Play each movie in the play list a single time
	MT_Looped,				// Looped playback mode.  Play all movies in the play list in order then start over until manaully cancelled
	MT_LoadingLoop,			// Alternate Looped mode.  Play all of the movies in the play list and loop just the last movie until loading is finished.
	MT_MAX
};

/** This viewport is a simple interface for the loading to use to display the video textures. */
class FMovieViewport : public ISlateViewport, public TSharedFromThis<FMovieViewport>
{
public:
	FMovieViewport() {}
	~FMovieViewport() {}

	/* ISlateViewport interface. */
	virtual FIntPoint GetSize() const override
	{
		return SlateTexture.IsValid() ? FIntPoint(SlateTexture.Pin()->GetWidth(), SlateTexture.Pin()->GetHeight()) : FIntPoint();
	}

	virtual class FSlateShaderResource* GetViewportRenderTargetTexture() const override
	{
		return SlateTexture.IsValid() ? SlateTexture.Pin().Get() : NULL;
	}

	virtual bool RequiresVsync() const override
	{
		return false;
	}

	void SetTexture(TWeakPtr<FSlateTexture2DRHIRef, ESPMode::ThreadSafe> InTexture)
	{
		SlateTexture = InTexture;
	}

private:
	TWeakPtr<FSlateTexture2DRHIRef, ESPMode::ThreadSafe> SlateTexture;
};


/** Interface for creating a movie streaming player. Should be one instance per platform. */
class IMovieStreamer
{
public:
	/**
	 * Initializes this movie streamer with all the movie paths (ordered) we want to play
	 * Movie paths are local to the current game's Content/Movies/ directory.
	 */

	virtual bool Init(const TArray<FString>& MoviePaths, TEnumAsByte<EMoviePlaybackType> inPlaybackType) = 0;
	
	/** Forces the movie streamer to cancel what it's streaming and close. */
	virtual void ForceCompletion() = 0;

	/** Code run every tick for any additional per tick handling of playing the movie. Returns true if done. */
	virtual bool Tick(float DeltaTime) = 0;

	/** Gets a viewport interface which will be used to draw the movie. */
	virtual TSharedPtr<class ISlateViewport> GetViewportInterface() = 0;

	/** Gets the aspect ratio of the movie frames being streamed. */
	virtual float GetAspectRatio() const = 0;

	/** returns the name of the movie currently being played */
	virtual FString GetMovieName() = 0;

	/** returns true if the movie being played in the last one in the play list */
	virtual bool IsLastMovieInPlaylist() = 0;

	/** Called to allow the movie streamer to cleanup any resources once there are no movies left to play. */
	virtual void Cleanup() = 0;

	virtual ~IMovieStreamer() {}
};



/** Struct of all the attributes a loading screen will have. */
struct MOVIEPLAYER_API FLoadingScreenAttributes
{
	FLoadingScreenAttributes()
		: MinimumLoadingScreenDisplayTime(-1.0f)
		, bAutoCompleteWhenLoadingCompletes(true)
		, bMoviesAreSkippable(true)
		, bWaitForManualStop(false)
		, PlaybackType(EMoviePlaybackType::MT_Normal) {}

	/** The widget to be displayed on top of the movie or simply standalone if there is no movie. */
	TSharedPtr<class SWidget> WidgetLoadingScreen;

	/** The movie paths local to the game's Content/Movies/ directory we will play. */
	TArray<FString> MoviePaths;

	/** The minimum time that a loading screen should be opened for. */
	float MinimumLoadingScreenDisplayTime;

	/** If true, the loading screen will disappear as soon as all movies are played and loading is done. */
	bool bAutoCompleteWhenLoadingCompletes;

	/** If true, movies can be skipped by clicking the loading screen as long as loading is done. */
	bool bMoviesAreSkippable;

	/** If true, movie playback continues until Stop is called. */
	bool bWaitForManualStop;

	/** Should we just play back, loop, etc.  NOTE: if the playback type is MT_LoopLast, then bAutoCompleteWhenLoadingCompletes will be togged on when the last movie is hit*/
	TEnumAsByte<EMoviePlaybackType> PlaybackType;

	/** True if there is either a standalone widget or any movie paths or both. */
	bool IsValid() const;

	/** Creates a simple test loading screen widget. */
	static TSharedRef<class SWidget> NewTestLoadingScreenWidget();
};


/** An interface to the movie player we will use for loading screens and gameplay movies */
class IGameMoviePlayer
{
public:
	/** Registers a movie streamer with the movie player. Set in the preloading screen stage. */
	virtual void RegisterMovieStreamer(TSharedPtr<IMovieStreamer> InMovieStreamer) = 0;

	/** This movie player needs to be given the slate renderer in order to run properly. Set in the launch engine loop. */
	virtual void SetSlateRenderer(TSharedPtr<class FSlateRenderer> InSlateRenderer) = 0;
	
	/** Initializes this movie player, creating the startup window and hiding the splash screen. To be called in the launch engine loop. */
	virtual void Initialize() = 0;

	/** Shutsdown the movie player. */
	virtual void Shutdown() = 0;

	/** Passes the loading screen window back to the game to use. For use by the launch engine loop only. */
	virtual void PassLoadingScreenWindowBackToGame() const = 0;
	
	/** Passes in a slate loading screen UI, movie paths, and any additional data. */
	virtual void SetupLoadingScreen(const FLoadingScreenAttributes& InLoadingScreenAttributes) = 0;
	
	/** Called before playing a movie if the loading screen has not been prepared. */
	DECLARE_EVENT(IGameMoviePlayer, FOnPrepareLoadingScreen)
	virtual FOnPrepareLoadingScreen& OnPrepareLoadingScreen() = 0;

	/** 
	 * Starts playing the movie given the last FLoadingScreenAttributes passed in
	 * @return true of a movie started playing.
	 */
	virtual bool PlayMovie() = 0;

	/** 
	 * Stops the currently playing movie, if any.
	 */
	virtual void StopMovie() = 0;
	
	/** Call only on the game thread. Spins this thread until the movie stops. */
	virtual void WaitForMovieToFinish() = 0;

	/** Called from to check if the game thread is finished loading. */
	virtual bool IsLoadingFinished() const = 0;

	/** True if the loading screen is currently running (i.e. PlayMovie but no WaitForMovieToFinish has been called). */
	virtual bool IsMovieCurrentlyPlaying() const = 0;

	/** True if we have either slate widgets or a movie to show. */
	virtual bool LoadingScreenIsPrepared() const = 0;
	
	/** Sets up an FLoadingScreenAttributes from the game's engine.ini, then calls the virtual SetupLoadingScreen. */
	virtual void SetupLoadingScreenFromIni() = 0;

	/** returns the name of the movie currently being played */
	virtual FString GetMovieName() = 0;

	/** returns true if the movie being played in the last one in the play list */
	virtual bool IsLastMovieInPlaylist() = 0;

	DECLARE_EVENT(IGameMoviePlayer, FOnMoviePlaybackFinished)
	virtual FOnMoviePlaybackFinished& OnMoviePlaybackFinished() = 0;
	
	/** Allows for a slate overlay widget to be set after playback. */
	virtual void SetSlateOverlayWidget(TSharedPtr<SWidget> NewOverlayWidget) = 0;

	void BroadcastMoviePlaybackFinished() { OnMoviePlaybackFinished().Broadcast(); }

	/** This function shouild return true if the movie will auto-complete the sequence when background loading has finished */
	virtual bool WillAutoCompleteWhenLoadFinishes() = 0;

	virtual ~IGameMoviePlayer() {}
};

/** Gets the movie player singleton for the engine. */
TSharedPtr<IGameMoviePlayer> MOVIEPLAYER_API GetMoviePlayer();

/** Returns true if the movie player is enabled. */
bool MOVIEPLAYER_API IsMoviePlayerEnabled();
