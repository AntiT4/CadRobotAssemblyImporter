#pragma once

#include "WorkflowTypes.h"

class UBlueprint;

namespace CadMasterBlueprintBuilder
{
	bool TryBuildBlueprint(
		const FCadMasterJsonDocument& MasterDocument,
		const FCadMasterWorkflowBuildInput& BuildInput,
		UBlueprint*& OutBlueprint,
		FString& OutError);
}
