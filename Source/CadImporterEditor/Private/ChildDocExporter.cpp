#include "ChildDocExporter.h"

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
#include "Workflow/ChildVisualCollector.h"

namespace
{
	FString MasterChildTypeToString(const ECadMasterChildActorType ActorType)
	{
		switch (ActorType)
		{
		case ECadMasterChildActorType::Movable:
			return TEXT("movable");
		case ECadMasterChildActorType::Static:
		default:
			return TEXT("static");
		}
	}

	FString MasterJointTypeToString(const ECadImportJointType JointType)
	{
		switch (JointType)
		{
		case ECadImportJointType::Revolute:
			return TEXT("revolute");
		case ECadImportJointType::Prismatic:
			return TEXT("prismatic");
		case ECadImportJointType::Fixed:
		default:
			return TEXT("fixed");
		}
	}

	FString GetActorDisplayNameForExtractor(const AActor* Actor)
	{
		return Actor ? Actor->GetActorNameOrLabel() : TEXT("(none)");
	}

	void BuildMovableChildHierarchyRecursive(
		AActor* CurrentActor,
		AActor* ParentActor,
		const FTransform& RootRelativeTransform,
		FCadChildDoc& InOutChildDocument)
	{
		if (!CurrentActor)
		{
			return;
		}

		const FString CurrentLinkName = GetActorDisplayNameForExtractor(CurrentActor);
		const bool bIsRoot = (ParentActor == nullptr);

		FCadChildLinkDef LinkTemplate;
		LinkTemplate.LinkName = CurrentLinkName;
		LinkTemplate.RelativeTransform = bIsRoot
			? RootRelativeTransform
			: CurrentActor->GetActorTransform().GetRelativeTransform(ParentActor->GetActorTransform());
		CadChildVisualCollector::CollectRootLinkVisuals(CurrentActor, LinkTemplate.Visuals);
		InOutChildDocument.Links.Add(MoveTemp(LinkTemplate));

		if (!bIsRoot)
		{
			const FString ParentLinkName = GetActorDisplayNameForExtractor(ParentActor);
			FCadChildJointDef JointTemplate;
			JointTemplate.JointName = FString::Printf(TEXT("%s_to_%s"), *ParentLinkName, *CurrentLinkName);
			JointTemplate.JointType = ECadImportJointType::Fixed;
			JointTemplate.ParentActorName = ParentLinkName;
			JointTemplate.ChildActorName = CurrentLinkName;
			JointTemplate.Axis = FVector::UpVector;
			InOutChildDocument.Joints.Add(MoveTemp(JointTemplate));
		}

		TArray<AActor*> Children;
		CadActorHierarchyUtils::GetSortedAttachedChildren(CurrentActor, Children, false);
		for (AActor* ChildActor : Children)
		{
			if (ChildActor && ChildActor->IsA<AStaticMeshActor>())
			{
				continue;
			}

			BuildMovableChildHierarchyRecursive(ChildActor, CurrentActor, RootRelativeTransform, InOutChildDocument);
		}
	}

	void BuildMovableChildTemplate(
		const FCadChildEntry& ChildEntry,
		AActor* ChildRootActor,
		FCadChildDoc& InOutChildDocument)
	{
		if (!ChildRootActor)
		{
			return;
		}

		BuildMovableChildHierarchyRecursive(ChildRootActor, nullptr, ChildEntry.RelativeTransform, InOutChildDocument);
		if (InOutChildDocument.Links.Num() <= 0)
		{
			return;
		}

		const FString RootLinkName = InOutChildDocument.Links[0].LinkName;
		if (RootLinkName.TrimStartAndEnd().IsEmpty())
		{
			return;
		}

		FCadChildJointDef RootAnchorJoint;
		RootAnchorJoint.JointName = FString::Printf(TEXT("master_to_%s"), *RootLinkName);
		RootAnchorJoint.JointType = ECadImportJointType::Fixed;
		RootAnchorJoint.ParentActorName = TEXT("");
		RootAnchorJoint.ChildActorName = RootLinkName;
		RootAnchorJoint.Axis = FVector::UpVector;
		InOutChildDocument.Joints.Insert(MoveTemp(RootAnchorJoint), 0);
	}

	TSharedPtr<FJsonObject> MakeJointTemplateObject(const FCadChildJointDef& Joint)
	{
		TSharedPtr<FJsonObject> JointObject = MakeShared<FJsonObject>();
		JointObject->SetStringField(TEXT("joint_name"), Joint.JointName);
		JointObject->SetStringField(TEXT("joint_type"), MasterJointTypeToString(Joint.JointType));
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

	bool TrySerializeChildDocument(const FCadChildDoc& ChildDocument, FString& OutJson, FString& OutError)
	{
		TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
		RootObject->SetStringField(TEXT("master_name"), ChildDocument.MasterName);
		RootObject->SetStringField(TEXT("child_actor_name"), ChildDocument.ChildActorName);
		RootObject->SetStringField(TEXT("source_actor_path"), ChildDocument.SourceActorPath);
		RootObject->SetStringField(TEXT("actor_type"), MasterChildTypeToString(ChildDocument.ActorType));
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
		if (!TrySerializeChildDocument(ChildDocument, JsonText, OutError))
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
			OutError = TEXT("actor_type is empty. Set each child actor_type to 'static' or 'movable' in master json before extraction.");
			return false;
		}

		if (RawType.Equals(TEXT("static"), ESearchCase::IgnoreCase))
		{
			OutType = ECadMasterChildActorType::Static;
			return true;
		}
		if (RawType.Equals(TEXT("movable"), ESearchCase::IgnoreCase))
		{
			OutType = ECadMasterChildActorType::Movable;
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported child actor_type '%s'. Expected 'static' or 'movable'."), *RawType);
		return false;
	}
}

namespace CadChildJsonService
{
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
			if (!ChildObject.IsValid())
			{
				OutError = FString::Printf(TEXT("Child entry at index %d is invalid."), ChildIndex);
				return false;
			}

			FCadChildEntry ChildEntry;
			if (!ChildObject->TryGetStringField(TEXT("actor_name"), ChildEntry.ActorName))
			{
				OutError = FString::Printf(TEXT("Child entry %d is missing 'actor_name'."), ChildIndex);
				return false;
			}
			ChildObject->TryGetStringField(TEXT("actor_path"), ChildEntry.ActorPath);
			ChildObject->TryGetStringField(TEXT("child_json_file_name"), ChildEntry.ChildJsonFileName);

			FString ActorTypeString;
			if (!ChildObject->TryGetStringField(TEXT("actor_type"), ActorTypeString))
			{
				OutError = FString::Printf(TEXT("Child '%s' is missing 'actor_type'."), *ChildEntry.ActorName);
				return false;
			}
			if (!TryParseChildType(ActorTypeString, ChildEntry.ActorType, OutError))
			{
				OutError = FString::Printf(TEXT("Child '%s' parse failed: %s"), *ChildEntry.ActorName, *OutError);
				return false;
			}

			const TSharedPtr<FJsonObject>* ChildTransformObject = nullptr;
			if (!ChildObject->TryGetObjectField(TEXT("relative_transform"), ChildTransformObject) ||
				!ChildTransformObject || !ChildTransformObject->IsValid())
			{
				OutError = FString::Printf(TEXT("Child '%s' is missing 'relative_transform'."), *ChildEntry.ActorName);
				return false;
			}
			if (!CadJsonTransformUtils::ParseTransformObject(*ChildTransformObject, ChildEntry.RelativeTransform, OutError))
			{
				OutError = FString::Printf(TEXT("Child '%s' transform parse failed: %s"), *ChildEntry.ActorName, *OutError);
				return false;
			}

			if (ChildEntry.ChildJsonFileName.TrimStartAndEnd().IsEmpty())
			{
				const FString SafeChildName = FPaths::MakeValidFileName(ChildEntry.ActorName);
				ChildEntry.ChildJsonFileName = FString::Printf(TEXT("%s.json"), SafeChildName.IsEmpty() ? TEXT("Child") : *SafeChildName);
			}

			OutDocument.Children.Add(MoveTemp(ChildEntry));
		}

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

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.CreateDirectoryTree(*ChildJsonFolderPath))
		{
			OutError = FString::Printf(TEXT("Failed to create child json folder: %s"), *ChildJsonFolderPath);
			return false;
		}

		TArray<FString> GeneratedChildJsonPaths;
		for (const FCadChildEntry& ChildEntry : MasterDocument.Children)
		{
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

			GeneratedChildJsonPaths.Add(OutputPath);
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
