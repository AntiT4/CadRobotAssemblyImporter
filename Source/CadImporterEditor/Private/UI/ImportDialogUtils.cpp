#include "UI/ImportDialogUtils.h"

#include "Import/AssetImportUtils.h"
#include "Import/ImportModelParser.h"
#include "CadImporterEditor.h"
#include "Editor.h"
#include "Misc/DefaultValueHelper.h"

namespace
{
	void AppendIndentedLinkLine(const FString& LinkName, int32 Depth, FString& OutText)
	{
		OutText += FString::Printf(TEXT("%s- %s\n"), *FString::ChrN(Depth * 2, TEXT(' ')), *LinkName);
	}

	void BuildLinkTreeRecursive(
		const FString& LinkName,
		const TMultiMap<FString, FString>& ChildrenByParent,
		TSet<FString>& InOutVisited,
		int32 Depth,
		FString& OutText)
	{
		AppendIndentedLinkLine(LinkName, Depth, OutText);
		InOutVisited.Add(LinkName);

		TArray<FString> Children;
		ChildrenByParent.MultiFind(LinkName, Children);
		Children.Sort();

		for (const FString& ChildName : Children)
		{
			if (InOutVisited.Contains(ChildName))
			{
				OutText += FString::Printf(TEXT("%s- %s (cycle)\n"), *FString::ChrN((Depth + 1) * 2, TEXT(' ')), *ChildName);
				continue;
			}

			BuildLinkTreeRecursive(ChildName, ChildrenByParent, InOutVisited, Depth + 1, OutText);
		}
	}
}

namespace CadImportDialogUtils
{
	FString ProfileToString(ECadImportModelProfile Profile)
	{
		switch (Profile)
		{
		case ECadImportModelProfile::FixedAssembly:
			return TEXT("fixed_assembly");
		case ECadImportModelProfile::DynamicRobot:
		default:
			return TEXT("dynamic_robot");
		}
	}

	FString JointDriveModeToString(ECadImportJointDriveMode Mode)
	{
		switch (Mode)
		{
		case ECadImportJointDriveMode::None:
			return TEXT("none");
		case ECadImportJointDriveMode::Velocity:
			return TEXT("velocity");
		case ECadImportJointDriveMode::Position:
		default:
			return TEXT("position");
		}
	}

	FString JointTypeToString(ECadImportJointType Type)
	{
		switch (Type)
		{
		case ECadImportJointType::Fixed:
			return TEXT("fixed");
		case ECadImportJointType::Revolute:
			return TEXT("revolute");
		case ECadImportJointType::Prismatic:
			return TEXT("prismatic");
		default:
			return TEXT("unknown");
		}
	}

	FString FormatVector(const FVector& Value)
	{
		return FString::Printf(TEXT("(%.3f, %.3f, %.3f)"), Value.X, Value.Y, Value.Z);
	}

	FString FormatRotator(const FRotator& Value)
	{
		return FString::Printf(TEXT("(Pitch=%.3f, Yaw=%.3f, Roll=%.3f)"), Value.Pitch, Value.Yaw, Value.Roll);
	}

	FString FormatColor(const FLinearColor& Value)
	{
		return FString::Printf(TEXT("(%.3f, %.3f, %.3f, %.3f)"), Value.R, Value.G, Value.B, Value.A);
	}

	FString BuildLinkStructurePreview(const FCadImportModel& Model)
	{
		FString Preview = FString::Printf(
			TEXT("Robot: %s\nRoot: %s\nLinks: %d, Joints: %d\n\n"),
			Model.RobotName.IsEmpty() ? TEXT("(unknown)") : *Model.RobotName,
			Model.RootLinkName.IsEmpty() ? TEXT("(unknown)") : *Model.RootLinkName,
			Model.Links.Num(),
			Model.Joints.Num());

		if (Model.Links.Num() == 0)
		{
			Preview += TEXT("(No links found)");
			return Preview;
		}

		TArray<FString> AllLinkNames;
		AllLinkNames.Reserve(Model.Links.Num());
		for (const FCadImportLink& Link : Model.Links)
		{
			AllLinkNames.Add(Link.Name);
		}
		AllLinkNames.Sort();

		TMultiMap<FString, FString> ChildrenByParent;
		TSet<FString> ChildNames;
		for (const FCadImportJoint& Joint : Model.Joints)
		{
			if (!Joint.Parent.IsEmpty() && !Joint.Child.IsEmpty())
			{
				ChildrenByParent.Add(Joint.Parent, Joint.Child);
				ChildNames.Add(Joint.Child);
			}
		}

		FString RootName = Model.RootLinkName;
		if (RootName.IsEmpty() || !AllLinkNames.Contains(RootName))
		{
			for (const FString& LinkName : AllLinkNames)
			{
				if (!ChildNames.Contains(LinkName))
				{
					RootName = LinkName;
					break;
				}
			}
		}
		if (RootName.IsEmpty())
		{
			RootName = AllLinkNames[0];
		}

		TSet<FString> Visited;
		BuildLinkTreeRecursive(RootName, ChildrenByParent, Visited, 0, Preview);

		TArray<FString> DisconnectedLinks;
		for (const FString& LinkName : AllLinkNames)
		{
			if (!Visited.Contains(LinkName))
			{
				DisconnectedLinks.Add(LinkName);
			}
		}

		if (DisconnectedLinks.Num() > 0)
		{
			Preview += TEXT("\nDisconnected Links:\n");
			for (const FString& LinkName : DisconnectedLinks)
			{
				AppendIndentedLinkLine(LinkName, 0, Preview);
			}
		}

		return Preview;
	}

	void FillImportOptionTextFields(
		const FCadFbxImportOptions& Options,
		FString& OutScaleText,
		FString& OutTxText,
		FString& OutTyText,
		FString& OutTzText,
		FString& OutPitchText,
		FString& OutYawText,
		FString& OutRollText)
	{
		OutScaleText = FString::Printf(TEXT("%.6g"), Options.ImportUniformScale);
		OutTxText = FString::Printf(TEXT("%.6g"), Options.ImportTranslation.X);
		OutTyText = FString::Printf(TEXT("%.6g"), Options.ImportTranslation.Y);
		OutTzText = FString::Printf(TEXT("%.6g"), Options.ImportTranslation.Z);
		OutPitchText = FString::Printf(TEXT("%.6g"), Options.ImportRotation.Pitch);
		OutYawText = FString::Printf(TEXT("%.6g"), Options.ImportRotation.Yaw);
		OutRollText = FString::Printf(TEXT("%.6g"), Options.ImportRotation.Roll);
	}

	bool TryParseImportOptionTextFields(
		const FString& InScaleText,
		const FString& InTxText,
		const FString& InTyText,
		const FString& InTzText,
		const FString& InPitchText,
		const FString& InYawText,
		const FString& InRollText,
		FCadFbxImportOptions& InOutOptions,
		FString& OutError)
	{
		float ScaleValue = InOutOptions.ImportUniformScale;
		float Tx = InOutOptions.ImportTranslation.X;
		float Ty = InOutOptions.ImportTranslation.Y;
		float Tz = InOutOptions.ImportTranslation.Z;
		float Pitch = InOutOptions.ImportRotation.Pitch;
		float Yaw = InOutOptions.ImportRotation.Yaw;
		float Roll = InOutOptions.ImportRotation.Roll;

		if (!FDefaultValueHelper::ParseFloat(InScaleText, ScaleValue) || ScaleValue <= 0.0f)
		{
			OutError = TEXT("Import Uniform Scale must be a valid number greater than 0.");
			return false;
		}

		if (!FDefaultValueHelper::ParseFloat(InTxText, Tx)
			|| !FDefaultValueHelper::ParseFloat(InTyText, Ty)
			|| !FDefaultValueHelper::ParseFloat(InTzText, Tz))
		{
			OutError = TEXT("Import Translation must contain valid numeric values.");
			return false;
		}

		if (!FDefaultValueHelper::ParseFloat(InPitchText, Pitch)
			|| !FDefaultValueHelper::ParseFloat(InYawText, Yaw)
			|| !FDefaultValueHelper::ParseFloat(InRollText, Roll))
		{
			OutError = TEXT("Import Rotation must contain valid numeric values.");
			return false;
		}

		InOutOptions.ImportUniformScale = ScaleValue;
		InOutOptions.ImportTranslation = FVector(Tx, Ty, Tz);
		InOutOptions.ImportRotation = FRotator(Pitch, Yaw, Roll);
		return true;
	}

	bool TryBuildPreviewFromJson(
		const FString& JsonPath,
		FString& OutPreview,
		FString& OutError)
	{
		FCadImportModel Model;
		FCadJsonParser Parser;
		if (!Parser.ParseFromFile(JsonPath, Model, OutError))
		{
			return false;
		}

		OutPreview = BuildLinkStructurePreview(Model);
		return true;
	}

	void SyncImportedAssetsInContentBrowser(const FCadImportResult& ImportResult)
	{
		if (!GEditor)
		{
			return;
		}

		TSet<FString> UniqueImportedPaths;
		for (const FString& ImportedPath : ImportResult.ImportedMeshAssetPaths)
		{
			if (!ImportedPath.IsEmpty())
			{
				UniqueImportedPaths.Add(ImportedPath);
			}
		}

		if (UniqueImportedPaths.Num() == 0)
		{
			for (const TPair<FString, TArray<FString>>& Pair : ImportResult.MeshAssetsByLink)
			{
				for (const FString& MeshPath : Pair.Value)
				{
					if (!MeshPath.IsEmpty())
					{
						UniqueImportedPaths.Add(MeshPath);
					}
				}
			}
		}

		TArray<UObject*> ImportedAssets;
		ImportedAssets.Reserve(UniqueImportedPaths.Num());
		for (const FString& PackagePath : UniqueImportedPaths)
		{
			const FString ObjectPath = CadAssetImportUtils::PackagePathToObjectPath(PackagePath);
			if (UObject* ImportedAsset = LoadObject<UObject>(nullptr, *ObjectPath))
			{
				ImportedAssets.Add(ImportedAsset);
			}
		}

		if (!ImportResult.BlueprintAssetPath.IsEmpty())
		{
			if (UObject* BlueprintAsset = LoadObject<UObject>(nullptr, *ImportResult.BlueprintAssetPath))
			{
				ImportedAssets.AddUnique(BlueprintAsset);
			}
		}

		if (ImportedAssets.Num() > 0)
		{
			GEditor->SyncBrowserToObjects(ImportedAssets);
		}
	}

	void LogModel(const FCadImportModel& Model, const FString& JsonPath)
	{
		UE_LOG(LogCadImporter, Display, TEXT("CAD JSON: %s"), *JsonPath);
		UE_LOG(LogCadImporter, Display, TEXT("Robot=%s Profile=%s Root=%s Units(length=%s, angle=%s) Links=%d Joints=%d"),
			*Model.RobotName,
			*ProfileToString(Model.Profile),
			*Model.RootLinkName,
			*Model.Units.Length,
			*Model.Units.Angle,
			Model.Links.Num(),
			Model.Joints.Num());
		if (Model.RootPlacement.bHasWorldTransform || !Model.RootPlacement.ParentActorName.IsEmpty())
		{
			UE_LOG(LogCadImporter, Display, TEXT("RootPlacement has_world=%s world_location=%s world_rotation=%s world_scale=%s parent_actor=%s"),
				Model.RootPlacement.bHasWorldTransform ? TEXT("true") : TEXT("false"),
				*FormatVector(Model.RootPlacement.WorldTransform.GetLocation()),
				*FormatRotator(Model.RootPlacement.WorldTransform.Rotator()),
				*FormatVector(Model.RootPlacement.WorldTransform.GetScale3D()),
				*Model.RootPlacement.ParentActorName);
		}

		for (const FCadImportLink& Link : Model.Links)
		{
			const FVector Location = Link.Transform.GetLocation();
			const FRotator Rotation = Link.Transform.GetRotation().Rotator();
			UE_LOG(LogCadImporter, Display, TEXT("Link %s visuals=%d mass=%.3f simulate=%s location=%s rotation=%s"),
				*Link.Name,
				Link.Visuals.Num(),
				Link.Physics.Mass,
				Link.Physics.bSimulatePhysics ? TEXT("true") : TEXT("false"),
				*FormatVector(Location),
				*FormatRotator(Rotation));

			for (const FCadImportVisual& Visual : Link.Visuals)
			{
				const FString ColorText = Visual.bHasColor ? FormatColor(Visual.Color) : TEXT("none");
				UE_LOG(LogCadImporter, Display, TEXT("  Visual mesh=%s material_path=%s material_name=%s location=%s rotation=%s color=%s"),
					*Visual.MeshPath,
					*Visual.MaterialPath,
					*Visual.MaterialName,
					*FormatVector(Visual.Transform.GetLocation()),
					*FormatRotator(Visual.Transform.GetRotation().Rotator()),
					*ColorText);
			}
		}

		for (const FCadImportJoint& Joint : Model.Joints)
		{
			const FVector Location = Joint.Transform.GetLocation();
			const FRotator Rotation = Joint.Transform.GetRotation().Rotator();
			const FString DriveSourceText = Joint.Drive.bHasDrive ? TEXT("json") : TEXT("profile_default");
			UE_LOG(LogCadImporter, Display, TEXT("Joint %s parent=%s child=%s type=%s axis=%s drive_enabled=%s drive_mode=%s drive_source=%s location=%s rotation=%s"),
				*Joint.Name,
				*Joint.Parent,
				*Joint.Child,
				*JointTypeToString(Joint.Type),
				*FormatVector(Joint.Axis),
				Joint.Drive.bEnabled ? TEXT("true") : TEXT("false"),
				*JointDriveModeToString(Joint.Drive.Mode),
				*DriveSourceText,
				*FormatVector(Location),
				*FormatRotator(Rotation));

			if (Joint.Limit.bHasLimit)
			{
				UE_LOG(LogCadImporter, Display, TEXT("  Limit lower=%.3f upper=%.3f effort=%.3f velocity=%.3f"),
					Joint.Limit.Lower,
					Joint.Limit.Upper,
					Joint.Limit.Effort,
					Joint.Limit.Velocity);
			}
		}
	}
}
