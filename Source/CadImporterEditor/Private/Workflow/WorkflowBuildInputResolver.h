#pragma once

#include "WorkflowTypes.h"

namespace CadWorkflowBuildInputResolver
{
	FCadMasterWorkflowBuildInput Resolve(
		const FCadMasterWorkflowBuildInput& BuildInput,
		const FCadMasterJsonDocument& MasterDocument);
}
