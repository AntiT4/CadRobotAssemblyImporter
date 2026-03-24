#include "MasterJsonGenerator.h"

#include "CadMasterActor.h"
#include "MasterJsonActorCollector.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString ToMasterChildTypeString(const ECadMasterChildActorType ActorType)
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

	TSharedPtr<FJsonObject> MakeMasterWorkflowTransformObject(const FTransform& Transform)
	{
		const FVector Location = Transform.GetLocation();
		const FRotator Rotation = Transform.GetRotation().Rotator();
		const FVector Scale = Transform.GetScale3D();

		TSharedPtr<FJsonObject> TransformObject = MakeShared<FJsonObject>();
		TransformObject->SetArrayField(TEXT("location"), TArray<TSharedPtr<FJsonValue>>
		{
			MakeShared<FJsonValueNumber>(Location.X),
			MakeShared<FJsonValueNumber>(Location.Y),
			MakeShared<FJsonValueNumber>(Location.Z)
		});
		TransformObject->SetArrayField(TEXT("rotation"), TArray<TSharedPtr<FJsonValue>>
		{
			MakeShared<FJsonValueNumber>(Rotation.Roll),
			MakeShared<FJsonValueNumber>(Rotation.Pitch),
			MakeShared<FJsonValueNumber>(Rotation.Yaw)
		});
		TransformObject->SetArrayField(TEXT("scale"), TArray<TSharedPtr<FJsonValue>>
		{
			MakeShared<FJsonValueNumber>(Scale.X),
			MakeShared<FJsonValueNumber>(Scale.Y),
			MakeShared<FJsonValueNumber>(Scale.Z)
		});

		return TransformObject;
	}

	TSharedPtr<FJsonObject> MakeMasterChildObject(const FCadChildEntry& ChildEntry)
	{
		TSharedPtr<FJsonObject> ChildObject = MakeShared<FJsonObject>();
		ChildObject->SetStringField(TEXT("actor_name"), ChildEntry.ActorName);
		ChildObject->SetStringField(TEXT("actor_type"), ToMasterChildTypeString(ChildEntry.ActorType));
		ChildObject->SetStringField(TEXT("child_json_file_name"), ChildEntry.ChildJsonFileName);
		ChildObject->SetObjectField(TEXT("relative_transform"), MakeMasterWorkflowTransformObject(ChildEntry.RelativeTransform));
		return ChildObject;
	}

	bool TrySerializeMasterDocument(const FCadMasterDoc& Document, FString& OutJson, FString& OutError)
	{
		TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
		RootObject->SetStringField(TEXT("master_name"), Document.MasterName);
		RootObject->SetStringField(TEXT("master_actor_path"), Document.MasterActorPath);
		RootObject->SetObjectField(TEXT("master_world_transform"), MakeMasterWorkflowTransformObject(Document.MasterWorldTransform));
		RootObject->SetStringField(TEXT("workspace_folder"), Document.WorkspaceFolder);
		RootObject->SetStringField(TEXT("child_json_folder_name"), Document.ChildJsonFolderName);
		RootObject->SetStringField(TEXT("content_root_path"), Document.ContentRootPath);

		TArray<TSharedPtr<FJsonValue>> ChildValues;
		for (const FCadChildEntry& ChildEntry : Document.Children)
		{
			ChildValues.Add(MakeShared<FJsonValueObject>(MakeMasterChildObject(ChildEntry)));
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

	bool TryBuildDocument(
		AActor& MasterActor,
		const FCadMasterSelection& SelectionResult,
		const FCadWorkspacePaths& WorkspacePaths,
		FCadMasterDoc& OutDocument,
		FString& OutError)
	{
		if (!SelectionResult.IsValid())
		{
			OutError = TEXT("Master actor selection result is invalid.");
			return false;
		}

		OutDocument = FCadMasterDoc();
		OutDocument.MasterName = WorkspacePaths.MasterName;
		OutDocument.MasterActorPath = MasterActor.GetPathName();
		OutDocument.MasterWorldTransform = MasterActor.GetActorTransform();
		OutDocument.WorkspaceFolder = WorkspacePaths.WorkspaceFolder;
		OutDocument.ChildJsonFolderName = FPaths::GetCleanFilename(WorkspacePaths.ChildJsonFolderPath);
		OutDocument.ContentRootPath = WorkspacePaths.ContentRootPath;
		OutDocument.Children = SelectionResult.Children;
		return true;
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
		if (!CadWorkspaceService::TryPrepareWorkspace(WorkspaceFolderInput, MasterNameInput, WorkspacePaths, OutError))
		{
			return false;
		}

		FCadMasterDoc Document;
		if (!TryBuildDocument(MasterActor, SelectionResult, WorkspacePaths, Document, OutError))
		{
			return false;
		}

		if (!CadMasterJsonGenerator::TryWriteDocument(Document, WorkspacePaths.MasterJsonPath, OutError))
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

namespace CadMasterJsonGenerator
{
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
		if (!CadMasterSelection::TryCollectFromSelection(SelectionResult, OutError))
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
		if (!CadMasterSelection::TryCollectFromMasterActor(MasterActor, SelectionResult, OutError))
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
