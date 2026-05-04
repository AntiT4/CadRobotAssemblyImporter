#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CadMasterActor.generated.h"

class ACadRobotActor;
class FCadRobotSocketClient;
class USceneComponent;

UENUM(BlueprintType)
enum class ECadMasterPlacementNodeType : uint8
{
	Static UMETA(DisplayName = "Static"),
	Background UMETA(DisplayName = "Background"),
	Robot UMETA(DisplayName = "Robot"),
	Master UMETA(DisplayName = "Master")
};

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
	FString SchemaVersion = TEXT("master_json_v2");
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

USTRUCT(BlueprintType)
struct CADIMPORTER_API FCadMasterPlacementNodeRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Hierarchy")
	FString NodeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Hierarchy")
	ECadMasterPlacementNodeType NodeType = ECadMasterPlacementNodeType::Static;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Hierarchy")
	FString ChildJsonFileName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Hierarchy")
	FString MasterJsonFileName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Hierarchy")
	FTransform RelativeTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master|Hierarchy")
	int32 ParentNodeIndex = INDEX_NONE;
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
	TArray<FCadMasterPlacementNodeRecord> HierarchyNodes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master", meta = (DeprecatedProperty, DeprecationMessage = "Use HierarchyNodes instead."))
	TArray<FCadMasterChildPlacement> ChildPlacements;

	bool ConnectRobot(ACadRobotActor* RobotActor, const FString& ServerAddress, int32 ServerPort, float ReconnectIntervalSec, bool bReconnectEnabled, int32& OutConnectionId);
	void DisconnectRobot(int32 ConnectionId);
	bool IsRobotConnected(int32 ConnectionId) const;
	bool SendRobotData(int32 ConnectionId, TArray<uint8> Data);
	bool HasRobotConnection(int32 ConnectionId) const;

private:
	void HandleSocketConnected(int32 ConnectionId);

	void HandleSocketDisconnected(int32 ConnectionId, bool bWillReconnect);

	void HandleSocketMessageReceived(int32 ConnectionId, TArray<uint8> Message);

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "CAD Master", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<ACadRobotActor>> RobotByConnectionId;

	TMap<int32, TSharedPtr<FCadRobotSocketClient>> SocketClientByConnectionId;
	int32 NextConnectionId = 0;
};
