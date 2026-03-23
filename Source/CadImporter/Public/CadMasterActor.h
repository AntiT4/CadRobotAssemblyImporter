#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CadMasterActor.generated.h"

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master")
	FCadMasterActorMetadata Metadata;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAD Master")
	TArray<FCadMasterChildPlacement> ChildPlacements;

private:
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "CAD Master", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;
};
