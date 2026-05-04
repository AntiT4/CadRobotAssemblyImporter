#include "CadMasterActor.h"

#include "IO/CadRobotSocketClient.h"
#include "CadRobotActor.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogCadMasterIO, Log, All);

ACadMasterActor::ACadMasterActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	SetRootComponent(SceneRoot);
}

void ACadMasterActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TArray<int32> ConnectionIds;
	RobotByConnectionId.GetKeys(ConnectionIds);

	for (const int32 ConnectionId : ConnectionIds)
	{
		DisconnectRobot(ConnectionId);
	}

	Super::EndPlay(EndPlayReason);
}

bool ACadMasterActor::ConnectRobot(ACadRobotActor* RobotActor, const FString& ServerAddress, int32 ServerPort, float ReconnectIntervalSec, bool bReconnectEnabled, int32& OutConnectionId)
{
	OutConnectionId = INDEX_NONE;

	if (!IsValid(RobotActor))
	{
		return false;
	}

	const int32 ConnectionId = NextConnectionId++;
	TWeakObjectPtr<ACadMasterActor> WeakThis(this);

	TSharedPtr<FCadRobotSocketClient> SocketClient = MakeShared<FCadRobotSocketClient>(
		ConnectionId,
		ServerAddress,
		ServerPort,
		ReconnectIntervalSec,
		bReconnectEnabled,
		[WeakThis](int32 ConnectedConnectionId)
		{
			if (ACadMasterActor* MasterActor = WeakThis.Get())
			{
				MasterActor->HandleSocketConnected(ConnectedConnectionId);
			}
		},
		[WeakThis](int32 DisconnectedConnectionId, bool bWillReconnect)
		{
			if (ACadMasterActor* MasterActor = WeakThis.Get())
			{
				MasterActor->HandleSocketDisconnected(DisconnectedConnectionId, bWillReconnect);
			}
		},
		[WeakThis](int32 MessageConnectionId, TArray<uint8> Message)
		{
			if (ACadMasterActor* MasterActor = WeakThis.Get())
			{
				MasterActor->HandleSocketMessageReceived(MessageConnectionId, MoveTemp(Message));
			}
		});

	RobotByConnectionId.Add(ConnectionId, RobotActor);
	SocketClientByConnectionId.Add(ConnectionId, SocketClient);
	OutConnectionId = ConnectionId;
	SocketClient->Start();
	return true;
}

void ACadMasterActor::DisconnectRobot(int32 ConnectionId)
{
	if (TSharedPtr<FCadRobotSocketClient>* SocketClient = SocketClientByConnectionId.Find(ConnectionId))
	{
		if (SocketClient->IsValid())
		{
			(*SocketClient)->Stop();
		}
	}

	SocketClientByConnectionId.Remove(ConnectionId);
	RobotByConnectionId.Remove(ConnectionId);
}

bool ACadMasterActor::IsRobotConnected(int32 ConnectionId) const
{
	if (const TSharedPtr<FCadRobotSocketClient>* SocketClient = SocketClientByConnectionId.Find(ConnectionId))
	{
		return SocketClient->IsValid() && (*SocketClient)->IsConnected();
	}

	return false;
}

bool ACadMasterActor::SendRobotData(int32 ConnectionId, TArray<uint8> Data)
{
	if (TSharedPtr<FCadRobotSocketClient>* SocketClient = SocketClientByConnectionId.Find(ConnectionId))
	{
		if (SocketClient->IsValid() && (*SocketClient)->IsConnected())
		{
			(*SocketClient)->SendData(MoveTemp(Data));
			return true;
		}
	}

	return false;
}

bool ACadMasterActor::HasRobotConnection(int32 ConnectionId) const
{
	return SocketClientByConnectionId.Contains(ConnectionId);
}

void ACadMasterActor::HandleSocketConnected(int32 ConnectionId)
{
	if (TObjectPtr<ACadRobotActor>* RobotActor = RobotByConnectionId.Find(ConnectionId))
	{
		if (IsValid(*RobotActor))
		{
			(*RobotActor)->NotifySocketConnected(ConnectionId);
			return;
		}
	}

	UE_LOG(LogCadMasterIO, Verbose, TEXT("Dropping connected event for unknown connection_id=%d."), ConnectionId);
}

void ACadMasterActor::HandleSocketDisconnected(int32 ConnectionId, bool bWillReconnect)
{
	TObjectPtr<ACadRobotActor> RobotActor = nullptr;
	if (TObjectPtr<ACadRobotActor>* FoundRobotActor = RobotByConnectionId.Find(ConnectionId))
	{
		RobotActor = *FoundRobotActor;
	}

	if (!bWillReconnect)
	{
		SocketClientByConnectionId.Remove(ConnectionId);
		RobotByConnectionId.Remove(ConnectionId);
	}

	if (IsValid(RobotActor))
	{
		RobotActor->NotifySocketDisconnected(ConnectionId);
		return;
	}

	UE_LOG(LogCadMasterIO, Verbose, TEXT("Dropping disconnected event for unknown connection_id=%d."), ConnectionId);
}

void ACadMasterActor::HandleSocketMessageReceived(int32 ConnectionId, TArray<uint8> Message)
{
	if (TObjectPtr<ACadRobotActor>* RobotActor = RobotByConnectionId.Find(ConnectionId))
	{
		if (IsValid(*RobotActor))
		{
			(*RobotActor)->NotifySocketMessageReceived(ConnectionId, Message);
			return;
		}
	}

	UE_LOG(LogCadMasterIO, Verbose, TEXT("Dropping message for unknown connection_id=%d."), ConnectionId);
}
