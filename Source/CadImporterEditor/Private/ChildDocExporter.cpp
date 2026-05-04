#include "ChildDocExporter.h"

#include "CadImportStringUtils.h"
#include "CadImporterEditor.h"
#include "Editor/ActorHierarchyUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMeshActor.h"
#include "HAL/PlatformFileManager.h"
#include "Json/CadJsonTransformUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Workflow/WorkflowBlueprintBuild.h"
#include "Workflow/ChildVisualCollector.h"

namespace
{
	void BuildMovableChildTemplate(
		const FCadChildEntry& ChildEntry,
		AActor* ChildRootActor,
		FCadChildDoc& InOutChildDocument)
	{
		if (!ChildRootActor)
		{
			return;
		}

		TArray<AActor*> DirectChildren;
		CadActorHierarchyUtils::GetSortedAttachedChildren(ChildRootActor, DirectChildren, false);
		for (AActor* DirectChildActor : DirectChildren)
		{
			if (!DirectChildActor || DirectChildActor->IsA<AStaticMeshActor>())
			{
				continue;
			}

			const FString LinkName = CadActorHierarchyUtils::GetActorDisplayName(DirectChildActor);

			FCadChildLinkDef LinkTemplate;
			LinkTemplate.LinkName = LinkName;
			LinkTemplate.RelativeTransform = DirectChildActor->GetActorTransform().GetRelativeTransform(ChildRootActor->GetActorTransform());
			CadChildVisualCollector::CollectRootLinkVisuals(DirectChildActor, LinkTemplate.Visuals);
			InOutChildDocument.Links.Add(MoveTemp(LinkTemplate));

			FCadChildJointDef JointTemplate;
			JointTemplate.JointName = FString::Printf(TEXT("world_to_%s"), *LinkName);
			JointTemplate.JointType = ECadImportJointType::Fixed;
			JointTemplate.ParentActorName = FString();
			JointTemplate.ChildActorName = LinkName;
			JointTemplate.Axis = FVector::UpVector;
			InOutChildDocument.Joints.Add(MoveTemp(JointTemplate));
		}
	}

	TSharedPtr<FJsonObject> MakeJointTemplateObject(const FCadChildJointDef& Joint)
	{
		TSharedPtr<FJsonObject> JointObject = MakeShared<FJsonObject>();
		JointObject->SetStringField(TEXT("joint_name"), Joint.JointName);
		JointObject->SetStringField(TEXT("joint_type"), CadImportStringUtils::ToJointTypeString(Joint.JointType));
		JointObject->SetStringField(TEXT("parent_actor_name"), Joint.ParentActorName);
		JointObject->SetStringField(TEXT("child_actor_name"), Joint.ChildActorName);
		JointObject->SetArrayField(TEXT("axis"), TArray<TSharedPtr<FJsonValue>>
		{
			MakeShared<FJsonValueNumber>(Joint.Axis.X),
			MakeShared<FJsonValueNumber>(Joint.Axis.Y),
			MakeShared<FJsonValueNumber>(Joint.Axis.Z)
		});

		if (Joint.Limit.bHasLimit)
		{
			TSharedPtr<FJsonObject> LimitObject = MakeShared<FJsonObject>();
			LimitObject->SetNumberField(TEXT("lower"), Joint.Limit.Lower);
			LimitObject->SetNumberField(TEXT("upper"), Joint.Limit.Upper);
			LimitObject->SetNumberField(TEXT("effort"), Joint.Limit.Effort);
			LimitObject->SetNumberField(TEXT("velocity"), Joint.Limit.Velocity);
			JointObject->SetObjectField(TEXT("limit"), LimitObject);
		}

		return JointObject;
	}

	TSharedPtr<FJsonObject> MakeChildVisualObject(const FCadChildVisual& Visual)
	{
		TSharedPtr<FJsonObject> VisualObject = MakeShared<FJsonObject>();
		VisualObject->SetStringField(TEXT("mesh_path"), Visual.MeshPath);
		VisualObject->SetObjectField(TEXT("relative_transform"), CadJsonTransformUtils::MakeTransformObject(Visual.RelativeTransform));
		VisualObject->SetStringField(TEXT("material_path"), Visual.MaterialPath);
		VisualObject->SetStringField(TEXT("material_name"), Visual.MaterialName);
		return VisualObject;
	}

	TSharedPtr<FJsonObject> MakeChildLinkObject(const FCadChildLinkDef& LinkTemplate)
	{
		TSharedPtr<FJsonObject> LinkObject = MakeShared<FJsonObject>();
		LinkObject->SetStringField(TEXT("link_name"), LinkTemplate.LinkName);
		LinkObject->SetObjectField(TEXT("relative_transform"), CadJsonTransformUtils::MakeTransformObject(LinkTemplate.RelativeTransform));

		TArray<TSharedPtr<FJsonValue>> VisualValues;
		for (const FCadChildVisual& Visual : LinkTemplate.Visuals)
		{
			VisualValues.Add(MakeShared<FJsonValueObject>(MakeChildVisualObject(Visual)));
		}
		LinkObject->SetArrayField(TEXT("visuals"), VisualValues);
		return LinkObject;
	}

	FCadChildDoc BuildChildDocumentTemplate(
		const FCadMasterDoc& MasterDocument,
		const FCadChildEntry& ChildEntry,
		AActor* ChildRootActor)
	{
		FCadChildDoc ChildDocument;
		ChildDocument.MasterName = MasterDocument.MasterName;
		ChildDocument.ChildActorName = ChildEntry.ActorName;
		ChildDocument.SourceActorPath = ChildEntry.ActorPath;
		ChildDocument.ActorType = ChildEntry.ActorType;
		ChildDocument.RelativeTransform = ChildEntry.RelativeTransform;
		ChildDocument.Physics.Mass = 0.0f;
		ChildDocument.Physics.bSimulatePhysics = false;

		if (ChildRootActor)
		{
			if (ChildEntry.ActorType == ECadMasterChildActorType::Movable)
			{
				BuildMovableChildTemplate(ChildEntry, ChildRootActor, ChildDocument);
			}
			else
			{
				CadChildVisualCollector::CollectStaticChildVisuals(ChildRootActor, ChildDocument.Visuals);
			}
		}
		else
		{
			UE_LOG(
				LogCadImporter,
				Warning,
				TEXT("Child actor for json extraction was not found in level. child='%s', path='%s'. Visual collection is skipped."),
				*ChildEntry.ActorName,
				*ChildEntry.ActorPath);
		}

		if (ChildEntry.ActorType == ECadMasterChildActorType::Movable)
		{
			ChildDocument.Physics.Mass = 1.0f;
			ChildDocument.Physics.bSimulatePhysics = true;
		}

		return ChildDocument;
	}

	bool TrySerializeChildDocumentInternal(const FCadChildDoc& ChildDocument, FString& OutJson, FString& OutError)
	{
		TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
		RootObject->SetStringField(TEXT("master_name"), ChildDocument.MasterName);
		RootObject->SetStringField(TEXT("child_actor_name"), ChildDocument.ChildActorName);
		RootObject->SetStringField(TEXT("source_actor_path"), ChildDocument.SourceActorPath);
		if (CadMasterChildActorTypeShouldGenerateJson(ChildDocument.ActorType))
		{
			RootObject->SetStringField(TEXT("actor_type"), CadImportStringUtils::ToMasterChildActorTypeString(ChildDocument.ActorType));
		}
		RootObject->SetObjectField(TEXT("relative_transform"), CadJsonTransformUtils::MakeTransformObject(ChildDocument.RelativeTransform));

		TSharedPtr<FJsonObject> PhysicsObject = MakeShared<FJsonObject>();
		PhysicsObject->SetNumberField(TEXT("mass"), ChildDocument.Physics.Mass);
		PhysicsObject->SetBoolField(TEXT("simulate_physics"), ChildDocument.Physics.bSimulatePhysics);
		RootObject->SetObjectField(TEXT("physics"), PhysicsObject);

		TArray<TSharedPtr<FJsonValue>> VisualValues;
		for (const FCadChildVisual& Visual : ChildDocument.Visuals)
		{
			VisualValues.Add(MakeShared<FJsonValueObject>(MakeChildVisualObject(Visual)));
		}
		RootObject->SetArrayField(TEXT("visuals"), VisualValues);

		TArray<TSharedPtr<FJsonValue>> LinkValues;
		for (const FCadChildLinkDef& LinkTemplate : ChildDocument.Links)
		{
			LinkValues.Add(MakeShared<FJsonValueObject>(MakeChildLinkObject(LinkTemplate)));
		}
		RootObject->SetArrayField(TEXT("links"), LinkValues);

		TArray<TSharedPtr<FJsonValue>> JointValues;
		for (const FCadChildJointDef& Joint : ChildDocument.Joints)
		{
			JointValues.Add(MakeShared<FJsonValueObject>(MakeJointTemplateObject(Joint)));
		}
		RootObject->SetArrayField(TEXT("joints"), JointValues);

		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutJson);
		if (!FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
		{
			OutError = TEXT("Failed to serialize child json document.");
			return false;
		}

		return true;
	}

	bool TryWriteChildDocumentToFile(const FCadChildDoc& ChildDocument, const FString& OutputPath, FString& OutError)
	{
		FString JsonText;
		if (!TrySerializeChildDocumentInternal(ChildDocument, JsonText, OutError))
		{
			return false;
		}

		if (!FFileHelper::SaveStringToFile(JsonText, *OutputPath))
		{
			OutError = FString::Printf(TEXT("Failed to write child json file: %s"), *OutputPath);
			return false;
		}

		return true;
	}

	bool TryParseChildType(const FString& RawType, ECadMasterChildActorType& OutType, FString& OutError)
	{
		if (RawType.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("actor_type is empty. Set each child actor_type to 'static', 'background', or 'movable' in master json before extraction.");
			return false;
		}

		if (CadImportStringUtils::TryParseMasterChildActorTypeString(RawType, OutType, false))
		{
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported child actor_type '%s'. Expected 'static', 'background', or 'movable'."), *RawType);
		return false;
	}

	void FlattenHierarchyNodeLeaves(
		const FCadMasterHierarchyNode& Node,
		const FTransform& ParentRelativeTransform,
		TArray<FCadChildEntry>& OutChildren)
	{
		const FTransform AccumulatedRelativeTransform = Node.RelativeTransform * ParentRelativeTransform;
		if (CadMasterNodeTypeUsesChildJson(Node.NodeType))
		{
			FCadChildEntry ChildEntry;
			ChildEntry.ActorName = Node.ActorName;
			ChildEntry.ActorPath = Node.ActorPath;
			ChildEntry.RelativeTransform = AccumulatedRelativeTransform;
			ChildEntry.ActorType = CadMasterChildActorTypeFromNodeType(Node.NodeType);
			ChildEntry.ChildJsonFileName = Node.ChildJsonFileName;
			OutChildren.Add(MoveTemp(ChildEntry));
			return;
		}

		for (const FCadMasterHierarchyNode& ChildNode : Node.Children)
		{
			FlattenHierarchyNodeLeaves(ChildNode, AccumulatedRelativeTransform, OutChildren);
		}
	}

	void FlattenHierarchyNodes(const TArray<FCadMasterHierarchyNode>& HierarchyNodes, TArray<FCadChildEntry>& OutChildren)
	{
		OutChildren.Reset();
		for (const FCadMasterHierarchyNode& Node : HierarchyNodes)
		{
			FlattenHierarchyNodeLeaves(Node, FTransform::Identity, OutChildren);
		}
	}

	bool HasReferencedNestedMasterChildren(const FCadMasterDoc& MasterDocument)
	{
		return MasterDocument.HierarchyChildren.ContainsByPredicate([](const FCadMasterHierarchyNode& Node)
		{
			return Node.NodeType == ECadMasterNodeType::Master && !Node.MasterJsonFileName.TrimStartAndEnd().IsEmpty();
		});
	}

	bool TryExtractChildJsonFilesRecursive(
		const FString& MasterJsonPath,
		const FCadMasterDoc& MasterDocument,
		TArray<FString>& InOutGeneratedChildJsonPaths,
		FString& OutError)
	{
		const FString WorkspaceFolder = FPaths::ConvertRelativePathToFull(MasterDocument.WorkspaceFolder);
		const FString ChildFolderName = MasterDocument.ChildJsonFolderName.TrimStartAndEnd();
		const FString ChildJsonFolderPath = FPaths::Combine(WorkspaceFolder, ChildFolderName);

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.CreateDirectoryTree(*ChildJsonFolderPath))
		{
			OutError = FString::Printf(TEXT("Failed to create child json folder: %s"), *ChildJsonFolderPath);
			return false;
		}

		TArray<FCadChildEntry> LeafChildren;
		if (MasterDocument.HierarchyChildren.Num() > 0)
		{
			CadWorkflowBlueprintBuild::CollectDirectLeafChildrenForBuild(MasterDocument, LeafChildren);
		}
		if (LeafChildren.Num() == 0)
		{
			LeafChildren = MasterDocument.Children;
		}
		if (LeafChildren.Num() == 0 && MasterDocument.HierarchyChildren.Num() > 0)
		{
			FlattenHierarchyNodes(MasterDocument.HierarchyChildren, LeafChildren);
		}

		for (const FCadChildEntry& ChildEntry : LeafChildren)
		{
			if (!CadMasterChildActorTypeShouldGenerateJson(ChildEntry.ActorType))
			{
				continue;
			}

			AActor* ChildRootActor = CadActorHierarchyUtils::FindByPath(ChildEntry.ActorPath);
			FCadChildDoc ChildDocument = BuildChildDocumentTemplate(MasterDocument, ChildEntry, ChildRootActor);

			FString ChildFileName = ChildEntry.ChildJsonFileName.TrimStartAndEnd();
			if (ChildFileName.IsEmpty())
			{
				const FString SafeActorName = FPaths::MakeValidFileName(ChildEntry.ActorName);
				ChildFileName = FString::Printf(TEXT("%s.json"), SafeActorName.IsEmpty() ? TEXT("Child") : *SafeActorName);
			}

			const FString OutputPath = FPaths::Combine(ChildJsonFolderPath, ChildFileName);
			if (!TryWriteChildDocumentToFile(ChildDocument, OutputPath, OutError))
			{
				return false;
			}

			InOutGeneratedChildJsonPaths.Add(OutputPath);
		}

		if (!HasReferencedNestedMasterChildren(MasterDocument))
		{
			return true;
		}

		for (const FCadMasterHierarchyNode& ChildNode : MasterDocument.HierarchyChildren)
		{
			if (ChildNode.NodeType != ECadMasterNodeType::Master)
			{
				continue;
			}

			const FString NestedMasterJsonFileName = ChildNode.MasterJsonFileName.TrimStartAndEnd();
			if (NestedMasterJsonFileName.IsEmpty())
			{
				continue;
			}

			const FString NestedMasterJsonPath = FPaths::Combine(ChildJsonFolderPath, NestedMasterJsonFileName);
			FCadMasterDoc NestedMasterDocument;
			if (!CadChildDocExporter::TryParseMasterDocument(NestedMasterJsonPath, NestedMasterDocument, OutError))
			{
				return false;
			}

			if (!TryExtractChildJsonFilesRecursive(NestedMasterJsonPath, NestedMasterDocument, InOutGeneratedChildJsonPaths, OutError))
			{
				return false;
			}
		}

		return true;
	}

	bool TryParseMasterHierarchyNode(
		const TSharedPtr<FJsonObject>& ChildObject,
		const FString& NodeContext,
		FCadMasterHierarchyNode& OutNode,
		FString& OutError)
	{
		if (!ChildObject.IsValid())
		{
			OutError = FString::Printf(TEXT("%s is invalid."), *NodeContext);
			return false;
		}

		OutNode = FCadMasterHierarchyNode();
		if (!ChildObject->TryGetStringField(TEXT("actor_name"), OutNode.ActorName))
		{
			OutError = FString::Printf(TEXT("%s is missing 'actor_name'."), *NodeContext);
			return false;
		}

		ChildObject->TryGetStringField(TEXT("actor_path"), OutNode.ActorPath);
		ChildObject->TryGetStringField(TEXT("master_json_file_name"), OutNode.MasterJsonFileName);
		ChildObject->TryGetStringField(TEXT("child_json_folder_name"), OutNode.ChildJsonFolderName);

		const TSharedPtr<FJsonObject>* ChildTransformObject = nullptr;
		if (!ChildObject->TryGetObjectField(TEXT("relative_transform"), ChildTransformObject) ||
			!ChildTransformObject || !ChildTransformObject->IsValid())
		{
			OutError = FString::Printf(TEXT("%s is missing 'relative_transform'."), *NodeContext);
			return false;
		}
		if (!CadJsonTransformUtils::ParseTransformObject(*ChildTransformObject, OutNode.RelativeTransform, OutError))
		{
			OutError = FString::Printf(TEXT("%s transform parse failed: %s"), *NodeContext, *OutError);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* NestedChildrenArray = nullptr;
		const bool bHasNestedChildrenField = ChildObject->TryGetArrayField(TEXT("children"), NestedChildrenArray) && NestedChildrenArray != nullptr;

		FString NodeTypeString;
		const bool bHasNodeType = ChildObject->TryGetStringField(TEXT("node_type"), NodeTypeString);
		if (bHasNodeType)
		{
			if (!CadImportStringUtils::TryParseMasterNodeTypeString(NodeTypeString, OutNode.NodeType))
			{
				OutError = FString::Printf(TEXT("%s has unsupported node_type '%s'."), *NodeContext, *NodeTypeString);
				return false;
			}
		}
		else
		{
			FString ActorTypeString;
			if (ChildObject->TryGetStringField(TEXT("actor_type"), ActorTypeString))
			{
				ECadMasterChildActorType ParsedActorType = ECadMasterChildActorType::Static;
				if (!TryParseChildType(ActorTypeString, ParsedActorType, OutError))
				{
					OutError = FString::Printf(TEXT("%s parse failed: %s"), *NodeContext, *OutError);
					return false;
				}

				OutNode.NodeType = CadMasterNodeTypeFromChildActorType(ParsedActorType);
			}
			else if (bHasNestedChildrenField)
			{
				OutNode.NodeType = ECadMasterNodeType::Master;
			}
			else
			{
				OutError = FString::Printf(TEXT("%s is missing 'node_type' or 'actor_type'."), *NodeContext);
				return false;
			}
		}

		if (CadMasterNodeTypeUsesChildJson(OutNode.NodeType))
		{
			ChildObject->TryGetStringField(TEXT("child_json_file_name"), OutNode.ChildJsonFileName);
			if (OutNode.ChildJsonFileName.TrimStartAndEnd().IsEmpty())
			{
				const FString SafeChildName = FPaths::MakeValidFileName(OutNode.ActorName);
				OutNode.ChildJsonFileName = FString::Printf(TEXT("%s.json"), SafeChildName.IsEmpty() ? TEXT("Child") : *SafeChildName);
			}

			if (bHasNestedChildrenField && NestedChildrenArray->Num() > 0)
			{
				OutError = FString::Printf(TEXT("%s is a leaf node but also defines nested children."), *NodeContext);
				return false;
			}

			return true;
		}

		if (bHasNestedChildrenField)
		{
			for (int32 ChildIndex = 0; ChildIndex < NestedChildrenArray->Num(); ++ChildIndex)
			{
				const TSharedPtr<FJsonObject> NestedChildObject = (*NestedChildrenArray)[ChildIndex].IsValid()
					? (*NestedChildrenArray)[ChildIndex]->AsObject()
					: nullptr;

				FCadMasterHierarchyNode NestedChildNode;
				if (!TryParseMasterHierarchyNode(
					NestedChildObject,
					FString::Printf(TEXT("%s.children[%d]"), *NodeContext, ChildIndex),
					NestedChildNode,
					OutError))
				{
					return false;
				}

				OutNode.Children.Add(MoveTemp(NestedChildNode));
			}
		}

		return true;
	}
}

namespace CadChildDocExporter
{
	void BuildChildDocumentPreview(
		const FCadMasterDoc& MasterDocument,
		const FCadChildEntry& ChildEntry,
		FCadChildDoc& OutDocument)
	{
		AActor* ChildRootActor = CadActorHierarchyUtils::FindByPath(ChildEntry.ActorPath);
		OutDocument = BuildChildDocumentTemplate(MasterDocument, ChildEntry, ChildRootActor);
	}

	bool TrySerializeChildDocument(
		const FCadChildDoc& ChildDocument,
		FString& OutJson,
		FString& OutError)
	{
		return TrySerializeChildDocumentInternal(ChildDocument, OutJson, OutError);
	}

	bool TryParseMasterDocument(
		const FString& MasterJsonPath,
		FCadMasterDoc& OutDocument,
		FString& OutError)
	{
		OutDocument = FCadMasterDoc();
		OutError.Reset();

		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *MasterJsonPath))
		{
			OutError = FString::Printf(TEXT("Failed to read master json file: %s"), *MasterJsonPath);
			return false;
		}

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			OutError = FString::Printf(TEXT("Failed to parse master json file: %s"), *MasterJsonPath);
			return false;
		}

		if (!RootObject->TryGetStringField(TEXT("master_name"), OutDocument.MasterName))
		{
			OutError = TEXT("Master json is missing 'master_name'.");
			return false;
		}

		RootObject->TryGetStringField(TEXT("master_actor_path"), OutDocument.MasterActorPath);
		RootObject->TryGetStringField(TEXT("workspace_folder"), OutDocument.WorkspaceFolder);
		RootObject->TryGetStringField(TEXT("child_json_folder_name"), OutDocument.ChildJsonFolderName);
		RootObject->TryGetStringField(TEXT("content_root_path"), OutDocument.ContentRootPath);

		const TSharedPtr<FJsonObject>* MasterTransformObject = nullptr;
		if (RootObject->TryGetObjectField(TEXT("master_world_transform"), MasterTransformObject) &&
			MasterTransformObject && MasterTransformObject->IsValid())
		{
			if (!CadJsonTransformUtils::ParseTransformObject(*MasterTransformObject, OutDocument.MasterWorldTransform, OutError))
			{
				OutError = FString::Printf(TEXT("Failed to parse 'master_world_transform': %s"), *OutError);
				return false;
			}
		}
		else
		{
			OutDocument.MasterWorldTransform = FTransform::Identity;
		}

		const TArray<TSharedPtr<FJsonValue>>* ChildrenArray = nullptr;
		if (!RootObject->TryGetArrayField(TEXT("children"), ChildrenArray) || !ChildrenArray)
		{
			OutError = TEXT("Master json is missing 'children' array.");
			return false;
		}

		for (int32 ChildIndex = 0; ChildIndex < ChildrenArray->Num(); ++ChildIndex)
		{
			const TSharedPtr<FJsonObject> ChildObject = (*ChildrenArray)[ChildIndex].IsValid()
				? (*ChildrenArray)[ChildIndex]->AsObject()
				: nullptr;
			FCadMasterHierarchyNode HierarchyNode;
			if (!TryParseMasterHierarchyNode(
				ChildObject,
				FString::Printf(TEXT("children[%d]"), ChildIndex),
				HierarchyNode,
				OutError))
			{
				return false;
			}

			OutDocument.HierarchyChildren.Add(MoveTemp(HierarchyNode));
		}

		FlattenHierarchyNodes(OutDocument.HierarchyChildren, OutDocument.Children);

		if (OutDocument.WorkspaceFolder.TrimStartAndEnd().IsEmpty())
		{
			OutDocument.WorkspaceFolder = FPaths::GetPath(MasterJsonPath);
		}
		if (OutDocument.ChildJsonFolderName.TrimStartAndEnd().IsEmpty())
		{
			const FString SafeMasterName = FPaths::MakeValidFileName(OutDocument.MasterName);
			OutDocument.ChildJsonFolderName = SafeMasterName.IsEmpty() ? TEXT("Master") : SafeMasterName;
		}
		if (OutDocument.ContentRootPath.TrimStartAndEnd().IsEmpty())
		{
			const FString SafeMasterName = FPaths::MakeValidFileName(OutDocument.MasterName);
			OutDocument.ContentRootPath = FString::Printf(TEXT("/Game/%s"), SafeMasterName.IsEmpty() ? TEXT("Master") : *SafeMasterName);
		}

		return true;
	}

	bool TryExtractChildJsonFilesFromDocument(
		const FString& MasterJsonPath,
		const FCadMasterDoc& MasterDocument,
		FCadChildJsonResult& OutResult,
		FString& OutError)
	{
		OutResult = FCadChildJsonResult();
		OutError.Reset();

		const FString WorkspaceFolder = FPaths::ConvertRelativePathToFull(MasterDocument.WorkspaceFolder);
		const FString ChildFolderName = MasterDocument.ChildJsonFolderName.TrimStartAndEnd();
		const FString ChildJsonFolderPath = FPaths::Combine(WorkspaceFolder, ChildFolderName);
		TArray<FString> GeneratedChildJsonPaths;
		if (!TryExtractChildJsonFilesRecursive(MasterJsonPath, MasterDocument, GeneratedChildJsonPaths, OutError))
		{
			return false;
		}

		OutResult.MasterJsonPath = MasterJsonPath;
		OutResult.ChildJsonFolderPath = ChildJsonFolderPath;
		OutResult.MasterDocument = MasterDocument;
		OutResult.GeneratedChildJsonPaths = MoveTemp(GeneratedChildJsonPaths);
		OutResult.BuildInput.WorkspaceFolder = WorkspaceFolder;
		OutResult.BuildInput.MasterJsonPath = MasterJsonPath;
		OutResult.BuildInput.ChildJsonFolderPath = ChildJsonFolderPath;
		OutResult.BuildInput.ContentRootPath = MasterDocument.ContentRootPath;
		return true;
	}

	bool TryExtractChildJsonFiles(
		const FString& MasterJsonPath,
		FCadChildJsonResult& OutResult,
		FString& OutError)
	{
		FCadMasterDoc MasterDocument;
		if (!TryParseMasterDocument(MasterJsonPath, MasterDocument, OutError))
		{
			return false;
		}

		return TryExtractChildJsonFilesFromDocument(MasterJsonPath, MasterDocument, OutResult, OutError);
	}
}
