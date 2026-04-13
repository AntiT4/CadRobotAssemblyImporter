#include "Workflow/WorkflowBlueprintBuild.h"

#include "CadImporterEditor.h"
#include "ChildDocExporter.h"
#include "ChildDocParser.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Import/ImportExecutor.h"
#include "Workflow/ChildImportModelBuilder.h"
#include "Workflow/MasterBlueprintBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
	const FName BackgroundActorTag(TEXT("Background"));

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
		CadWorkflowBlueprintBuild::CollectDirectLeafChildrenForBuild(NestedDocument, NestedDocument.Children);
		return NestedDocument;
	}

	bool TryResolveNestedMasterDocument(
		const FCadMasterDoc& ParentDocument,
		const FCadMasterHierarchyNode& MasterNode,
		const FString& ParentChildJsonFolderPath,
		FCadMasterDoc& OutNestedMasterDocument,
		FString& OutNestedMasterJsonPath,
		FString& OutError)
	{
		if (!MasterNode.MasterJsonFileName.TrimStartAndEnd().IsEmpty())
		{
			OutNestedMasterJsonPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(ParentChildJsonFolderPath, MasterNode.MasterJsonFileName));
			return CadChildDocExporter::TryParseMasterDocument(OutNestedMasterJsonPath, OutNestedMasterDocument, OutError);
		}

		if (MasterNode.Children.Num() > 0)
		{
			OutNestedMasterJsonPath = FPaths::Combine(
				ParentChildJsonFolderPath,
				FString::Printf(TEXT("%s.json"), *FPaths::MakeValidFileName(MasterNode.ActorName)));
			OutNestedMasterDocument = BuildInlineNestedMasterDocument(ParentDocument, MasterNode, ParentChildJsonFolderPath);
			return true;
		}

		return false;
	}
}

namespace CadWorkflowBlueprintBuild
{
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
			if (!TryResolveNestedMasterDocument(
				MasterDocument,
				Node,
				ChildJsonFolderPath,
				NestedMasterDocument,
				NestedMasterJsonPath,
				OutError))
			{
				if (Node.MasterJsonFileName.TrimStartAndEnd().IsEmpty() && Node.Children.Num() == 0)
				{
					continue;
				}
				return false;
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
			if (!TryResolveNestedMasterDocument(
				MasterDocument,
				Node,
				ChildJsonFolderPath,
				NestedMasterDocument,
				NestedMasterJsonPath,
				OutError))
			{
				if (Node.MasterJsonFileName.TrimStartAndEnd().IsEmpty() && Node.Children.Num() == 0)
				{
					continue;
				}
				return false;
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
