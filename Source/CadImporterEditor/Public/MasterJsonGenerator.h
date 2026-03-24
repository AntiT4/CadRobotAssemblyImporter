#pragma once

#include "CoreMinimal.h"
#include "MasterJsonWorkspaceService.h"

class ACadMasterActor;
struct FCadMasterSelection;

struct FCadMasterJsonGenerationResult
{
	FCadMasterDoc Document;
	FCadWorkspacePaths WorkspacePaths;
	FCadWorkflowBuildInput BuildInput;
};

namespace CadMasterJsonGenerator
{
	bool TryGenerateAndWriteFromSelectionResult(
		const FCadMasterSelection& SelectionResult,
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
		const FCadMasterDoc& Document,
		const FString& OutputPath,
		FString& OutError);
}
