#pragma once

#include "CoreMinimal.h"

struct FCadFbxImportOptions
{
	bool bShowDialog = true;
	bool bConvertScene = true;
	bool bForceFrontXAxis = false;
	bool bConvertSceneUnit = true;
	float ImportUniformScale = 0.001f;
	FVector ImportTranslation = FVector::ZeroVector;
	FRotator ImportRotation = FRotator(0.0f, -90.0f, 90.0f);
	bool bCombineMeshes = false;
	bool bAutoGenerateCollision = true;
	bool bBuildNanite = false;
};
