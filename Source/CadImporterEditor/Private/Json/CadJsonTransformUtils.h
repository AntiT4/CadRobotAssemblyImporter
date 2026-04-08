#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CadJsonTransformUtils
{
	bool ParseTransformObject(
		const TSharedPtr<FJsonObject>& TransformObject,
		FTransform& OutTransform,
		FString& OutError);

	bool ParseVectorArray3(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* FieldName,
		FVector& OutVector,
		FString& OutError);

	TSharedPtr<FJsonObject> MakeTransformObject(const FTransform& Transform);
}
