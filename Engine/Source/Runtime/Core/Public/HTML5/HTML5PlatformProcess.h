// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.


/*=============================================================================================
	HTML5Process.h: KickStart platform Process functions
==============================================================================================*/

#pragma once
#include "GenericPlatform/GenericPlatformProcess.h"

/** Dummy process handle for platforms that use generic implementation. */
struct FProcHandle : public TProcHandle<void*, nullptr>
{
public:
	/** Default constructor. */
	FORCEINLINE FProcHandle()
		: TProcHandle()
	{}

	/** Initialization constructor. */
	FORCEINLINE explicit FProcHandle( HandleType Other )
		: TProcHandle( Other )
	{}
};

/**
 * HTML5 implementation of the Process OS functions
 **/
struct CORE_API FHTML5PlatformProcess : public FGenericPlatformProcess
{
	static const TCHAR* ComputerName();
	static const TCHAR* BaseDir();
	static void Sleep(float Seconds);
	static void SleepNoStats(float Seconds);
	static void SleepInfinite();
	static class FEvent* CreateSynchEvent(bool bIsManualReset = 0);
	static class FRunnableThread* CreateRunnableThread();
	static bool SupportsMultithreading();
	static void LaunchURL( const TCHAR* URL, const TCHAR* Parms, FString* Error );
	static const TCHAR* ExecutableName(bool bRemoveExtension = true);
};

typedef FHTML5PlatformProcess FPlatformProcess;

/**
 * @todo html5 threads: Dummy critical section
 */
class FHTML5CriticalSection
{
public:

	FHTML5CriticalSection() {}

	/**
	 * Locks the critical section
	 */
	FORCEINLINE void Lock(void)
	{
	}

	/**
	 * Releases the lock on the critical seciton
	 */
	FORCEINLINE void Unlock(void)
	{
	}

private:
	FHTML5CriticalSection(const FHTML5CriticalSection&);
	FHTML5CriticalSection& operator=(const FHTML5CriticalSection&);
};

typedef FHTML5CriticalSection FCriticalSection;

typedef FSystemWideCriticalSectionNotImplemented FSystemWideCriticalSection;
