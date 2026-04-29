#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CadRobotTypes.h"
#include "CadRobotActor.generated.h"

class ACadMasterActor;
class UPhysicsConstraintComponent;

enum class ECadRobotResolvedJointType : uint8
{
	Unknown,
	Fixed,
	Revolute,
	Prismatic
};

enum class ECadRobotResolvedAngularAxis : uint8
{
	Twist,
	Swing1,
	Swing2
};

enum class ECadRobotResolvedLinearAxis : uint8
{
	X,
	Y,
	Z
};

struct FCadRobotResolvedJoint
{
	FName PublishedName = NAME_None;
	TWeakObjectPtr<UPhysicsConstraintComponent> Constraint;
	ECadRobotResolvedJointType Type = ECadRobotResolvedJointType::Unknown;
	ECadRobotResolvedAngularAxis AngularAxis = ECadRobotResolvedAngularAxis::Twist;
	ECadRobotResolvedLinearAxis LinearAxis = ECadRobotResolvedLinearAxis::X;
	int32 StatusIndex = INDEX_NONE;
	float LastPublishedPosition = 0.0f;
	bool bHasLastPublishedPosition = false;
};

UCLASS(Blueprintable)
class CADIMPORTER_API ACadRobotActor : public AActor
{
	GENERATED_BODY()

public:
	ACadRobotActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "CAD Robot")
	FCadRobotCommand Command;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "CAD Robot")
	FCadRobotStatus Status;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot|Import")
	FCadRobotRootPlacementHint ImportRootPlacementHint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot|IO")
	bool bEnableSocketIO = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot|IO", meta = (EditCondition = "bEnableSocketIO"))
	bool bAutoConnect = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot|IO", meta = (EditCondition = "bEnableSocketIO"))
	FString ServerAddress = TEXT("127.0.0.1");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot|IO", meta = (ClampMin = "1", ClampMax = "65535", EditCondition = "bEnableSocketIO"))
	int32 ServerPort = 5500;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot|IO", meta = (ClampMin = "0.05", EditCondition = "bEnableSocketIO"))
	float ReconnectIntervalSec = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot|IO", meta = (EditCondition = "bEnableSocketIO"))
	bool bPublishStatusEveryTick = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CAD Robot|IO")
	bool bSocketConnected = false;

	UFUNCTION(BlueprintCallable, Category = "CAD Robot|IO")
	bool ApplyCommand(const FCadRobotCommand& InCommand);

	UFUNCTION(BlueprintCallable, Category = "CAD Robot|IO")
	bool RunController(float DeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = "CAD Robot|IO")
	bool UpdateStatus(float DeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = "CAD Robot|IO")
	bool ConnectIO();

	UFUNCTION(BlueprintCallable, Category = "CAD Robot|IO")
	void DisconnectIO();

	UFUNCTION(BlueprintPure, Category = "CAD Robot|IO")
	bool IsIOConnected() const;

private:
	void DiscoverRobotJoints();
	int32 FindResolvedJointIndex(FName JointName) const;
	void RegisterResolvedJointAlias(FName JointName, int32 JointIndex);
	void TryReconnectIfNeeded();
	void ApplyQueuedCommandIfAny();
	void ProcessPendingReceiveBytes();
	bool TryExtractNextCommandFrame(FString& OutCommandJson);
	bool PublishStatus();
	bool TryConsumeCommandFrame(const FString& CommandJson);
	float ReadJointPosition(const FCadRobotResolvedJoint& Joint) const;
	void ApplyJointTarget(const FCadRobotResolvedJoint& Joint, float TargetPosition) const;
	bool EnsureMasterActor();
	static FName NormalizeJointAlias(const FString& JointName);

public:
	void NotifySocketConnected(int32 ConnectionId);
	void NotifySocketDisconnected(int32 ConnectionId);
	void NotifySocketMessageReceived(int32 ConnectionId, TArray<uint8>& Message);

private:
	UPROPERTY(Transient)
	TObjectPtr<ACadMasterActor> MasterActor = nullptr;

	int32 ActiveConnectionId = INDEX_NONE;
	bool bSocketConnectInFlight = false;
	double LastConnectionAttemptTimeSec = -1.0;
	TArray<uint8> PendingReceiveBytes;
	bool bHasQueuedCommand = false;
	FCadRobotCommand QueuedCommand;
	TArray<FCadRobotResolvedJoint> ResolvedJoints;
	TMap<FName, int32> ResolvedJointIndexByName;
};
