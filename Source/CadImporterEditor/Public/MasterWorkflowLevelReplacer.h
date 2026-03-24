#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

class UBlueprint;

struct FCadMasterWorkflowReplaceResult
{
	FString MasterActorPath;
	FString SpawnedActorPath;
	int32 SpawnedChildActorCount = 0;
	int32 DeletedActorCount = 0;
};

namespace CadMasterWorkflowLevelReplacer
{
	bool TryReplaceMasterHierarchyWithBlueprints(
		const FCadMasterJsonDocument& MasterDocument,
		UBlueprint* MasterBlueprint,
		const TMap<FString, UBlueprint*>& ChildBlueprintsByChildName,
		FCadMasterWorkflowReplaceResult& OutResult,
		FString& OutError);
}
