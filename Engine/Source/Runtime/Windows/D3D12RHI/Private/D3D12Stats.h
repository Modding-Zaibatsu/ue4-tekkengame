// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12Stats.h: D3D12 Statistics and Timing Interfaces
=============================================================================*/
#pragma once

/**
* The D3D RHI stats.
*/
DECLARE_CYCLE_STAT_EXTERN(TEXT("Present time"), STAT_D3D12PresentTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("CreateTexture time"), STAT_D3D12CreateTextureTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("LockTexture time"), STAT_D3D12LockTextureTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("UnlockTexture time"), STAT_D3D12UnlockTextureTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("CopyTexture time"), STAT_D3D12CopyTextureTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("CreateBoundShaderState time"), STAT_D3D12CreateBoundShaderStateTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("New bound shader state time"), STAT_D3D12NewBoundShaderStateTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Clean uniform buffer pool"), STAT_D3D12CleanUniformBufferTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Clear shader resources"), STAT_D3D12ClearShaderResourceTime, STATGROUP_D3D12RHI, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Uniform buffer pool num free"), STAT_D3D12NumFreeUniformBuffers, STATGROUP_D3D12RHI, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Bound Shader State"), STAT_D3D12NumBoundShaderState, STATGROUP_D3D12RHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Uniform buffer pool memory"), STAT_D3D12FreeUniformBufferMemory, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Update uniform buffer"), STAT_D3D12UpdateUniformBufferTime, STATGROUP_D3D12RHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Textures Allocated"), STAT_D3D12TexturesAllocated, STATGROUP_D3D12RHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Textures Released"), STAT_D3D12TexturesReleased, STATGROUP_D3D12RHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Texture object pool memory"), STAT_D3D12TexturePoolMemory, STATGROUP_D3D12RHI, );

DECLARE_CYCLE_STAT_EXTERN(TEXT("Commit resource tables"), STAT_D3D12CommitResourceTables, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Cache resource tables"), STAT_D3D12CacheResourceTables, STATGROUP_D3D12RHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Num cached resource tables"), STAT_D3D12CacheResourceTableCalls, STATGROUP_D3D12RHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Num textures in tables"), STAT_D3D12SetTextureInTableCalls, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("SetShaderTexture time"), STAT_D3D12SetShaderTextureTime, STATGROUP_D3D12RHI, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("SetShaderTexture calls"), STAT_D3D12SetShaderTextureCalls, STATGROUP_D3D12RHI, );

DECLARE_CYCLE_STAT_EXTERN(TEXT("ApplyState time"), STAT_D3D12ApplyStateTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("ApplyState: Rebuild PSO time"), STAT_D3D12ApplyStateRebuildPSOTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("ApplyState: Set SRV time"), STAT_D3D12ApplyStateSetSRVTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("ApplyState: Set UAV time"), STAT_D3D12ApplyStateSetUAVTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("ApplyState: Set Vertex Buffer time"), STAT_D3D12ApplyStateSetVertexBufferTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("ApplyState: Set Constant Buffer time"), STAT_D3D12ApplyStateSetConstantBufferTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("ApplyState: PSO Create time"), STAT_D3D12ApplyStatePSOCreateTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Clear MRT time"), STAT_D3D12ClearMRT, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Commit graphics constants"), STAT_D3D12CommitGraphicsConstants, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Set shader uniform buffer"), STAT_D3D12SetShaderUniformBuffer, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Set bound shader state"), STAT_D3D12SetBoundShaderState, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("ExecuteCommandList time"), STAT_D3D12ExecuteCommandListTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("WaitForFence time"), STAT_D3D12WaitForFenceTime, STATGROUP_D3D12RHI, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Resource Clean Up time"), STAT_D3D12ResourceCleanUpTime, STATGROUP_D3D12RHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Used Video Memory"), STAT_D3D12UsedVideoMemory, STATGROUP_D3D12RHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Available Video Memory"), STAT_D3D12AvailableVideoMemory, STATGROUP_D3D12RHI, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Total Video Memory"), STAT_D3D12TotalVideoMemory, STATGROUP_D3D12RHI, );

struct FD3D12GlobalStats
{
	// in bytes, never change after RHI, needed to scale game features
	static int64 GDedicatedVideoMemory;

	// in bytes, never change after RHI, needed to scale game features
	static int64 GDedicatedSystemMemory;

	// in bytes, never change after RHI, needed to scale game features
	static int64 GSharedSystemMemory;

	// In bytes. Never changed after RHI init. Our estimate of the amount of memory that we can use for graphics resources in total.
	static int64 GTotalGraphicsMemory;
};

// This class has multiple inheritance but really FGPUTiming is a static class
class FD3D12BufferedGPUTiming : public FRenderResource, public FGPUTiming, public FD3D12DeviceChild
{
public:
	/**
	* Constructor.
	*
	* @param InD3DRHI			RHI interface
	* @param InBufferSize		Number of buffered measurements
	*/
	FD3D12BufferedGPUTiming(class FD3D12Device* InParent, int32 BufferSize);

	FD3D12BufferedGPUTiming()
	{
	}

	/**
	* Start a GPU timing measurement.
	*/
	void	StartTiming();

	/**
	* End a GPU timing measurement.
	* The timing for this particular measurement will be resolved at a later time by the GPU.
	*/
	void	EndTiming();

	/**
	* Retrieves the most recently resolved timing measurement.
	* The unit is the same as for FPlatformTime::Cycles(). Returns 0 if there are no resolved measurements.
	*
	* @return	Value of the most recently resolved timing, or 0 if no measurements have been resolved by the GPU yet.
	*/
	uint64	GetTiming(bool bGetCurrentResultsAndBlock = false);

	/**
	* Initializes all D3D resources.
	*/
	virtual void InitDynamicRHI() override;

	/**
	* Releases all D3D resources.
	*/
	virtual void ReleaseDynamicRHI() override;

private:
	/**
	* Initializes the static variables, if necessary.
	*/
	static void PlatformStaticInitialize(void* UserData);

	/**
	* Get the StartTimestampQueryHeapIndex.
	*/
	FORCEINLINE int32 GetStartTimestampIndex(int32 Timestamp) const
	{
		// Multiply by 2 because each timestamp has a start/end pair.
		return Timestamp * 2;
	}

	/**
	* Get the EndTimestampQueryHeapIndex.
	*/
	FORCEINLINE int32 GetEndTimestampIndex(int32 Timestamp) const
	{
		return GetStartTimestampIndex(Timestamp) + 1;
	}

	/** Number of timestamps created in 'StartTimestamps' and 'EndTimestamps'. */
	int32						BufferSize;
	/** Current timing being measured on the CPU. */
	int32						CurrentTimestamp;
	/** Number of measurements in the buffers (0 - BufferSize). */
	int32						NumIssuedTimestamps;
	/** Timestamps */
	TRefCountPtr<ID3D12QueryHeap>	TimestampQueryHeap;
	TArray<FD3D12CLSyncPoint>		TimestampListHandles;
	TRefCountPtr<FD3D12Resource>	TimestampQueryHeapBuffer;
	uint64*							TimestampQueryHeapBufferData;
	/** Whether we are currently timing the GPU: between StartTiming() and EndTiming(). */
	bool						bIsTiming;
	/** Whether stable power state is currently enabled */
	bool                        bStablePowerState;
};

/** A single perf event node, which tracks information about a appBeginDrawEvent/appEndDrawEvent range. */
class FD3D12EventNode : public FGPUProfilerEventNode, public FD3D12DeviceChild
{
public:
	FD3D12EventNode(const TCHAR* InName, FGPUProfilerEventNode* InParent, class FD3D12Device* InParentDevice) :
		FGPUProfilerEventNode(InName, InParent),
		Timing(InParentDevice, 1)
	{
		// Initialize Buffered timestamp queries 
		Timing.InitDynamicRHI();
	}

	virtual ~FD3D12EventNode()
	{
		Timing.ReleaseDynamicRHI();
	}

	/**
	* Returns the time in ms that the GPU spent in this draw event.
	* This blocks the CPU if necessary, so can cause hitching.
	*/
	virtual float GetTiming() override;

	virtual void StartTiming() override
	{
		Timing.StartTiming();
	}

	virtual void StopTiming() override
	{
		Timing.EndTiming();
	}

	FD3D12BufferedGPUTiming Timing;
};

/** An entire frame of perf event nodes, including ancillary timers. */
class FD3D12EventNodeFrame : public FGPUProfilerEventNodeFrame, public FD3D12DeviceChild
{
public:

	FD3D12EventNodeFrame(class FD3D12Device* InParent) :
		FGPUProfilerEventNodeFrame(),
		RootEventTiming(InParent, 1)
	{
		RootEventTiming.InitDynamicRHI();
	}

	~FD3D12EventNodeFrame()
	{
		RootEventTiming.ReleaseDynamicRHI();
	}

	/** Start this frame of per tracking */
	virtual void StartFrame() override;

	/** End this frame of per tracking, but do not block yet */
	virtual void EndFrame() override;

	/** Calculates root timing base frequency (if needed by this RHI) */
	virtual float GetRootTimingResults() override;

	virtual void LogDisjointQuery() override;

	/** Timer tracking inclusive time spent in the root nodes. */
	FD3D12BufferedGPUTiming RootEventTiming;
};

namespace D3D12RHI
{
	/**
	* Encapsulates GPU profiling logic and data.
	* There's only one global instance of this struct so it should only contain global data, nothing specific to a frame.
	*/
	struct FD3DGPUProfiler : public FGPUProfiler, public FD3D12DeviceChild
	{
		/** Used to measure GPU time per frame. */
		FD3D12BufferedGPUTiming FrameTiming;

		/** GPU hitch profile histories */
		TIndirectArray<FD3D12EventNodeFrame> GPUHitchEventNodeFrames;

		FD3DGPUProfiler()
		{}

		//FD3DGPUProfiler(class FD3D12Device* InParent) :
		//	FGPUProfiler(),
		//    FrameTiming(InParent, 4),
		//    FD3D12DeviceChild(InParent)
		//{
		//	// Initialize Buffered timestamp queries 
		//	FrameTiming.InitResource();
		//}

		void Init(class FD3D12Device* InParent)
		{
			Parent = InParent;
			FrameTiming = FD3D12BufferedGPUTiming(InParent, 4);
			// Initialize Buffered timestamp queries 
			FrameTiming.InitResource();
		}

		virtual FGPUProfilerEventNode* CreateEventNode(const TCHAR* InName, FGPUProfilerEventNode* InParent) override
		{
			FD3D12EventNode* EventNode = new FD3D12EventNode(InName, InParent, GetParentDevice());
			return EventNode;
		}

		virtual void PushEvent(const TCHAR* Name, FColor Color) override;
		virtual void PopEvent() override;

		void BeginFrame(class FD3D12DynamicRHI* InRHI);

		void EndFrame();
	};
}