#include "Workflow/WorkspaceUtils.h"

#include "CadImporterEditor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

namespace
{
	struct FAnyDirectoryEntryVisitor final : IPlatformFile::FDirectoryVisitor
	{
		bool bFoundAnyEntry = false;

		virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
		{
			static_cast<void>(FilenameOrDirectory);
			static_cast<void>(bIsDirectory);
			bFoundAnyEntry = true;
			return false;
		}
	};
}

namespace CadWorkspaceUtils
{
	FString NormalizeOptionalDirectoryPath(const FString& PathInput)
	{
		FString NormalizedPath = PathInput.TrimStartAndEnd();
		if (NormalizedPath.IsEmpty())
		{
			return FString();
		}

		if (FPaths::IsRelative(NormalizedPath))
		{
			NormalizedPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), NormalizedPath));
		}
		else
		{
			NormalizedPath = FPaths::ConvertRelativePathToFull(NormalizedPath);
		}

		FPaths::NormalizeDirectoryName(NormalizedPath);
		return NormalizedPath;
	}

	FString NormalizeOptionalFilePath(const FString& PathInput)
	{
		FString NormalizedPath = PathInput.TrimStartAndEnd();
		if (NormalizedPath.IsEmpty())
		{
			return FString();
		}

		if (FPaths::IsRelative(NormalizedPath))
		{
			NormalizedPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), NormalizedPath));
		}
		else
		{
			NormalizedPath = FPaths::ConvertRelativePathToFull(NormalizedPath);
		}

		FPaths::NormalizeFilename(NormalizedPath);
		return NormalizedPath;
	}

	bool TryNormalizePath(
		const FString& WorkspaceInput,
		FString& OutTrimmedWorkspace,
		FString& OutNormalizedWorkspace,
		FString& OutError)
	{
		OutTrimmedWorkspace.Reset();
		OutNormalizedWorkspace.Reset();
		OutError.Reset();

		OutTrimmedWorkspace = WorkspaceInput.TrimStartAndEnd();
		if (OutTrimmedWorkspace.IsEmpty())
		{
			OutError = TEXT("Workspace path is empty. Apply a valid workspace folder first.");
			return false;
		}

		FString NormalizedWorkspace = NormalizeOptionalDirectoryPath(OutTrimmedWorkspace);
		if (NormalizedWorkspace.IsEmpty())
		{
			OutError = TEXT("Workspace path normalization produced an empty value.");
			return false;
		}

		OutNormalizedWorkspace = MoveTemp(NormalizedWorkspace);
		return true;
	}

	bool TryValidateForApply(
		const FString& WorkspaceInput,
		FString& OutNormalizedWorkspace,
		FString& OutError)
	{
		FString TrimmedWorkspace;
		if (!TryNormalizePath(WorkspaceInput, TrimmedWorkspace, OutNormalizedWorkspace, OutError))
		{
			return false;
		}

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		const bool bWorkspaceDirectoryExists = PlatformFile.DirectoryExists(*OutNormalizedWorkspace);

		bool bWorkspaceDirectoryEmpty = true;
		if (bWorkspaceDirectoryExists)
		{
			FAnyDirectoryEntryVisitor Visitor;
			PlatformFile.IterateDirectory(*OutNormalizedWorkspace, Visitor);
			bWorkspaceDirectoryEmpty = !Visitor.bFoundAnyEntry;
		}

		UE_LOG(
			LogCadImporter,
			Display,
			TEXT("Workspace validation before apply: raw='%s', trimmed='%s', normalized='%s', exists=%s, empty=%s"),
			*WorkspaceInput,
			*TrimmedWorkspace,
			*OutNormalizedWorkspace,
			bWorkspaceDirectoryExists ? TEXT("true") : TEXT("false"),
			bWorkspaceDirectoryEmpty ? TEXT("true") : TEXT("false"));

		if (bWorkspaceDirectoryExists && !bWorkspaceDirectoryEmpty)
		{
			OutError = FString::Printf(
				TEXT("Workspace folder is not empty. Choose an empty folder or create a new one.\npath=%s"),
				*OutNormalizedWorkspace);
			return false;
		}

		return true;
	}

	bool TryValidateForGeneration(
		const FString& WorkspaceInput,
		FString& OutNormalizedWorkspace,
		FString& OutError)
	{
		FString TrimmedWorkspace;
		if (!TryNormalizePath(WorkspaceInput, TrimmedWorkspace, OutNormalizedWorkspace, OutError))
		{
			return false;
		}

		const bool bWorkspaceDirectoryExists = IFileManager::Get().DirectoryExists(*OutNormalizedWorkspace);
		UE_LOG(
			LogCadImporter,
			Display,
			TEXT("Workspace validation before generation: raw='%s', trimmed='%s', normalized='%s', exists=%s"),
			*WorkspaceInput,
			*TrimmedWorkspace,
			*OutNormalizedWorkspace,
			bWorkspaceDirectoryExists ? TEXT("true") : TEXT("false"));

		return true;
	}
}
