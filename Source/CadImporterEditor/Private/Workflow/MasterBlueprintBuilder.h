#pragma once

#include "WorkflowTypes.h"

class UBlueprint;

namespace CadMasterBlueprintBuilder
{
	bool TryBuildBlueprint(
		const FCadMasterDoc& MasterDocument,
		const FCadWorkflowBuildInput& BuildInput,
		UBlueprint*& OutBlueprint,
		FString& OutError);
}
