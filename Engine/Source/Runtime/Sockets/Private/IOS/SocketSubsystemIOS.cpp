// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "SocketsPrivatePCH.h"
#include "SocketSubsystemIOS.h"
#include "ModuleManager.h"
#include "BSDIPv6Sockets/SocketsBSDIPv6.h"
#include <ifaddrs.h>


FSocketSubsystemIOS* FSocketSubsystemIOS::SocketSingleton = NULL;

FName CreateSocketSubsystem( FSocketSubsystemModule& SocketSubsystemModule )
{
	FName SubsystemName(TEXT("IOS"));
	// Create and register our singleton factor with the main online subsystem for easy access
	FSocketSubsystemIOS* SocketSubsystem = FSocketSubsystemIOS::Create();
	FString Error;
	if (SocketSubsystem->Init(Error))
	{
		SocketSubsystemModule.RegisterSocketSubsystem(SubsystemName, SocketSubsystem);
		return SubsystemName;
	}
	else
	{
		FSocketSubsystemIOS::Destroy();
		return NAME_None;
	}
}

void DestroySocketSubsystem( FSocketSubsystemModule& SocketSubsystemModule )
{
	SocketSubsystemModule.UnregisterSocketSubsystem(FName(TEXT("IOS")));
	FSocketSubsystemIOS::Destroy();
}

FSocketSubsystemIOS* FSocketSubsystemIOS::Create()
{
	if (SocketSingleton == NULL)
	{
		SocketSingleton = new FSocketSubsystemIOS();
	}

	return SocketSingleton;
}

void FSocketSubsystemIOS::Destroy()
{
	if (SocketSingleton != NULL)
	{
		SocketSingleton->Shutdown();
		delete SocketSingleton;
		SocketSingleton = NULL;
	}
}

bool FSocketSubsystemIOS::Init(FString& Error)
{
	return true;
}

void FSocketSubsystemIOS::Shutdown(void)
{
}


bool FSocketSubsystemIOS::HasNetworkDevice()
{
	return true;
}

FSocket* FSocketSubsystemIOS::CreateSocket(const FName& SocketType, const FString& SocketDescription, bool bForceUDP)
{
	FSocketBSDIPv6* NewSocket = (FSocketBSDIPv6*)FSocketSubsystemBSDIPv6::CreateSocket(SocketType, SocketDescription, bForceUDP);
	if (NewSocket)
	{
		NewSocket->SetIPv6Only(false);

		// disable the SIGPIPE exception 
		int bAllow = 1;
		setsockopt(NewSocket->GetNativeSocket(), SOL_SOCKET, SO_NOSIGPIPE, &bAllow, sizeof(bAllow));
	}
	return NewSocket;
}

TSharedRef<FInternetAddr> FSocketSubsystemIOS::GetLocalHostAddr(FOutputDevice& Out, bool& bCanBindAll)
{
	TSharedRef<FInternetAddr> HostAddr = CreateInternetAddr();
	HostAddr->SetAnyAddress();

	ifaddrs* Interfaces = NULL;
	int32 WifiAddress;
	bool bWasWifiSet = false;
	int32 CellAddress;
	bool bWasCellSet = false;

	// get all of the addresses
	if (getifaddrs(&Interfaces) == 0) 
	{
		// Loop through linked list of interfaces
		for (ifaddrs* Travel = Interfaces; Travel != NULL; Travel = Travel->ifa_next)
		{
			if (Travel->ifa_addr->sa_family == AF_INET)
			{
				if (strcmp(Travel->ifa_name, "en0") == 0)
				{
					WifiAddress = ((sockaddr_in*)Travel->ifa_addr)->sin_addr.s_addr;
					bWasWifiSet = true;
					// this is the best, no need to go on
					break;
				}
				else if (strcmp(Travel->ifa_name, "pdp_ip0") == 0)
				{
					CellAddress = ((sockaddr_in*)Travel->ifa_addr)->sin_addr.s_addr;
					bWasCellSet = true;
				}
			}
		}
		// Free memory
		freeifaddrs(Interfaces);

		if (bWasWifiSet)
		{
			HostAddr->SetIp(WifiAddress);
			UE_LOG(LogIOS, Log, TEXT("Host addr is WIFI: %s"), *HostAddr->ToString(false));
		}
		else if (bWasCellSet)
		{
			HostAddr->SetIp(CellAddress);
			UE_LOG(LogIOS, Log, TEXT("Host addr is CELL: %s"), *HostAddr->ToString(false));
		}
		else
		{
			UE_LOG(LogIOS, Log, TEXT("Host addr is INVALID"));
		}
	}

	// return the newly created address
	return HostAddr;
}
