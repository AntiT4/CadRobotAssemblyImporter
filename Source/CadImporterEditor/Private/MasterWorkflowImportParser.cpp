#include "MasterWorkflowImportParser.h"

#include "MasterChildJsonExtractor.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ParseMasterWorkflowTransformArray3(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* FieldName,
		TArray<double>& OutValues,
		FString& OutError)
	{
		OutValues.Reset();
		const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
		if (!Object->TryGetArrayField(FieldName, ArrayValues) || !ArrayValues)
		{
			OutError = FString::Printf(TEXT("Missing transform field: %s"), FieldName);
			return false;
		}

		if (ArrayValues->Num() != 3)
		{
			OutError = FString::Printf(TEXT("Transform field '%s' must contain exactly 3 numbers."), FieldName);
			return false;
		}

		for (int32 Index = 0; Index < 3; ++Index)
		{
			double NumberValue = 0.0;
			if (!(*ArrayValues)[Index].IsValid() || !(*ArrayValues)[Index]->TryGetNumber(NumberValue))
			{
				OutError = FString::Printf(TEXT("Transform field '%s' contains a non-number at index %d."), FieldName, Index);
				return false;
			}
			OutValues.Add(NumberValue);
		}

		return true;
	}

	bool ParseMasterWorkflowTransformObject(const TSharedPtr<FJsonObject>& TransformObject, FTransform& OutTransform, FString& OutError)
	{
		if (!TransformObject.IsValid())
		{
			OutError = TEXT("Transform object is invalid.");
			return false;
		}

		TArray<double> LocationValues;
		TArray<double> RotationValues;
		TArray<double> ScaleValues;
		if (!ParseMasterWorkflowTransformArray3(TransformObject, TEXT("location"), LocationValues, OutError) ||
			!ParseMasterWorkflowTransformArray3(TransformObject, TEXT("rotation"), RotationValues, OutError) ||
			!ParseMasterWorkflowTransformArray3(TransformObject, TEXT("scale"), ScaleValues, OutError))
		{
			return false;
		}

		const FVector Location(LocationValues[0], LocationValues[1], LocationValues[2]);
		const FRotator Rotation(RotationValues[1], RotationValues[2], RotationValues[0]);
		const FVector Scale(ScaleValues[0], ScaleValues[1], ScaleValues[2]);
		OutTransform = FTransform(Rotation, Location, Scale);
		return true;
	}

	bool ParseMasterWorkflowVectorArray3(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* FieldName,
		FVector& OutVector,
		FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* AxisArray = nullptr;
		if (!Object->TryGetArrayField(FieldName, AxisArray) || !AxisArray)
		{
			OutError = FString::Printf(TEXT("Missing vector field: %s"), FieldName);
			return false;
		}

		if (AxisArray->Num() != 3)
		{
			OutError = FString::Printf(TEXT("Vector field '%s' must contain exactly 3 numbers."), FieldName);
			return false;
		}

		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		if (!(*AxisArray)[0]->TryGetNumber(X) ||
			!(*AxisArray)[1]->TryGetNumber(Y) ||
			!(*AxisArray)[2]->TryGetNumber(Z))
		{
			OutError = FString::Printf(TEXT("Vector field '%s' contains non-number values."), FieldName);
			return false;
		}

		OutVector = FVector(X, Y, Z);
		return true;
	}

	bool ParseMasterWorkflowChildActorType(const FString& RawType, ECadMasterChildActorType& OutType, FString& OutError)
	{
		if (RawType.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("actor_type is empty. Set child actor_type to 'static' or 'movable'.");
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

		OutError = FString::Printf(TEXT("Unsupported actor_type '%s'. Expected 'static' or 'movable'."), *RawType);
		return false;
	}

	bool ParseMasterWorkflowJointType(const FString& RawType, ECadImportJointType& OutType, FString& OutError)
	{
		if (RawType.Equals(TEXT("fixed"), ESearchCase::IgnoreCase))
		{
			OutType = ECadImportJointType::Fixed;
			return true;
		}
		if (RawType.Equals(TEXT("revolute"), ESearchCase::IgnoreCase))
		{
			OutType = ECadImportJointType::Revolute;
			return true;
		}
		if (RawType.Equals(TEXT("prismatic"), ESearchCase::IgnoreCase))
		{
			OutType = ECadImportJointType::Prismatic;
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported joint_type '%s'. Expected 'fixed', 'revolute', or 'prismatic'."), *RawType);
		return false;
	}

	FString ResolveMasterWorkflowRootLinkName(const FCadChildJsonDocument& ChildDocument);

	bool TryLoadChildDocumentFromJsonPathInternal(const FString& ChildJsonPath, FCadChildJsonDocument& OutDocument, FString& OutError)
	{
		OutDocument = FCadChildJsonDocument();
		OutError.Reset();

		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *ChildJsonPath))
		{
			OutError = FString::Printf(TEXT("Failed to read child json file: %s"), *ChildJsonPath);
			return false;
		}

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			OutError = FString::Printf(TEXT("Failed to parse child json file: %s"), *ChildJsonPath);
			return false;
		}

		RootObject->TryGetStringField(TEXT("master_name"), OutDocument.MasterName);
		if (!RootObject->TryGetStringField(TEXT("child_actor_name"), OutDocument.ChildActorName))
		{
			OutError = FString::Printf(TEXT("Child json is missing 'child_actor_name': %s"), *ChildJsonPath);
			return false;
		}
		RootObject->TryGetStringField(TEXT("source_actor_path"), OutDocument.SourceActorPath);

		FString RawActorType;
		if (RootObject->TryGetStringField(TEXT("actor_type"), RawActorType))
		{
			if (!ParseMasterWorkflowChildActorType(RawActorType, OutDocument.ActorType, OutError))
			{
				OutError = FString::Printf(TEXT("Child '%s' parse failed: %s"), *OutDocument.ChildActorName, *OutError);
				return false;
			}
		}

		const TSharedPtr<FJsonObject>* RelativeTransformObject = nullptr;
		if (!RootObject->TryGetObjectField(TEXT("relative_transform"), RelativeTransformObject) ||
			!RelativeTransformObject ||
			!RelativeTransformObject->IsValid())
		{
			OutError = FString::Printf(TEXT("Child '%s' is missing 'relative_transform'."), *OutDocument.ChildActorName);
			return false;
		}
		if (!ParseMasterWorkflowTransformObject(*RelativeTransformObject, OutDocument.RelativeTransform, OutError))
		{
			OutError = FString::Printf(TEXT("Child '%s' transform parse failed: %s"), *OutDocument.ChildActorName, *OutError);
			return false;
		}

		const TSharedPtr<FJsonObject>* PhysicsObject = nullptr;
		if (RootObject->TryGetObjectField(TEXT("physics"), PhysicsObject) && PhysicsObject && PhysicsObject->IsValid())
		{
			(*PhysicsObject)->TryGetNumberField(TEXT("mass"), OutDocument.Physics.Mass);
			(*PhysicsObject)->TryGetBoolField(TEXT("simulate_physics"), OutDocument.Physics.bSimulatePhysics);
		}

		const TArray<TSharedPtr<FJsonValue>>* VisualArray = nullptr;
		if (RootObject->TryGetArrayField(TEXT("visuals"), VisualArray) && VisualArray)
		{
			for (int32 VisualIndex = 0; VisualIndex < VisualArray->Num(); ++VisualIndex)
			{
				const TSharedPtr<FJsonObject> VisualObject = (*VisualArray)[VisualIndex].IsValid()
					? (*VisualArray)[VisualIndex]->AsObject()
					: nullptr;
				if (!VisualObject.IsValid())
				{
					OutError = FString::Printf(TEXT("Child '%s' visual[%d] is invalid."), *OutDocument.ChildActorName, VisualIndex);
					return false;
				}

				FCadChildVisualEntry VisualEntry;
				VisualObject->TryGetStringField(TEXT("mesh_path"), VisualEntry.MeshPath);
				VisualObject->TryGetStringField(TEXT("material_path"), VisualEntry.MaterialPath);
				VisualObject->TryGetStringField(TEXT("material_name"), VisualEntry.MaterialName);

				const TSharedPtr<FJsonObject>* VisualTransformObject = nullptr;
				if (VisualObject->TryGetObjectField(TEXT("relative_transform"), VisualTransformObject) &&
					VisualTransformObject &&
					VisualTransformObject->IsValid())
				{
					if (!ParseMasterWorkflowTransformObject(*VisualTransformObject, VisualEntry.RelativeTransform, OutError))
					{
						OutError = FString::Printf(TEXT("Child '%s' visual[%d] transform parse failed: %s"), *OutDocument.ChildActorName, VisualIndex, *OutError);
						return false;
					}
				}
				else
				{
					VisualEntry.RelativeTransform = FTransform::Identity;
				}

				OutDocument.Visuals.Add(MoveTemp(VisualEntry));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* LinkArray = nullptr;
		if (RootObject->TryGetArrayField(TEXT("links"), LinkArray) && LinkArray)
		{
			for (int32 LinkIndex = 0; LinkIndex < LinkArray->Num(); ++LinkIndex)
			{
				const TSharedPtr<FJsonObject> LinkObject = (*LinkArray)[LinkIndex].IsValid()
					? (*LinkArray)[LinkIndex]->AsObject()
					: nullptr;
				if (!LinkObject.IsValid())
				{
					OutError = FString::Printf(TEXT("Child '%s' link[%d] is invalid."), *OutDocument.ChildActorName, LinkIndex);
					return false;
				}

				FCadChildLinkTemplate LinkTemplate;
				LinkObject->TryGetStringField(TEXT("link_name"), LinkTemplate.LinkName);

				const TSharedPtr<FJsonObject>* LinkTransformObject = nullptr;
				if (LinkObject->TryGetObjectField(TEXT("relative_transform"), LinkTransformObject) &&
					LinkTransformObject &&
					LinkTransformObject->IsValid())
				{
					if (!ParseMasterWorkflowTransformObject(*LinkTransformObject, LinkTemplate.RelativeTransform, OutError))
					{
						OutError = FString::Printf(TEXT("Child '%s' link[%d] transform parse failed: %s"), *OutDocument.ChildActorName, LinkIndex, *OutError);
						return false;
					}
				}
				else
				{
					LinkTemplate.RelativeTransform = FTransform::Identity;
				}

				const TArray<TSharedPtr<FJsonValue>>* LinkVisualArray = nullptr;
				if (LinkObject->TryGetArrayField(TEXT("visuals"), LinkVisualArray) && LinkVisualArray)
				{
					for (int32 VisualIndex = 0; VisualIndex < LinkVisualArray->Num(); ++VisualIndex)
					{
						const TSharedPtr<FJsonObject> VisualObject = (*LinkVisualArray)[VisualIndex].IsValid()
							? (*LinkVisualArray)[VisualIndex]->AsObject()
							: nullptr;
						if (!VisualObject.IsValid())
						{
							OutError = FString::Printf(TEXT("Child '%s' link[%d] visual[%d] is invalid."), *OutDocument.ChildActorName, LinkIndex, VisualIndex);
							return false;
						}

						FCadChildVisualEntry VisualEntry;
						VisualObject->TryGetStringField(TEXT("mesh_path"), VisualEntry.MeshPath);
						VisualObject->TryGetStringField(TEXT("material_path"), VisualEntry.MaterialPath);
						VisualObject->TryGetStringField(TEXT("material_name"), VisualEntry.MaterialName);

						const TSharedPtr<FJsonObject>* VisualTransformObject = nullptr;
						if (VisualObject->TryGetObjectField(TEXT("relative_transform"), VisualTransformObject) &&
							VisualTransformObject &&
							VisualTransformObject->IsValid())
						{
							if (!ParseMasterWorkflowTransformObject(*VisualTransformObject, VisualEntry.RelativeTransform, OutError))
							{
								OutError = FString::Printf(
									TEXT("Child '%s' link[%d] visual[%d] transform parse failed: %s"),
									*OutDocument.ChildActorName,
									LinkIndex,
									VisualIndex,
									*OutError);
								return false;
							}
						}
						else
						{
							VisualEntry.RelativeTransform = FTransform::Identity;
						}

						LinkTemplate.Visuals.Add(MoveTemp(VisualEntry));
					}
				}

				OutDocument.Links.Add(MoveTemp(LinkTemplate));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* JointArray = nullptr;
		if (RootObject->TryGetArrayField(TEXT("joints"), JointArray) && JointArray)
		{
			for (int32 JointIndex = 0; JointIndex < JointArray->Num(); ++JointIndex)
			{
				const TSharedPtr<FJsonObject> JointObject = (*JointArray)[JointIndex].IsValid()
					? (*JointArray)[JointIndex]->AsObject()
					: nullptr;
				if (!JointObject.IsValid())
				{
					OutError = FString::Printf(TEXT("Child '%s' joint[%d] is invalid."), *OutDocument.ChildActorName, JointIndex);
					return false;
				}

				FCadChildJointTemplate JointTemplate;
				JointObject->TryGetStringField(TEXT("joint_name"), JointTemplate.JointName);
				JointObject->TryGetStringField(TEXT("parent_actor_name"), JointTemplate.ParentActorName);
				JointObject->TryGetStringField(TEXT("child_actor_name"), JointTemplate.ChildActorName);

				FString JointTypeString;
				if (JointObject->TryGetStringField(TEXT("joint_type"), JointTypeString))
				{
					if (!ParseMasterWorkflowJointType(JointTypeString, JointTemplate.JointType, OutError))
					{
						OutError = FString::Printf(TEXT("Child '%s' joint[%d] parse failed: %s"), *OutDocument.ChildActorName, JointIndex, *OutError);
						return false;
					}
				}

				FVector Axis = FVector::UpVector;
				FString AxisParseError;
				if (JointObject->HasField(TEXT("axis")) && !ParseMasterWorkflowVectorArray3(JointObject, TEXT("axis"), Axis, AxisParseError))
				{
					OutError = FString::Printf(TEXT("Child '%s' joint[%d] axis parse failed: %s"), *OutDocument.ChildActorName, JointIndex, *AxisParseError);
					return false;
				}
				JointTemplate.Axis = Axis;

				const TSharedPtr<FJsonObject>* LimitObject = nullptr;
				if (JointObject->TryGetObjectField(TEXT("limit"), LimitObject) && LimitObject && LimitObject->IsValid())
				{
					JointTemplate.Limit.bHasLimit = true;
					(*LimitObject)->TryGetNumberField(TEXT("lower"), JointTemplate.Limit.Lower);
					(*LimitObject)->TryGetNumberField(TEXT("upper"), JointTemplate.Limit.Upper);
					(*LimitObject)->TryGetNumberField(TEXT("effort"), JointTemplate.Limit.Effort);
					(*LimitObject)->TryGetNumberField(TEXT("velocity"), JointTemplate.Limit.Velocity);
				}

				if (JointTemplate.JointName.TrimStartAndEnd().IsEmpty())
				{
					const FString RootLinkName = ResolveMasterWorkflowRootLinkName(OutDocument);
					const FString ParentName = JointTemplate.ParentActorName.TrimStartAndEnd().IsEmpty()
						? TEXT("master")
						: JointTemplate.ParentActorName;
					const FString ChildName = JointTemplate.ChildActorName.TrimStartAndEnd().IsEmpty()
						? RootLinkName
						: JointTemplate.ChildActorName;
					JointTemplate.JointName = FString::Printf(TEXT("%s_to_%s"), *ParentName, *ChildName);
				}

				OutDocument.Joints.Add(MoveTemp(JointTemplate));
			}
		}

		return true;
	}

	FString ResolveWorkspaceFolderForBuildInput(const FCadMasterWorkflowBuildInput& BuildInput, const FCadMasterJsonDocument& MasterDocument, const FString& MasterJsonPath)
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

	FString ResolveChildFolderForBuildInput(const FCadMasterWorkflowBuildInput& BuildInput, const FCadMasterJsonDocument& MasterDocument, const FString& WorkspaceFolder)
	{
		const FString ExplicitChildFolder = BuildInput.ChildJsonFolderPath.TrimStartAndEnd();
		if (!ExplicitChildFolder.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(ExplicitChildFolder);
		}

		const FString ChildFolderName = MasterDocument.ChildJsonFolderName.TrimStartAndEnd();
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(WorkspaceFolder, ChildFolderName));
	}

	void FillModelUnitsDefaults(FCadImportModel& InOutModel)
	{
		InOutModel.Units.Length = TEXT("centimeter");
		InOutModel.Units.Angle = TEXT("degree");
		InOutModel.Units.UpAxis = TEXT("z");
		InOutModel.Units.FrontAxis = TEXT("x");
		InOutModel.Units.Handedness = TEXT("left");
		InOutModel.Units.EulerOrder = TEXT("xyz");
		InOutModel.Units.MeshScale = 1.0f;
	}

	bool EnsureMasterWorkflowLinkExists(const TMap<FString, FCadImportLink>& LinksByName, const FString& LinkName)
	{
		return !LinkName.TrimStartAndEnd().IsEmpty() && LinksByName.Contains(LinkName);
	}

	FString ResolveMasterWorkflowRootLinkName(const FCadChildJsonDocument& ChildDocument)
	{
		for (const FCadChildLinkTemplate& LinkTemplate : ChildDocument.Links)
		{
			const FString LinkName = LinkTemplate.LinkName.TrimStartAndEnd();
			if (!LinkName.IsEmpty())
			{
				return LinkName;
			}
		}

		return ChildDocument.ChildActorName;
	}

	void AppendMasterWorkflowVisualsToImportLink(
		const TArray<FCadChildVisualEntry>& ChildVisuals,
		FCadImportLink& OutLink)
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

	bool HasMasterWorkflowRootAnchorJoint(
		const FCadChildJsonDocument& ChildDocument,
		const FString& MasterRootLinkName,
		const FString& ChildRootLinkName)
	{
		for (const FCadChildJointTemplate& JointTemplate : ChildDocument.Joints)
		{
			const FString ParentName = JointTemplate.ParentActorName.TrimStartAndEnd().IsEmpty()
				? MasterRootLinkName
				: JointTemplate.ParentActorName;
			const FString ChildName = JointTemplate.ChildActorName.TrimStartAndEnd().IsEmpty()
				? ChildRootLinkName
				: JointTemplate.ChildActorName;
			if (ParentName == MasterRootLinkName && ChildName == ChildRootLinkName)
			{
				return true;
			}
		}

		return false;
	}
}

namespace CadMasterWorkflowImportParser
{
	bool TryLoadChildDocumentFromJsonPath(const FString& ChildJsonPath, FCadChildJsonDocument& OutDocument, FString& OutError)
	{
		return TryLoadChildDocumentFromJsonPathInternal(ChildJsonPath, OutDocument, OutError);
	}

	bool TryBuildImportModel(
		const FCadMasterWorkflowBuildInput& BuildInput,
		FCadMasterWorkflowImportParseResult& OutResult,
		FString& OutError)
	{
		OutResult = FCadMasterWorkflowImportParseResult();
		OutError.Reset();

		const FString MasterJsonPath = BuildInput.MasterJsonPath.TrimStartAndEnd();
		if (MasterJsonPath.IsEmpty())
		{
			OutError = TEXT("Master workflow build input is missing MasterJsonPath.");
			return false;
		}

		FCadMasterJsonDocument MasterDocument;
		if (!CadMasterChildJsonExtractor::TryParseMasterDocument(MasterJsonPath, MasterDocument, OutError))
		{
			return false;
		}

		const FString WorkspaceFolder = ResolveWorkspaceFolderForBuildInput(BuildInput, MasterDocument, MasterJsonPath);
		const FString ChildJsonFolderPath = ResolveChildFolderForBuildInput(BuildInput, MasterDocument, WorkspaceFolder);

		TArray<FCadChildJsonDocument> ChildDocuments;
		ChildDocuments.Reserve(MasterDocument.Children.Num());
		for (const FCadMasterChildEntry& ChildEntry : MasterDocument.Children)
		{
			const FString ChildFileName = ChildEntry.ChildJsonFileName.TrimStartAndEnd();
			if (ChildFileName.IsEmpty())
			{
				OutError = FString::Printf(TEXT("Master child '%s' is missing child_json_file_name."), *ChildEntry.ActorName);
				return false;
			}

			const FString ChildJsonPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(ChildJsonFolderPath, ChildFileName));
			FCadChildJsonDocument ChildDocument;
			if (!TryLoadChildDocumentFromJsonPathInternal(ChildJsonPath, ChildDocument, OutError))
			{
				OutError = FString::Printf(TEXT("Failed to load child json for '%s': %s"), *ChildEntry.ActorName, *OutError);
				return false;
			}

			if (ChildDocument.ChildActorName.TrimStartAndEnd().IsEmpty())
			{
				ChildDocument.ChildActorName = ChildEntry.ActorName;
			}

			ChildDocuments.Add(MoveTemp(ChildDocument));
		}

		FCadImportModel Model;
		Model.RobotName = MasterDocument.MasterName;
		Model.RootLinkName = MasterDocument.MasterName;
		Model.SourceDirectory = ChildJsonFolderPath;
		Model.RootPlacement.bHasWorldTransform = true;
		Model.RootPlacement.WorldTransform = MasterDocument.MasterWorldTransform;
		FillModelUnitsDefaults(Model);

		FCadImportLink RootLink;
		RootLink.Name = Model.RootLinkName;
		RootLink.Transform = MasterDocument.MasterWorldTransform;
		RootLink.Physics.Mass = 0.0f;
		RootLink.Physics.bSimulatePhysics = false;
		Model.Links.Add(RootLink);

		bool bHasMovableChild = false;
		for (const FCadChildJsonDocument& ChildDocument : ChildDocuments)
		{
			bHasMovableChild = bHasMovableChild || (ChildDocument.ActorType == ECadMasterChildActorType::Movable);

			if (ChildDocument.Links.Num() > 0)
			{
				for (const FCadChildLinkTemplate& LinkTemplate : ChildDocument.Links)
				{
					const FString LinkName = LinkTemplate.LinkName.TrimStartAndEnd();
					if (LinkName.IsEmpty())
					{
						OutError = FString::Printf(TEXT("Child '%s' has a link with empty link_name."), *ChildDocument.ChildActorName);
						return false;
					}

					FCadImportLink ChildLink;
					ChildLink.Name = LinkName;
					ChildLink.Transform = LinkTemplate.RelativeTransform;
					ChildLink.Physics = ChildDocument.Physics;
					AppendMasterWorkflowVisualsToImportLink(LinkTemplate.Visuals, ChildLink);
					Model.Links.Add(MoveTemp(ChildLink));
				}
			}
			else
			{
				FCadImportLink ChildLink;
				ChildLink.Name = ChildDocument.ChildActorName;
				ChildLink.Transform = ChildDocument.RelativeTransform;
				ChildLink.Physics = ChildDocument.Physics;
				AppendMasterWorkflowVisualsToImportLink(ChildDocument.Visuals, ChildLink);
				Model.Links.Add(MoveTemp(ChildLink));
			}
		}
		Model.Profile = bHasMovableChild ? ECadImportModelProfile::DynamicRobot : ECadImportModelProfile::FixedAssembly;

		TMap<FString, FCadImportLink> LinksByName;
		for (const FCadImportLink& Link : Model.Links)
		{
			if (Link.Name.TrimStartAndEnd().IsEmpty())
			{
				OutError = TEXT("Merged link contains an empty name.");
				return false;
			}
			if (LinksByName.Contains(Link.Name))
			{
				OutError = FString::Printf(TEXT("Duplicate link name '%s' was found while merging child json files."), *Link.Name);
				return false;
			}

			LinksByName.Add(Link.Name, Link);
		}

		for (const FCadChildJsonDocument& ChildDocument : ChildDocuments)
		{
			const FString DefaultParentName = Model.RootLinkName;
			const FString DefaultChildName = ResolveMasterWorkflowRootLinkName(ChildDocument);
			if (!EnsureMasterWorkflowLinkExists(LinksByName, DefaultChildName))
			{
				OutError = FString::Printf(TEXT("Child '%s' root link '%s' was not found in merged links."),
					*ChildDocument.ChildActorName,
					*DefaultChildName);
				return false;
			}

			if (!HasMasterWorkflowRootAnchorJoint(ChildDocument, DefaultParentName, DefaultChildName))
			{
				FCadImportJoint AnchorJoint;
				AnchorJoint.Name = FString::Printf(TEXT("%s_to_%s"), *DefaultParentName, *DefaultChildName);
				AnchorJoint.Parent = DefaultParentName;
				AnchorJoint.Child = DefaultChildName;
				AnchorJoint.ComponentName1 = DefaultParentName;
				AnchorJoint.ComponentName2 = DefaultChildName;
				AnchorJoint.Type = ECadImportJointType::Fixed;
				AnchorJoint.Axis = FVector::UpVector;
				if (const FCadImportLink* RootChildLink = LinksByName.Find(DefaultChildName))
				{
					AnchorJoint.Transform = RootChildLink->Transform;
				}
				else
				{
					AnchorJoint.Transform = ChildDocument.RelativeTransform;
				}
				Model.Joints.Add(MoveTemp(AnchorJoint));
			}

			for (const FCadChildJointTemplate& JointTemplate : ChildDocument.Joints)
			{
				const FString ParentName = JointTemplate.ParentActorName.TrimStartAndEnd().IsEmpty()
					? DefaultParentName
					: JointTemplate.ParentActorName;
				const FString ChildName = JointTemplate.ChildActorName.TrimStartAndEnd().IsEmpty()
					? DefaultChildName
					: JointTemplate.ChildActorName;

				if (!EnsureMasterWorkflowLinkExists(LinksByName, ParentName))
				{
					OutError = FString::Printf(TEXT("Joint parent link '%s' was not found in merged links."), *ParentName);
					return false;
				}

				if (!EnsureMasterWorkflowLinkExists(LinksByName, ChildName))
				{
					OutError = FString::Printf(TEXT("Joint child link '%s' was not found in merged links."), *ChildName);
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

				Model.Joints.Add(MoveTemp(Joint));
			}
		}

		OutResult.BuildInput = BuildInput;
		OutResult.BuildInput.WorkspaceFolder = WorkspaceFolder;
		OutResult.BuildInput.MasterJsonPath = MasterJsonPath;
		OutResult.BuildInput.ChildJsonFolderPath = ChildJsonFolderPath;
		OutResult.BuildInput.ContentRootPath = BuildInput.ContentRootPath.TrimStartAndEnd().IsEmpty()
			? MasterDocument.ContentRootPath
			: BuildInput.ContentRootPath;
		OutResult.MasterDocument = MasterDocument;
		OutResult.ChildDocuments = MoveTemp(ChildDocuments);
		OutResult.Model = MoveTemp(Model);
		return true;
	}
}
