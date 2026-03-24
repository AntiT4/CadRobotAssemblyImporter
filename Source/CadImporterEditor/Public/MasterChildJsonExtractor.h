#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

struct FCadChildJsonExtractionResult
{
	FString MasterJsonPath;
	FString ChildJsonFolderPath;
	FCadMasterJsonDocument MasterDocument;
	FCadMasterWorkflowBuildInput BuildInput;
	TArray<FString> GeneratedChildJsonPaths;
};

namespace CadMasterChildJsonExtractor
{
	bool TryParseMasterDocument(
		const FString& MasterJsonPath,
		FCadMasterJsonDocument& OutDocument,
		FString& OutError);

	bool TryExtractChildJsonFilesFromDocument(
		const FString& MasterJsonPath,
		const FCadMasterJsonDocument& MasterDocument,
		FCadChildJsonExtractionResult& OutResult,
		FString& OutError);

	bool TryExtractChildJsonFiles(
		const FString& MasterJsonPath,
		FCadChildJsonExtractionResult& OutResult,
		FString& OutError);
}
