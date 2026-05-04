#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

class UBlueprint;

namespace CadWorkflowBlueprintBuild
{
	void CollectDirectLeafChildrenForBuild(const FCadMasterDoc& MasterDocument, TArray<FCadChildEntry>& OutChildren);

	bool TryBuildMasterBlueprintsRecursive(
		const FCadMasterDoc& MasterDocument,
		const FString& MasterJsonPath,
		TMap<FString, UBlueprint*>& InOutMasterBlueprintsByJsonPath,
		FString& OutError);

	bool TryBuildChildBlueprintsRecursive(
		const FCadMasterDoc& MasterDocument,
		const FString& MasterJsonPath,
		const FString& DefaultContentRootPath,
		TMap<FString, UBlueprint*>& InOutChildBlueprintsByJsonPath,
		FString& OutError);
}
