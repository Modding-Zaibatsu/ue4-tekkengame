// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CapturePin.h: CapturePin definition
=============================================================================*/
#ifndef _CAPTUREPIN_HEADER_
#define _CAPTUREPIN_HEADER_

#if PLATFORM_WINDOWS && !UE_BUILD_MINIMAL

typedef TCHAR* PTCHAR;

#ifdef __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wreorder"	// warning : field 'x' will be initialized after field 'y' [-Wreorder]
	#pragma clang diagnostic ignored "-Woverloaded-virtual"	 // note: hidden overloaded virtual function 'x' declared here: different number of parameters
#else
	#pragma warning(push)
	#pragma warning(disable : 4263) // 'function' : member function does not override any base class virtual member function
	#pragma warning(disable : 4264) // 'virtual_function' : no override available for virtual member function from base 
#endif
#include "AllowWindowsPlatformTypes.h"
#include <streams.h>
#include "HideWindowsPlatformTypes.h"
#ifdef __clang__
	#pragma clang diagnostic pop
#else
	#pragma warning(pop)
#endif

class FAVIWriter;
struct FCapturedFrame;

class FCapturePin : public CSourceStream
{
protected:

	/** The length of the frame, used for playback */
	const REFERENCE_TIME FrameLength;
	/** The current image height */
	int32 ImageHeight;
	/** And current image width */
	int32 ImageWidth;
	/** Protects our internal state */
	CCritSec SharedState;

	/** The writer to which we belong */
	const FAVIWriter& Writer;

	/** The frame we're currently processing */
	const FCapturedFrame* CurrentFrame;

	TOptional<HRESULT> ProcessFrames();

public:

	FCapturePin(HRESULT *phr, CSource *pFilter, const FAVIWriter& InWriter);
	~FCapturePin();

	// Override the version that offers exactly one media type
	HRESULT DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pRequest);
	HRESULT FillBuffer(IMediaSample *pSample);

	// the loop executed while running
	HRESULT DoBufferProcessingLoop(void);

	// Set the agreed media type and set up the necessary parameters
	HRESULT SetMediaType(const CMediaType *pMediaType);

	// Support multiple display formats
	HRESULT CheckMediaType(const CMediaType *pMediaType);
	HRESULT GetMediaType(int32 iPosition, CMediaType *pmt);

	// Quality control
	// Not implemented because we aren't going in real time.
	// If the file-writing filter slows the graph down, we just do nothing, which means
	// wait until we're unblocked. No frames are ever dropped.
	STDMETHODIMP Notify(IBaseFilter *pSelf, Quality q)
	{
		return E_FAIL;
	}

};
#endif //#if PLATFORM_WINDOWS


#endif	//#ifndef _CAPTUREPIN_HEADER_

