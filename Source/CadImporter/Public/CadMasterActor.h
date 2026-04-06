#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CadMasterActor.generated.h"

class ACadRobotActor;
class ATcpSocketConnection;
class USceneComponent;

USTRUCT(BlueprintType)
struct CADIMPORTER_API FCadMasterActorMetadata
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Metadata")
	FString MasterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Metadata")
	FString WorkspaceFolder;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Metadata")
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Metadata")
	FString SchemaVersion = TEXT("master_json_v1");
};

USTRUCT(BlueprintType)
struct CADIMPORTER_API FCadMasterChildPlacement
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Children")
	FString ChildName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Children")
	FString ChildJsonFileName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Children")
	FTransform RelativeTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Children")
	bool bMovable = false;
};

UCLASS(BlueprintType, Blueprintable)
class CADIMPORTER_API ACadMasterActor : public AActor
{
	GENERATED_BODY()

public:
	ACadMasterActor();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master")
	FCadMasterActorMetadata Metadata;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master")
	TArray<FCadMasterChildPlacement> ChildPlacements;

	bool ConnectRobot(ACadRobotActor* RobotActor, const FString& ServerAddress, int32 ServerPort, int32& OutConnectionId);
	void DisconnectRobot(int32 ConnectionId);
	bool IsRobotConnected(int32 ConnectionId) const;
	bool SendRobotData(int32 ConnectionId, TArray<uint8> Data);

private:
	UFUNCTION()
	void HandleSocketConnected(int32 ConnectionId);

	UFUNCTION()
	void HandleSocketDisconnected(int32 ConnectionId);

	UFUNCTION()
	void HandleSocketMessageReceived(int32 ConnectionId, UPARAM(ref) TArray<uint8>& Message);

	bool EnsureSocketConnectionActor();

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "CAD Master", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(Transient)
	TObjectPtr<ATcpSocketConnection> SocketConnectionActor = nullptr;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<ACadRobotActor>> RobotByConnectionId;
};
