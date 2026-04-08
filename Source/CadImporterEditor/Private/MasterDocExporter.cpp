#include "MasterDocExporter.h"

#include "CadMasterActor.h"
#include "CadImportStringUtils.h"
#include "Json/CadJsonTransformUtils.h"
#include "MasterSelectionCollector.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FCadMasterHierarchyNode BuildMasterDocHierarchyNodeFromChildEntry(const FCadChildEntry& ChildEntry)
	{
		FCadMasterHierarchyNode Node;
		Node.ActorName = ChildEntry.ActorName;
		Node.ActorPath = ChildEntry.ActorPath;
		Node.RelativeTransform = ChildEntry.RelativeTransform;
		Node.NodeType = CadMasterNodeTypeFromChildActorType(ChildEntry.ActorType);
		Node.ChildJsonFileName = ChildEntry.ChildJsonFileName;
		return Node;
	}

	TArray<FCadMasterHierarchyNode> BuildMasterDocHierarchyNodesFromFlatChildren(const TArray<FCadChildEntry>& Children)
	{
		TArray<FCadMasterHierarchyNode> HierarchyNodes;
		HierarchyNodes.Reserve(Children.Num());
		for (const FCadChildEntry& ChildEntry : Children)
		{
			if (!CadMasterChildActorTypeShouldGenerateJson(ChildEntry.ActorType))
			{
				continue;
			}

			HierarchyNodes.Add(BuildMasterDocHierarchyNodeFromChildEntry(ChildEntry));
		}

		return HierarchyNodes;
	}

	TSharedPtr<FJsonObject> MakeMasterChildObject(const FCadMasterHierarchyNode& ChildNode)
	{
		TSharedPtr<FJsonObject> ChildObject = MakeShared<FJsonObject>();
		ChildObject->SetStringField(TEXT("actor_name"), ChildNode.ActorName);
		if (!ChildNode.ActorPath.TrimStartAndEnd().IsEmpty())
		{
			ChildObject->SetStringField(TEXT("actor_path"), ChildNode.ActorPath);
		}
		ChildObject->SetStringField(TEXT("node_type"), CadImportStringUtils::ToMasterNodeTypeString(ChildNode.NodeType));
		ChildObject->SetObjectField(TEXT("relative_transform"), CadJsonTransformUtils::MakeTransformObject(ChildNode.RelativeTransform));

		if (CadMasterNodeTypeUsesChildJson(ChildNode.NodeType))
		{
			ChildObject->SetStringField(
				TEXT("actor_type"),
				CadImportStringUtils::ToMasterChildActorTypeString(CadMasterChildActorTypeFromNodeType(ChildNode.NodeType)));
			ChildObject->SetStringField(TEXT("child_json_file_name"), ChildNode.ChildJsonFileName);
		}
		else
		{
			if (!ChildNode.MasterJsonFileName.TrimStartAndEnd().IsEmpty())
			{
				ChildObject->SetStringField(TEXT("master_json_file_name"), ChildNode.MasterJsonFileName);
			}
			if (!ChildNode.ChildJsonFolderName.TrimStartAndEnd().IsEmpty())
			{
				ChildObject->SetStringField(TEXT("child_json_folder_name"), ChildNode.ChildJsonFolderName);
			}
		}

		TArray<TSharedPtr<FJsonValue>> ChildValues;
		for (const FCadMasterHierarchyNode& NestedChildNode : ChildNode.Children)
		{
			ChildValues.Add(MakeShared<FJsonValueObject>(MakeMasterChildObject(NestedChildNode)));
		}
		ChildObject->SetArrayField(TEXT("children"), ChildValues);
		return ChildObject;
	}

	bool TrySerializeMasterDocument(const FCadMasterDoc& Document, FString& OutJson, FString& OutError)
	{
		TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
		RootObject->SetStringField(TEXT("schema_version"), TEXT("master_json_v2"));
		RootObject->SetStringField(TEXT("master_name"), Document.MasterName);
		RootObject->SetStringField(TEXT("master_actor_path"), Document.MasterActorPath);
		RootObject->SetObjectField(TEXT("master_world_transform"), CadJsonTransformUtils::MakeTransformObject(Document.MasterWorldTransform));
		RootObject->SetStringField(TEXT("workspace_folder"), Document.WorkspaceFolder);
		RootObject->SetStringField(TEXT("child_json_folder_name"), Document.ChildJsonFolderName);
		RootObject->SetStringField(TEXT("content_root_path"), Document.ContentRootPath);

		const TArray<FCadMasterHierarchyNode> HierarchyChildren = Document.HierarchyChildren.Num() > 0
			? Document.HierarchyChildren
			: BuildMasterDocHierarchyNodesFromFlatChildren(Document.Children);
		TArray<TSharedPtr<FJsonValue>> ChildValues;
		for (const FCadMasterHierarchyNode& ChildNode : HierarchyChildren)
		{
			ChildValues.Add(MakeShared<FJsonValueObject>(MakeMasterChildObject(ChildNode)));
		}
		RootObject->SetArrayField(TEXT("children"), ChildValues);

		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutJson);
		if (!FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
		{
			OutError = TEXT("Failed to serialize master json document.");
			return false;
		}

		return true;
	}

	FString ResolveMasterNameInput(const AActor& MasterActor)
	{
		if (const ACadMasterActor* CadMasterActor = Cast<ACadMasterActor>(&MasterActor))
		{
			const FString MetadataMasterName = CadMasterActor->Metadata.MasterName.TrimStartAndEnd();
			if (!MetadataMasterName.IsEmpty())
			{
				return MetadataMasterName;
			}
		}

		return MasterActor.GetActorNameOrLabel();
	}

	FString ResolveWorkspaceFolderInput(const AActor& MasterActor, const FString& WorkspaceFolderOverride)
	{
		const FString OverrideValue = WorkspaceFolderOverride.TrimStartAndEnd();
		if (!OverrideValue.IsEmpty())
		{
			return OverrideValue;
		}

		if (const ACadMasterActor* CadMasterActor = Cast<ACadMasterActor>(&MasterActor))
		{
			return CadMasterActor->Metadata.WorkspaceFolder;
		}

		return FString();
	}

	bool EnsureMasterExportDirectoryExists(const FString& DirectoryPath, FString& OutError)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (PlatformFile.CreateDirectoryTree(*DirectoryPath))
		{
			return true;
		}

		OutError = FString::Printf(TEXT("Failed to create directory: %s"), *DirectoryPath);
		return false;
	}

	FString MakeSafeMasterPathName(const FString& MasterName)
	{
		const FString SafeName = FPaths::MakeValidFileName(MasterName.TrimStartAndEnd());
		return SafeName.IsEmpty() ? TEXT("Master") : SafeName;
	}

	FString BuildNestedContentRootPath(const FString& ParentContentRootPath, const FString& SafeMasterName)
	{
		const FString TrimmedParent = ParentContentRootPath.TrimStartAndEnd();
		if (TrimmedParent.IsEmpty())
		{
			return FString::Printf(TEXT("/Game/%s"), *SafeMasterName);
		}

		return FString::Printf(TEXT("%s/%s"), *TrimmedParent, *SafeMasterName);
	}

	void AddDirectLeafChildToDocument(const FCadMasterHierarchyNode& SourceChildNode, FCadMasterDoc& InOutDocument)
	{
		FCadMasterHierarchyNode DirectChildNode = SourceChildNode;
		DirectChildNode.Children.Reset();
		DirectChildNode.MasterJsonFileName.Reset();
		DirectChildNode.ChildJsonFolderName.Reset();
		InOutDocument.HierarchyChildren.Add(DirectChildNode);

		if (!CadMasterNodeTypeUsesChildJson(SourceChildNode.NodeType))
		{
			return;
		}

		FCadChildEntry ChildEntry;
		ChildEntry.ActorName = SourceChildNode.ActorName;
		ChildEntry.ActorPath = SourceChildNode.ActorPath;
		ChildEntry.RelativeTransform = SourceChildNode.RelativeTransform;
		ChildEntry.ActorType = CadMasterChildActorTypeFromNodeType(SourceChildNode.NodeType);
		ChildEntry.ChildJsonFileName = SourceChildNode.ChildJsonFileName;
		InOutDocument.Children.Add(MoveTemp(ChildEntry));
	}

	bool TryBuildAndWriteMasterDocumentRecursive(
		const FString& MasterName,
		const FString& MasterActorPath,
		const FTransform& MasterWorldTransform,
		const TArray<FCadMasterHierarchyNode>& SourceChildren,
		const FString& WorkspaceFolder,
		const FString& ChildJsonFolderName,
		const FString& ContentRootPath,
		const FString& MasterJsonPath,
		FCadMasterDoc& OutDocument,
		FString& OutError)
	{
		const FString ChildJsonFolderPath = FPaths::Combine(WorkspaceFolder, ChildJsonFolderName);
		if (!EnsureMasterExportDirectoryExists(WorkspaceFolder, OutError) || !EnsureMasterExportDirectoryExists(ChildJsonFolderPath, OutError))
		{
			return false;
		}

		OutDocument = FCadMasterDoc();
		OutDocument.MasterName = MasterName;
		OutDocument.MasterActorPath = MasterActorPath;
		OutDocument.MasterWorldTransform = MasterWorldTransform;
		OutDocument.WorkspaceFolder = WorkspaceFolder;
		OutDocument.ChildJsonFolderName = ChildJsonFolderName;
		OutDocument.ContentRootPath = ContentRootPath;

		for (const FCadMasterHierarchyNode& SourceChildNode : SourceChildren)
		{
			if (SourceChildNode.NodeType != ECadMasterNodeType::Master)
			{
				AddDirectLeafChildToDocument(SourceChildNode, OutDocument);
				continue;
			}

			const FString NestedMasterName = SourceChildNode.ActorName.TrimStartAndEnd().IsEmpty()
				? TEXT("Master")
				: SourceChildNode.ActorName;
			const FString SafeNestedMasterName = MakeSafeMasterPathName(NestedMasterName);
			const FString NestedMasterJsonFileName = FString::Printf(TEXT("%s.json"), *SafeNestedMasterName);
			const FString NestedChildJsonFolderName = SafeNestedMasterName;
			const FString NestedWorkspaceFolder = ChildJsonFolderPath;
			const FString NestedMasterJsonPath = FPaths::Combine(NestedWorkspaceFolder, NestedMasterJsonFileName);
			const FString NestedContentRootPath = BuildNestedContentRootPath(ContentRootPath, SafeNestedMasterName);

			FCadMasterDoc NestedMasterDocument;
			if (!TryBuildAndWriteMasterDocumentRecursive(
				NestedMasterName,
				SourceChildNode.ActorPath,
				SourceChildNode.RelativeTransform * MasterWorldTransform,
				SourceChildNode.Children,
				NestedWorkspaceFolder,
				NestedChildJsonFolderName,
				NestedContentRootPath,
				NestedMasterJsonPath,
				NestedMasterDocument,
				OutError))
			{
				return false;
			}

			FCadMasterHierarchyNode MasterReferenceNode = SourceChildNode;
			MasterReferenceNode.ChildJsonFileName.Reset();
			MasterReferenceNode.MasterJsonFileName = NestedMasterJsonFileName;
			MasterReferenceNode.ChildJsonFolderName = NestedChildJsonFolderName;
			MasterReferenceNode.Children.Reset();
			OutDocument.HierarchyChildren.Add(MoveTemp(MasterReferenceNode));
		}

		return CadMasterDocExporter::TryWriteDocument(OutDocument, MasterJsonPath, OutError);
	}

	bool TryGenerateInternal(
		AActor& MasterActor,
		const FCadMasterSelection& SelectionResult,
		const FString& WorkspaceFolderOverride,
		FCadMasterJsonGenerationResult& OutResult,
		FString& OutError)
	{
		const FString WorkspaceFolderInput = ResolveWorkspaceFolderInput(MasterActor, WorkspaceFolderOverride);
		const FString MasterNameInput = ResolveMasterNameInput(MasterActor);

		FCadWorkspacePaths WorkspacePaths;
		if (!CadWorkspacePrep::TryPrepareWorkspace(WorkspaceFolderInput, MasterNameInput, WorkspacePaths, OutError))
		{
			return false;
		}

		const TArray<FCadMasterHierarchyNode> RootSourceChildren = SelectionResult.HierarchyChildren.Num() > 0
			? SelectionResult.HierarchyChildren
			: BuildMasterDocHierarchyNodesFromFlatChildren(SelectionResult.Children);

		FCadMasterDoc Document;
		if (!TryBuildAndWriteMasterDocumentRecursive(
			WorkspacePaths.MasterName,
			MasterActor.GetPathName(),
			MasterActor.GetActorTransform(),
			RootSourceChildren,
			WorkspacePaths.WorkspaceFolder,
			FPaths::GetCleanFilename(WorkspacePaths.ChildJsonFolderPath),
			WorkspacePaths.ContentRootPath,
			WorkspacePaths.MasterJsonPath,
			Document,
			OutError))
		{
			return false;
		}

		OutResult = FCadMasterJsonGenerationResult();
		OutResult.Document = MoveTemp(Document);
		OutResult.WorkspacePaths = WorkspacePaths;
		OutResult.BuildInput = OutResult.WorkspacePaths.ToBuildInput();
		return true;
	}
}

namespace CadMasterDocExporter
{
	bool TrySerializeDocument(const FCadMasterDoc& Document, FString& OutJson, FString& OutError)
	{
		return TrySerializeMasterDocument(Document, OutJson, OutError);
	}

	bool TryGenerateAndWriteFromSelectionResult(
		const FCadMasterSelection& SelectionResult,
		const FString& WorkspaceFolderOverride,
		FCadMasterJsonGenerationResult& OutResult,
		FString& OutError)
	{
		if (!SelectionResult.IsValid())
		{
			OutError = TEXT("Master actor selection result is invalid.");
			return false;
		}

		AActor* MasterActor = SelectionResult.MasterActor.Get();
		if (!MasterActor)
		{
			OutError = TEXT("Selected master-candidate actor is invalid.");
			return false;
		}

		return TryGenerateInternal(*MasterActor, SelectionResult, WorkspaceFolderOverride, OutResult, OutError);
	}

	bool TryGenerateAndWriteFromSelection(
		const FString& WorkspaceFolderOverride,
		FCadMasterJsonGenerationResult& OutResult,
		FString& OutError)
	{
		FCadMasterSelection SelectionResult;
		if (!CadMasterSelectionCollector::TryCollectFromSelection(SelectionResult, OutError))
		{
			return false;
		}
		return TryGenerateAndWriteFromSelectionResult(SelectionResult, WorkspaceFolderOverride, OutResult, OutError);
	}

	bool TryGenerateAndWriteFromMasterActor(
		ACadMasterActor* MasterActor,
		const FString& WorkspaceFolderOverride,
		FCadMasterJsonGenerationResult& OutResult,
		FString& OutError)
	{
		if (!MasterActor)
		{
			OutError = TEXT("Master actor is null.");
			return false;
		}

		FCadMasterSelection SelectionResult;
		if (!CadMasterSelectionCollector::TryCollectFromMasterActor(MasterActor, SelectionResult, OutError))
		{
			return false;
		}

		return TryGenerateInternal(*MasterActor, SelectionResult, WorkspaceFolderOverride, OutResult, OutError);
	}

	bool TryWriteDocument(const FCadMasterDoc& Document, const FString& OutputPath, FString& OutError)
	{
		FString JsonText;
		if (!TrySerializeMasterDocument(Document, JsonText, OutError))
		{
			return false;
		}

		if (!FFileHelper::SaveStringToFile(JsonText, *OutputPath))
		{
			OutError = FString::Printf(TEXT("Failed to write master json file: %s"), *OutputPath);
			return false;
		}

		return true;
	}
}
