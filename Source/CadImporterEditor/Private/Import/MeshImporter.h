#pragma once

#include "CoreMinimal.h"
#include "ImportModelTypes.h"
#include "ImportOptions.h"

class UFbxImportUI;
class UStaticMesh;

class FCadImportAssetImporter
{
public:
	bool ImportMeshes(const FCadImportModel& Model, const FCadImportPaths& Paths, const FCadFbxImportOptions& Options, FCadImportResult& OutResult, FString& OutError);

private:
	bool ConfigureFbxImportOnce(const FCadFbxImportOptions& Options, FString& OutError);
	bool EnsureSimpleCollision(UStaticMesh* StaticMesh, FString& OutError) const;
	bool ShouldGenerateSimpleCollisionForModel(const FCadImportModel& Model) const;
	bool ImportMeshForLink(
		const FCadImportModel& Model,
		const FCadImportPaths& Paths,
		const FCadImportLink& Link,
		bool bGenerateSimpleCollision,
		TMap<FString, FString>& InOutImportedMeshBySource,
		FCadImportResult& OutResult,
		FString& OutError);

	TObjectPtr<UFbxImportUI> CachedFbxImportUI;
};
