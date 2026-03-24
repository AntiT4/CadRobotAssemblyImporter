#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

class ACadMasterActor;

struct FCadWorkspacePaths
{
	FString WorkspaceFolder;
	FString MasterName;
	FString MasterJsonPath;
	FString ChildJsonFolderPath;
	FString ContentRootPath;
	FString ContentDir;

	FCadMasterWorkflowBuildInput ToBuildInput() const
	{
		FCadMasterWorkflowBuildInput BuildInput;
		BuildInput.WorkspaceFolder = WorkspaceFolder;
		BuildInput.MasterJsonPath = MasterJsonPath;
		BuildInput.ChildJsonFolderPath = ChildJsonFolderPath;
		BuildInput.ContentRootPath = ContentRootPath;
		return BuildInput;
	}
};

namespace CadWorkspaceService
{
	bool TryPrepareWorkspace(
		const FString& WorkspaceFolderInput,
		const FString& MasterNameInput,
		FCadWorkspacePaths& OutPaths,
		FString& OutError);

	bool TryPrepareWorkspaceForMasterActor(
		const ACadMasterActor* MasterActor,
		FCadWorkspacePaths& OutPaths,
		FString& OutError);
}
