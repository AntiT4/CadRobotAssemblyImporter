#pragma once

#include "CoreMinimal.h"
#include "WorkflowTypes.h"

class ACadMasterActor;

struct FCadMasterWorkflowWorkspacePaths
{
	FString WorkspaceFolder;
	FString MasterName;
	FString MasterJsonPath;
	FString ChildJsonFolderPath;
	FString ContentRootPath;
	FString ContentDiskFolderPath;

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

namespace CadMasterJsonWorkspaceService
{
	bool TryPrepareWorkspace(
		const FString& WorkspaceFolderInput,
		const FString& MasterNameInput,
		FCadMasterWorkflowWorkspacePaths& OutPaths,
		FString& OutError);

	bool TryPrepareWorkspaceForMasterActor(
		const ACadMasterActor* MasterActor,
		FCadMasterWorkflowWorkspacePaths& OutPaths,
		FString& OutError);
}
