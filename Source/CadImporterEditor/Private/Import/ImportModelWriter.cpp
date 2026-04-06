#include "Import/ImportModelWriter.h"

#include "CadImportStringUtils.h"
#include "Dom/JsonObject.h"
#include "Json/CadJsonTransformUtils.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString ModelProfileToString(const ECadImportModelProfile Profile)
	{
		return CadImportStringUtils::ToImportModelProfileString(Profile);
	}

	FString JointDriveModeToString(const ECadImportJointDriveMode Mode)
	{
		return CadImportStringUtils::ToJointDriveModeString(Mode);
	}

	TSharedPtr<FJsonObject> MakeRootPlacementObject(const FCadImportRootPlacement& Placement)
	{
		if (!Placement.bHasWorldTransform && Placement.ParentActorName.IsEmpty())
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> PlacementObject = MakeShared<FJsonObject>();
		if (Placement.bHasWorldTransform)
		{
			PlacementObject->SetObjectField(TEXT("world_transform"), CadJsonTransformUtils::MakeTransformObject(Placement.WorldTransform));
		}
		if (!Placement.ParentActorName.IsEmpty())
		{
			PlacementObject->SetStringField(TEXT("parent_actor"), Placement.ParentActorName);
		}
		return PlacementObject;
	}

	TSharedPtr<FJsonObject> MakeJointObject(const FCadImportJoint& Joint)
	{
		TSharedPtr<FJsonObject> JointObject = MakeShared<FJsonObject>();
		JointObject->SetStringField(TEXT("name"), Joint.Name);
		const FString ExportComponentName1 = Joint.ComponentName1.IsEmpty() ? Joint.Parent : Joint.ComponentName1;
		const FString ExportComponentName2 = Joint.ComponentName2.IsEmpty() ? Joint.Child : Joint.ComponentName2;
		if (!ExportComponentName1.IsEmpty())
		{
			JointObject->SetStringField(TEXT("component_name1"), ExportComponentName1);
		}
		if (!ExportComponentName2.IsEmpty())
		{
			JointObject->SetStringField(TEXT("component_name2"), ExportComponentName2);
		}

		JointObject->SetStringField(TEXT("type"), CadImportStringUtils::ToJointTypeString(Joint.Type));

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
		if (Joint.Drive.bHasDrive)
		{
			TSharedPtr<FJsonObject> DriveObject = MakeShared<FJsonObject>();
			DriveObject->SetBoolField(TEXT("enabled"), Joint.Drive.bEnabled);
			DriveObject->SetStringField(TEXT("mode"), JointDriveModeToString(Joint.Drive.Mode));
			DriveObject->SetNumberField(TEXT("strength"), Joint.Drive.Strength);
			DriveObject->SetNumberField(TEXT("damping"), Joint.Drive.Damping);
			DriveObject->SetNumberField(TEXT("max_force"), Joint.Drive.MaxForce);
			JointObject->SetObjectField(TEXT("drive"), DriveObject);
		}

		return JointObject;
	}

	TSharedPtr<FJsonObject> MakeVisualObject(const FCadImportVisual& Visual)
	{
		TSharedPtr<FJsonObject> VisualObject = MakeShared<FJsonObject>();
		VisualObject->SetStringField(TEXT("mesh_path"), Visual.MeshPath);
		VisualObject->SetObjectField(TEXT("transform"), CadJsonTransformUtils::MakeTransformObject(Visual.Transform));

		if (!Visual.MaterialPath.IsEmpty())
		{
			VisualObject->SetStringField(TEXT("material_path"), Visual.MaterialPath);
		}

		if (!Visual.MaterialName.IsEmpty())
		{
			VisualObject->SetStringField(TEXT("material_name"), Visual.MaterialName);
		}

		if (Visual.bHasColor)
		{
			VisualObject->SetArrayField(TEXT("color"), TArray<TSharedPtr<FJsonValue>>
			{
				MakeShared<FJsonValueNumber>(Visual.Color.R),
				MakeShared<FJsonValueNumber>(Visual.Color.G),
				MakeShared<FJsonValueNumber>(Visual.Color.B),
				MakeShared<FJsonValueNumber>(Visual.Color.A)
			});
		}

		return VisualObject;
	}

	TSharedPtr<FJsonObject> BuildLinkObjectRecursive(
		const FString& LinkName,
		const TMap<FString, FCadImportLink>& LinksByName,
		const TMultiMap<FString, FString>& ChildrenByParent,
		const TMap<FString, FCadImportJoint>& JointsByChild)
	{
		const FCadImportLink* Link = LinksByName.Find(LinkName);
		if (!Link)
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> LinkObject = MakeShared<FJsonObject>();
		LinkObject->SetStringField(TEXT("name"), Link->Name);
		LinkObject->SetObjectField(TEXT("transform"), CadJsonTransformUtils::MakeTransformObject(Link->Transform));

		if (Link->Physics.Mass > 0.0f || Link->Physics.bSimulatePhysics)
		{
			TSharedPtr<FJsonObject> PhysicsObject = MakeShared<FJsonObject>();
			PhysicsObject->SetNumberField(TEXT("mass"), Link->Physics.Mass);
			PhysicsObject->SetBoolField(TEXT("simulate_physics"), Link->Physics.bSimulatePhysics);
			LinkObject->SetObjectField(TEXT("physics"), PhysicsObject);
		}

		TArray<TSharedPtr<FJsonValue>> VisualValues;
		for (const FCadImportVisual& Visual : Link->Visuals)
		{
			VisualValues.Add(MakeShared<FJsonValueObject>(MakeVisualObject(Visual)));
		}
		LinkObject->SetArrayField(TEXT("visuals"), VisualValues);

		if (const FCadImportJoint* Joint = JointsByChild.Find(Link->Name))
		{
			LinkObject->SetObjectField(TEXT("joint"), MakeJointObject(*Joint));
		}

		TArray<FString> Children;
		ChildrenByParent.MultiFind(Link->Name, Children);
		Children.Sort();

		TArray<TSharedPtr<FJsonValue>> ChildValues;
		for (const FString& ChildName : Children)
		{
			if (TSharedPtr<FJsonObject> ChildObject = BuildLinkObjectRecursive(ChildName, LinksByName, ChildrenByParent, JointsByChild))
			{
				ChildValues.Add(MakeShared<FJsonValueObject>(ChildObject));
			}
		}
		LinkObject->SetArrayField(TEXT("children"), ChildValues);

		return LinkObject;
	}
}

bool FCadJsonWriter::WriteToString(const FCadImportModel& Model, FString& OutJson, FString& OutError) const
{
	if (Model.Links.Num() == 0)
	{
		OutError = TEXT("Cannot write JSON because the model has no links.");
		return false;
	}

	FString RootLinkName = Model.RootLinkName;
	if (RootLinkName.IsEmpty())
	{
		RootLinkName = Model.Links[0].Name;
	}

	TMap<FString, FCadImportLink> LinksByName;
	for (const FCadImportLink& Link : Model.Links)
	{
		LinksByName.Add(Link.Name, Link);
	}

	if (!LinksByName.Contains(RootLinkName))
	{
		OutError = FString::Printf(TEXT("Root link '%s' was not found in the model."), *RootLinkName);
		return false;
	}

	TMultiMap<FString, FString> ChildrenByParent;
	TMap<FString, FCadImportJoint> JointsByChild;
	for (const FCadImportJoint& Joint : Model.Joints)
	{
		if (!Joint.Parent.IsEmpty() && !Joint.Child.IsEmpty())
		{
			ChildrenByParent.Add(Joint.Parent, Joint.Child);
			JointsByChild.Add(Joint.Child, Joint);
		}
	}

	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("robot_name"), Model.RobotName.IsEmpty() ? TEXT("Robot") : Model.RobotName);
	RootObject->SetStringField(TEXT("profile"), ModelProfileToString(Model.Profile));
	if (TSharedPtr<FJsonObject> PlacementObject = MakeRootPlacementObject(Model.RootPlacement))
	{
		RootObject->SetObjectField(TEXT("root_actor"), PlacementObject);
	}

	// Normalize output to Unreal-native units so writer does not depend on inverse axis/unit conversion.
	TSharedPtr<FJsonObject> UnitsObject = MakeShared<FJsonObject>();
	UnitsObject->SetStringField(TEXT("length"), TEXT("centimeter"));
	UnitsObject->SetStringField(TEXT("angle"), TEXT("degree"));
	UnitsObject->SetStringField(TEXT("up_axis"), TEXT("z"));
	UnitsObject->SetStringField(TEXT("front_axis"), TEXT("x"));
	UnitsObject->SetStringField(TEXT("handedness"), TEXT("left"));
	UnitsObject->SetStringField(TEXT("euler_order"), TEXT("xyz"));
	UnitsObject->SetNumberField(TEXT("mesh_scale"), 1.0);
	RootObject->SetObjectField(TEXT("units"), UnitsObject);

	TSharedPtr<FJsonObject> RootLinkObject = BuildLinkObjectRecursive(RootLinkName, LinksByName, ChildrenByParent, JointsByChild);
	if (!RootLinkObject.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to build root link object for '%s'."), *RootLinkName);
		return false;
	}

	RootObject->SetObjectField(TEXT("root_link"), RootLinkObject);

	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutJson);
	if (!FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
	{
		OutError = TEXT("Failed to serialize CAD JSON.");
		return false;
	}

	return true;
}

bool FCadJsonWriter::WriteToFile(const FCadImportModel& Model, const FString& OutputPath, FString& OutError) const
{
	FString JsonText;
	if (!WriteToString(Model, JsonText, OutError))
	{
		return false;
	}

	if (!FFileHelper::SaveStringToFile(JsonText, *OutputPath))
	{
		OutError = FString::Printf(TEXT("Failed to write CAD JSON file: %s"), *OutputPath);
		return false;
	}

	return true;
}
