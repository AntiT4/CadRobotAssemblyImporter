#include "ImportService.h"

#include "CadImporterEditor.h"
#include "Engine/Blueprint.h"
#include "Import/ImportExecutor.h"
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
}

bool FCadImportService::BuildFromWorkflow(
	const FCadWorkflowBuildInput& BuildInput,
	FCadLevelReplaceResult* OutReplaceResult) const
{
	if (OutReplaceResult)
	{
		*OutReplaceResult = FCadLevelReplaceResult();
	}

	FString Error;
	const FString MasterJsonPath = BuildInput.MasterJsonPath.TrimStartAndEnd();
	if (MasterJsonPath.IsEmpty())
	{
		return ReportFailure(TEXT("Master workflow build failed"), TEXT("Master workflow build failed"), TEXT("MasterJsonPath is empty."));
	}

	FCadMasterDoc MasterDocument;
	if (!CadChildDocExporter::TryParseMasterDocument(MasterJsonPath, MasterDocument, Error))
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
		if (!CadChildDocParser::TryLoadChildDocumentFromJsonPath(ChildJsonPath, ChildDocument, Error))
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
		if (!CadImportExecutor::TryImportModel(ChildModel, ChildJsonPath, &ChildBlueprint, Error))
		{
			return ReportFailure(
				TEXT("Child blueprint build failed"),
				FString::Printf(TEXT("Child blueprint build failed for '%s'"), *ChildName),
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

	if (OutReplaceResult)
	{
		*OutReplaceResult = ReplaceResult;
	}

	return true;
}
