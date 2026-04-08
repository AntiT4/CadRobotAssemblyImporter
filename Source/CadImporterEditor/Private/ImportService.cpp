#include "ImportService.h"

#include "CadImporterEditor.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Import/ImportExecutor.h"
#include "ChildDocExporter.h"
#include "ChildDocParser.h"
#include "LevelReplacer.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Workflow/ChildImportModelBuilder.h"
#include "Workflow/MasterBlueprintBuilder.h"
#include "Workflow/WorkflowBuildInputResolver.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/PlatformFileManager.h"
#include "Modules/ModuleManager.h"

namespace
{
	const FName BackgroundActorTag(TEXT("Background"));

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

	void ApplyBackgroundTagToBlueprint(UBlueprint* Blueprint, const bool bShouldApplyTag)
	{
		if (!Blueprint || !Blueprint->GeneratedClass)
		{
			return;
		}

		if (AActor* ActorDefaultObject = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()))
		{
			const bool bHadTag = ActorDefaultObject->Tags.Contains(BackgroundActorTag);
			if (bShouldApplyTag)
			{
				ActorDefaultObject->Tags.AddUnique(BackgroundActorTag);
			}
			else
			{
				ActorDefaultObject->Tags.Remove(BackgroundActorTag);
			}

			const bool bHasTag = ActorDefaultObject->Tags.Contains(BackgroundActorTag);
			if (bHadTag != bHasTag)
			{
				ActorDefaultObject->Modify();
				Blueprint->MarkPackageDirty();
			}
		}
	}

	void CollectDirectLeafChildrenForBuild(const FCadMasterDoc& MasterDocument, TArray<FCadChildEntry>& OutChildren)
	{
		OutChildren.Reset();
		if (MasterDocument.HierarchyChildren.Num() == 0)
		{
			OutChildren = MasterDocument.Children;
			return;
		}

		for (const FCadMasterHierarchyNode& Node : MasterDocument.HierarchyChildren)
		{
			if (!CadMasterNodeTypeUsesChildJson(Node.NodeType))
			{
				continue;
			}

			FCadChildEntry ChildEntry;
			ChildEntry.ActorName = Node.ActorName;
			ChildEntry.ActorPath = Node.ActorPath;
			ChildEntry.RelativeTransform = Node.RelativeTransform;
			ChildEntry.ActorType = CadMasterChildActorTypeFromNodeType(Node.NodeType);
			ChildEntry.ChildJsonFileName = Node.ChildJsonFileName;
			OutChildren.Add(MoveTemp(ChildEntry));
		}
	}

	bool EnsureContentRootReady(const FString& ContentRootPath, FString& OutError)
	{
		const FString TrimmedContentRoot = ContentRootPath.TrimStartAndEnd();
		if (TrimmedContentRoot.IsEmpty())
		{
			return true;
		}

		FString RelativeContentPath = TrimmedContentRoot;
		if (RelativeContentPath.StartsWith(TEXT("/Game/")))
		{
			RelativeContentPath.RightChopInline(6, EAllowShrinking::No);
		}
		else if (RelativeContentPath.Equals(TEXT("/Game"), ESearchCase::CaseSensitive))
		{
			RelativeContentPath.Reset();
		}

		const FString ContentDir = RelativeContentPath.IsEmpty()
			? FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir())
			: FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), RelativeContentPath));
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.CreateDirectoryTree(*ContentDir))
		{
			OutError = FString::Printf(TEXT("Failed to create content directory: %s"), *ContentDir);
			return false;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FString> PathsToScan;
		PathsToScan.Add(TrimmedContentRoot);
		AssetRegistryModule.Get().ScanPathsSynchronous(PathsToScan, true);
		return true;
	}

	FString ResolveDocumentChildJsonFolderPath(const FCadMasterDoc& MasterDocument, const FString& MasterJsonPath)
	{
		const FString WorkspaceFolder = MasterDocument.WorkspaceFolder.TrimStartAndEnd().IsEmpty()
			? FPaths::GetPath(MasterJsonPath)
			: MasterDocument.WorkspaceFolder;
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(WorkspaceFolder, MasterDocument.ChildJsonFolderName));
	}

	FCadWorkflowBuildInput BuildMasterBuildInput(const FCadMasterDoc& MasterDocument, const FString& MasterJsonPath)
	{
		FCadWorkflowBuildInput BuildInput;
		BuildInput.WorkspaceFolder = MasterDocument.WorkspaceFolder;
		BuildInput.MasterJsonPath = FPaths::ConvertRelativePathToFull(MasterJsonPath);
		BuildInput.ChildJsonFolderPath = ResolveDocumentChildJsonFolderPath(MasterDocument, MasterJsonPath);
		BuildInput.ContentRootPath = MasterDocument.ContentRootPath;
		return BuildInput;
	}

	FCadMasterDoc BuildInlineNestedMasterDocument(
		const FCadMasterDoc& ParentDocument,
		const FCadMasterHierarchyNode& MasterNode,
		const FString& ParentChildJsonFolderPath)
	{
		FCadMasterDoc NestedDocument;
		NestedDocument.MasterName = MasterNode.ActorName;
		NestedDocument.MasterActorPath = MasterNode.ActorPath;
		NestedDocument.MasterWorldTransform = MasterNode.RelativeTransform * ParentDocument.MasterWorldTransform;
		NestedDocument.WorkspaceFolder = ParentChildJsonFolderPath;
		NestedDocument.ChildJsonFolderName = MasterNode.ChildJsonFolderName.TrimStartAndEnd().IsEmpty()
			? FPaths::MakeValidFileName(MasterNode.ActorName)
			: MasterNode.ChildJsonFolderName;
		if (NestedDocument.ChildJsonFolderName.IsEmpty())
		{
			NestedDocument.ChildJsonFolderName = TEXT("Master");
		}
		NestedDocument.ContentRootPath = ParentDocument.ContentRootPath.TrimStartAndEnd().IsEmpty()
			? FString::Printf(TEXT("/Game/%s"), *NestedDocument.ChildJsonFolderName)
			: FString::Printf(TEXT("%s/%s"), *ParentDocument.ContentRootPath, *NestedDocument.ChildJsonFolderName);
		NestedDocument.HierarchyChildren = MasterNode.Children;
		CollectDirectLeafChildrenForBuild(NestedDocument, NestedDocument.Children);
		return NestedDocument;
	}

	bool TryBuildMasterBlueprintsRecursive(
		const FCadMasterDoc& MasterDocument,
		const FString& MasterJsonPath,
		TMap<FString, UBlueprint*>& InOutMasterBlueprintsByJsonPath,
		FString& OutError)
	{
		const FString NormalizedMasterJsonPath = FPaths::ConvertRelativePathToFull(MasterJsonPath);
		if (!EnsureContentRootReady(MasterDocument.ContentRootPath, OutError))
		{
			return false;
		}

		UBlueprint* MasterBlueprint = nullptr;
		if (!CadMasterBlueprintBuilder::TryBuildBlueprint(
			MasterDocument,
			BuildMasterBuildInput(MasterDocument, NormalizedMasterJsonPath),
			MasterBlueprint,
			OutError))
		{
			return false;
		}

		InOutMasterBlueprintsByJsonPath.Add(NormalizedMasterJsonPath, MasterBlueprint);

		const FString ChildJsonFolderPath = ResolveDocumentChildJsonFolderPath(MasterDocument, NormalizedMasterJsonPath);
		for (const FCadMasterHierarchyNode& Node : MasterDocument.HierarchyChildren)
		{
			if (Node.NodeType != ECadMasterNodeType::Master)
			{
				continue;
			}

			FCadMasterDoc NestedMasterDocument;
			FString NestedMasterJsonPath;
			if (!Node.MasterJsonFileName.TrimStartAndEnd().IsEmpty())
			{
				NestedMasterJsonPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(ChildJsonFolderPath, Node.MasterJsonFileName));
				if (!CadChildDocExporter::TryParseMasterDocument(NestedMasterJsonPath, NestedMasterDocument, OutError))
				{
					return false;
				}
			}
			else if (Node.Children.Num() > 0)
			{
				NestedMasterJsonPath = FPaths::Combine(ChildJsonFolderPath, FString::Printf(TEXT("%s.json"), *FPaths::MakeValidFileName(Node.ActorName)));
				NestedMasterDocument = BuildInlineNestedMasterDocument(MasterDocument, Node, ChildJsonFolderPath);
			}
			else
			{
				continue;
			}

			if (!TryBuildMasterBlueprintsRecursive(
				NestedMasterDocument,
				NestedMasterJsonPath,
				InOutMasterBlueprintsByJsonPath,
				OutError))
			{
				return false;
			}
		}

		return true;
	}

	bool TryBuildChildBlueprintsRecursive(
		const FCadMasterDoc& MasterDocument,
		const FString& MasterJsonPath,
		const FString& DefaultContentRootPath,
		TMap<FString, UBlueprint*>& InOutChildBlueprintsByJsonPath,
		FString& OutError)
	{
		const FString ChildJsonFolderPath = ResolveDocumentChildJsonFolderPath(MasterDocument, MasterJsonPath);
		const FString ChildBlueprintOutputRoot = MasterDocument.ContentRootPath.TrimStartAndEnd().IsEmpty()
			? DefaultContentRootPath
			: MasterDocument.ContentRootPath.TrimStartAndEnd();
		if (!EnsureContentRootReady(ChildBlueprintOutputRoot, OutError))
		{
			return false;
		}

		TArray<FCadChildEntry> DirectLeafChildren;
		CollectDirectLeafChildrenForBuild(MasterDocument, DirectLeafChildren);
		for (const FCadChildEntry& ChildEntry : DirectLeafChildren)
		{
			const FString ChildName = ChildEntry.ActorName.TrimStartAndEnd();
			const FString ChildJsonFileName = ChildEntry.ChildJsonFileName.TrimStartAndEnd();
			if (ChildName.IsEmpty())
			{
				OutError = TEXT("Child actor_name is empty.");
				return false;
			}
			if (ChildJsonFileName.IsEmpty())
			{
				OutError = FString::Printf(TEXT("child_json_file_name is empty for child '%s'."), *ChildName);
				return false;
			}

			const FString ChildJsonPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(ChildJsonFolderPath, ChildJsonFileName));
			FCadChildDoc ChildDocument;
			if (!CadChildDocParser::TryLoadChildDocumentFromJsonPath(ChildJsonPath, ChildDocument, OutError))
			{
				return false;
			}

			if (ChildDocument.ChildActorName.TrimStartAndEnd().IsEmpty())
			{
				ChildDocument.ChildActorName = ChildName;
			}

			FCadImportModel ChildModel;
			if (!CadChildImportModelBuilder::TryBuildImportModel(
				MasterDocument,
				ChildEntry,
				ChildDocument,
				ChildJsonFolderPath,
				ChildBlueprintOutputRoot,
				ChildModel,
				OutError))
			{
				return false;
			}

			UBlueprint* ChildBlueprint = nullptr;
			if (!CadImportExecutor::TryImportModel(ChildModel, ChildJsonPath, &ChildBlueprint, OutError))
			{
				return false;
			}

			ApplyBackgroundTagToBlueprint(ChildBlueprint, ChildEntry.ActorType == ECadMasterChildActorType::Background);
			InOutChildBlueprintsByJsonPath.Add(ChildJsonPath, ChildBlueprint);
			UE_LOG(
				LogCadImporter,
				Display,
				TEXT("Built child blueprint from json. child=%s json=%s blueprint=%s"),
				*ChildName,
				*ChildJsonPath,
				ChildBlueprint ? *ChildBlueprint->GetPathName() : TEXT("(null)"));
		}

		for (const FCadMasterHierarchyNode& Node : MasterDocument.HierarchyChildren)
		{
			if (Node.NodeType != ECadMasterNodeType::Master)
			{
				continue;
			}

			FCadMasterDoc NestedMasterDocument;
			FString NestedMasterJsonPath;
			if (!Node.MasterJsonFileName.TrimStartAndEnd().IsEmpty())
			{
				NestedMasterJsonPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(ChildJsonFolderPath, Node.MasterJsonFileName));
				if (!CadChildDocExporter::TryParseMasterDocument(NestedMasterJsonPath, NestedMasterDocument, OutError))
				{
					return false;
				}
			}
			else if (Node.Children.Num() > 0)
			{
				NestedMasterJsonPath = FPaths::Combine(ChildJsonFolderPath, FString::Printf(TEXT("%s.json"), *FPaths::MakeValidFileName(Node.ActorName)));
				NestedMasterDocument = BuildInlineNestedMasterDocument(MasterDocument, Node, ChildJsonFolderPath);
			}
			else
			{
				continue;
			}

			if (!TryBuildChildBlueprintsRecursive(
				NestedMasterDocument,
				NestedMasterJsonPath,
				ChildBlueprintOutputRoot,
				InOutChildBlueprintsByJsonPath,
				OutError))
			{
				return false;
			}
		}

		return true;
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

	TMap<FString, UBlueprint*> MasterBlueprintsByJsonPath;
	if (!TryBuildMasterBlueprintsRecursive(MasterDocument, MasterJsonPath, MasterBlueprintsByJsonPath, Error))
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
	if (!TryBuildChildBlueprintsRecursive(MasterDocument, MasterJsonPath, ChildBlueprintOutputRoot, ChildBlueprintsByJsonPath, Error))
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
