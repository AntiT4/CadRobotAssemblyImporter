#include "MasterJsonWorkspaceService.h"

#include "CadMasterActor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
	bool EnsureDirectoryExists(const FString& DirectoryPath, FString& OutError)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (PlatformFile.CreateDirectoryTree(*DirectoryPath))
		{
			return true;
		}

		OutError = FString::Printf(TEXT("Failed to create directory: %s"), *DirectoryPath);
		return false;
	}

	FString NormalizeWorkspaceFolder(const FString& WorkspaceFolderInput)
	{
		FString WorkspaceFolder = WorkspaceFolderInput.TrimStartAndEnd();
		if (WorkspaceFolder.IsEmpty())
		{
			return FString();
		}

		if (FPaths::IsRelative(WorkspaceFolder))
		{
			WorkspaceFolder = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), WorkspaceFolder));
		}
		else
		{
			WorkspaceFolder = FPaths::ConvertRelativePathToFull(WorkspaceFolder);
		}

		FPaths::NormalizeDirectoryName(WorkspaceFolder);
		return WorkspaceFolder;
	}

	bool ValidateAndNormalizeInputs(
		const FString& WorkspaceFolderInput,
		const FString& MasterNameInput,
		FString& OutWorkspaceFolder,
		FString& OutMasterName,
		FString& OutError)
	{
		OutError.Reset();

		OutWorkspaceFolder = NormalizeWorkspaceFolder(WorkspaceFolderInput);
		if (OutWorkspaceFolder.IsEmpty())
		{
			OutError = TEXT("Workspace folder is empty. Set a workspace folder first.");
			return false;
		}

		const FString TrimmedMasterName = MasterNameInput.TrimStartAndEnd();
		if (TrimmedMasterName.IsEmpty())
		{
			OutError = TEXT("Master name is empty. Set a valid master actor name.");
			return false;
		}

		OutMasterName = FPaths::MakeValidFileName(TrimmedMasterName);
		if (OutMasterName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Master name '%s' is invalid for file/package names."), *TrimmedMasterName);
			return false;
		}

		return true;
	}

	bool EnsureContentFolder(const FString& ContentRootPath, const FString& ContentDiskFolderPath, FString& OutError)
	{
		if (!EnsureDirectoryExists(ContentDiskFolderPath, OutError))
		{
			return false;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FString> PathsToScan;
		PathsToScan.Add(ContentRootPath);
		AssetRegistryModule.Get().ScanPathsSynchronous(PathsToScan, true);
		return true;
	}
}

namespace CadMasterJsonWorkspaceService
{
	bool TryPrepareWorkspace(
		const FString& WorkspaceFolderInput,
		const FString& MasterNameInput,
		FCadMasterWorkflowWorkspacePaths& OutPaths,
		FString& OutError)
	{
		OutPaths = FCadMasterWorkflowWorkspacePaths();
		OutError.Reset();

		FString WorkspaceFolder;
		FString MasterName;
		if (!ValidateAndNormalizeInputs(WorkspaceFolderInput, MasterNameInput, WorkspaceFolder, MasterName, OutError))
		{
			return false;
		}

		const FString MasterJsonPath = FPaths::Combine(WorkspaceFolder, FString::Printf(TEXT("%s.json"), *MasterName));
		const FString ChildJsonFolderPath = FPaths::Combine(WorkspaceFolder, MasterName);
		const FString ContentRootPath = FString::Printf(TEXT("/Game/%s"), *MasterName);
		const FString ContentDiskFolderPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), MasterName));

		if (!EnsureDirectoryExists(WorkspaceFolder, OutError))
		{
			return false;
		}

		if (!EnsureDirectoryExists(ChildJsonFolderPath, OutError))
		{
			return false;
		}

		if (!EnsureContentFolder(ContentRootPath, ContentDiskFolderPath, OutError))
		{
			return false;
		}

		OutPaths.WorkspaceFolder = WorkspaceFolder;
		OutPaths.MasterName = MasterName;
		OutPaths.MasterJsonPath = MasterJsonPath;
		OutPaths.ChildJsonFolderPath = ChildJsonFolderPath;
		OutPaths.ContentRootPath = ContentRootPath;
		OutPaths.ContentDiskFolderPath = ContentDiskFolderPath;
		return true;
	}

	bool TryPrepareWorkspaceForMasterActor(
		const ACadMasterActor* MasterActor,
		FCadMasterWorkflowWorkspacePaths& OutPaths,
		FString& OutError)
	{
		OutPaths = FCadMasterWorkflowWorkspacePaths();
		OutError.Reset();

		if (!MasterActor)
		{
			OutError = TEXT("Master actor is null.");
			return false;
		}

		FString MasterName = MasterActor->Metadata.MasterName.TrimStartAndEnd();
		if (MasterName.IsEmpty())
		{
			MasterName = MasterActor->GetActorNameOrLabel();
		}

		return TryPrepareWorkspace(MasterActor->Metadata.WorkspaceFolder, MasterName, OutPaths, OutError);
	}
}
