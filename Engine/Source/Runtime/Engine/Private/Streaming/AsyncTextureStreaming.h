// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
AsyncTextureStreaming.h: Definitions of classes used for texture streaming async task.
=============================================================================*/

#pragma once

#include "StreamingManagerTexture.h"

/** Thread-safe helper struct for streaming information. */
class FAsyncTextureStreamingData
{
public:

	/** Set the data but do as little as possible, called from the game thread. */
	void Init(TArray<FStreamingViewInfo> InViewInfos, float InWorldTime, TArray<FLevelTextureManager>& LevelTextureManagers, FDynamicComponentTextureManager& DynamicComponentManager);

	/** Update everything internally so to allow calls to CalcWantedMips */
	void UpdateBoundSizes_Async(const FTextureStreamingSettings& Settings);

	void UpdatePerfectWantedMips_Async(FStreamingTexture& StreamingTexture, const FTextureStreamingSettings& Settings, bool bOutputToLog = false) const;

	uint32 GetAllocatedSize() const { return ViewInfos.GetAllocatedSize() + StaticInstancesViews.GetAllocatedSize(); }

	FORCEINLINE const FTextureInstanceAsyncView& GetDynamicInstancesView() const { return DynamicInstancesView; }
	FORCEINLINE const TArray<FTextureInstanceAsyncView>& GetStaticInstancesViews() const { return StaticInstancesViews; }
	FORCEINLINE const TArray<FStreamingViewInfo>& GetViewInfos() const { return ViewInfos; }


private:

	/** Cached from FStreamingManagerBase. */
	TArray<FStreamingViewInfo> ViewInfos;
	
	FTextureInstanceAsyncView DynamicInstancesView;

	/** Cached from each ULevel. */
	TArray<FTextureInstanceAsyncView> StaticInstancesViews;

	/** Time since last full update. Used to know if something is immediately visible. */
	float LastUpdateTime;
};


/** Async work class for calculating priorities for all textures. */
// this could implement a better abandon, but give how it is used, it does that anyway via the abort mechanism
class FAsyncTextureStreamingTask : public FNonAbandonableTask
{
public:

	FAsyncTextureStreamingTask( FStreamingManagerTexture* InStreamingManager )
	:	StreamingManager( *InStreamingManager )
	,	bAbort( false )
	{
		Reset(0, 0, 0, 0);
		MemoryBudget = 0;
	}

	/** Resets the state to start a new async job. */
	void Reset(int64 InAllocatedMemory, int64 InPoolSize, int64 InTempMemoryBudget, int64 InMemoryMargin)
	{
		AllocatedMemory = InAllocatedMemory;
		PoolSize = InPoolSize;
		TempMemoryBudget = InTempMemoryBudget;
		MemoryMargin = InMemoryMargin;

		bAbort = false;
	}

	/** Notifies the async work that it should abort the thread ASAP. */
	void Abort() { bAbort = true; }

	/** Whether the async work is being aborted. Can be used in conjunction with IsDone() to see if it has finished. */
	bool IsAborted() const { return bAbort;	}

	/** Returns the resulting priorities, matching the FStreamingManagerTexture::StreamingTextures array. */
	const TArray<int32>& GetLoadRequests() const { return LoadRequests; }
	const TArray<int32>& GetCancelationRequests() const { return CancelationRequests; }

	FAsyncTextureStreamingData StreamingData;
	
	/** Performs the async work. */
	void DoWork();

private:

	friend class FAsyncTask<FAsyncTextureStreamingTask>;

	struct FCompareTextureByRetentionPriority // Bigger retention priority first.
	{
		FCompareTextureByRetentionPriority(const TArray<FStreamingTexture>& InStreamingTextures) : StreamingTextures(InStreamingTextures) {}
		const TArray<FStreamingTexture>& StreamingTextures;

		FORCEINLINE bool operator()( int32 IndexA, int32 IndexB ) const
		{
			const int32 PrioA = StreamingTextures[IndexA].RetentionPriority;
			const int32 PrioB = StreamingTextures[IndexB].RetentionPriority;
			if ( PrioA > PrioB )  return true;
			if ( PrioA == PrioB ) return IndexA > IndexB;  // Sorting by index so that it gets deterministic.
			return false;
		}
	};

	struct FCompareTextureByLoadOrderPriority // Bigger load order priority first.
	{
		FCompareTextureByLoadOrderPriority(const TArray<FStreamingTexture>& InStreamingTextures) : StreamingTextures(InStreamingTextures) {}
		const TArray<FStreamingTexture>& StreamingTextures;

		FORCEINLINE bool operator()( int32 IndexA, int32 IndexB ) const
		{
			const int32 PrioA = StreamingTextures[IndexA].LoadOrderPriority;
			const int32 PrioB = StreamingTextures[IndexB].LoadOrderPriority;
			if ( PrioA > PrioB )  return true;
			if ( PrioA == PrioB ) return IndexA > IndexB;  // Sorting by index so that it gets deterministic.
			return false;
		}
	};

	void UpdateBudgetedMips_Async(int64& OutMemoryUsed, int64& OutTempMemoryUsed);

	void UpdateLoadAndCancelationRequests_Async(int64 MemoryUsed, int64 TempMemoryUsed);

	void UpdateStats_Async();

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncTextureStreamingTask, STATGROUP_ThreadPoolAsyncTasks);
	}

	/** Reference to the owning streaming manager, for accessing the thread-safe data. */
	FStreamingManagerTexture& StreamingManager;

	/** Indices for load/unload requests, sorted by load order. */
	TArray<int32>	LoadRequests;
	TArray<int32>	CancelationRequests;

	/** Whether the async work should abort its processing. */
	volatile bool				bAbort;

	/** How much gpu resources are currently allocated in the texture pool (all category). */
	int64 AllocatedMemory; 

	/** Size of the pool once non streaming data is removed and value is stabilized */
	int64 PoolSize;

	/** How much temp memory is allowed (temp memory is taken when changing mip count). */
	int64 TempMemoryBudget;

	/** How much temp memory is allowed (temp memory is taken when changing mip count). */
	int64 MemoryMargin;

	/** How much memory is available for textures. */
	int64 MemoryBudget;
};
