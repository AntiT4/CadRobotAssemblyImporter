#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

class UBlueprint;

struct FCadLevelReplaceResult
{
	FString MasterActorPath;
	FString SpawnedActorPath;
	int32 SpawnedChildActorCount = 0;
	int32 PreservedChildActorCount = 0;
	int32 DeletedActorCount = 0;
	bool bUsedTransaction = false;
};

struct FCadLevelReplacePlan
{
	FString MasterActorPath;
	FString MasterParentActorPath;
	FString ChildJsonRootPath;
	int32 HierarchyRootCount = 0;
	TArray<FString> CandidateDeleteActorPaths;
	TArray<FString> PreservedDirectChildPaths;
};

namespace CadLevelReplacer
{
	bool TryBuildReplacementPlan(
		const FCadMasterDoc& MasterDocument,
		const TMap<FString, UBlueprint*>& MasterBlueprintsByJsonPath,
		const TMap<FString, UBlueprint*>& ChildBlueprintsByJsonPath,
		FCadLevelReplacePlan& OutPlan,
		FString& OutError);

	bool TryReplaceMasterHierarchyWithBlueprints(
		const FCadMasterDoc& MasterDocument,
		UBlueprint* MasterBlueprint,
		const TMap<FString, UBlueprint*>& MasterBlueprintsByJsonPath,
		const TMap<FString, UBlueprint*>& ChildBlueprintsByJsonPath,
		FCadLevelReplaceResult& OutResult,
		FString& OutError);
}
