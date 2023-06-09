// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemUtilsPrivatePCH.h"
#include "OnlineEngineInterfaceImpl.h"

UOnlineEngineInterfaceImpl::UOnlineEngineInterfaceImpl(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UOnlineEngineInterfaceImpl::IsLoaded(FName OnlineIdentifier)
{
	return IOnlineSubsystem::IsLoaded(OnlineIdentifier);
}

FName UOnlineEngineInterfaceImpl::GetOnlineIdentifier(FWorldContext& WorldContext)
{
	IOnlineSubsystemUtils* Utils = Online::GetUtils();
	if (Utils)
	{
		return Utils->GetOnlineIdentifier(WorldContext);
	}
	return NAME_None;
}

FName UOnlineEngineInterfaceImpl::GetOnlineIdentifier(UWorld* World)
{
	IOnlineSubsystemUtils* Utils = Online::GetUtils();
	if (Utils)
	{
		return Utils->GetOnlineIdentifier(World);
	}

	return NAME_None;
}

bool UOnlineEngineInterfaceImpl::DoesInstanceExist(FName OnlineIdentifier)
{
	return IOnlineSubsystem::DoesInstanceExist(OnlineIdentifier);
}

void UOnlineEngineInterfaceImpl::ShutdownOnlineSubsystem(FName OnlineIdentifier)
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get(OnlineIdentifier);
	if (OnlineSub)
	{
		OnlineSub->Shutdown();
	}
}

void UOnlineEngineInterfaceImpl::DestroyOnlineSubsystem(FName OnlineIdentifier)
{
	IOnlineSubsystem::Destroy(OnlineIdentifier);
}

TSharedPtr<const FUniqueNetId> UOnlineEngineInterfaceImpl::CreateUniquePlayerId(const FString& Str)
{
	IOnlineIdentityPtr IdentityInt = Online::GetIdentityInterface();
	if (IdentityInt.IsValid())
	{
		return IdentityInt->CreateUniquePlayerId(Str);
	}
	return nullptr;
}

TSharedPtr<const FUniqueNetId> UOnlineEngineInterfaceImpl::GetUniquePlayerId(UWorld* World, int32 LocalUserNum)
{
	IOnlineIdentityPtr IdentityInt = Online::GetIdentityInterface(World);
	if (IdentityInt.IsValid())
	{
		TSharedPtr<const FUniqueNetId> UniqueId = IdentityInt->GetUniquePlayerId(LocalUserNum);
		return UniqueId;
	}
	return nullptr;
}

FString UOnlineEngineInterfaceImpl::GetPlayerNickname(UWorld* World, const FUniqueNetId& UniqueId)
{
	IOnlineIdentityPtr IdentityInt = Online::GetIdentityInterface(World);
	if (IdentityInt.IsValid())
	{	
		return IdentityInt->GetPlayerNickname(UniqueId);
	}

	static FString InvalidName(TEXT("InvalidOSSUser"));
	return InvalidName;
}

bool UOnlineEngineInterfaceImpl::GetPlayerPlatformNickname(UWorld* World, int32 LocalUserNum, FString& OutNickname)
{
	IOnlineSubsystem* PlatformSubsystem = IOnlineSubsystem::GetByPlatform(false);
	if (PlatformSubsystem)
	{
		IOnlineIdentityPtr OnlineIdentityInt = PlatformSubsystem->GetIdentityInterface();
		if (OnlineIdentityInt.IsValid())
		{
			OutNickname = OnlineIdentityInt->GetPlayerNickname(LocalUserNum);
			if (!OutNickname.IsEmpty())
			{
				return true;
			}
		}
	}
	return false;
}

bool UOnlineEngineInterfaceImpl::AutoLogin(UWorld* World, int32 LocalUserNum, const FOnlineAutoLoginComplete& InCompletionDelegate)
{
	IOnlineIdentityPtr IdentityInt = Online::GetIdentityInterface(World);
	if (IdentityInt.IsValid())
	{
		FName OnlineIdentifier = GetOnlineIdentifier(World);

		OnLoginCompleteDelegateHandle = IdentityInt->AddOnLoginCompleteDelegate_Handle(LocalUserNum, FOnLoginCompleteDelegate::CreateUObject(this, &ThisClass::OnAutoLoginComplete, OnlineIdentifier, InCompletionDelegate));
		if (IdentityInt->AutoLogin(LocalUserNum))
		{
			// Async login started
			return true;
		}
	}

	// Not waiting for async login
	return false;
}

void UOnlineEngineInterfaceImpl::OnAutoLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error, FName OnlineIdentifier, FOnlineAutoLoginComplete InCompletionDelegate)
{
	IOnlineIdentityPtr IdentityInt = Online::GetIdentityInterface(OnlineIdentifier);
	if (IdentityInt.IsValid())
	{
		IdentityInt->ClearOnLoginCompleteDelegate_Handle(LocalUserNum, OnLoginCompleteDelegateHandle);
	}

	InCompletionDelegate.ExecuteIfBound(LocalUserNum, bWasSuccessful, Error);
}

bool UOnlineEngineInterfaceImpl::IsLoggedIn(UWorld* World, int32 LocalUserNum)
{
	IOnlineIdentityPtr IdentityInt = Online::GetIdentityInterface(World);
	if (IdentityInt.IsValid())
	{
		return (IdentityInt->GetLoginStatus(LocalUserNum) == ELoginStatus::LoggedIn);
	}

	return false;
}

void UOnlineEngineInterfaceImpl::StartSession(UWorld* World, FName SessionName, FOnlineSessionStartComplete& InCompletionDelegate)
{
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid())
	{
		FName OnlineIdentifier = GetOnlineIdentifier(World);

		FDelegateHandle StartSessionCompleteHandle = SessionInt->AddOnStartSessionCompleteDelegate_Handle(FOnStartSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnStartSessionComplete, OnlineIdentifier, InCompletionDelegate));
		OnStartSessionCompleteDelegateHandles.Add(OnlineIdentifier, StartSessionCompleteHandle);
		
		SessionInt->StartSession(SessionName);
	}
	else
	{
		InCompletionDelegate.ExecuteIfBound(SessionName, false);
	}
}

void UOnlineEngineInterfaceImpl::OnStartSessionComplete(FName SessionName, bool bWasSuccessful, FName OnlineIdentifier, FOnlineSessionStartComplete CompletionDelegate)
{
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(OnlineIdentifier);
	if (SessionInt.IsValid())
	{
		// Cleanup the login delegate before calling create below
		FDelegateHandle* DelegateHandle = OnStartSessionCompleteDelegateHandles.Find(OnlineIdentifier);
		if (DelegateHandle)
		{
			SessionInt->ClearOnStartSessionCompleteDelegate_Handle(*DelegateHandle);
		}
	}

	CompletionDelegate.ExecuteIfBound(SessionName, bWasSuccessful);
}

void UOnlineEngineInterfaceImpl::EndSession(UWorld* World, FName SessionName, FOnlineSessionEndComplete& InCompletionDelegate)
{
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid())
	{
		FName OnlineIdentifier = GetOnlineIdentifier(World);

		FDelegateHandle EndSessionCompleteHandle = SessionInt->AddOnEndSessionCompleteDelegate_Handle(FOnEndSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnEndSessionComplete, OnlineIdentifier, InCompletionDelegate));
		OnEndSessionCompleteDelegateHandles.Add(OnlineIdentifier, EndSessionCompleteHandle);

		SessionInt->EndSession(SessionName);
	}
	else
	{
		InCompletionDelegate.ExecuteIfBound(SessionName, false);
	}
}

void UOnlineEngineInterfaceImpl::OnEndSessionComplete(FName SessionName, bool bWasSuccessful, FName OnlineIdentifier, FOnlineSessionEndComplete CompletionDelegate)
{
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(OnlineIdentifier);
	if (SessionInt.IsValid())
	{
		FDelegateHandle* DelegateHandle = OnEndSessionCompleteDelegateHandles.Find(OnlineIdentifier);
		if (DelegateHandle)
		{
			SessionInt->ClearOnEndSessionCompleteDelegate_Handle(*DelegateHandle);
		}
	}

	CompletionDelegate.ExecuteIfBound(SessionName, bWasSuccessful);
}

bool UOnlineEngineInterfaceImpl::DoesSessionExist(UWorld* World, FName SessionName)
{
	FOnlineSessionSettings* SessionSettings = nullptr;
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid())
	{
		SessionSettings = SessionInt->GetSessionSettings(SessionName);
	}

	return SessionSettings != nullptr;
}

bool UOnlineEngineInterfaceImpl::GetSessionJoinability(UWorld* World, FName SessionName, FJoinabilitySettings& OutSettings)
{
	bool bValidData = false;

	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid())
	{
		FOnlineSessionSettings* SessionSettings = SessionInt->GetSessionSettings(SessionName);
		if (SessionSettings)
		{
			OutSettings.SessionName = SessionName;
			OutSettings.bPublicSearchable = SessionSettings->bShouldAdvertise;
			OutSettings.bAllowInvites = SessionSettings->bAllowInvites;
			OutSettings.bJoinViaPresence = SessionSettings->bAllowJoinViaPresence;
			OutSettings.bJoinViaPresenceFriendsOnly = SessionSettings->bAllowJoinViaPresenceFriendsOnly;
			bValidData = true;
		}
	}

	return bValidData;
}

void UOnlineEngineInterfaceImpl::UpdateSessionJoinability(UWorld* World, FName SessionName, bool bPublicSearchable, bool bAllowInvites, bool bJoinViaPresence, bool bJoinViaPresenceFriendsOnly)
{
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid())
	{
		FOnlineSessionSettings* SessionSettings = SessionInt->GetSessionSettings(SessionName);
		if (SessionSettings != nullptr)
		{
			SessionSettings->bShouldAdvertise = bPublicSearchable;
			SessionSettings->bAllowInvites = bAllowInvites;
			SessionSettings->bAllowJoinViaPresence = bJoinViaPresence && !bJoinViaPresenceFriendsOnly;
			SessionSettings->bAllowJoinViaPresenceFriendsOnly = bJoinViaPresenceFriendsOnly;
			SessionInt->UpdateSession(SessionName, *SessionSettings, true);
		}
	}
}

void UOnlineEngineInterfaceImpl::RegisterPlayer(UWorld* World, FName SessionName, const FUniqueNetId& UniqueId, bool bWasInvited)
{
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid() && UniqueId.IsValid())
	{
		SessionInt->RegisterPlayer(SessionName, UniqueId, bWasInvited);
	}
}

void UOnlineEngineInterfaceImpl::UnregisterPlayer(UWorld* World, FName SessionName, const FUniqueNetId& UniqueId)
{
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid())
	{
		SessionInt->UnregisterPlayer(SessionName, UniqueId);
	}
}

bool UOnlineEngineInterfaceImpl::GetResolvedConnectString(UWorld* World, FName SessionName, FString& URL)
{
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (SessionInt.IsValid() && SessionInt->GetResolvedConnectString(SessionName, URL))
	{
		return true;
	}

	return false;
}

TSharedPtr<FVoicePacket> UOnlineEngineInterfaceImpl::GetLocalPacket(UWorld* World, uint8 LocalUserNum)
{
	IOnlineVoicePtr VoiceInt = Online::GetVoiceInterface(World);
	if (VoiceInt.IsValid())
	{
		TSharedPtr<FVoicePacket> LocalPacket = VoiceInt->GetLocalPacket(LocalUserNum);
		return LocalPacket;
	}

	return nullptr;
}

TSharedPtr<FVoicePacket> UOnlineEngineInterfaceImpl::SerializeRemotePacket(UWorld* World, FArchive& Ar)
{
	IOnlineVoicePtr VoiceInt = Online::GetVoiceInterface(World);
	if (VoiceInt.IsValid())
	{
		return VoiceInt->SerializeRemotePacket(Ar);
	}
	return nullptr;
}

void UOnlineEngineInterfaceImpl::StartNetworkedVoice(UWorld* World, uint8 LocalUserNum)
{
	IOnlineVoicePtr VoiceInt = Online::GetVoiceInterface(World);
	if (VoiceInt.IsValid())
	{
		VoiceInt->StartNetworkedVoice(LocalUserNum);
	}
}

void UOnlineEngineInterfaceImpl::StopNetworkedVoice(UWorld* World, uint8 LocalUserNum)
{
	IOnlineVoicePtr VoiceInt = Online::GetVoiceInterface(World);
	if (VoiceInt.IsValid())
	{
		VoiceInt->StopNetworkedVoice(LocalUserNum);
	}
}

void UOnlineEngineInterfaceImpl::ClearVoicePackets(UWorld* World)
{
	IOnlineVoicePtr VoiceInt = Online::GetVoiceInterface(World);
	if (VoiceInt.IsValid())
	{
		VoiceInt->ClearVoicePackets();
	}
}

bool UOnlineEngineInterfaceImpl::MuteRemoteTalker(UWorld* World, uint8 LocalUserNum, const FUniqueNetId& PlayerId, bool bIsSystemWide)
{
	IOnlineVoicePtr VoiceInt = Online::GetVoiceInterface(World);
	if (VoiceInt.IsValid())
	{
		return VoiceInt->MuteRemoteTalker(LocalUserNum, PlayerId, bIsSystemWide);
	}
	return false;
}

bool UOnlineEngineInterfaceImpl::UnmuteRemoteTalker(UWorld* World, uint8 LocalUserNum, const FUniqueNetId& PlayerId, bool bIsSystemWide)
{
	IOnlineVoicePtr VoiceInt = Online::GetVoiceInterface(World);
	if (VoiceInt.IsValid())
	{
		return VoiceInt->UnmuteRemoteTalker(LocalUserNum, PlayerId, bIsSystemWide);
	}
	return false;
}

int32 UOnlineEngineInterfaceImpl::GetNumLocalTalkers(UWorld* World)
{
	IOnlineVoicePtr VoiceInt = Online::GetVoiceInterface(World);
	if (VoiceInt.IsValid())
	{
		return VoiceInt->GetNumLocalTalkers();
	}

	return 0;
}

void UOnlineEngineInterfaceImpl::ShowLeaderboardUI(UWorld* World, const FString& CategoryName)
{
	IOnlineExternalUIPtr ExternalUI = Online::GetExternalUIInterface();
	if(ExternalUI.IsValid())
	{
		ExternalUI->ShowLeaderboardUI(CategoryName);
	}
}

void UOnlineEngineInterfaceImpl::ShowAchievementsUI(UWorld* World, int32 LocalUserNum)
{
	IOnlineExternalUIPtr ExternalUI = Online::GetExternalUIInterface();
	if (ExternalUI.IsValid())
	{
		ExternalUI->ShowAchievementsUI(LocalUserNum);
	}
}

void UOnlineEngineInterfaceImpl::BindToExternalUIOpening(const FOnlineExternalUIChanged& Delegate)
{
	IOnlineSubsystem* SubSystem = IOnlineSubsystem::IsLoaded() ? IOnlineSubsystem::Get() : nullptr;
	if (SubSystem != nullptr)
	{
		IOnlineExternalUIPtr ExternalUI = SubSystem->GetExternalUIInterface();
		if (ExternalUI.IsValid())
		{
			FOnExternalUIChangeDelegate OnExternalUIChangeDelegate;
			OnExternalUIChangeDelegate.BindUObject(this, &ThisClass::OnExternalUIChange, Delegate);

			ExternalUI->AddOnExternalUIChangeDelegate_Handle(OnExternalUIChangeDelegate);
		}
	}

	IOnlineSubsystem* SubSystemConsole = IOnlineSubsystem::GetByPlatform();
	if (SubSystemConsole != nullptr &&
		SubSystem != SubSystemConsole)
	{
		IOnlineExternalUIPtr ExternalUI = SubSystemConsole->GetExternalUIInterface();
		if (ExternalUI.IsValid())
		{
			FOnExternalUIChangeDelegate OnExternalUIChangeDelegate;
			OnExternalUIChangeDelegate.BindUObject(this, &ThisClass::OnExternalUIChange, Delegate);

			ExternalUI->AddOnExternalUIChangeDelegate_Handle(OnExternalUIChangeDelegate);
		}
	}
}

void UOnlineEngineInterfaceImpl::OnExternalUIChange(bool bInIsOpening, FOnlineExternalUIChanged Delegate)
{
	Delegate.ExecuteIfBound(bInIsOpening);
}

void UOnlineEngineInterfaceImpl::DumpSessionState(UWorld* World)
{
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(GetWorld());
	if (SessionInt.IsValid())
	{
		SessionInt->DumpSessionState();
	}
}

void UOnlineEngineInterfaceImpl::DumpPartyState(UWorld* World)
{
	IOnlinePartyPtr PartyInt = Online::GetPartyInterface(World);
	if (PartyInt.IsValid())
	{
		PartyInt->DumpPartyState();
	}
}

void UOnlineEngineInterfaceImpl::DumpVoiceState(UWorld* World)
{
	IOnlineVoicePtr VoiceInt = Online::GetVoiceInterface(World);
	if (VoiceInt.IsValid())
	{
		UE_LOG(LogOnline, Verbose, TEXT("\n%s"), *VoiceInt->GetVoiceDebugState());
	}
}

void UOnlineEngineInterfaceImpl::DumpChatState(UWorld* World)
{
	IOnlineChatPtr ChatInt = Online::GetChatInterface(World);
	if (ChatInt.IsValid())
	{
		ChatInt->DumpChatState();
	}
}

#if WITH_EDITOR
bool UOnlineEngineInterfaceImpl::SupportsOnlinePIE()
{
	return Online::GetUtils()->SupportsOnlinePIE();
}

int32 UOnlineEngineInterfaceImpl::GetNumPIELogins()
{
	return Online::GetUtils()->GetNumPIELogins();
}

void UOnlineEngineInterfaceImpl::SetForceDedicated(FName OnlineIdentifier, bool bForce)
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get(OnlineIdentifier);
	if (OnlineSub)
	{
		OnlineSub->SetForceDedicated(bForce);
	}
}

void UOnlineEngineInterfaceImpl::LoginPIEInstance(FName OnlineIdentifier, int32 LocalUserNum, int32 PIELoginNum, FOnPIELoginComplete& CompletionDelegate)
{
	FString ErrorStr;
	if (SupportsOnlinePIE())
	{
		TArray<FOnlineAccountCredentials> PIELogins;
		Online::GetUtils()->GetPIELogins(PIELogins);
		if (PIELogins.IsValidIndex(PIELoginNum))
		{
			IOnlineIdentityPtr IdentityInt = Online::GetIdentityInterface(OnlineIdentifier);
			if (IdentityInt.IsValid())
			{
				FDelegateHandle DelegateHandle = IdentityInt->AddOnLoginCompleteDelegate_Handle(LocalUserNum, FOnLoginCompleteDelegate::CreateUObject(this, &ThisClass::OnPIELoginComplete, OnlineIdentifier, CompletionDelegate));
				OnLoginPIECompleteDelegateHandlesForPIEInstances.Add(OnlineIdentifier, DelegateHandle);
				IdentityInt->Login(LocalUserNum, PIELogins[PIELoginNum]);
			}
			else
			{
				ErrorStr = TEXT("No identify interface to login");
			}
		}
		else
		{ 
			ErrorStr = TEXT("Invalid credentials for PIE login");
		}
	}
	else
	{
		ErrorStr = TEXT("PIE login not supported");
	}

	if (!ErrorStr.IsEmpty())
	{
		CompletionDelegate.ExecuteIfBound(LocalUserNum, false, ErrorStr);
	}
}

void UOnlineEngineInterfaceImpl::OnPIELoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error, FName OnlineIdentifier, FOnlineAutoLoginComplete InCompletionDelegate)
{
	IOnlineIdentityPtr IdentityInt = Online::GetIdentityInterface(OnlineIdentifier);

	// Cleanup the login delegate before calling create below
	FDelegateHandle* DelegateHandle = OnLoginPIECompleteDelegateHandlesForPIEInstances.Find(OnlineIdentifier);
	if (DelegateHandle)
	{
		IdentityInt->ClearOnLoginCompleteDelegate_Handle(LocalUserNum, *DelegateHandle);
		OnLoginPIECompleteDelegateHandlesForPIEInstances.Remove(OnlineIdentifier);
	}

	InCompletionDelegate.ExecuteIfBound(LocalUserNum, bWasSuccessful, Error);
}

#endif
