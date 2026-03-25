#pragma once

#include "CoreMinimal.h"
#include "ImportModelTypes.h"
#include "WorkflowTypes.h"

struct FCadLevelReplaceResult;

class FCadImportService
{
public:
	bool BuildFromWorkflow(
		const FCadWorkflowBuildInput& BuildInput,
		FCadLevelReplaceResult* OutReplaceResult = nullptr) const;
};
