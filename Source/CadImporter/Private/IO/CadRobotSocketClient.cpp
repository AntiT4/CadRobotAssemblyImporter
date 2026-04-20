// Copyright (c) 2019 CodeSpartan. Portions adapted from UE4TcpSocketPlugin.
// Copyright (c) 2026 BlowDigitalTwin contributors.
// SPDX-License-Identifier: MIT

#include "IO/CadRobotSocketClient.h"

#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

DEFINE_LOG_CATEGORY_STATIC(LogCadRobotSocketClient, Log, All);

FCadRobotSocketClient::FCadRobotSocketClient(
	int32 InConnectionId,
	FString InServerAddress,
	int32 InServerPort,
	float InReconnectIntervalSec,
	bool bInReconnectEnabled,
	FConnectedCallback InOnConnected,
	FDisconnectedCallback InOnDisconnected,
	FMessageReceivedCallback InOnMessageReceived)
	: ConnectionId(InConnectionId)
	, ServerAddress(MoveTemp(InServerAddress))
	, ServerPort(InServerPort)
	, ReconnectIntervalSec(FMath::Max(0.05f, InReconnectIntervalSec))
	, bReconnectEnabled(bInReconnectEnabled)
	, OnConnected(MoveTemp(InOnConnected))
	, OnDisconnected(MoveTemp(InOnDisconnected))
	, OnMessageReceived(MoveTemp(InOnMessageReceived))
{
}

FCadRobotSocketClient::~FCadRobotSocketClient()
{
	Stop();

	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	CloseSocket();
}

void FCadRobotSocketClient::Start()
{
	check(!Thread);

	bRun = true;
	Thread = FRunnableThread::Create(
		this,
		*FString::Printf(TEXT("FCadRobotSocketClient %s:%d"), *ServerAddress, ServerPort),
		128 * 1024,
		TPri_Normal);

	if (!Thread)
	{
		bRun = false;
		UE_LOG(LogCadRobotSocketClient, Error, TEXT("Failed to start robot IO socket thread for %s:%d."), *ServerAddress, ServerPort);
	}
}

void FCadRobotSocketClient::Stop()
{
	bRun = false;
}

void FCadRobotSocketClient::SendData(TArray<uint8> Data)
{
	Outbox.Enqueue(MoveTemp(Data));
}

bool FCadRobotSocketClient::IsConnected() const
{
	return bConnected;
}

bool FCadRobotSocketClient::Init()
{
	bConnected = false;
	return true;
}

uint32 FCadRobotSocketClient::Run()
{
	while (bRun)
	{
		if (!TryConnect())
		{
			CloseSocket();

			if (!bReconnectEnabled)
			{
				NotifyDisconnected(false);
				break;
			}

			SleepBeforeReconnect();
			continue;
		}

		bConnected = true;
		NotifyConnected();

		while (bRun && bConnected)
		{
			if (!PumpConnectedSocket())
			{
				bConnected = false;
				CloseSocket();

				if (!bRun)
				{
					break;
				}

				NotifyDisconnected(bReconnectEnabled);
				if (!bReconnectEnabled)
				{
					bRun = false;
					break;
				}

				SleepBeforeReconnect();
				break;
			}

			FPlatformProcess::Sleep(0.008f);
		}
	}

	bConnected = false;
	CloseSocket();
	return 0;
}

void FCadRobotSocketClient::Exit()
{
}

bool FCadRobotSocketClient::TryConnect()
{
	FIPv4Address IpAddress;
	if (!FIPv4Address::Parse(ServerAddress, IpAddress))
	{
		UE_LOG(LogCadRobotSocketClient, Warning, TEXT("Invalid robot IO server address '%s'."), *ServerAddress);
		return false;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogCadRobotSocketClient, Warning, TEXT("No socket subsystem available for robot IO."));
		return false;
	}

	CloseSocket();
	Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("CadRobotIO"), false);
	if (!Socket)
	{
		UE_LOG(LogCadRobotSocketClient, Warning, TEXT("Failed to create robot IO socket."));
		return false;
	}

	TSharedRef<FInternetAddr> InternetAddr = SocketSubsystem->CreateInternetAddr();
	InternetAddr->SetIp(IpAddress.Value);
	InternetAddr->SetPort(ServerPort);

	if (!Socket->Connect(*InternetAddr))
	{
		return false;
	}

	Socket->SetNonBlocking(true);
	return true;
}

bool FCadRobotSocketClient::PumpConnectedSocket()
{
	if (!Socket || Socket->GetConnectionState() == SCS_ConnectionError)
	{
		return false;
	}

	return SendQueuedData() && ReceivePendingData();
}

bool FCadRobotSocketClient::SendQueuedData()
{
	TArray<uint8> Data;
	while (Outbox.Dequeue(Data))
	{
		if (!Socket)
		{
			return false;
		}

		Socket->SetNonBlocking(false);

		int32 TotalSent = 0;
		while (TotalSent < Data.Num())
		{
			int32 BytesSent = 0;
			if (!Socket->Send(Data.GetData() + TotalSent, Data.Num() - TotalSent, BytesSent) || BytesSent <= 0)
			{
				Socket->SetNonBlocking(true);
				return false;
			}

			TotalSent += BytesSent;
		}

		Socket->SetNonBlocking(true);
	}

	return true;
}

bool FCadRobotSocketClient::ReceivePendingData()
{
	uint32 PendingDataSize = 0;
	while (Socket && Socket->HasPendingData(PendingDataSize) && PendingDataSize > 0)
	{
		TArray<uint8> ReceivedData;
		ReceivedData.SetNumUninitialized(static_cast<int32>(PendingDataSize));

		int32 BytesRead = 0;
		if (!Socket->Recv(ReceivedData.GetData(), ReceivedData.Num(), BytesRead))
		{
			return false;
		}

		if (BytesRead > 0)
		{
			ReceivedData.SetNum(BytesRead, EAllowShrinking::No);
			FMessageReceivedCallback MessageCallback = OnMessageReceived;
			const int32 CallbackConnectionId = ConnectionId;

			AsyncTask(ENamedThreads::GameThread, [MessageCallback = MoveTemp(MessageCallback), CallbackConnectionId, Message = MoveTemp(ReceivedData)]() mutable
			{
				if (MessageCallback)
				{
					MessageCallback(CallbackConnectionId, MoveTemp(Message));
				}
			});
		}
	}

	return true;
}

void FCadRobotSocketClient::CloseSocket()
{
	if (!Socket)
	{
		return;
	}

	Socket->Close();

	if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
	{
		SocketSubsystem->DestroySocket(Socket);
	}

	Socket = nullptr;
}

void FCadRobotSocketClient::SleepBeforeReconnect() const
{
	const double EndTimeSec = FPlatformTime::Seconds() + static_cast<double>(ReconnectIntervalSec);
	while (bRun && FPlatformTime::Seconds() < EndTimeSec)
	{
		FPlatformProcess::Sleep(0.05f);
	}
}

void FCadRobotSocketClient::NotifyConnected()
{
	FConnectedCallback ConnectedCallback = OnConnected;
	const int32 CallbackConnectionId = ConnectionId;

	AsyncTask(ENamedThreads::GameThread, [ConnectedCallback = MoveTemp(ConnectedCallback), CallbackConnectionId]() mutable
	{
		if (ConnectedCallback)
		{
			ConnectedCallback(CallbackConnectionId);
		}
	});
}

void FCadRobotSocketClient::NotifyDisconnected(bool bWillReconnect)
{
	FDisconnectedCallback DisconnectedCallback = OnDisconnected;
	const int32 CallbackConnectionId = ConnectionId;

	AsyncTask(ENamedThreads::GameThread, [DisconnectedCallback = MoveTemp(DisconnectedCallback), CallbackConnectionId, bWillReconnect]() mutable
	{
		if (DisconnectedCallback)
		{
			DisconnectedCallback(CallbackConnectionId, bWillReconnect);
		}
	});
}
