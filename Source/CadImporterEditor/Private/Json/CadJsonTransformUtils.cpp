#include "Json/CadJsonTransformUtils.h"

namespace
{
	bool ParseNumberArray3(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* FieldName,
		TArray<double>& OutValues,
		FString& OutError)
	{
		OutValues.Reset();
		const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
		if (!Object->TryGetArrayField(FieldName, ArrayValues) || !ArrayValues)
		{
			OutError = FString::Printf(TEXT("Missing field: %s"), FieldName);
			return false;
		}

		if (ArrayValues->Num() != 3)
		{
			OutError = FString::Printf(TEXT("Field '%s' must contain exactly 3 numbers."), FieldName);
			return false;
		}

		for (int32 Index = 0; Index < 3; ++Index)
		{
			double NumberValue = 0.0;
			if (!(*ArrayValues)[Index].IsValid() || !(*ArrayValues)[Index]->TryGetNumber(NumberValue))
			{
				OutError = FString::Printf(TEXT("Field '%s' contains a non-number at index %d."), FieldName, Index);
				return false;
			}
			OutValues.Add(NumberValue);
		}

		return true;
	}
}

namespace CadJsonTransformUtils
{
	bool ParseTransformObject(
		const TSharedPtr<FJsonObject>& TransformObject,
		FTransform& OutTransform,
		FString& OutError)
	{
		if (!TransformObject.IsValid())
		{
			OutError = TEXT("Transform object is invalid.");
			return false;
		}

		TArray<double> LocationValues;
		TArray<double> RotationValues;
		TArray<double> ScaleValues;
		if (!ParseNumberArray3(TransformObject, TEXT("location"), LocationValues, OutError) ||
			!ParseNumberArray3(TransformObject, TEXT("rotation"), RotationValues, OutError) ||
			!ParseNumberArray3(TransformObject, TEXT("scale"), ScaleValues, OutError))
		{
			return false;
		}

		const FVector Location(LocationValues[0], LocationValues[1], LocationValues[2]);
		const FRotator Rotation(RotationValues[1], RotationValues[2], RotationValues[0]);
		const FVector Scale(ScaleValues[0], ScaleValues[1], ScaleValues[2]);
		OutTransform = FTransform(Rotation, Location, Scale);
		return true;
	}

	bool ParseVectorArray3(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* FieldName,
		FVector& OutVector,
		FString& OutError)
	{
		TArray<double> VectorValues;
		if (!ParseNumberArray3(Object, FieldName, VectorValues, OutError))
		{
			return false;
		}

		OutVector = FVector(VectorValues[0], VectorValues[1], VectorValues[2]);
		return true;
	}

	TSharedPtr<FJsonObject> MakeTransformObject(const FTransform& Transform)
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
}
