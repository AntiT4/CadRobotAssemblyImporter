#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

struct FCadChildJsonResult
{
	FString MasterJsonPath;
	FString ChildJsonFolderPath;
	FCadMasterJsonDocument MasterDocument;
	FCadMasterWorkflowBuildInput BuildInput;
	TArray<FString> GeneratedChildJsonPaths;
};

namespace CadChildJsonService
{
	bool TryParseMasterDocument(
		const FString& MasterJsonPath,
		FCadMasterJsonDocument& OutDocument,
		FString& OutError);

	bool TryExtractChildJsonFilesFromDocument(
		const FString& MasterJsonPath,
		const FCadMasterJsonDocument& MasterDocument,
		FCadChildJsonResult& OutResult,
		FString& OutError);

	bool TryExtractChildJsonFiles(
		const FString& MasterJsonPath,
		FCadChildJsonResult& OutResult,
		FString& OutError);
}
