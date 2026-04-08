#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

namespace CadChildDocParser
{
	bool TryLoadChildDocumentFromJsonPath(
		const FString& ChildJsonPath,
		FCadChildDoc& OutDocument,
		FString& OutError);
}
