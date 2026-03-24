#include "ImportService.h"

#include "CadImporterEditor.h"
#include "DesktopPlatformModule.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Import/ImportExecutor.h"
#include "Import/ImportModelParser.h"
#include "ChildDocExporter.h"
#include "ChildDocParser.h"
#include "LevelReplacer.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Workflow/ChildImportModelBuilder.h"
#include "Workflow/MasterBlueprintBuilder.h"
#include "Workflow/WorkflowBuildInputResolver.h"

namespace
{
	void ShowErrorDialog(const FString& Title, const FString& Error)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("%s:\n%s"), *Title, *Error)));
	}

	bool ReportFailure(const TCHAR* LogPrefix, const FString& DialogTitle, const FString& Error)
	{
		UE_LOG(LogCadImporter, Error, TEXT("%s: %s"), LogPrefix, *Error);
		ShowErrorDialog(DialogTitle, Error);
		return false;
	}

	bool SelectJsonPath(
		const bool bSaveDialog,
		const FString& Title,
		const FString& DefaultFile,
		FString& OutJsonPath)
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (!DesktopPlatform)
		{
			UE_LOG(LogCadImporter, Error, TEXT("Desktop platform module is not available."));
			return false;
		}

		const void* ParentWindowHandle = nullptr;
		if (FSlateApplication::IsInitialized())
		{
			ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		}

		TArray<FString> OutFiles;
		const FString DefaultPath = FPaths::ProjectDir();
		const FString FileTypes = TEXT("JSON Files (*.json)|*.json");
		const bool bOpened = bSaveDialog
			? DesktopPlatform->SaveFileDialog(
				const_cast<void*>(ParentWindowHandle),
				Title,
				DefaultPath,
				DefaultFile,
				FileTypes,
				EFileDialogFlags::None,
				OutFiles)
			: DesktopPlatform->OpenFileDialog(
				const_cast<void*>(ParentWindowHandle),
				Title,
				DefaultPath,
				DefaultFile,
				FileTypes,
				EFileDialogFlags::None,
				OutFiles);

		if (!bOpened || OutFiles.Num() == 0)
		{
			return false;
		}

		OutJsonPath = OutFiles[0];
		return true;
	}
}

bool FCadImportService::RunImport(const FString& JsonPath, const FCadFbxImportOptions& ImportOptions) const
{
	FCadImportModel Model;
	FString Error;
	FCadJsonParser Parser;
	if (!Parser.ParseFromFile(JsonPath, Model, Error))
	{
		return ReportFailure(TEXT("CAD json parse failed"), TEXT("JSON parse failed"), Error);
	}

	if (!CadImportExecutor::TryImportModel(Model, JsonPath, ImportOptions, nullptr, Error))
	{
		return ReportFailure(TEXT("CAD import failed"), TEXT("Import failed"), Error);
	}

	return true;
}

bool FCadImportService::BuildFromWorkflow(const FCadWorkflowBuildInput& BuildInput, const FCadFbxImportOptions& ImportOptions) const
{
	FString Error;
	const FString MasterJsonPath = BuildInput.MasterJsonPath.TrimStartAndEnd();
	if (MasterJsonPath.IsEmpty())
	{
		return ReportFailure(TEXT("Master workflow build failed"), TEXT("Master workflow build failed"), TEXT("MasterJsonPath is empty."));
	}

	FCadMasterDoc MasterDocument;
	if (!CadChildJsonService::TryParseMasterDocument(MasterJsonPath, MasterDocument, Error))
	{
		return ReportFailure(TEXT("Master workflow parse failed"), TEXT("Master workflow parse failed"), Error);
	}

	FCadWorkflowBuildInput ResolvedBuildInput = BuildInput;
	ResolvedBuildInput.MasterJsonPath = MasterJsonPath;
	ResolvedBuildInput = CadWorkflowBuildInputResolver::Resolve(ResolvedBuildInput, MasterDocument);
	const FString ChildBlueprintOutputRoot = ResolvedBuildInput.ContentRootPath.TrimStartAndEnd();

	UBlueprint* MasterBlueprint = nullptr;
	if (!CadMasterBlueprintBuilder::TryBuildBlueprint(MasterDocument, ResolvedBuildInput, MasterBlueprint, Error))
	{
		return ReportFailure(TEXT("Master blueprint generation failed"), TEXT("Master blueprint generation failed"), Error);
	}

	TMap<FString, UBlueprint*> ChildBlueprintsByChildName;
	for (const FCadChildEntry& ChildEntry : MasterDocument.Children)
	{
		const FString ChildName = ChildEntry.ActorName.TrimStartAndEnd();
		const FString ChildJsonFileName = ChildEntry.ChildJsonFileName.TrimStartAndEnd();
		if (ChildName.IsEmpty())
		{
			return ReportFailure(TEXT("Master workflow parse failed"), TEXT("Master workflow parse failed"), TEXT("Child actor_name is empty."));
		}
		if (ChildJsonFileName.IsEmpty())
		{
			return ReportFailure(
				TEXT("Master workflow parse failed"),
				TEXT("Master workflow parse failed"),
				FString::Printf(TEXT("child_json_file_name is empty for child '%s'."), *ChildName));
		}

		const FString ChildJsonPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(ResolvedBuildInput.ChildJsonFolderPath, ChildJsonFileName));
		FCadChildDoc ChildDocument;
		if (!CadMasterWorkflowImportParser::TryLoadChildDocumentFromJsonPath(ChildJsonPath, ChildDocument, Error))
		{
			return ReportFailure(
				TEXT("Failed to load child json"),
				FString::Printf(TEXT("Failed to load child json for '%s'"), *ChildName),
				Error);
		}

		if (ChildDocument.ChildActorName.TrimStartAndEnd().IsEmpty())
		{
			ChildDocument.ChildActorName = ChildName;
		}

		FCadImportModel ChildModel;
		if (!CadChildImportModelBuilder::TryBuildImportModel(
			ChildEntry,
			ChildDocument,
			ResolvedBuildInput.ChildJsonFolderPath,
			ChildBlueprintOutputRoot,
			ChildModel,
			Error))
		{
			return ReportFailure(
				TEXT("Child model build failed"),
				FString::Printf(TEXT("Child model build failed for '%s'"), *ChildName),
				Error);
		}

		UBlueprint* ChildBlueprint = nullptr;
		if (!CadImportExecutor::TryImportModel(ChildModel, ChildJsonPath, ImportOptions, &ChildBlueprint, Error))
		{
			return ReportFailure(
				TEXT("Child blueprint import failed"),
				FString::Printf(TEXT("Child blueprint import failed for '%s'"), *ChildName),
				Error);
		}

		ChildBlueprintsByChildName.Add(ChildName, ChildBlueprint);
		UE_LOG(
			LogCadImporter,
			Display,
			TEXT("Built child blueprint from json. child=%s json=%s blueprint=%s"),
			*ChildName,
			*ChildJsonPath,
			ChildBlueprint ? *ChildBlueprint->GetPathName() : TEXT("(null)"));
	}

	FCadLevelReplaceResult ReplaceResult;
	if (!CadLevelReplacer::TryReplaceMasterHierarchyWithBlueprints(
		MasterDocument,
		MasterBlueprint,
		ChildBlueprintsByChildName,
		ReplaceResult,
		Error))
	{
		return ReportFailure(TEXT("Master workflow level replacement failed"), TEXT("Master workflow level replacement failed"), Error);
	}

	UE_LOG(LogCadImporter, Display, TEXT("Master workflow level replacement succeeded. spawned_master=%s spawned_children=%d deleted=%d"),
		*ReplaceResult.SpawnedActorPath,
		ReplaceResult.SpawnedChildActorCount,
		ReplaceResult.DeletedActorCount);
	return true;
}

bool FCadImportService::SelectJsonFile(FString& OutJsonPath) const
{
	return SelectJsonPath(false, TEXT("Select CAD JSON"), TEXT(""), OutJsonPath);
}

bool FCadImportService::SelectJsonSavePath(FString& OutJsonPath) const
{
	return SelectJsonPath(true, TEXT("Save CAD JSON"), TEXT("ActorExport.json"), OutJsonPath);
}
