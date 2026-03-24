#pragma once

#include "ImportModelTypes.h"
#include "WorkflowTypes.h"

namespace CadChildImportModelBuilder
{
	bool TryBuildImportModel(
		const FCadMasterChildEntry& ChildEntry,
		const FCadChildJsonDocument& ChildDocument,
		const FString& ChildJsonFolderPath,
		const FString& OutputRootPath,
		FCadImportModel& OutModel,
		FString& OutError);
}
