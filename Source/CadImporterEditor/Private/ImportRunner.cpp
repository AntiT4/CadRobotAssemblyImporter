#include "ImportRunner.h"

#include "CadMasterActor.h"
#include "Import/AssemblyBuilder.h"
#include "MasterChildJsonExtractor.h"
#include "MasterWorkflowImportParser.h"
#include "MasterWorkflowLevelReplacer.h"
#include "Import/MeshImporter.h"
#include "UI/DialogUtils.h"
#include "Import/JsonParser.h"
#include "Import/PathResolver.h"
#include "CadImporterEditor.h"
#include "DesktopPlatformModule.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace
{
	void FillImportModelUnitsDefaults(FCadImportModel& InOutModel)
	{
		InOutModel.Units.Length = TEXT("centimeter");
		InOutModel.Units.Angle = TEXT("degree");
		InOutModel.Units.UpAxis = TEXT("z");
		InOutModel.Units.FrontAxis = TEXT("x");
		InOutModel.Units.Handedness = TEXT("left");
		InOutModel.Units.EulerOrder = TEXT("xyz");
		InOutModel.Units.MeshScale = 1.0f;
	}

	FString ResolveWorkflowWorkspaceFolder(
		const FCadMasterWorkflowBuildInput& BuildInput,
		const FCadMasterJsonDocument& MasterDocument,
		const FString& MasterJsonPath)
	{
		const FString InputWorkspace = BuildInput.WorkspaceFolder.TrimStartAndEnd();
		if (!InputWorkspace.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(InputWorkspace);
		}

		const FString DocumentWorkspace = MasterDocument.WorkspaceFolder.TrimStartAndEnd();
		if (!DocumentWorkspace.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(DocumentWorkspace);
		}

		return FPaths::ConvertRelativePathToFull(FPaths::GetPath(MasterJsonPath));
	}

	FString ResolveWorkflowChildJsonFolder(
		const FCadMasterWorkflowBuildInput& BuildInput,
		const FCadMasterJsonDocument& MasterDocument,
		const FString& WorkspaceFolder)
	{
		const FString ExplicitChildFolder = BuildInput.ChildJsonFolderPath.TrimStartAndEnd();
		if (!ExplicitChildFolder.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(ExplicitChildFolder);
		}

		return FPaths::ConvertRelativePathToFull(FPaths::Combine(WorkspaceFolder, MasterDocument.ChildJsonFolderName));
	}

	FString ResolveChildRootLinkName(const FCadChildJsonDocument& ChildDocument, const FCadMasterChildEntry& ChildEntry)
	{
		for (const FCadChildLinkTemplate& LinkTemplate : ChildDocument.Links)
		{
			const FString LinkName = LinkTemplate.LinkName.TrimStartAndEnd();
			if (!LinkName.IsEmpty())
			{
				return LinkName;
			}
		}

		const FString ChildName = ChildDocument.ChildActorName.TrimStartAndEnd();
		if (!ChildName.IsEmpty())
		{
			return ChildName;
		}

		return ChildEntry.ActorName.TrimStartAndEnd();
	}

	void AppendChildVisualsToLink(const TArray<FCadChildVisualEntry>& ChildVisuals, FCadImportLink& OutLink)
	{
		for (const FCadChildVisualEntry& ChildVisual : ChildVisuals)
		{
			FCadImportVisual Visual;
			Visual.MeshPath = ChildVisual.MeshPath;
			Visual.Transform = ChildVisual.RelativeTransform;
			Visual.MaterialPath = ChildVisual.MaterialPath;
			Visual.MaterialName = ChildVisual.MaterialName;
			OutLink.Visuals.Add(MoveTemp(Visual));
		}
	}

	bool TryBuildImportModelFromChildDocument(
		const FCadMasterChildEntry& ChildEntry,
		const FCadChildJsonDocument& ChildDocument,
		const FString& ChildJsonFolderPath,
		const FString& OutputRootPath,
		FCadImportModel& OutModel,
		FString& OutError)
	{
		OutModel = FCadImportModel();
		OutError.Reset();

		const FString RootLinkName = ResolveChildRootLinkName(ChildDocument, ChildEntry);
		if (RootLinkName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Child '%s' has no valid root link name."), *ChildEntry.ActorName);
			return false;
		}

		OutModel.Profile = (ChildDocument.ActorType == ECadMasterChildActorType::Movable)
			? ECadImportModelProfile::DynamicRobot
			: ECadImportModelProfile::FixedAssembly;
		OutModel.RobotName = ChildEntry.ActorName.TrimStartAndEnd().IsEmpty()
			? RootLinkName
			: ChildEntry.ActorName.TrimStartAndEnd();
		OutModel.OutputRootPath = OutputRootPath.TrimStartAndEnd();
		OutModel.RootLinkName = RootLinkName;
		OutModel.SourceDirectory = ChildJsonFolderPath;
		FillImportModelUnitsDefaults(OutModel);

		if (ChildDocument.Links.Num() > 0)
		{
			for (const FCadChildLinkTemplate& LinkTemplate : ChildDocument.Links)
			{
				const FString LinkName = LinkTemplate.LinkName.TrimStartAndEnd();
				if (LinkName.IsEmpty())
				{
					OutError = FString::Printf(TEXT("Child '%s' has an empty link_name in links[]."), *ChildEntry.ActorName);
					return false;
				}

				FCadImportLink Link;
				Link.Name = LinkName;
				Link.Transform = LinkTemplate.RelativeTransform;
				Link.Physics = ChildDocument.Physics;
				AppendChildVisualsToLink(LinkTemplate.Visuals, Link);
				OutModel.Links.Add(MoveTemp(Link));
			}
		}
		else
		{
			FCadImportLink RootLink;
			RootLink.Name = RootLinkName;
			RootLink.Transform = FTransform::Identity;
			RootLink.Physics = ChildDocument.Physics;
			AppendChildVisualsToLink(ChildDocument.Visuals, RootLink);
			OutModel.Links.Add(MoveTemp(RootLink));
		}

		TMap<FString, FCadImportLink> LinksByName;
		for (const FCadImportLink& Link : OutModel.Links)
		{
			if (LinksByName.Contains(Link.Name))
			{
				OutError = FString::Printf(TEXT("Child '%s' has duplicate link name '%s'."), *ChildEntry.ActorName, *Link.Name);
				return false;
			}
			LinksByName.Add(Link.Name, Link);
		}

		for (const FCadChildJointTemplate& JointTemplate : ChildDocument.Joints)
		{
			const FString RawParentName = JointTemplate.ParentActorName.TrimStartAndEnd();
			const FString ParentName = RawParentName.IsEmpty() ? TEXT("master") : RawParentName;
			if (ParentName.Equals(TEXT("master"), ESearchCase::IgnoreCase))
			{
				// Parent->master anchor is consumed at level assembly stage.
				continue;
			}

			const FString ChildName = JointTemplate.ChildActorName.TrimStartAndEnd().IsEmpty()
				? RootLinkName
				: JointTemplate.ChildActorName.TrimStartAndEnd();
			if (!LinksByName.Contains(ParentName))
			{
				OutError = FString::Printf(TEXT("Child '%s' joint parent link '%s' was not found."), *ChildEntry.ActorName, *ParentName);
				return false;
			}
			if (!LinksByName.Contains(ChildName))
			{
				OutError = FString::Printf(TEXT("Child '%s' joint child link '%s' was not found."), *ChildEntry.ActorName, *ChildName);
				return false;
			}

			FCadImportJoint Joint;
			Joint.Name = JointTemplate.JointName.TrimStartAndEnd().IsEmpty()
				? FString::Printf(TEXT("%s_to_%s"), *ParentName, *ChildName)
				: JointTemplate.JointName;
			Joint.Parent = ParentName;
			Joint.Child = ChildName;
			Joint.ComponentName1 = ParentName;
			Joint.ComponentName2 = ChildName;
			Joint.Type = JointTemplate.JointType;
			Joint.Axis = JointTemplate.Axis.GetSafeNormal();
			if (Joint.Axis.IsNearlyZero())
			{
				Joint.Axis = FVector::UpVector;
			}
			Joint.Limit = JointTemplate.Limit;

			if (const FCadImportLink* ChildLink = LinksByName.Find(ChildName))
			{
				Joint.Transform = ChildLink->Transform;
			}
			else
			{
				Joint.Transform = FTransform::Identity;
			}

			if (ChildDocument.ActorType == ECadMasterChildActorType::Movable)
			{
				Joint.Drive.bHasDrive = true;
				Joint.Drive.bEnabled = true;
				Joint.Drive.Mode = ECadImportJointDriveMode::Position;
			}

			OutModel.Joints.Add(MoveTemp(Joint));
		}

		return true;
	}

	FString BuildMasterBlueprintPackagePath(const FCadMasterJsonDocument& MasterDocument, const FCadMasterWorkflowBuildInput& ResolvedBuildInput)
	{
		const FString SafeMasterName = FPaths::MakeValidFileName(MasterDocument.MasterName).IsEmpty()
			? TEXT("CadMaster")
			: FPaths::MakeValidFileName(MasterDocument.MasterName);

		FString ContentRootPath = ResolvedBuildInput.ContentRootPath.TrimStartAndEnd();
		if (ContentRootPath.IsEmpty())
		{
			ContentRootPath = MasterDocument.ContentRootPath.TrimStartAndEnd();
		}
		if (ContentRootPath.IsEmpty())
		{
			ContentRootPath = FString::Printf(TEXT("/Game/%s"), *SafeMasterName);
		}

		return FString::Printf(TEXT("%s/BP_%s_Master"), *ContentRootPath, *SafeMasterName);
	}

	bool TryBuildMasterBlueprintFromDocument(
		const FCadMasterJsonDocument& MasterDocument,
		const FCadMasterWorkflowBuildInput& ResolvedBuildInput,
		UBlueprint*& OutBlueprint,
		FString& OutError)
	{
		OutBlueprint = nullptr;
		OutError.Reset();

		const FString BlueprintPackagePath = BuildMasterBlueprintPackagePath(MasterDocument, ResolvedBuildInput);
		const FString BlueprintAssetName = FPackageName::GetLongPackageAssetName(BlueprintPackagePath);
		if (BlueprintAssetName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Invalid master blueprint package path: %s"), *BlueprintPackagePath);
			return false;
		}

		const FString BlueprintObjectPath = FString::Printf(TEXT("%s.%s"), *BlueprintPackagePath, *BlueprintAssetName);
		UBlueprint* MasterBlueprint = LoadObject<UBlueprint>(nullptr, *BlueprintObjectPath);
		if (!MasterBlueprint)
		{
			UPackage* Package = CreatePackage(*BlueprintPackagePath);
			if (!Package)
			{
				OutError = FString::Printf(TEXT("Failed to create package for master blueprint: %s"), *BlueprintPackagePath);
				return false;
			}

			MasterBlueprint = FKismetEditorUtilities::CreateBlueprint(
				ACadMasterActor::StaticClass(),
				Package,
				FName(*BlueprintAssetName),
				BPTYPE_Normal,
				UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass(),
				FName(TEXT("CadImporter")));
			if (!MasterBlueprint)
			{
				OutError = FString::Printf(TEXT("Failed to create master blueprint asset: %s"), *BlueprintPackagePath);
				return false;
			}
		}
		else if (MasterBlueprint->ParentClass != ACadMasterActor::StaticClass())
		{
			MasterBlueprint->ParentClass = ACadMasterActor::StaticClass();
			FBlueprintEditorUtils::RefreshAllNodes(MasterBlueprint);
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(MasterBlueprint);
		FKismetEditorUtilities::CompileBlueprint(MasterBlueprint);

		if (!MasterBlueprint->GeneratedClass)
		{
			OutError = FString::Printf(TEXT("Master blueprint generated class is invalid: %s"), *BlueprintObjectPath);
			return false;
		}

		ACadMasterActor* MasterDefaultObject = Cast<ACadMasterActor>(MasterBlueprint->GeneratedClass->GetDefaultObject());
		if (!MasterDefaultObject)
		{
			OutError = FString::Printf(TEXT("Master blueprint default object cast failed: %s"), *BlueprintObjectPath);
			return false;
		}

		MasterDefaultObject->Metadata.MasterName = MasterDocument.MasterName;
		MasterDefaultObject->Metadata.WorkspaceFolder = ResolvedBuildInput.WorkspaceFolder;
		MasterDefaultObject->Metadata.Description = FString::Printf(
			TEXT("Generated from master json: %s"),
			*ResolvedBuildInput.MasterJsonPath);
		MasterDefaultObject->Metadata.SchemaVersion = TEXT("master_json_v1");
		MasterDefaultObject->ChildPlacements.Reset();

		for (const FCadMasterChildEntry& ChildEntry : MasterDocument.Children)
		{
			FCadMasterChildPlacement Placement;
			Placement.ChildName = ChildEntry.ActorName;
			Placement.ChildJsonFileName = ChildEntry.ChildJsonFileName;
			Placement.RelativeTransform = ChildEntry.RelativeTransform;
			Placement.bMovable = (ChildEntry.ActorType == ECadMasterChildActorType::Movable);
			MasterDefaultObject->ChildPlacements.Add(MoveTemp(Placement));
		}

		MasterBlueprint->MarkPackageDirty();
		OutBlueprint = MasterBlueprint;
		return true;
	}
}

bool FCadImporterRunner::RunImport(const FString& JsonPath, const FCadFbxImportOptions& ImportOptions) const
{
	FCadImportModel Model;
	FString Error;
	FCadImportJsonParser Parser;
	if (!Parser.ParseFromFile(JsonPath, Model, Error))
	{
		UE_LOG(LogCadImporter, Error, TEXT("CAD json parse failed: %s"), *Error);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("JSON parse failed:\n%s"), *Error)));
		return false;
	}

	return RunImportModel(Model, JsonPath, ImportOptions);
}

bool FCadImporterRunner::RunMasterWorkflowImport(const FCadMasterWorkflowBuildInput& BuildInput, const FCadFbxImportOptions& ImportOptions) const
{
	FString Error;
	const FString MasterJsonPath = BuildInput.MasterJsonPath.TrimStartAndEnd();
	if (MasterJsonPath.IsEmpty())
	{
		UE_LOG(LogCadImporter, Error, TEXT("Master workflow build failed: MasterJsonPath is empty."));
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Master workflow build failed:\nMasterJsonPath is empty.")));
		return false;
	}

	FCadMasterJsonDocument MasterDocument;
	if (!CadMasterChildJsonExtractor::TryParseMasterDocument(MasterJsonPath, MasterDocument, Error))
	{
		UE_LOG(LogCadImporter, Error, TEXT("Master workflow parse failed: %s"), *Error);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Master workflow parse failed:\n%s"), *Error)));
		return false;
	}

	FCadMasterWorkflowBuildInput ResolvedBuildInput = BuildInput;
	ResolvedBuildInput.MasterJsonPath = MasterJsonPath;
	ResolvedBuildInput.WorkspaceFolder = ResolveWorkflowWorkspaceFolder(BuildInput, MasterDocument, MasterJsonPath);
	ResolvedBuildInput.ChildJsonFolderPath = ResolveWorkflowChildJsonFolder(BuildInput, MasterDocument, ResolvedBuildInput.WorkspaceFolder);
	ResolvedBuildInput.ContentRootPath = BuildInput.ContentRootPath.TrimStartAndEnd().IsEmpty()
		? MasterDocument.ContentRootPath
		: BuildInput.ContentRootPath;
	const FString ChildBlueprintOutputRoot = ResolvedBuildInput.ContentRootPath.TrimStartAndEnd();

	UBlueprint* MasterBlueprint = nullptr;
	if (!TryBuildMasterBlueprintFromDocument(MasterDocument, ResolvedBuildInput, MasterBlueprint, Error))
	{
		UE_LOG(LogCadImporter, Error, TEXT("Master blueprint generation failed: %s"), *Error);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Master blueprint generation failed:\n%s"), *Error)));
		return false;
	}

	TMap<FString, UBlueprint*> ChildBlueprintsByChildName;
	for (const FCadMasterChildEntry& ChildEntry : MasterDocument.Children)
	{
		const FString ChildName = ChildEntry.ActorName.TrimStartAndEnd();
		const FString ChildJsonFileName = ChildEntry.ChildJsonFileName.TrimStartAndEnd();
		if (ChildName.IsEmpty())
		{
			UE_LOG(LogCadImporter, Error, TEXT("Master workflow parse failed: child actor_name is empty."));
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Master workflow parse failed:\nChild actor_name is empty.")));
			return false;
		}
		if (ChildJsonFileName.IsEmpty())
		{
			UE_LOG(LogCadImporter, Error, TEXT("Master workflow parse failed: child_json_file_name is empty for child '%s'."), *ChildName);
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(
				TEXT("Master workflow parse failed:\nchild_json_file_name is empty for child '%s'."),
				*ChildName)));
			return false;
		}

		const FString ChildJsonPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(ResolvedBuildInput.ChildJsonFolderPath, ChildJsonFileName));
		FCadChildJsonDocument ChildDocument;
		if (!CadMasterWorkflowImportParser::TryLoadChildDocumentFromJsonPath(ChildJsonPath, ChildDocument, Error))
		{
			UE_LOG(LogCadImporter, Error, TEXT("Failed to load child json for '%s': %s"), *ChildName, *Error);
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(
				TEXT("Failed to load child json for '%s':\n%s"),
				*ChildName,
				*Error)));
			return false;
		}

		if (ChildDocument.ChildActorName.TrimStartAndEnd().IsEmpty())
		{
			ChildDocument.ChildActorName = ChildName;
		}

		FCadImportModel ChildModel;
		if (!TryBuildImportModelFromChildDocument(
			ChildEntry,
			ChildDocument,
			ResolvedBuildInput.ChildJsonFolderPath,
			ChildBlueprintOutputRoot,
			ChildModel,
			Error))
		{
			UE_LOG(LogCadImporter, Error, TEXT("Child model build failed for '%s': %s"), *ChildName, *Error);
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(
				TEXT("Child model build failed for '%s':\n%s"),
				*ChildName,
				*Error)));
			return false;
		}

		UBlueprint* ChildBlueprint = nullptr;
		if (!RunImportModel(ChildModel, ChildJsonPath, ImportOptions, &ChildBlueprint))
		{
			return false;
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

	FCadMasterWorkflowReplaceResult ReplaceResult;
	if (!CadMasterWorkflowLevelReplacer::TryReplaceMasterHierarchyWithBlueprints(
		MasterDocument,
		MasterBlueprint,
		ChildBlueprintsByChildName,
		ReplaceResult,
		Error))
	{
		UE_LOG(LogCadImporter, Error, TEXT("Master workflow level replacement failed: %s"), *Error);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Master workflow level replacement failed:\n%s"), *Error)));
		return false;
	}

	UE_LOG(LogCadImporter, Display, TEXT("Master workflow level replacement succeeded. spawned_master=%s spawned_children=%d deleted=%d"),
		*ReplaceResult.SpawnedActorPath,
		ReplaceResult.SpawnedChildActorCount,
		ReplaceResult.DeletedActorCount);
	return true;
}

bool FCadImporterRunner::RunImportModel(
	const FCadImportModel& Model,
	const FString& SourceLabel,
	const FCadFbxImportOptions& ImportOptions,
	UBlueprint** OutBuiltBlueprint) const
{
	FString Error;
	CadImportDialogUtils::LogModel(Model, SourceLabel);

	const FCadImportPathResolver PathResolver;
	const FCadImportPaths Paths = PathResolver.BuildPaths(Model);

	FCadImportAssetImporter AssetImporter;
	FCadImportResult ImportResult;
	UE_LOG(LogCadImporter, Display, TEXT("FBX Import Effective Options: convert_scene=%s force_front_x_axis=%s convert_scene_unit=%s combine_meshes=%s auto_collision=%s nanite=%s scale=%.4f translation=%s rotation=%s"),
		ImportOptions.bConvertScene ? TEXT("true") : TEXT("false"),
		ImportOptions.bForceFrontXAxis ? TEXT("true") : TEXT("false"),
		ImportOptions.bConvertSceneUnit ? TEXT("true") : TEXT("false"),
		ImportOptions.bCombineMeshes ? TEXT("true") : TEXT("false"),
		ImportOptions.bAutoGenerateCollision ? TEXT("true") : TEXT("false"),
		ImportOptions.bBuildNanite ? TEXT("true") : TEXT("false"),
		ImportOptions.ImportUniformScale,
		*CadImportDialogUtils::FormatVector(ImportOptions.ImportTranslation),
		*CadImportDialogUtils::FormatRotator(ImportOptions.ImportRotation));

	if (!AssetImporter.ImportMeshes(Model, Paths, ImportOptions, ImportResult, Error))
	{
		UE_LOG(LogCadImporter, Error, TEXT("CAD FBX import failed: %s"), *Error);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("FBX import failed:\n%s"), *Error)));
		return false;
	}

	if (ImportResult.ImportedMeshAssetPaths.Num() == 0)
	{
		UE_LOG(LogCadImporter, Warning, TEXT("No static meshes were imported."));
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Import finished, but no static meshes were created.")));
		return false;
	}

	FCadImportAssemblyBuilder AssemblyBuilder;
	UBlueprint* RobotBlueprint = AssemblyBuilder.BuildRobotBlueprint(Model, Paths, ImportResult, Error);
	if (!RobotBlueprint)
	{
		UE_LOG(LogCadImporter, Error, TEXT("CAD assembly build failed: %s"), *Error);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Actor assembly failed:\n%s"), *Error)));
		return false;
	}

	ImportResult.BlueprintAssetPath = RobotBlueprint->GetPathName();
	CadImportDialogUtils::SyncImportedAssetsInContentBrowser(ImportResult);
	UE_LOG(LogCadImporter, Display, TEXT("Imported %d static mesh assets and built actor blueprint: %s"), ImportResult.ImportedMeshAssetPaths.Num(), *ImportResult.BlueprintAssetPath);
	if (OutBuiltBlueprint)
	{
		*OutBuiltBlueprint = RobotBlueprint;
	}
	return true;
}

bool FCadImporterRunner::SelectJsonFile(FString& OutJsonPath) const
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
	const FString Title = TEXT("Select CAD JSON");
	const FString DefaultPath = FPaths::ProjectDir();
	const FString FileTypes = TEXT("JSON Files (*.json)|*.json");
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		const_cast<void*>(ParentWindowHandle),
		Title,
		DefaultPath,
		TEXT(""),
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

bool FCadImporterRunner::SelectOutputJsonFile(FString& OutJsonPath) const
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
	const FString Title = TEXT("Save CAD JSON");
	const FString DefaultPath = FPaths::ProjectDir();
	const FString DefaultFile = TEXT("ActorExport.json");
	const FString FileTypes = TEXT("JSON Files (*.json)|*.json");
	const bool bOpened = DesktopPlatform->SaveFileDialog(
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
