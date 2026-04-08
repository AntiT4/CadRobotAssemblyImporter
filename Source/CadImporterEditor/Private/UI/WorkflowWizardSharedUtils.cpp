#include "UI/WorkflowWizardSharedUtils.h"

#include "CadImporterEditorUserSettings.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Editor/ActorHierarchyUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"

namespace CadWorkflowWizardShared
{
	bool IsMovableChildActorType(const ECadMasterChildActorType ActorType)
	{
		return ActorType == ECadMasterChildActorType::Movable;
	}

	bool JointTypeUsesAxis(const ECadImportJointType JointType)
	{
		return JointType == ECadImportJointType::Revolute || JointType == ECadImportJointType::Prismatic;
	}

	FString BoolToYesNoString(const bool bValue)
	{
		return bValue ? TEXT("yes") : TEXT("no");
	}

	int32 ComputeMaxDepthFromActor(AActor* RootActor)
	{
		if (!RootActor)
		{
			return 0;
		}

		TArray<AActor*> DirectChildren;
		CadActorHierarchyUtils::GetSortedAttachedChildren(RootActor, DirectChildren, false);
		int32 MaxDepth = 0;
		for (AActor* ChildActor : DirectChildren)
		{
			MaxDepth = FMath::Max(MaxDepth, 1 + ComputeMaxDepthFromActor(ChildActor));
		}

		return MaxDepth;
	}

	FString BuildChildHierarchyInfoLabel(const FCadChildEntry& ChildEntry)
	{
		AActor* ChildActor = CadActorHierarchyUtils::FindByPath(ChildEntry.ActorPath);
		TArray<AActor*> DirectChildren;
		CadActorHierarchyUtils::GetSortedAttachedChildren(ChildActor, DirectChildren, false);
		const int32 MaxDepth = ComputeMaxDepthFromActor(ChildActor);

		return FString::Printf(TEXT("direct=%d, depth=%d"), DirectChildren.Num(), MaxDepth);
	}

	FString BuildBranchDepthTableText(const TArray<FCadHierarchyBranchStats>& BranchStats)
	{
		if (BranchStats.Num() == 0)
		{
			return TEXT("Branch\tDepth\tDirect\tDesc\tNestedMesh\n(no direct child branches)");
		}

		FString Result = TEXT("Branch\tDepth\tDirect\tDesc\tNestedMesh");
		for (const FCadHierarchyBranchStats& BranchStat : BranchStats)
		{
			Result += FString::Printf(
				TEXT("\n%s\t%d\t%d\t%d\t%d"),
				*BranchStat.BranchName,
				BranchStat.MaxDepthFromRoot,
				BranchStat.DirectChildCount,
				BranchStat.DescendantCount,
				BranchStat.NestedStaticMeshActorCount);
		}

		return Result;
	}

	FString BuildAutoJointName(const FCadChildJointDef& JointDef)
	{
		const FString ParentName = JointDef.ParentActorName.TrimStartAndEnd();
		const FString ChildName = JointDef.ChildActorName.TrimStartAndEnd();
		if (ChildName.IsEmpty())
		{
			return TEXT("joint");
		}

		return ParentName.IsEmpty()
			? FString::Printf(TEXT("World_to_%s"), *ChildName)
			: FString::Printf(TEXT("%s_to_%s"), *ParentName, *ChildName);
	}

	void AppendFlattenPreviewEntriesRecursive(
		const FCadChildEntry& SourceEntry,
		AActor* MasterActor,
		const TSet<FString>& PendingFlattenBranchPaths,
		const TMap<FString, ECadMasterChildActorType>& ActorTypesByPath,
		TArray<FCadChildEntry>& OutChildEntries,
		TSet<FString>& OutPreviewPromotedChildActorPaths)
	{
		if (!PendingFlattenBranchPaths.Contains(SourceEntry.ActorPath))
		{
			FCadChildEntry ResolvedEntry = SourceEntry;
			if (const ECadMasterChildActorType* ExistingType = ActorTypesByPath.Find(ResolvedEntry.ActorPath))
			{
				ResolvedEntry.ActorType = *ExistingType;
			}

			OutChildEntries.Add(MoveTemp(ResolvedEntry));
			return;
		}

		AActor* BranchActor = CadActorHierarchyUtils::FindByPath(SourceEntry.ActorPath);
		if (!BranchActor || !MasterActor)
		{
			return;
		}

		TArray<AActor*> PromotedActors;
		CadActorHierarchyUtils::GetSortedAttachedChildren(BranchActor, PromotedActors, false);
		for (AActor* PromotedActor : PromotedActors)
		{
			if (!PromotedActor)
			{
				continue;
			}

			FCadChildEntry PreviewEntry;
			PreviewEntry.ActorName = PromotedActor->GetActorNameOrLabel();
			PreviewEntry.ActorPath = PromotedActor->GetPathName();
			PreviewEntry.RelativeTransform = PromotedActor->GetActorTransform().GetRelativeTransform(MasterActor->GetActorTransform());
			PreviewEntry.ChildJsonFileName = FString::Printf(TEXT("%s.json"), *FPaths::MakeValidFileName(PreviewEntry.ActorName));
			PreviewEntry.ActorType = SourceEntry.ActorType;
			if (const ECadMasterChildActorType* ExistingType = ActorTypesByPath.Find(PreviewEntry.ActorPath))
			{
				PreviewEntry.ActorType = *ExistingType;
			}

			OutPreviewPromotedChildActorPaths.Add(PreviewEntry.ActorPath);
			AppendFlattenPreviewEntriesRecursive(
				PreviewEntry,
				MasterActor,
				PendingFlattenBranchPaths,
				ActorTypesByPath,
				OutChildEntries,
				OutPreviewPromotedChildActorPaths);
		}
	}

	FColor JointTypeToPreviewColor(const UCadImporterEditorUserSettings* Settings, const ECadImportJointType JointType)
	{
		if (!Settings)
		{
			switch (JointType)
			{
			case ECadImportJointType::Revolute:
				return FColor(255, 170, 0);
			case ECadImportJointType::Prismatic:
				return FColor(0, 200, 255);
			case ECadImportJointType::Fixed:
			default:
				return FColor(160, 160, 160);
			}
		}

		switch (JointType)
		{
		case ECadImportJointType::Revolute:
			return Settings->RevoluteJointPreviewColor.ToFColor(true);
		case ECadImportJointType::Prismatic:
			return Settings->PrismaticJointPreviewColor.ToFColor(true);
		case ECadImportJointType::Fixed:
		default:
			return Settings->FixedJointPreviewColor.ToFColor(true);
		}
	}

	void CollectNonStaticDescendantActorLocations(
		AActor* CurrentActor,
		AActor* RootActor,
		TMap<FString, FVector>& OutLocations)
	{
		if (!CurrentActor)
		{
			return;
		}

		if (CurrentActor != RootActor)
		{
			OutLocations.Add(CurrentActor->GetActorNameOrLabel(), CurrentActor->GetActorLocation());
		}

		TArray<AActor*> Children;
		CadActorHierarchyUtils::GetSortedAttachedChildren(CurrentActor, Children, false);
		for (AActor* ChildActor : Children)
		{
			if (!ChildActor || ChildActor->IsA<AStaticMeshActor>())
			{
				continue;
			}

			CollectNonStaticDescendantActorLocations(ChildActor, RootActor, OutLocations);
		}
	}

	AActor* FindNonStaticDescendantActorByName(AActor* RootActor, const FString& ActorName)
	{
		if (!RootActor || ActorName.TrimStartAndEnd().IsEmpty())
		{
			return nullptr;
		}

		TArray<AActor*> Descendants;
		CadActorHierarchyUtils::GetSortedAttachedChildren(RootActor, Descendants, true);
		for (AActor* DescendantActor : Descendants)
		{
			if (!DescendantActor || DescendantActor->IsA<AStaticMeshActor>())
			{
				continue;
			}

			if (DescendantActor->GetActorNameOrLabel().Equals(ActorName, ESearchCase::IgnoreCase))
			{
				return DescendantActor;
			}
		}

		return nullptr;
	}

	void BuildAxisBasis(const FVector& AxisDirection, FVector& OutAxisX, FVector& OutAxisY)
	{
		const FVector SafeAxis = AxisDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FVector Reference = FMath::Abs(SafeAxis.Z) < 0.99f ? FVector::UpVector : FVector::ForwardVector;
		OutAxisX = FVector::CrossProduct(SafeAxis, Reference).GetSafeNormal();
		if (OutAxisX.IsNearlyZero())
		{
			OutAxisX = FVector::RightVector;
		}
		OutAxisY = FVector::CrossProduct(SafeAxis, OutAxisX).GetSafeNormal();
		if (OutAxisY.IsNearlyZero())
		{
			OutAxisY = FVector::ForwardVector;
		}
	}

	void DrawDebugArc(
		UWorld* World,
		const FVector& Center,
		const FVector& AxisDirection,
		const float Radius,
		const float StartAngleDegrees,
		const float EndAngleDegrees,
		const FColor& Color,
		const bool bPersistent,
		const float LifeTime,
		const uint8 DepthPriority,
		const float Thickness)
	{
		if (!World || Radius <= UE_SMALL_NUMBER)
		{
			return;
		}

		FVector BasisX = FVector::RightVector;
		FVector BasisY = FVector::ForwardVector;
		BuildAxisBasis(AxisDirection, BasisX, BasisY);

		const float AngleSpanDegrees = EndAngleDegrees - StartAngleDegrees;
		const int32 SegmentCount = FMath::Max(12, FMath::CeilToInt(FMath::Abs(AngleSpanDegrees) / 12.0f));
		FVector PreviousPoint = Center
			+ (BasisX * Radius * FMath::Cos(FMath::DegreesToRadians(StartAngleDegrees)))
			+ (BasisY * Radius * FMath::Sin(FMath::DegreesToRadians(StartAngleDegrees)));

		for (int32 SegmentIndex = 1; SegmentIndex <= SegmentCount; ++SegmentIndex)
		{
			const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
			const float AngleDegrees = FMath::Lerp(StartAngleDegrees, EndAngleDegrees, Alpha);
			const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
			const FVector CurrentPoint = Center
				+ (BasisX * Radius * FMath::Cos(AngleRadians))
				+ (BasisY * Radius * FMath::Sin(AngleRadians));
			DrawDebugLine(World, PreviousPoint, CurrentPoint, Color, bPersistent, LifeTime, DepthPriority, Thickness);
			PreviousPoint = CurrentPoint;
		}
	}

	void DrawDebugActorBounds(
		UWorld* World,
		AActor* Actor,
		const FColor& Color,
		const bool bPersistent,
		const float LifeTime,
		const float Thickness)
	{
		if (!World || !Actor)
		{
			return;
		}

		TFunction<void(AActor*, FBox&)> AccumulatePrimitiveBounds = [&](AActor* CurrentActor, FBox& InOutBounds)
		{
			if (!CurrentActor)
			{
				return;
			}

			TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(CurrentActor);
			for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
			{
				if (!PrimitiveComponent ||
					!PrimitiveComponent->IsRegistered() ||
					PrimitiveComponent->IsVisualizationComponent())
				{
					continue;
				}

				InOutBounds += PrimitiveComponent->Bounds.GetBox();
			}

			TArray<AActor*> DirectChildren;
			CadActorHierarchyUtils::GetSortedAttachedChildren(CurrentActor, DirectChildren, false);
			for (AActor* DirectChild : DirectChildren)
			{
				AccumulatePrimitiveBounds(DirectChild, InOutBounds);
			}
		};

		FBox Bounds(EForceInit::ForceInit);
		AccumulatePrimitiveBounds(Actor, Bounds);

		if (!Bounds.IsValid)
		{
			return;
		}

		DrawDebugBox(
			World,
			Bounds.GetCenter(),
			Bounds.GetExtent(),
			FQuat::Identity,
			Color,
			bPersistent,
			LifeTime,
			0,
			Thickness);
	}

	void DrawDebugActorAxes(
		UWorld* World,
		AActor* Actor,
		const float AxisLength,
		const bool bPersistent,
		const float LifeTime,
		const uint8 DepthPriority,
		const float Thickness)
	{
		if (!World || !Actor)
		{
			return;
		}

		const FTransform ActorTransform = Actor->GetActorTransform();
		const FVector Origin = ActorTransform.GetLocation();
		const FVector AxisX = ActorTransform.TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal();
		const FVector AxisY = ActorTransform.TransformVectorNoScale(FVector::RightVector).GetSafeNormal();
		const FVector AxisZ = ActorTransform.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();

		DrawDebugLine(World, Origin, Origin + (AxisX * AxisLength), FColor(255, 80, 80), bPersistent, LifeTime, DepthPriority, Thickness);
		DrawDebugLine(World, Origin, Origin + (AxisY * AxisLength), FColor(80, 220, 120), bPersistent, LifeTime, DepthPriority, Thickness);
		DrawDebugLine(World, Origin, Origin + (AxisZ * AxisLength), FColor(80, 160, 255), bPersistent, LifeTime, DepthPriority, Thickness);
	}
}
