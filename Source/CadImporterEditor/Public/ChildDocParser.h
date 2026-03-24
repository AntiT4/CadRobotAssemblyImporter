#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

namespace CadMasterWorkflowImportParser
{
	bool TryLoadChildDocumentFromJsonPath(
		const FString& ChildJsonPath,
		FCadChildDoc& OutDocument,
		FString& OutError);
}
