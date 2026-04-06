#include "ChildDocParser.h"

#include "CadImportStringUtils.h"
#include "Dom/JsonObject.h"
#include "Json/CadJsonTransformUtils.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ParseChildActorType(const FString& RawType, ECadMasterChildActorType& OutType, FString& OutError)
	{
		if (RawType.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("actor_type is empty. Set child actor_type to 'static', 'background', or 'movable'.");
			return false;
		}

		if (CadImportStringUtils::TryParseMasterChildActorTypeString(RawType, OutType, false))
		{
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported actor_type '%s'. Expected 'static', 'background', or 'movable'."), *RawType);
		return false;
	}

	bool ParseJointType(const FString& RawType, ECadImportJointType& OutType, FString& OutError)
	{
		if (CadImportStringUtils::TryParseJointTypeString(RawType, OutType))
		{
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported joint_type '%s'. Expected 'fixed', 'revolute', or 'prismatic'."), *RawType);
		return false;
	}

	FString ResolveRootLinkName(const FCadChildDoc& ChildDocument)
	{
		for (const FCadChildLinkDef& LinkTemplate : ChildDocument.Links)
		{
			const FString LinkName = LinkTemplate.LinkName.TrimStartAndEnd();
			if (!LinkName.IsEmpty())
			{
				return LinkName;
			}
		}

		return ChildDocument.ChildActorName;
	}
}

namespace CadChildDocParser
{
	bool TryLoadChildDocumentFromJsonPath(
		const FString& ChildJsonPath,
		FCadChildDoc& OutDocument,
		FString& OutError)
	{
		OutDocument = FCadChildDoc();
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
			if (!ParseChildActorType(RawActorType, OutDocument.ActorType, OutError))
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
		if (!CadJsonTransformUtils::ParseTransformObject(*RelativeTransformObject, OutDocument.RelativeTransform, OutError))
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

				FCadChildVisual VisualEntry;
				VisualObject->TryGetStringField(TEXT("mesh_path"), VisualEntry.MeshPath);
				VisualObject->TryGetStringField(TEXT("material_path"), VisualEntry.MaterialPath);
				VisualObject->TryGetStringField(TEXT("material_name"), VisualEntry.MaterialName);

				const TSharedPtr<FJsonObject>* VisualTransformObject = nullptr;
				if (VisualObject->TryGetObjectField(TEXT("relative_transform"), VisualTransformObject) &&
					VisualTransformObject &&
					VisualTransformObject->IsValid())
				{
					if (!CadJsonTransformUtils::ParseTransformObject(*VisualTransformObject, VisualEntry.RelativeTransform, OutError))
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

				FCadChildLinkDef LinkTemplate;
				LinkObject->TryGetStringField(TEXT("link_name"), LinkTemplate.LinkName);

				const TSharedPtr<FJsonObject>* LinkTransformObject = nullptr;
				if (LinkObject->TryGetObjectField(TEXT("relative_transform"), LinkTransformObject) &&
					LinkTransformObject &&
					LinkTransformObject->IsValid())
				{
					if (!CadJsonTransformUtils::ParseTransformObject(*LinkTransformObject, LinkTemplate.RelativeTransform, OutError))
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

						FCadChildVisual VisualEntry;
						VisualObject->TryGetStringField(TEXT("mesh_path"), VisualEntry.MeshPath);
						VisualObject->TryGetStringField(TEXT("material_path"), VisualEntry.MaterialPath);
						VisualObject->TryGetStringField(TEXT("material_name"), VisualEntry.MaterialName);

						const TSharedPtr<FJsonObject>* VisualTransformObject = nullptr;
						if (VisualObject->TryGetObjectField(TEXT("relative_transform"), VisualTransformObject) &&
							VisualTransformObject &&
							VisualTransformObject->IsValid())
						{
							if (!CadJsonTransformUtils::ParseTransformObject(*VisualTransformObject, VisualEntry.RelativeTransform, OutError))
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

				FCadChildJointDef JointTemplate;
				JointObject->TryGetStringField(TEXT("joint_name"), JointTemplate.JointName);
				JointObject->TryGetStringField(TEXT("parent_actor_name"), JointTemplate.ParentActorName);
				JointObject->TryGetStringField(TEXT("child_actor_name"), JointTemplate.ChildActorName);

				FString JointTypeString;
				if (JointObject->TryGetStringField(TEXT("joint_type"), JointTypeString))
				{
					if (!ParseJointType(JointTypeString, JointTemplate.JointType, OutError))
					{
						OutError = FString::Printf(TEXT("Child '%s' joint[%d] parse failed: %s"), *OutDocument.ChildActorName, JointIndex, *OutError);
						return false;
					}
				}

				FVector Axis = FVector::UpVector;
				FString AxisParseError;
				if (JointObject->HasField(TEXT("axis")) && !CadJsonTransformUtils::ParseVectorArray3(JointObject, TEXT("axis"), Axis, AxisParseError))
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
					const FString RootLinkName = ResolveRootLinkName(OutDocument);
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
}
