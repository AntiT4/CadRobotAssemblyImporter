#include "ImportService.h"

#include "CadImporterEditor.h"
#include "Engine/Blueprint.h"
#include "ChildDocExporter.h"
#include "LevelReplacer.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Workflow/WorkflowBlueprintBuild.h"
#include "Workflow/WorkflowBuildInputResolver.h"

namespace
{
	class FCadDialogNotifier final : public ICadImportServiceNotifier
	{
	public:
		virtual void ShowError(const FString& Title, const FString& Error) const override
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("%s:\n%s"), *Title, *Error)));
		}
	};
}

FCadImportService::FCadImportService(TSharedPtr<ICadImportServiceNotifier> InNotifier)
	: Notifier(InNotifier.IsValid() ? MoveTemp(InNotifier) : MakeShared<FCadDialogNotifier>())
{
}

bool FCadImportService::ReportFailure(const TCHAR* LogPrefix, const FString& DialogTitle, const FString& Error) const
{
	UE_LOG(LogCadImporter, Error, TEXT("%s: %s"), LogPrefix, *Error);
	if (Notifier.IsValid())
	{
		Notifier->ShowError(DialogTitle, Error);
	}
	return false;
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

	TMap<FString, UBlueprint*> MasterBlueprintsByJsonPath;
	if (!CadWorkflowBlueprintBuild::TryBuildMasterBlueprintsRecursive(MasterDocument, MasterJsonPath, MasterBlueprintsByJsonPath, Error))
	{
		return ReportFailure(TEXT("Master blueprint generation failed"), TEXT("Master blueprint generation failed"), Error);
	}
	UBlueprint* const* RootMasterBlueprintPtr = MasterBlueprintsByJsonPath.Find(FPaths::ConvertRelativePathToFull(MasterJsonPath));
	UBlueprint* MasterBlueprint = RootMasterBlueprintPtr ? *RootMasterBlueprintPtr : nullptr;
	if (!MasterBlueprint)
	{
		return ReportFailure(TEXT("Master blueprint generation failed"), TEXT("Master blueprint generation failed"), TEXT("Root master blueprint lookup failed."));
	}

	TMap<FString, UBlueprint*> ChildBlueprintsByJsonPath;
	if (!CadWorkflowBlueprintBuild::TryBuildChildBlueprintsRecursive(MasterDocument, MasterJsonPath, ChildBlueprintOutputRoot, ChildBlueprintsByJsonPath, Error))
	{
		return ReportFailure(TEXT("Child blueprint build failed"), TEXT("Child blueprint build failed"), Error);
	}

	FCadLevelReplaceResult ReplaceResult;
	if (!CadLevelReplacer::TryReplaceMasterHierarchyWithBlueprints(
		MasterDocument,
		MasterBlueprint,
		MasterBlueprintsByJsonPath,
		ChildBlueprintsByJsonPath,
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
