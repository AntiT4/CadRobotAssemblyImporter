// Copyright (c) 2019 CodeSpartan. Portions adapted from UE4TcpSocketPlugin.
// Copyright (c) 2026 BlowDigitalTwin contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"

class FSocket;

class FCadRobotSocketClient final : public FRunnable
{
public:
	using FConnectedCallback = TFunction<void(int32)>;
	using FDisconnectedCallback = TFunction<void(int32, bool)>;
	using FMessageReceivedCallback = TFunction<void(int32, TArray<uint8>)>;

	FCadRobotSocketClient(
		int32 InConnectionId,
		FString InServerAddress,
		int32 InServerPort,
		float InReconnectIntervalSec,
		bool bInReconnectEnabled,
		FConnectedCallback InOnConnected,
		FDisconnectedCallback InOnDisconnected,
		FMessageReceivedCallback InOnMessageReceived);
	virtual ~FCadRobotSocketClient() override;

	void Start();
	void Stop();
	void SendData(TArray<uint8> Data);
	bool IsConnected() const;

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Exit() override;

private:
	bool TryConnect();
	bool PumpConnectedSocket();
	bool SendQueuedData();
	bool ReceivePendingData();
	void CloseSocket();
	void SleepBeforeReconnect() const;
	void NotifyConnected();
	void NotifyDisconnected(bool bWillReconnect);

	int32 ConnectionId = INDEX_NONE;
	FString ServerAddress;
	int32 ServerPort = 0;
	float ReconnectIntervalSec = 1.0f;
	bool bReconnectEnabled = false;

	FConnectedCallback OnConnected;
	FDisconnectedCallback OnDisconnected;
	FMessageReceivedCallback OnMessageReceived;

	FRunnableThread* Thread = nullptr;
	FSocket* Socket = nullptr;
	FThreadSafeBool bRun = false;
	FThreadSafeBool bConnected = false;
	TQueue<TArray<uint8>, EQueueMode::Mpsc> Outbox;
};
