#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

namespace CadMasterWorkflowImportParser
{
	bool TryLoadChildDocumentFromJsonPath(
		const FString& ChildJsonPath,
		FCadChildJsonDocument& OutDocument,
		FString& OutError);
}
