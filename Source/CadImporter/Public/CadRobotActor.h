#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CadRobotTypes.h"
#include "CadRobotActor.generated.h"

UCLASS(Blueprintable)
class CADIMPORTER_API ACadRobotActor : public AActor
{
	GENERATED_BODY()

public:
	ACadRobotActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	FCadRobotCommand Command;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "CAD Robot")
	FCadRobotStatus Status;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Robot|Import")
	FCadRobotRootPlacementHint ImportRootPlacementHint;

	UFUNCTION(BlueprintCallable, Category = "CAD Robot|IO")
	bool ApplyCommand(const FCadRobotCommand& InCommand);

	UFUNCTION(BlueprintCallable, Category = "CAD Robot|IO")
	bool RunController(float DeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = "CAD Robot|IO")
	bool UpdateStatus(float DeltaSeconds);
};
