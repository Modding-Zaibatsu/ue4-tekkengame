// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.
#include "IcmpPrivatePCH.h"
#include "Icmp.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

FIcmpEchoResult IcmpEchoImpl(ISocketSubsystem* SocketSub, const FString& TargetAddress, float Timeout);

#if !PLATFORM_SUPPORTS_ICMP
FIcmpEchoResult IcmpEchoImpl(ISocketSubsystem* SocketSub, const FString& TargetAddress, float Timeout)
{
	FIcmpEchoResult Result;
	Result.Status = EIcmpResponseStatus::NotImplemented;
	return Result;
}
#endif

// Calculate one's complement checksum
int CalculateChecksum(uint8* Address, int Length)
{
	uint16* Paired = reinterpret_cast<uint16*>(Address);
	int Sum = 0;

	while (Length > 1)
	{
		Sum += *Paired++;
		Length -= 2;
	}

	if (Length == 1)
	{
		// Add the last odd byte
		Sum += *reinterpret_cast<uint8*>(Paired);
	}

	// Carry over overflow back to the LSB
	Sum = (Sum >> 16) + (Sum & 0xFFFF);
	// And in case the overflow caused another overflow, add it back again
	Sum += (Sum >> 16);

	return ~Sum;
}

bool ResolveIp(ISocketSubsystem* SocketSub, const FString& HostName, FString& OutIp)
{
	//ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSub)
	{
		TSharedRef<FInternetAddr> HostAddr = SocketSub->CreateInternetAddr();
		ESocketErrors HostResolveError = SocketSub->GetHostByName(TCHAR_TO_ANSI(*HostName), *HostAddr);
		if (HostResolveError == SE_NO_ERROR || HostResolveError == SE_EWOULDBLOCK)
		{
			OutIp = HostAddr->ToString(false);
			return true;
		}
	}

	return false;
}

class FIcmpAsyncResult
	: public FTickerObjectBase
{
public:

	FIcmpAsyncResult(ISocketSubsystem* InSocketSub, const FString& TargetAddress, float Timeout, FIcmpEchoResultCallback InCallback)
		: FTickerObjectBase(0)
		, SocketSub(InSocketSub)
		, Callback(InCallback)
		, bThreadCompleted(true)
	{
		if (SocketSub)
		{
			bThreadCompleted = false;
			TFunction<FIcmpEchoResult()> Task = [this, TargetAddress, Timeout]()
			{
				auto Result = IcmpEchoImpl(SocketSub, TargetAddress, Timeout);
				bThreadCompleted = true;
				return Result;
			};
			FutureResult = Async(EAsyncExecution::ThreadPool, Task);
		}
	}

	virtual ~FIcmpAsyncResult()
	{
		check(IsInGameThread());
		if (FutureResult.IsValid())
		{
			FutureResult.Wait();
		}
	}

private:
	virtual bool Tick(float DeltaTime) override
	{
		if (bThreadCompleted)
		{
			FIcmpEchoResult Result;
			if (FutureResult.IsValid())
			{
				Result = FutureResult.Get();
			}

			Callback(Result);

			delete this;
			return false;
		}
		return true;
	}

	/** Reference to the socket subsystem */
	ISocketSubsystem* SocketSub;
	/** Callback when the icmp result returns */
	FIcmpEchoResultCallback Callback;
	/** Thread task complete */
	FThreadSafeBool bThreadCompleted;
	/** Async result future */
	TFuture<FIcmpEchoResult> FutureResult;
};

void FIcmp::IcmpEcho(const FString& TargetAddress, float Timeout, FIcmpEchoResultCallback HandleResult)
{
	ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	new FIcmpAsyncResult(SocketSub, TargetAddress, Timeout, HandleResult);
}
