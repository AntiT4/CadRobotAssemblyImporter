#pragma once

#include "ImportModelTypes.h"
#include "WorkflowTypes.h"

namespace CadChildImportModelBuilder
{
	bool TryBuildImportModel(
		const FCadChildEntry& ChildEntry,
		const FCadChildDoc& ChildDocument,
		const FString& ChildJsonFolderPath,
		const FString& OutputRootPath,
		FCadImportModel& OutModel,
		FString& OutError);
}
