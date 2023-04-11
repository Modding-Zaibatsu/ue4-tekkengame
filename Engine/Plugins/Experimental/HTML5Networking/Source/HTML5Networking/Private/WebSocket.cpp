// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.
#include "HTML5NetworkingPCH.h"
#include "WebSocket.h"

#if PLATFORM_HTML5
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <emscripten.h>
#endif

#if PLATFORM_WINDOWS
#include "AllowWindowsPlatformTypes.h"
#endif


#if !PLATFORM_HTML5
#include "libwebsockets.h"
#include "private-libwebsockets.h"
#endif 

#if PLATFORM_WINDOWS
#include "HideWindowsPlatformTypes.h"
#endif


#if !PLATFORM_HTML5
uint8 PREPADDING[LWS_SEND_BUFFER_PRE_PADDING];
uint8 POSTPADDING[LWS_SEND_BUFFER_POST_PADDING];
#endif 


#if !PLATFORM_HTML5
static void libwebsocket_debugLogS(int level, const char *line)
{
	UE_LOG(LogHTML5Networking, Log, TEXT("client: %s"), ANSI_TO_TCHAR(line));
}
#endif 

FWebSocket::FWebSocket(
		const FInternetAddr& ServerAddress
)
:IsServerSide(false)
{

#if !PLATFORM_HTML5_BROWSER

#if !UE_BUILD_SHIPPING
	lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_DEBUG | LLL_INFO, libwebsocket_debugLogS);
#endif 

	Protocols = new libwebsocket_protocols[3];
	FMemory::Memzero(Protocols, sizeof(libwebsocket_protocols) * 3);

	Protocols[0].name = "binary";
	Protocols[0].callback = FWebSocket::unreal_networking_client;
	Protocols[0].per_session_data_size = 0;
	Protocols[0].rx_buffer_size = 10 * 1024 * 1024;

	Protocols[1].name = nullptr;
	Protocols[1].callback = nullptr;
	Protocols[1].per_session_data_size = 0;

	struct lws_context_creation_info Info;
	memset(&Info, 0, sizeof Info);

	Info.port = CONTEXT_PORT_NO_LISTEN;
	Info.protocols = &Protocols[0];
	Info.gid = -1;
	Info.uid = -1;
	Info.user = this;

	Context = libwebsocket_create_context(&Info);

	check(Context); 


	Wsi = libwebsocket_client_connect_extended
							(Context, 
							TCHAR_TO_ANSI(*ServerAddress.ToString(false)), 
							ServerAddress.GetPort(), 
							false, "/", TCHAR_TO_ANSI(*ServerAddress.ToString(false)), TCHAR_TO_ANSI(*ServerAddress.ToString(false)), Protocols[1].name, -1,this);

	check(Wsi);

#endif 

#if PLATFORM_HTML5_BROWSER

	struct sockaddr_in addr;
	int res;

	SockFd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (SockFd == -1) {
		UE_LOG(LogHTML5Networking, Error, TEXT("Socket creationg failed "));
	}
	else
	{
		UE_LOG(LogHTML5Networking, Warning, TEXT(" Socked %d created "), SockFd);
	}

	fcntl(SockFd, F_SETFL, O_NONBLOCK);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(ServerAddress.GetPort());

	if (inet_pton(AF_INET, TCHAR_TO_ANSI(*ServerAddress.ToString(false)), &addr.sin_addr) != 1) {
		UE_LOG(LogHTML5Networking, Warning, TEXT("inet_pton failed "));
		return; 
	}

	int Ret = connect(SockFd, (struct sockaddr *)&addr, sizeof(addr));
	UE_LOG(LogHTML5Networking, Warning, TEXT(" Connect socket returned %d"), Ret);

#endif 
}

FWebSocket::FWebSocket(WebSocketInternalContext* InContext, WebSocketInternal* InWsi)
	: Context(InContext)
	, Wsi(InWsi)
	, IsServerSide(true)
	, Protocols(nullptr)
{
}


bool FWebSocket::Send(uint8* Data, uint32 Size)
{
	TArray<uint8> Buffer;
	// insert size. 

#if !PLATFORM_HTML5
	Buffer.Append((uint8*)&PREPADDING, sizeof(PREPADDING));
#endif 

	Buffer.Append((uint8*)&Size, sizeof (uint32));
	Buffer.Append((uint8*)Data, Size);

#if !PLATFORM_HTML5
	Buffer.Append((uint8*)&POSTPADDING, sizeof(POSTPADDING));
#endif 

	OutgoingBuffer.Add(Buffer);

	return true;
}

void FWebSocket::SetRecieveCallBack(FWebsocketPacketRecievedCallBack CallBack)
{
	RecievedCallBack = CallBack; 
}

FString FWebSocket::RemoteEndPoint()
{
#if !PLATFORM_HTML5
	ANSICHAR Peer_Name[128];
	ANSICHAR Peer_Ip[128];
	libwebsockets_get_peer_addresses(Context, Wsi, libwebsocket_get_socket_fd(Wsi), Peer_Name, sizeof Peer_Name, Peer_Ip, sizeof Peer_Ip);
	return FString(Peer_Name);
#endif

#if PLATFORM_HTML5
	return FString(TEXT("TODO:REMOTEENDPOINT"));
#endif
}


FString FWebSocket::LocalEndPoint()
{
#if !PLATFORM_HTML5
	return FString(ANSI_TO_TCHAR(libwebsocket_canonical_hostname(Context)));
#endif 

#if PLATFORM_HTML5
	return FString(TEXT("TODO:LOCALENDPOINT"));
#endif

}

void FWebSocket::Tick()
{
	HandlePacket();
}

void FWebSocket::HandlePacket()
{
#if !PLATFORM_HTML5
	{
		libwebsocket_service(Context, 0);
		if (!IsServerSide)
			libwebsocket_callback_on_writable_all_protocol(&Protocols[0]);
	}
#endif 

#if PLATFORM_HTML5

	fd_set Fdr;
	fd_set Fdw;
	int Res;

	// make sure that server.fd is ready to read / write
	FD_ZERO(&Fdr);
	FD_ZERO(&Fdw);
	FD_SET(SockFd, &Fdr);
	FD_SET(SockFd, &Fdw);
	Res = select(64, &Fdr, &Fdw, NULL, NULL);

	if (Res == -1) {
		UE_LOG(LogHTML5Networking, Warning, TEXT("Select Failed!"));
		return;
	}
	
	if (FD_ISSET(SockFd, &Fdr)) {
		// we can read! 
		OnRawRecieve(NULL, NULL);
	}

	if (FD_ISSET(SockFd, &Fdw)) {
		// we can write
		OnRawWebSocketWritable(NULL);
	}
#endif 
}

void FWebSocket::Flush()
{
	auto PendingMesssages = OutgoingBuffer.Num();
	while (OutgoingBuffer.Num() > 0 && !IsServerSide)
	{
#if !PLATFORM_HTML5
		if (Protocols)
			libwebsocket_callback_on_writable_all_protocol(&Protocols[0]);
		else
			libwebsocket_callback_on_writable(Context, Wsi);
#endif
		HandlePacket();
		if (PendingMesssages >= OutgoingBuffer.Num()) 
		{
			UE_LOG(LogHTML5Networking, Warning, TEXT("Unable to flush all of OutgoingBuffer in FWebSocket."));
			break;
		}
	};
}

void FWebSocket::SetConnectedCallBack(FWebsocketInfoCallBack CallBack)
{
	ConnectedCallBack = CallBack; 
}

void FWebSocket::SetErrorCallBack(FWebsocketInfoCallBack CallBack)
{
	ErrorCallBack = CallBack; 
}

void FWebSocket::OnRawRecieve(void* Data, uint32 Size)
{
#if PLATFORM_HTML5
	check(Data == NULL); // jic this is not obvious, Data will be resigned to Buffer below

	uint8 Buffer[1024]; // should be at MAX PACKET SIZE.
	Data = (void*)Buffer;
	Size = recv(SockFd, Data, sizeof(Buffer), 0);
	while ( Size > 0 )
	{
#endif
		RecievedBuffer.Append((uint8*)Data, Size); // consumes all of Data
		while (RecievedBuffer.Num())
		{
			uint32 BytesToBeRead = *(uint32*)RecievedBuffer.GetData();
			if (BytesToBeRead <= ((uint32)RecievedBuffer.Num() - sizeof(uint32)))
			{
				RecievedCallBack.ExecuteIfBound((void*)((uint8*)RecievedBuffer.GetData() + sizeof(uint32)), BytesToBeRead);
				RecievedBuffer.RemoveAt(0, sizeof(uint32) + BytesToBeRead );
			}
			else
			{
				break;
			}
		}
#if PLATFORM_HTML5
		Size = recv(SockFd, Data, sizeof(Buffer), 0);
	}
#endif
}

void FWebSocket::OnRawWebSocketWritable(WebSocketInternal* wsi)
{
	if (OutgoingBuffer.Num() == 0)
		return;

	TArray <uint8>& Packet = OutgoingBuffer[0];

#if !PLATFORM_HTML5_BROWSER

	uint32 TotalDataSize = Packet.Num() - LWS_SEND_BUFFER_PRE_PADDING - LWS_SEND_BUFFER_POST_PADDING;
	uint32 DataToSend = TotalDataSize;
	while (DataToSend)
	{
		int Sent = libwebsocket_write(Wsi, Packet.GetData() + LWS_SEND_BUFFER_PRE_PADDING + (DataToSend-TotalDataSize), DataToSend, (libwebsocket_write_protocol)LWS_WRITE_BINARY);
		if (Sent < 0)
		{
			ErrorCallBack.ExecuteIfBound();
			return;
		}
		if ((uint32)Sent < DataToSend)
		{
			UE_LOG(LogHTML5Networking, Warning, TEXT("Could not write all '%d' bytes to socket"), DataToSend);
		}
		DataToSend-=Sent;
	}

	check(Wsi == wsi);

#endif

#if  PLATFORM_HTML5_BROWSER

	uint32 TotalDataSize = Packet.Num();
	uint32 DataToSend = TotalDataSize;
	while (DataToSend)
	{
		// send actual data in one go. 
		int Result = send(SockFd, Packet.GetData()+(DataToSend-TotalDataSize),DataToSend, 0);
		if (Result == -1)
		{
			// we are caught with our pants down. fail. 
			UE_LOG(LogHTML5Networking, Error, TEXT("Could not write %d bytes"), Packet.Num());
			ErrorCallBack.ExecuteIfBound(); 
			return;
		}
		UE_CLOG((uint32)Result < DataToSend, LogHTML5Networking, Warning, TEXT("Could not write all '%d' bytes to socket"), DataToSend);
		DataToSend-=Result;
	}
	
#endif 

	// this is very inefficient we need a constant size circular buffer to efficiently not do unnecessary allocations/deallocations. 
	OutgoingBuffer.RemoveAt(0);

}

FWebSocket::~FWebSocket()
{
	RecievedCallBack.Unbind();
	Flush();

#if !PLATFORM_HTML5
	if ( !IsServerSide)
	{
		libwebsocket_context_destroy(Context);
		Context = NULL;
		delete Protocols;
		Protocols = NULL;
	}
#endif 

#if PLATFORM_HTML5
	close(SockFd);
#endif 

}

#if !PLATFORM_HTML5
int FWebSocket::unreal_networking_client(
		struct libwebsocket_context *Context, 
		struct libwebsocket *Wsi, 
		enum libwebsocket_callback_reasons Reason, 
		void *User, 
		void *In, 
		size_t Len)
{
	FWebSocket* Socket = (FWebSocket*)libwebsocket_context_user(Context);;
	switch (Reason)
	{
	case LWS_CALLBACK_CLIENT_ESTABLISHED:
		{
			Socket->ConnectedCallBack.ExecuteIfBound();
			libwebsocket_set_timeout(Wsi, NO_PENDING_TIMEOUT, 0);
			check(Socket->Wsi == Wsi);
		}
		break;
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		{
			Socket->ErrorCallBack.ExecuteIfBound();
			return -1;
		}
		break;
	case LWS_CALLBACK_CLIENT_RECEIVE:
		{
			// push it on the socket. 
			Socket->OnRawRecieve(In, (uint32)Len); 
			check(Socket->Wsi == Wsi);
			libwebsocket_set_timeout(Wsi, NO_PENDING_TIMEOUT, 0);
			break;
		}
	case LWS_CALLBACK_CLIENT_WRITEABLE:
		{
			check(Socket->Wsi == Wsi);
			Socket->OnRawWebSocketWritable(Wsi); 
			libwebsocket_callback_on_writable(Context, Wsi);
			libwebsocket_set_timeout(Wsi, NO_PENDING_TIMEOUT, 0);
			break; 
		}
	case LWS_CALLBACK_CLOSED:
		{
			Socket->ErrorCallBack.ExecuteIfBound();
			return -1;
		}
	}

	return 0; 
}
#endif 

