#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

struct FCadChildJsonResult
{
	FString MasterJsonPath;
	FString ChildJsonFolderPath;
	FCadMasterDoc MasterDocument;
	FCadWorkflowBuildInput BuildInput;
	TArray<FString> GeneratedChildJsonPaths;
};

namespace CadChildDocExporter
{
	bool TryParseMasterDocument(
		const FString& MasterJsonPath,
		FCadMasterDoc& OutDocument,
		FString& OutError);

	bool TryExtractChildJsonFilesFromDocument(
		const FString& MasterJsonPath,
		const FCadMasterDoc& MasterDocument,
		FCadChildJsonResult& OutResult,
		FString& OutError);

	bool TryExtractChildJsonFiles(
		const FString& MasterJsonPath,
		FCadChildJsonResult& OutResult,
		FString& OutError);
}
