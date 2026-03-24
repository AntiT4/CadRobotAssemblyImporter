#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

class UBlueprint;

struct FCadLevelReplaceResult
{
	FString MasterActorPath;
	FString SpawnedActorPath;
	int32 SpawnedChildActorCount = 0;
	int32 DeletedActorCount = 0;
};

namespace CadLevelReplacer
{
	bool TryReplaceMasterHierarchyWithBlueprints(
		const FCadMasterJsonDocument& MasterDocument,
		UBlueprint* MasterBlueprint,
		const TMap<FString, UBlueprint*>& ChildBlueprintsByChildName,
		FCadLevelReplaceResult& OutResult,
		FString& OutError);
}
