#pragma once

#include "CoreMinimal.h"
#include "MasterJsonWorkspaceService.h"

class ACadMasterActor;
struct FCadMasterActorSelectionResult;

struct FCadMasterJsonGenerationResult
{
	FCadMasterJsonDocument Document;
	FCadMasterWorkflowWorkspacePaths WorkspacePaths;
	FCadMasterWorkflowBuildInput BuildInput;
};

namespace CadMasterJsonGenerator
{
	bool TryGenerateAndWriteFromSelectionResult(
		const FCadMasterActorSelectionResult& SelectionResult,
		const FString& WorkspaceFolderOverride,
		FCadMasterJsonGenerationResult& OutResult,
		FString& OutError);

	bool TryGenerateAndWriteFromSelection(
		const FString& WorkspaceFolderOverride,
		FCadMasterJsonGenerationResult& OutResult,
		FString& OutError);

	bool TryGenerateAndWriteFromMasterActor(
		ACadMasterActor* MasterActor,
		const FString& WorkspaceFolderOverride,
		FCadMasterJsonGenerationResult& OutResult,
		FString& OutError);

	bool TryWriteDocument(
		const FCadMasterJsonDocument& Document,
		const FString& OutputPath,
		FString& OutError);
}
