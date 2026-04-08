#include "CadMasterActor.h"

#include "CadRobotActor.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "TcpSocketConnection.h"

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

	if (SocketConnectionActor && IsValid(SocketConnectionActor))
	{
		SocketConnectionActor->Destroy();
		SocketConnectionActor = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

bool ACadMasterActor::ConnectRobot(ACadRobotActor* RobotActor, const FString& ServerAddress, int32 ServerPort, int32& OutConnectionId)
{
	OutConnectionId = INDEX_NONE;

	if (!IsValid(RobotActor))
	{
		return false;
	}

	if (!EnsureSocketConnectionActor())
	{
		return false;
	}

	FTcpSocketDisconnectDelegate OnDisconnected;
	OnDisconnected.BindDynamic(this, &ACadMasterActor::HandleSocketDisconnected);

	FTcpSocketConnectDelegate OnConnected;
	OnConnected.BindDynamic(this, &ACadMasterActor::HandleSocketConnected);

	FTcpSocketReceivedMessageDelegate OnMessageReceived;
	OnMessageReceived.BindDynamic(this, &ACadMasterActor::HandleSocketMessageReceived);

	int32 ConnectionId = INDEX_NONE;
	SocketConnectionActor->Connect(ServerAddress, ServerPort, OnDisconnected, OnConnected, OnMessageReceived, ConnectionId);
	if (ConnectionId == INDEX_NONE)
	{
		UE_LOG(LogCadMasterIO, Warning, TEXT("Master failed to allocate a connection id for %s."), *RobotActor->GetName());
		return false;
	}

	RobotByConnectionId.Add(ConnectionId, RobotActor);
	OutConnectionId = ConnectionId;
	return true;
}

void ACadMasterActor::DisconnectRobot(int32 ConnectionId)
{
	if (SocketConnectionActor && IsValid(SocketConnectionActor) && ConnectionId != INDEX_NONE)
	{
		SocketConnectionActor->Disconnect(ConnectionId);
	}

	RobotByConnectionId.Remove(ConnectionId);
}

bool ACadMasterActor::IsRobotConnected(int32 ConnectionId) const
{
	if (!SocketConnectionActor || !IsValid(SocketConnectionActor) || ConnectionId == INDEX_NONE)
	{
		return false;
	}

	return SocketConnectionActor->isConnected(ConnectionId);
}

bool ACadMasterActor::SendRobotData(int32 ConnectionId, TArray<uint8> Data)
{
	if (!SocketConnectionActor || !IsValid(SocketConnectionActor) || ConnectionId == INDEX_NONE)
	{
		return false;
	}

	return SocketConnectionActor->SendData(ConnectionId, MoveTemp(Data));
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

void ACadMasterActor::HandleSocketDisconnected(int32 ConnectionId)
{
	TObjectPtr<ACadRobotActor> RobotActor = nullptr;
	if (TObjectPtr<ACadRobotActor>* FoundRobotActor = RobotByConnectionId.Find(ConnectionId))
	{
		RobotActor = *FoundRobotActor;
	}

	RobotByConnectionId.Remove(ConnectionId);

	if (IsValid(RobotActor))
	{
		RobotActor->NotifySocketDisconnected(ConnectionId);
		return;
	}

	UE_LOG(LogCadMasterIO, Verbose, TEXT("Dropping disconnected event for unknown connection_id=%d."), ConnectionId);
}

void ACadMasterActor::HandleSocketMessageReceived(int32 ConnectionId, TArray<uint8>& Message)
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

bool ACadMasterActor::EnsureSocketConnectionActor()
{
	if (SocketConnectionActor && IsValid(SocketConnectionActor))
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SocketConnectionActor = World->SpawnActor<ATcpSocketConnection>(
		ATcpSocketConnection::StaticClass(),
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParameters);
	if (!SocketConnectionActor)
	{
		UE_LOG(LogCadMasterIO, Error, TEXT("Failed to spawn TcpSocketConnection helper actor."));
		return false;
	}

	SocketConnectionActor->SetActorHiddenInGame(true);
	SocketConnectionActor->SetActorEnableCollision(false);
	return true;
}
