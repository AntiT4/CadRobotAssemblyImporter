#pragma once

#include "CoreMinimal.h"
#include "CadRobotTypes.generated.h"

USTRUCT(BlueprintType)
struct CADIMPORTER_API FCadRobotJointCommand
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	FName JointName = NAME_None;

	// Target joint position command (unit depends on joint type, e.g. rad or m).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	float TargetPosition = 0.0f;
};

USTRUCT(BlueprintType)
struct CADIMPORTER_API FCadRobotCommand
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	double TimestampSec = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	TArray<FCadRobotJointCommand> Joints;
};

USTRUCT(BlueprintType)
struct CADIMPORTER_API FCadRobotJointStatus
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	FName JointName = NAME_None;

	// Current joint position (unit depends on joint type, e.g. rad or m).
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	float JointPosition = 0.0f;

	// Current joint velocity (unit depends on joint type, e.g. rad/s or m/s).
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	float JointVelocity = 0.0f;
};

USTRUCT(BlueprintType)
struct CADIMPORTER_API FCadRobotStatus
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	double TimestampSec = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	int32 ConnectionId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	TArray<FCadRobotJointStatus> Joints;
};

USTRUCT(BlueprintType)
struct CADIMPORTER_API FCadRobotRootPlacementHint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	bool bHasWorldTransform = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	FRotator WorldRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	FVector WorldScale = FVector::OneVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	FString ParentActorName;
};
