#pragma once

#include "WorkflowTypes.h"

namespace CadWorkflowBuildInputResolver
{
	FCadWorkflowBuildInput Resolve(
		const FCadWorkflowBuildInput& BuildInput,
		const FCadMasterDoc& MasterDocument);
}
