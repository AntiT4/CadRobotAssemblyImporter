#pragma once

#include "CoreMinimal.h"
#include "ImportTypes.h"

struct FCadMasterWorkflowImportParseResult
{
	FCadMasterWorkflowBuildInput BuildInput;
	FCadMasterJsonDocument MasterDocument;
	TArray<FCadChildJsonDocument> ChildDocuments;
	FCadImportModel Model;
};

namespace CadMasterWorkflowImportParser
{
	bool TryLoadChildDocumentFromJsonPath(
		const FString& ChildJsonPath,
		FCadChildJsonDocument& OutDocument,
		FString& OutError);

	bool TryBuildImportModel(
		const FCadMasterWorkflowBuildInput& BuildInput,
		FCadMasterWorkflowImportParseResult& OutResult,
		FString& OutError);
}
