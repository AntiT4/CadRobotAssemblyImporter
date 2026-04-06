#pragma once

#include "CoreMinimal.h"
#include "Editor/ActorHierarchyUtils.h"
#include "WorkflowTypes.h"

class AActor;
class UCadImporterEditorUserSettings;
class UWorld;

namespace CadWorkflowWizardShared
{
	bool IsMovableChildActorType(ECadMasterChildActorType ActorType);
	bool JointTypeUsesAxis(ECadImportJointType JointType);
	FString BoolToYesNoString(bool bValue);
	int32 ComputeMaxDepthFromActor(AActor* RootActor);
	FString BuildChildHierarchyInfoLabel(const FCadChildEntry& ChildEntry);
	FString BuildBranchDepthTableText(const TArray<FCadHierarchyBranchStats>& BranchStats);
	FString BuildAutoJointName(const FCadChildJointDef& JointDef);
	void AppendFlattenPreviewEntriesRecursive(
		const FCadChildEntry& SourceEntry,
		AActor* MasterActor,
		const TSet<FString>& PendingFlattenBranchPaths,
		const TMap<FString, ECadMasterChildActorType>& ActorTypesByPath,
		TArray<FCadChildEntry>& OutChildEntries,
		TSet<FString>& OutPreviewPromotedChildActorPaths);
	FColor JointTypeToPreviewColor(const UCadImporterEditorUserSettings* Settings, ECadImportJointType JointType);
	void CollectNonStaticDescendantActorLocations(
		AActor* CurrentActor,
		AActor* RootActor,
		TMap<FString, FVector>& OutLocations);
	AActor* FindNonStaticDescendantActorByName(AActor* RootActor, const FString& ActorName);
	void BuildAxisBasis(const FVector& AxisDirection, FVector& OutAxisX, FVector& OutAxisY);
	void DrawDebugArc(
		UWorld* World,
		const FVector& Center,
		const FVector& AxisDirection,
		float Radius,
		float StartAngleDegrees,
		float EndAngleDegrees,
		const FColor& Color,
		bool bPersistent,
		float LifeTime,
		uint8 DepthPriority,
		float Thickness);
	void DrawDebugActorBounds(
		UWorld* World,
		AActor* Actor,
		const FColor& Color,
		bool bPersistent,
		float LifeTime,
		float Thickness);
	void DrawDebugActorAxes(
		UWorld* World,
		AActor* Actor,
		float AxisLength,
		bool bPersistent,
		float LifeTime,
		uint8 DepthPriority,
		float Thickness);
}
