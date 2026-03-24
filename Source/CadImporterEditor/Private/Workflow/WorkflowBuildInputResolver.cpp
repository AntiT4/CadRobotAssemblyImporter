#include "Workflow/WorkflowBuildInputResolver.h"

#include "Misc/Paths.h"

namespace
{
	FString ResolveWorkspaceFolder(
		const FCadWorkflowBuildInput& BuildInput,
		const FCadMasterDoc& MasterDocument)
	{
		const FString InputWorkspace = BuildInput.WorkspaceFolder.TrimStartAndEnd();
		if (!InputWorkspace.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(InputWorkspace);
		}

		const FString DocumentWorkspace = MasterDocument.WorkspaceFolder.TrimStartAndEnd();
		if (!DocumentWorkspace.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(DocumentWorkspace);
		}

		return FPaths::ConvertRelativePathToFull(FPaths::GetPath(BuildInput.MasterJsonPath));
	}

	FString ResolveChildJsonFolder(
		const FCadWorkflowBuildInput& BuildInput,
		const FCadMasterDoc& MasterDocument,
		const FString& WorkspaceFolder)
	{
		const FString ExplicitChildFolder = BuildInput.ChildJsonFolderPath.TrimStartAndEnd();
		if (!ExplicitChildFolder.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(ExplicitChildFolder);
		}

		return FPaths::ConvertRelativePathToFull(FPaths::Combine(WorkspaceFolder, MasterDocument.ChildJsonFolderName));
	}
}

namespace CadWorkflowBuildInputResolver
{
	FCadWorkflowBuildInput Resolve(
		const FCadWorkflowBuildInput& BuildInput,
		const FCadMasterDoc& MasterDocument)
	{
		FCadWorkflowBuildInput ResolvedBuildInput = BuildInput;
		ResolvedBuildInput.MasterJsonPath = FPaths::ConvertRelativePathToFull(BuildInput.MasterJsonPath.TrimStartAndEnd());
		ResolvedBuildInput.WorkspaceFolder = ResolveWorkspaceFolder(ResolvedBuildInput, MasterDocument);
		ResolvedBuildInput.ChildJsonFolderPath = ResolveChildJsonFolder(ResolvedBuildInput, MasterDocument, ResolvedBuildInput.WorkspaceFolder);
		ResolvedBuildInput.ContentRootPath = BuildInput.ContentRootPath.TrimStartAndEnd().IsEmpty()
			? MasterDocument.ContentRootPath
			: BuildInput.ContentRootPath.TrimStartAndEnd();
		return ResolvedBuildInput;
	}
}
