// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Net/OnlineEngineInterface.h"
#include "OnlineEngineInterfaceImpl.generated.h"

class UWorld;
class FVoicePacket;
class FArchive;
class FUniqueNetId;

UCLASS(config = Engine)
class ONLINESUBSYSTEMUTILS_API UOnlineEngineInterfaceImpl : public UOnlineEngineInterface
{
	GENERATED_UCLASS_BODY()

	/**
	 * Subsystem
	 */
public:

	virtual bool IsLoaded(FName OnlineIdentifier) override;
	virtual FName GetOnlineIdentifier(FWorldContext& WorldContext) override;
	virtual bool DoesInstanceExist(FName OnlineIdentifier) override;
	virtual void ShutdownOnlineSubsystem(FName OnlineIdentifier) override;
	virtual void DestroyOnlineSubsystem(FName OnlineIdentifier) override;

private:

	FName GetOnlineIdentifier(UWorld* World);

	/**
	 * Identity
	 */
public:

	virtual TSharedPtr<const FUniqueNetId> CreateUniquePlayerId(const FString& Str) override;
	virtual TSharedPtr<const FUniqueNetId> GetUniquePlayerId(UWorld* World, int32 LocalUserNum) override;

	virtual FString GetPlayerNickname(UWorld* World, const FUniqueNetId& UniqueId) override;
	virtual bool GetPlayerPlatformNickname(UWorld* World, int32 LocalUserNum, FString& OutNickname) override;

	virtual bool AutoLogin(UWorld* World, int32 LocalUserNum, const FOnlineAutoLoginComplete& InCompletionDelegate) override;
	virtual bool IsLoggedIn(UWorld* World, int32 LocalUserNum) override;

private:

	FDelegateHandle OnLoginCompleteDelegateHandle;
	void OnAutoLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error, FName OnlineIdentifier, FOnlineAutoLoginComplete InCompletionDelegate);

	/**
	 * Session
	 */
public:

	virtual void StartSession(UWorld* World, FName SessionName, FOnlineSessionStartComplete& InCompletionDelegate) override;
	virtual void EndSession(UWorld* World, FName SessionName, FOnlineSessionEndComplete& InCompletionDelegate) override;
	virtual bool DoesSessionExist(UWorld* World, FName SessionName) override;

	virtual bool GetSessionJoinability(UWorld* World, FName SessionName, FJoinabilitySettings& OutSettings) override;
	virtual void UpdateSessionJoinability(UWorld* World, FName SessionName, bool bPublicSearchable, bool bAllowInvites, bool bJoinViaPresence, bool bJoinViaPresenceFriendsOnly) override;

	virtual void RegisterPlayer(UWorld* World, FName SessionName, const FUniqueNetId& UniqueId, bool bWasInvited) override;
	virtual void UnregisterPlayer(UWorld* World, FName SessionName, const FUniqueNetId& UniqueId) override;

	virtual bool GetResolvedConnectString(UWorld* World, FName SessionName, FString& URL) override;

private:

	/** Mapping of delegate handles for each online StartSession() call while in flight */
	TMap<FName, FDelegateHandle> OnStartSessionCompleteDelegateHandles;
	void OnStartSessionComplete(FName SessionName, bool bWasSuccessful, FName OnlineIdentifier, FOnlineSessionStartComplete CompletionDelegate);

	/** Mapping of delegate handles for each online EndSession() call while in flight */
	TMap<FName, FDelegateHandle> OnEndSessionCompleteDelegateHandles;
	void OnEndSessionComplete(FName SessionName, bool bWasSuccessful, FName OnlineIdentifier, FOnlineSessionEndComplete CompletionDelegate);

	/**
	 * Voice
	 */
public:

	virtual TSharedPtr<FVoicePacket> GetLocalPacket(UWorld* World, uint8 LocalUserNum) override;
	virtual TSharedPtr<FVoicePacket> SerializeRemotePacket(UWorld* World, FArchive& Ar) override;

	virtual void StartNetworkedVoice(UWorld* World, uint8 LocalUserNum) override;
	virtual void StopNetworkedVoice(UWorld* World, uint8 LocalUserNum) override;
	virtual void ClearVoicePackets(UWorld* World) override;

	virtual bool MuteRemoteTalker(UWorld* World, uint8 LocalUserNum, const FUniqueNetId& PlayerId, bool bIsSystemWide) override;
	virtual bool UnmuteRemoteTalker(UWorld* World, uint8 LocalUserNum, const FUniqueNetId& PlayerId, bool bIsSystemWide) override;

	virtual int32 GetNumLocalTalkers(UWorld* World) override;

	/**
	 * External UI
	 */
public:

	virtual void ShowLeaderboardUI(UWorld* World, const FString& CategoryName) override;
	virtual void ShowAchievementsUI(UWorld* World, int32 LocalUserNum) override;
	virtual void BindToExternalUIOpening(const FOnlineExternalUIChanged& Delegate) override;

private:

	void OnExternalUIChange(bool bInIsOpening, FOnlineExternalUIChanged Delegate);

	/**
	 * Debug
	 */
public:

	virtual void DumpSessionState(UWorld* World) override;
	virtual void DumpPartyState(UWorld* World) override;
	virtual void DumpVoiceState(UWorld* World) override;
	virtual void DumpChatState(UWorld* World) override;

#if WITH_EDITOR
	/**
	 * PIE Utilities
	 */
public:

	virtual bool SupportsOnlinePIE() override;
	virtual int32 GetNumPIELogins() override;
	virtual void SetForceDedicated(FName OnlineIdentifier, bool bForce) override;
	virtual void LoginPIEInstance(FName OnlineIdentifier, int32 LocalUserNum, int32 PIELoginNum, FOnPIELoginComplete& CompletionDelegate) override;
#endif

private:

	/** Mapping of delegate handles for each online Login() call while in flight */
	TMap<FName, FDelegateHandle> OnLoginPIECompleteDelegateHandlesForPIEInstances;
	void OnPIELoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error, FName OnlineIdentifier, FOnlineAutoLoginComplete InCompletionDelegate);

};

