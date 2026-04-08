#pragma once

#include "CoreMinimal.h"
#include "WorkspacePrep.h"

class ACadMasterActor;
struct FCadMasterSelection;

struct FCadMasterJsonGenerationResult
{
	FCadMasterDoc Document;
	FCadWorkspacePaths WorkspacePaths;
	FCadWorkflowBuildInput BuildInput;
};

namespace CadMasterDocExporter
{
	bool TrySerializeDocument(
		const FCadMasterDoc& Document,
		FString& OutJson,
		FString& OutError);

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
