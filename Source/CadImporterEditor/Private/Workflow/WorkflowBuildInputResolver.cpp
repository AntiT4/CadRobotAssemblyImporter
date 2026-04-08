#include "Workflow/WorkflowBuildInputResolver.h"

#include "Workflow/WorkspaceUtils.h"

namespace
{
	FString ResolveWorkspaceFolder(
		const FCadWorkflowBuildInput& BuildInput,
		const FCadMasterDoc& MasterDocument)
	{
		const FString InputWorkspace = CadWorkspaceUtils::NormalizeOptionalDirectoryPath(BuildInput.WorkspaceFolder);
		if (!InputWorkspace.IsEmpty())
		{
			return InputWorkspace;
		}

		const FString DocumentWorkspace = CadWorkspaceUtils::NormalizeOptionalDirectoryPath(MasterDocument.WorkspaceFolder);
		if (!DocumentWorkspace.IsEmpty())
		{
			return DocumentWorkspace;
		}

		return CadWorkspaceUtils::NormalizeOptionalDirectoryPath(FPaths::GetPath(BuildInput.MasterJsonPath));
	}

	FString ResolveChildJsonFolder(
		const FCadWorkflowBuildInput& BuildInput,
		const FCadMasterDoc& MasterDocument,
		const FString& WorkspaceFolder)
	{
		const FString ExplicitChildFolder = CadWorkspaceUtils::NormalizeOptionalDirectoryPath(BuildInput.ChildJsonFolderPath);
		if (!ExplicitChildFolder.IsEmpty())
		{
			return ExplicitChildFolder;
		}

		return CadWorkspaceUtils::NormalizeOptionalDirectoryPath(FPaths::Combine(WorkspaceFolder, MasterDocument.ChildJsonFolderName));
	}
}

namespace CadWorkflowBuildInputResolver
{
	FCadWorkflowBuildInput Resolve(
		const FCadWorkflowBuildInput& BuildInput,
		const FCadMasterDoc& MasterDocument)
	{
		FCadWorkflowBuildInput ResolvedBuildInput = BuildInput;
		ResolvedBuildInput.MasterJsonPath = CadWorkspaceUtils::NormalizeOptionalFilePath(BuildInput.MasterJsonPath);
		ResolvedBuildInput.WorkspaceFolder = ResolveWorkspaceFolder(ResolvedBuildInput, MasterDocument);
		ResolvedBuildInput.ChildJsonFolderPath = ResolveChildJsonFolder(ResolvedBuildInput, MasterDocument, ResolvedBuildInput.WorkspaceFolder);
		ResolvedBuildInput.ContentRootPath = BuildInput.ContentRootPath.TrimStartAndEnd().IsEmpty()
			? MasterDocument.ContentRootPath
			: BuildInput.ContentRootPath.TrimStartAndEnd();
		return ResolvedBuildInput;
	}
}
