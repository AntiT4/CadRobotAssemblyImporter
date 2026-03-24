#include "Import/ImportModelParser.h"

#include "CadImporterEditor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace
{
	struct FCadImportUnitScales
	{
		float Length = 1.0f;
		float Angle = 1.0f;
		FVector SourceForward = FVector::XAxisVector;
		FVector SourceRight = FVector::YAxisVector;
		FVector SourceUp = FVector::ZAxisVector;
		FString EulerOrder = TEXT("xyz");
	};

	// Read an optional [x,y,z] numeric array; leave the output unchanged when absent.
	bool ReadVector3Optional(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FVector& OutValue, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayPtr = nullptr;
		if (!Object->TryGetArrayField(FieldName, ArrayPtr))
		{
			return true;
		}

		if (ArrayPtr->Num() != 3)
		{
			OutError = FString::Printf(TEXT("Expected 3 elements for field '%s'."), FieldName);
			return false;
		}

		OutValue.X = static_cast<float>((*ArrayPtr)[0]->AsNumber());
		OutValue.Y = static_cast<float>((*ArrayPtr)[1]->AsNumber());
		OutValue.Z = static_cast<float>((*ArrayPtr)[2]->AsNumber());
		return true;
	}

	// Read an optional [r,g,b,a] numeric array and report whether the field was present.
	bool ReadColorOptional(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FLinearColor& OutColor, bool& bOutFound, FString& OutError)
	{
		bOutFound = false;
		const TArray<TSharedPtr<FJsonValue>>* ArrayPtr = nullptr;
		if (!Object->TryGetArrayField(FieldName, ArrayPtr))
		{
			return true;
		}

		bOutFound = true;
		if (ArrayPtr->Num() != 4)
		{
			OutError = FString::Printf(TEXT("Expected 4 elements for field '%s'."), FieldName);
			return false;
		}

		OutColor.R = static_cast<float>((*ArrayPtr)[0]->AsNumber());
		OutColor.G = static_cast<float>((*ArrayPtr)[1]->AsNumber());
		OutColor.B = static_cast<float>((*ArrayPtr)[2]->AsNumber());
		OutColor.A = static_cast<float>((*ArrayPtr)[3]->AsNumber());
		return true;
	}

	// Normalize axis tokens like +X, -z_axis, yup into canonical signed basis vectors.
	bool ParseAxisToken(const FString& RawValue, FVector& OutAxis, FString& OutCanonical, FString& OutError)
	{
		FString Value = RawValue.ToLower();
		Value.ReplaceInline(TEXT(" "), TEXT(""));
		Value.ReplaceInline(TEXT("_"), TEXT(""));
		Value.ReplaceInline(TEXT("-axis"), TEXT(""));
		Value.ReplaceInline(TEXT("axis"), TEXT(""));

		int32 Sign = 1;
		if (Value.StartsWith(TEXT("+")))
		{
			Value.RightChopInline(1);
		}
		else if (Value.StartsWith(TEXT("-")))
		{
			Sign = -1;
			Value.RightChopInline(1);
		}

		if (Value.EndsWith(TEXT("up")))
		{
			Value.LeftChopInline(2);
		}

		FVector Axis = FVector::ZeroVector;
		if (Value == TEXT("x"))
		{
			Axis = FVector::XAxisVector;
		}
		else if (Value == TEXT("y"))
		{
			Axis = FVector::YAxisVector;
		}
		else if (Value == TEXT("z"))
		{
			Axis = FVector::ZAxisVector;
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported axis token: %s"), *RawValue);
			return false;
		}

		OutAxis = Axis * static_cast<float>(Sign);
		OutCanonical = Sign < 0 ? FString::Printf(TEXT("-%s"), *Value) : Value;
		return true;
	}

	// Normalize handedness aliases to a right/left flag and canonical text.
	bool ParseHandedness(const FString& RawValue, bool& bOutRightHanded, FString& OutCanonical, FString& OutError)
	{
		FString Value = RawValue.ToLower();
		Value.ReplaceInline(TEXT(" "), TEXT(""));
		Value.ReplaceInline(TEXT("_"), TEXT(""));
		Value.ReplaceInline(TEXT("-"), TEXT(""));

		if (Value == TEXT("right") || Value == TEXT("rh") || Value == TEXT("righthand") || Value == TEXT("righthanded"))
		{
			bOutRightHanded = true;
			OutCanonical = TEXT("right");
			return true;
		}

		if (Value == TEXT("left") || Value == TEXT("lh") || Value == TEXT("lefthand") || Value == TEXT("lefthanded"))
		{
			bOutRightHanded = false;
			OutCanonical = TEXT("left");
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported handedness: %s"), *RawValue);
		return false;
	}

	// Validate and normalize euler_order so it contains each of x,y,z once.
	bool ParseEulerOrder(const FString& RawValue, FString& OutOrder, FString& OutError)
	{
		FString Value = RawValue.ToLower();
		Value.ReplaceInline(TEXT(" "), TEXT(""));
		Value.ReplaceInline(TEXT("_"), TEXT(""));
		Value.ReplaceInline(TEXT("-"), TEXT(""));

		if (Value.Len() != 3)
		{
			OutError = FString::Printf(TEXT("euler_order must have 3 axes. Current value: %s"), *RawValue);
			return false;
		}

		TSet<TCHAR> UniqueAxes;
		for (int32 Index = 0; Index < Value.Len(); ++Index)
		{
			const TCHAR Axis = Value[Index];
			if (Axis != TEXT('x') && Axis != TEXT('y') && Axis != TEXT('z'))
			{
				OutError = FString::Printf(TEXT("euler_order contains unsupported axis: %c"), Axis);
				return false;
			}
			UniqueAxes.Add(Axis);
		}

		if (UniqueAxes.Num() != 3)
		{
			OutError = FString::Printf(TEXT("euler_order must contain each of x,y,z exactly once: %s"), *RawValue);
			return false;
		}

		OutOrder = Value;
		return true;
	}

	// Parse profile aliases into runtime import profile enum values.
	bool ParseModelProfile(const FString& RawValue, ECadImportModelProfile& OutProfile, FString& OutError)
	{
		FString Value = RawValue.ToLower();
		Value.ReplaceInline(TEXT(" "), TEXT(""));
		Value.ReplaceInline(TEXT("_"), TEXT(""));
		Value.ReplaceInline(TEXT("-"), TEXT(""));

		if (Value == TEXT("dynamicrobot") || Value == TEXT("dynamic"))
		{
			OutProfile = ECadImportModelProfile::DynamicRobot;
			return true;
		}

		if (Value == TEXT("fixedassembly") || Value == TEXT("fixed"))
		{
			OutProfile = ECadImportModelProfile::FixedAssembly;
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported profile: %s"), *RawValue);
		return false;
	}

	// Parse drive.mode aliases into a supported joint drive mode.
	bool ParseDriveMode(const FString& RawValue, ECadImportJointDriveMode& OutMode, FString& OutError)
	{
		FString Value = RawValue.ToLower();
		Value.ReplaceInline(TEXT(" "), TEXT(""));
		Value.ReplaceInline(TEXT("_"), TEXT(""));
		Value.ReplaceInline(TEXT("-"), TEXT(""));

		if (Value == TEXT("none"))
		{
			OutMode = ECadImportJointDriveMode::None;
			return true;
		}

		if (Value == TEXT("velocity"))
		{
			OutMode = ECadImportJointDriveMode::Velocity;
			return true;
		}

		if (Value == TEXT("position"))
		{
			OutMode = ECadImportJointDriveMode::Position;
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported drive.mode: %s"), *RawValue);
		return false;
	}

	// Fill implicit drive defaults for profiles that omit a drive block in JSON.
	void ApplyDriveDefaults(const ECadImportModelProfile Profile, FCadImportJoint& InOutJoint)
	{
		if (InOutJoint.Drive.bHasDrive)
		{
			return;
		}

		InOutJoint.Drive.bEnabled = (Profile == ECadImportModelProfile::DynamicRobot);
		InOutJoint.Drive.Mode = InOutJoint.Drive.bEnabled
			? ECadImportJointDriveMode::Position
			: ECadImportJointDriveMode::None;
	}

	// Fast path check: true when source axis metadata already matches Unreal conventions.
	bool IsUnrealNativeAxisConvention(const FCadImportUnitScales& Scales)
	{
		constexpr float AxisTolerance = 1.e-4f;
		return Scales.SourceForward.Equals(FVector::XAxisVector, AxisTolerance)
			&& Scales.SourceRight.Equals(FVector::YAxisVector, AxisTolerance)
			&& Scales.SourceUp.Equals(FVector::ZAxisVector, AxisTolerance)
			&& Scales.EulerOrder.Equals(TEXT("xyz"), ESearchCase::IgnoreCase);
	}

	// Remap vectors from source basis into Unreal basis using configured unit axes.
	FVector ConvertSourceVectorToUnreal(const FVector& SourceVector, const FCadImportUnitScales& Scales)
	{
		return FVector(
			FVector::DotProduct(SourceVector, Scales.SourceForward),
			FVector::DotProduct(SourceVector, Scales.SourceRight),
			FVector::DotProduct(SourceVector, Scales.SourceUp));
	}

	// Build a per-axis quaternion from source Euler input (degrees).
	FQuat BuildAxisQuat(TCHAR AxisToken, const FVector& SourceRotationDegrees)
	{
		switch (AxisToken)
		{
		case TEXT('x'):
			return FQuat(FVector::XAxisVector, FMath::DegreesToRadians(SourceRotationDegrees.X));
		case TEXT('y'):
			return FQuat(FVector::YAxisVector, FMath::DegreesToRadians(SourceRotationDegrees.Y));
		case TEXT('z'):
			return FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(SourceRotationDegrees.Z));
		default:
			return FQuat::Identity;
		}
	}

	// Convert source Euler rotation metadata into an Unreal rotator.
	bool ConvertSourceRotationToUnreal(const FVector& SourceRotationDegrees, const FCadImportUnitScales& Scales, FRotator& OutRotator, FString& OutError)
	{
		if (Scales.EulerOrder.Len() != 3)
		{
			OutError = FString::Printf(TEXT("Invalid euler_order: %s"), *Scales.EulerOrder);
			return false;
		}

		const FQuat Q0 = BuildAxisQuat(Scales.EulerOrder[0], SourceRotationDegrees);
		const FQuat Q1 = BuildAxisQuat(Scales.EulerOrder[1], SourceRotationDegrees);
		const FQuat Q2 = BuildAxisQuat(Scales.EulerOrder[2], SourceRotationDegrees);
		const FQuat SourceQuat = (Q2 * Q1 * Q0).GetNormalized();

		FVector XAxis = ConvertSourceVectorToUnreal(SourceQuat.RotateVector(Scales.SourceForward), Scales).GetSafeNormal();
		FVector YAxis = ConvertSourceVectorToUnreal(SourceQuat.RotateVector(Scales.SourceRight), Scales).GetSafeNormal();
		FVector ZAxis = ConvertSourceVectorToUnreal(SourceQuat.RotateVector(Scales.SourceUp), Scales).GetSafeNormal();

		if (!XAxis.Normalize() || !YAxis.Normalize() || !ZAxis.Normalize())
		{
			OutError = TEXT("Failed to convert source rotation basis to Unreal.");
			return false;
		}

		FMatrix Orientation = FMatrix::Identity;
		Orientation.SetAxes(&XAxis, &YAxis, &ZAxis);
		OutRotator = FQuat(Orientation).Rotator();
		return true;
	}

	// Parse and convert transform fields (location/rotation/scale) into Unreal space.
	bool ParseTransform(const TSharedPtr<FJsonObject>& TransformObject, const FCadImportUnitScales& Scales, FTransform& OutTransform, FString& OutError)
	{
		FVector SourceLocation = FVector::ZeroVector;
		FVector SourceRotation = FVector::ZeroVector;
		FVector SourceScale = FVector::OneVector;

		if (!ReadVector3Optional(TransformObject, TEXT("location"), SourceLocation, OutError))
		{
			return false;
		}

		if (!ReadVector3Optional(TransformObject, TEXT("rotation"), SourceRotation, OutError))
		{
			return false;
		}
		if (!ReadVector3Optional(TransformObject, TEXT("scale"), SourceScale, OutError))
		{
			return false;
		}

		SourceLocation *= Scales.Length;
		SourceRotation *= Scales.Angle;

		const FVector UnrealLocation = ConvertSourceVectorToUnreal(SourceLocation, Scales);
		const FVector UnrealScale = ConvertSourceVectorToUnreal(SourceScale, Scales);
		if (FMath::IsNearlyZero(UnrealScale.X) || FMath::IsNearlyZero(UnrealScale.Y) || FMath::IsNearlyZero(UnrealScale.Z))
		{
			OutError = FString::Printf(
				TEXT("transform.scale contains zero component after axis conversion: (%.6f, %.6f, %.6f)"),
				UnrealScale.X,
				UnrealScale.Y,
				UnrealScale.Z);
			return false;
		}

		// For Unreal-native axis metadata (front=x, up=z, handedness=left, euler_order=xyz),
		// keep JSON rotation semantics 1:1 with component Rotator values.
		if (IsUnrealNativeAxisConvention(Scales))
		{
			const FRotator UnrealRotation(SourceRotation.Y, SourceRotation.Z, SourceRotation.X);
			OutTransform = FTransform(UnrealRotation, UnrealLocation, UnrealScale);
			return true;
		}

		FRotator UnrealRotation = FRotator::ZeroRotator;
		if (!ConvertSourceRotationToUnreal(SourceRotation, Scales, UnrealRotation, OutError))
		{
			return false;
		}

		OutTransform = FTransform(UnrealRotation, UnrealLocation, UnrealScale);
		return true;
	}

	// Parse optional per-link physics attributes.
	void ParsePhysics(const TSharedPtr<FJsonObject>& PhysicsObject, FCadImportPhysics& OutPhysics)
	{
		double Number = 0.0;
		if (PhysicsObject->TryGetNumberField(TEXT("mass"), Number))
		{
			OutPhysics.Mass = static_cast<float>(Number);
		}

		bool bSimulate = false;
		if (PhysicsObject->TryGetBoolField(TEXT("simulate_physics"), bSimulate))
		{
			OutPhysics.bSimulatePhysics = bSimulate;
		}
	}

	// Parse one visual block, including mesh path, transform, material, and optional color.
	bool ParseVisual(const TSharedPtr<FJsonObject>& VisualObject, const FCadImportUnitScales& Scales, FCadImportVisual& OutVisual, FString& OutError)
	{
		if (!VisualObject->TryGetStringField(TEXT("mesh_path"), OutVisual.MeshPath) || OutVisual.MeshPath.IsEmpty())
		{
			OutError = TEXT("Visual entry is missing mesh_path.");
			return false;
		}

		const TSharedPtr<FJsonObject>* TransformObject = nullptr;
		if (VisualObject->TryGetObjectField(TEXT("transform"), TransformObject) && TransformObject && TransformObject->IsValid())
		{
			if (!ParseTransform(*TransformObject, Scales, OutVisual.Transform, OutError))
			{
				return false;
			}
		}

		VisualObject->TryGetStringField(TEXT("material_path"), OutVisual.MaterialPath);
		VisualObject->TryGetStringField(TEXT("material_name"), OutVisual.MaterialName);
		if (OutVisual.MaterialName.IsEmpty() && !OutVisual.MaterialPath.IsEmpty())
		{
			const FString MaterialPackagePath = OutVisual.MaterialPath.Contains(TEXT("."))
				? FPackageName::ObjectPathToPackageName(OutVisual.MaterialPath)
				: OutVisual.MaterialPath;
			OutVisual.MaterialName = FPackageName::GetLongPackageAssetName(MaterialPackagePath);
		}

		bool bHasColor = false;
		FLinearColor Color = FLinearColor::White;
		if (!ReadColorOptional(VisualObject, TEXT("color"), Color, bHasColor, OutError))
		{
			return false;
		}

		OutVisual.bHasColor = bHasColor;
		if (bHasColor)
		{
			OutVisual.Color = Color;
		}

		return true;
	}

	// Parse one joint block and normalize endpoint, axis, limit, and drive fields.
	bool ParseJoint(const TSharedPtr<FJsonObject>& JointObject, const FCadImportUnitScales& Scales, FCadImportJoint& OutJoint, FString& OutError)
	{
		if (!JointObject->TryGetStringField(TEXT("name"), OutJoint.Name) || OutJoint.Name.IsEmpty())
		{
			OutError = TEXT("Joint entry is missing name.");
			return false;
		}

		FString JointType;
		if (!JointObject->TryGetStringField(TEXT("type"), JointType) || JointType.IsEmpty())
		{
			OutError = TEXT("Joint entry is missing type.");
			return false;
		}

		const FString JointTypeLower = JointType.ToLower();
		if (JointTypeLower == TEXT("fixed"))
		{
			OutJoint.Type = ECadImportJointType::Fixed;
		}
		else if (JointTypeLower == TEXT("revolute"))
		{
			OutJoint.Type = ECadImportJointType::Revolute;
		}
		else if (JointTypeLower == TEXT("prismatic"))
		{
			OutJoint.Type = ECadImportJointType::Prismatic;
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported joint type: %s"), *JointType);
			return false;
		}

		FVector SourceAxis = FVector::UpVector;
		if (!ReadVector3Optional(JointObject, TEXT("axis"), SourceAxis, OutError))
		{
			return false;
		}
		OutJoint.Axis = ConvertSourceVectorToUnreal(SourceAxis, Scales).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		JointObject->TryGetStringField(TEXT("parent"), OutJoint.Parent);
		JointObject->TryGetStringField(TEXT("child"), OutJoint.Child);
		if (!JointObject->TryGetStringField(TEXT("component_name1"), OutJoint.ComponentName1))
		{
			JointObject->TryGetStringField(TEXT("componentName1"), OutJoint.ComponentName1);
		}
		if (!JointObject->TryGetStringField(TEXT("component_name2"), OutJoint.ComponentName2))
		{
			JointObject->TryGetStringField(TEXT("componentName2"), OutJoint.ComponentName2);
		}
		if (OutJoint.ComponentName1.IsEmpty() && !OutJoint.Parent.IsEmpty())
		{
			OutJoint.ComponentName1 = OutJoint.Parent;
		}
		if (OutJoint.ComponentName2.IsEmpty() && !OutJoint.Child.IsEmpty())
		{
			OutJoint.ComponentName2 = OutJoint.Child;
		}
		if (OutJoint.Parent.IsEmpty() && !OutJoint.ComponentName1.IsEmpty())
		{
			OutJoint.Parent = OutJoint.ComponentName1;
		}
		if (OutJoint.Child.IsEmpty() && !OutJoint.ComponentName2.IsEmpty())
		{
			OutJoint.Child = OutJoint.ComponentName2;
		}

		const TSharedPtr<FJsonObject>* LimitObject = nullptr;
		if (JointObject->TryGetObjectField(TEXT("limit"), LimitObject) && LimitObject && LimitObject->IsValid())
		{
			OutJoint.Limit.bHasLimit = true;
			const float JointLimitScale = (OutJoint.Type == ECadImportJointType::Prismatic)
				? Scales.Length
				: Scales.Angle;
			double Number = 0.0;
			if ((*LimitObject)->TryGetNumberField(TEXT("lower"), Number))
			{
				OutJoint.Limit.Lower = static_cast<float>(Number) * JointLimitScale;
			}
			if ((*LimitObject)->TryGetNumberField(TEXT("upper"), Number))
			{
				OutJoint.Limit.Upper = static_cast<float>(Number) * JointLimitScale;
			}
			if ((*LimitObject)->TryGetNumberField(TEXT("effort"), Number))
			{
				OutJoint.Limit.Effort = static_cast<float>(Number);
			}
			if ((*LimitObject)->TryGetNumberField(TEXT("velocity"), Number))
			{
				OutJoint.Limit.Velocity = static_cast<float>(Number) * JointLimitScale;
			}
		}

		const TSharedPtr<FJsonObject>* DriveObject = nullptr;
		if (JointObject->TryGetObjectField(TEXT("drive"), DriveObject) && DriveObject && DriveObject->IsValid())
		{
			OutJoint.Drive.bHasDrive = true;

			bool bEnabled = OutJoint.Drive.bEnabled;
			if ((*DriveObject)->TryGetBoolField(TEXT("enabled"), bEnabled))
			{
				OutJoint.Drive.bEnabled = bEnabled;
			}

			FString DriveModeValue;
			if ((*DriveObject)->TryGetStringField(TEXT("mode"), DriveModeValue) && !DriveModeValue.IsEmpty())
			{
				if (!ParseDriveMode(DriveModeValue, OutJoint.Drive.Mode, OutError))
				{
					return false;
				}
			}
			else
			{
				if (!OutJoint.Drive.bEnabled)
				{
					OutJoint.Drive.Mode = ECadImportJointDriveMode::None;
				}
				else if (OutJoint.Limit.bHasLimit && FMath::Abs(OutJoint.Limit.Velocity) > KINDA_SMALL_NUMBER)
				{
					OutJoint.Drive.Mode = ECadImportJointDriveMode::Velocity;
				}
				else
				{
					OutJoint.Drive.Mode = ECadImportJointDriveMode::Position;
				}
			}

			double DriveNumber = 0.0;
			if ((*DriveObject)->TryGetNumberField(TEXT("strength"), DriveNumber))
			{
				OutJoint.Drive.Strength = static_cast<float>(DriveNumber);
			}
			if ((*DriveObject)->TryGetNumberField(TEXT("damping"), DriveNumber))
			{
				OutJoint.Drive.Damping = static_cast<float>(DriveNumber);
			}
			if ((*DriveObject)->TryGetNumberField(TEXT("max_force"), DriveNumber))
			{
				OutJoint.Drive.MaxForce = static_cast<float>(DriveNumber);
			}
		}

		return true;
	}

	// Recursively parse root/child links and synthesize parent-child joint relationships.
	bool ParseLinkNode(const TSharedPtr<FJsonObject>& LinkObject, const FString& ParentName, const FCadImportUnitScales& Scales, FCadImportModel& OutModel, FString& OutError)
	{
		FCadImportLink Link;
		if (!LinkObject->TryGetStringField(TEXT("name"), Link.Name) || Link.Name.IsEmpty())
		{
			OutError = TEXT("Link entry is missing name.");
			return false;
		}

		const TSharedPtr<FJsonObject>* TransformObject = nullptr;
		if (LinkObject->TryGetObjectField(TEXT("transform"), TransformObject) && TransformObject && TransformObject->IsValid())
		{
			if (!ParseTransform(*TransformObject, Scales, Link.Transform, OutError))
			{
				return false;
			}
		}

		const TSharedPtr<FJsonObject>* PhysicsObject = nullptr;
		if (LinkObject->TryGetObjectField(TEXT("physics"), PhysicsObject) && PhysicsObject && PhysicsObject->IsValid())
		{
			ParsePhysics(*PhysicsObject, Link.Physics);
		}

		const TArray<TSharedPtr<FJsonValue>>* VisualsArray = nullptr;
		if (LinkObject->TryGetArrayField(TEXT("visuals"), VisualsArray))
		{
			for (int32 VisualIndex = 0; VisualIndex < VisualsArray->Num(); ++VisualIndex)
			{
				const TSharedPtr<FJsonValue>& VisualValue = (*VisualsArray)[VisualIndex];
				const TSharedPtr<FJsonObject> VisualObject = VisualValue->AsObject();
				if (!VisualObject.IsValid())
				{
					OutError = TEXT("Visual entry is invalid.");
					return false;
				}
				FString MeshPathCandidate;
				if (!VisualObject->TryGetStringField(TEXT("mesh_path"), MeshPathCandidate) || MeshPathCandidate.TrimStartAndEnd().IsEmpty())
				{
					UE_LOG(LogCadImporter, Warning, TEXT("Skipping empty visual schema on link '%s' at visuals[%d]."), *Link.Name, VisualIndex);
					continue;
				}

				FCadImportVisual Visual;
				if (!ParseVisual(VisualObject, Scales, Visual, OutError))
				{
					return false;
				}

				Link.Visuals.Add(Visual);
			}
		}

		OutModel.Links.Add(Link);
		if (ParentName.IsEmpty())
		{
			OutModel.RootLinkName = Link.Name;
		}
		else
		{
			const TSharedPtr<FJsonObject>* JointObject = nullptr;
			if (!LinkObject->TryGetObjectField(TEXT("joint"), JointObject) || !JointObject || !JointObject->IsValid())
			{
				if (OutModel.Profile != ECadImportModelProfile::FixedAssembly)
				{
					OutError = FString::Printf(TEXT("Link '%s' is missing joint definition."), *Link.Name);
					return false;
				}

				FCadImportJoint SyntheticJoint;
				SyntheticJoint.Name = FString::Printf(TEXT("%s_to_%s"), *ParentName, *Link.Name);
				SyntheticJoint.Parent = ParentName;
				SyntheticJoint.Child = Link.Name;
				SyntheticJoint.ComponentName1 = ParentName;
				SyntheticJoint.ComponentName2 = Link.Name;
				SyntheticJoint.Type = ECadImportJointType::Fixed;
				SyntheticJoint.Axis = FVector::UpVector;
				SyntheticJoint.Transform = Link.Transform;
				SyntheticJoint.Drive.bHasDrive = true;
				SyntheticJoint.Drive.bEnabled = false;
				SyntheticJoint.Drive.Mode = ECadImportJointDriveMode::None;
				OutModel.Joints.Add(SyntheticJoint);
			}
			else
			{
				FCadImportJoint Joint;
				if (!ParseJoint(*JointObject, Scales, Joint, OutError))
				{
					return false;
				}

				// Nested hierarchy remains the source of truth for assembly today.
				Joint.Parent = ParentName;
				Joint.Child = Link.Name;
				Joint.Transform = Link.Transform;
				ApplyDriveDefaults(OutModel.Profile, Joint);
				if (OutModel.Profile == ECadImportModelProfile::FixedAssembly && Joint.Drive.bEnabled)
				{
					UE_LOG(LogCadImporter, Warning, TEXT("Joint '%s' enables drive in fixed_assembly profile."), *Joint.Name);
				}
				OutModel.Joints.Add(Joint);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* ChildrenArray = nullptr;
		if (LinkObject->TryGetArrayField(TEXT("children"), ChildrenArray))
		{
			for (const TSharedPtr<FJsonValue>& ChildValue : *ChildrenArray)
			{
				const TSharedPtr<FJsonObject> ChildObject = ChildValue->AsObject();
				if (!ChildObject.IsValid())
				{
					OutError = TEXT("Child link entry is invalid.");
					return false;
				}

				if (!ParseLinkNode(ChildObject, Link.Name, Scales, OutModel, OutError))
				{
					return false;
				}
			}
		}

		return true;
	}

	// Parse units/axis metadata and build conversion scales used across the full import.
	bool BuildUnitScales(const TSharedPtr<FJsonObject>& RootObject, FCadImportModel& OutModel, FCadImportUnitScales& OutScales, FString& OutError)
	{
		OutModel.Units.Length = TEXT("meter");
		OutModel.Units.Angle = TEXT("radian");
		OutModel.Units.UpAxis = TEXT("z");
		OutModel.Units.FrontAxis = TEXT("x");
		OutModel.Units.Handedness = TEXT("left");
		OutModel.Units.EulerOrder = TEXT("xyz");
		OutModel.Units.MeshScale = 1.0f;

		const TSharedPtr<FJsonObject>* UnitsObject = nullptr;
		if (RootObject->TryGetObjectField(TEXT("units"), UnitsObject) && UnitsObject && UnitsObject->IsValid())
		{
			(*UnitsObject)->TryGetStringField(TEXT("length"), OutModel.Units.Length);
			(*UnitsObject)->TryGetStringField(TEXT("angle"), OutModel.Units.Angle);

			if (!(*UnitsObject)->TryGetStringField(TEXT("up_axis"), OutModel.Units.UpAxis))
			{
				(*UnitsObject)->TryGetStringField(TEXT("up"), OutModel.Units.UpAxis);
			}

			if (!(*UnitsObject)->TryGetStringField(TEXT("front_axis"), OutModel.Units.FrontAxis))
			{
				(*UnitsObject)->TryGetStringField(TEXT("front"), OutModel.Units.FrontAxis);
			}

			if (!(*UnitsObject)->TryGetStringField(TEXT("handedness"), OutModel.Units.Handedness))
			{
				(*UnitsObject)->TryGetStringField(TEXT("handed"), OutModel.Units.Handedness);
			}

			(*UnitsObject)->TryGetStringField(TEXT("euler_order"), OutModel.Units.EulerOrder);

			double MeshScaleNumber = 1.0;
			if ((*UnitsObject)->TryGetNumberField(TEXT("mesh_scale"), MeshScaleNumber))
			{
				OutModel.Units.MeshScale = static_cast<float>(MeshScaleNumber);
			}
		}

		const FString LengthUnit = OutModel.Units.Length.ToLower();
		if (LengthUnit == TEXT("meter") || LengthUnit == TEXT("metre"))
		{
			OutScales.Length = 100.0f;
		}
		else if (LengthUnit == TEXT("centimeter") || LengthUnit == TEXT("cm"))
		{
			OutScales.Length = 1.0f;
		}
		else if (LengthUnit == TEXT("millimeter") || LengthUnit == TEXT("mm"))
		{
			OutScales.Length = 0.1f;
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported length unit: %s"), *OutModel.Units.Length);
			return false;
		}

		const FString AngleUnit = OutModel.Units.Angle.ToLower();
		if (AngleUnit == TEXT("radian") || AngleUnit == TEXT("rad"))
		{
			OutScales.Angle = 180.0f / PI;
		}
		else if (AngleUnit == TEXT("degree") || AngleUnit == TEXT("deg"))
		{
			OutScales.Angle = 1.0f;
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported angle unit: %s"), *OutModel.Units.Angle);
			return false;
		}

		if (OutModel.Units.MeshScale <= 0.0f)
		{
			OutError = FString::Printf(TEXT("mesh_scale must be > 0. Current value: %.6f"), OutModel.Units.MeshScale);
			return false;
		}

		FVector UpAxis = FVector::ZAxisVector;
		FString CanonicalUpAxis;
		if (!ParseAxisToken(OutModel.Units.UpAxis, UpAxis, CanonicalUpAxis, OutError))
		{
			return false;
		}

		FVector FrontAxis = FVector::XAxisVector;
		FString CanonicalFrontAxis;
		if (!ParseAxisToken(OutModel.Units.FrontAxis, FrontAxis, CanonicalFrontAxis, OutError))
		{
			return false;
		}

		if (FMath::Abs(FVector::DotProduct(UpAxis.GetSafeNormal(), FrontAxis.GetSafeNormal())) > 0.999f)
		{
			OutError = FString::Printf(TEXT("up_axis and front_axis must not be parallel: up=%s front=%s"), *OutModel.Units.UpAxis, *OutModel.Units.FrontAxis);
			return false;
		}

		bool bRightHanded = false;
		FString CanonicalHandedness;
		if (!ParseHandedness(OutModel.Units.Handedness, bRightHanded, CanonicalHandedness, OutError))
		{
			return false;
		}

		FString CanonicalEulerOrder;
		if (!ParseEulerOrder(OutModel.Units.EulerOrder, CanonicalEulerOrder, OutError))
		{
			return false;
		}

		const FVector RightAxis = bRightHanded
			? FVector::CrossProduct(FrontAxis, UpAxis).GetSafeNormal()
			: FVector::CrossProduct(UpAxis, FrontAxis).GetSafeNormal();
		if (RightAxis.IsNearlyZero())
		{
			OutError = TEXT("Failed to derive right axis from front/up/handedness.");
			return false;
		}

		OutScales.SourceForward = FrontAxis.GetSafeNormal();
		OutScales.SourceUp = UpAxis.GetSafeNormal();
		OutScales.SourceRight = RightAxis;
		OutScales.EulerOrder = CanonicalEulerOrder;

		OutModel.Units.UpAxis = CanonicalUpAxis;
		OutModel.Units.FrontAxis = CanonicalFrontAxis;
		OutModel.Units.Handedness = CanonicalHandedness;
		OutModel.Units.EulerOrder = CanonicalEulerOrder;
		return true;
	}
}

bool FCadJsonParser::ParseFromFile(const FString& JsonPath, FCadImportModel& OutModel, FString& OutError) const
{
	OutModel = FCadImportModel();

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *JsonPath))
	{
		OutError = FString::Printf(TEXT("Failed to read json file: %s"), *JsonPath);
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to parse json: %s"), *JsonPath);
		return false;
	}

	OutModel.SourceDirectory = FPaths::GetPath(JsonPath);
	return ParseRoot(RootObject, OutModel, OutError);
}

bool FCadJsonParser::ParseRoot(const TSharedPtr<FJsonObject>& RootObject, FCadImportModel& OutModel, FString& OutError) const
{
	if (!RootObject.IsValid())
	{
		OutError = TEXT("Root json object is invalid.");
		return false;
	}

	if (!RootObject->TryGetStringField(TEXT("robot_name"), OutModel.RobotName) || OutModel.RobotName.IsEmpty())
	{
		OutModel.RobotName = TEXT("Robot");
	}

	FString ProfileValue;
	if (RootObject->TryGetStringField(TEXT("profile"), ProfileValue) && !ProfileValue.IsEmpty())
	{
		if (!ParseModelProfile(ProfileValue, OutModel.Profile, OutError))
		{
			return false;
		}
	}
	else
	{
		OutModel.Profile = ECadImportModelProfile::DynamicRobot;
	}

	FCadImportUnitScales Scales;
	if (!BuildUnitScales(RootObject, OutModel, Scales, OutError))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* RootActorObject = nullptr;
	if (RootObject->TryGetObjectField(TEXT("root_actor"), RootActorObject) && RootActorObject && RootActorObject->IsValid())
	{
		const TSharedPtr<FJsonObject>* WorldTransformObject = nullptr;
		if ((*RootActorObject)->TryGetObjectField(TEXT("world_transform"), WorldTransformObject) && WorldTransformObject && WorldTransformObject->IsValid())
		{
			if (!ParseTransform(*WorldTransformObject, Scales, OutModel.RootPlacement.WorldTransform, OutError))
			{
				return false;
			}
			OutModel.RootPlacement.bHasWorldTransform = true;
		}

		if (!(*RootActorObject)->TryGetStringField(TEXT("parent_actor"), OutModel.RootPlacement.ParentActorName))
		{
			(*RootActorObject)->TryGetStringField(TEXT("parent"), OutModel.RootPlacement.ParentActorName);
		}
	}

	const TSharedPtr<FJsonObject>* RootLinkObject = nullptr;
	if (!RootObject->TryGetObjectField(TEXT("root_link"), RootLinkObject) || !RootLinkObject || !RootLinkObject->IsValid())
	{
		OutError = TEXT("Root link entry is missing.");
		return false;
	}

	return ParseLinkNode(*RootLinkObject, FString(), Scales, OutModel, OutError);
}
