#include "Workflow/ChildVisualCollector.h"

#include "Components/StaticMeshComponent.h"
#include "Editor/ActorHierarchyUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInterface.h"

namespace
{
	FString GetAssetPackagePath(const UObject* Asset)
	{
		return Asset ? Asset->GetOutermost()->GetName() : FString();
	}

	void FillMaterialOverride(const UStaticMeshComponent* MeshComponent, FCadChildVisual& OutVisual)
	{
		if (!MeshComponent)
		{
			return;
		}

		const int32 MaterialCount = MeshComponent->GetNumMaterials();
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			const UMaterialInterface* Material = MeshComponent->GetMaterial(MaterialIndex);
			if (Material)
			{
				OutVisual.MaterialPath = GetAssetPackagePath(Material);
				OutVisual.MaterialName = Material->GetName();
				return;
			}
		}
	}

	void AppendVisualsFromActor(
		const AActor* Actor,
		const FTransform* RootTransform,
		TArray<FCadChildVisual>& OutVisuals)
	{
		if (!Actor)
		{
			return;
		}

		TInlineComponentArray<UStaticMeshComponent*> MeshComponents(const_cast<AActor*>(Actor));
		MeshComponents.Sort([](const UStaticMeshComponent& Left, const UStaticMeshComponent& Right)
		{
			return Left.GetName() < Right.GetName();
		});

		for (UStaticMeshComponent* MeshComponent : MeshComponents)
		{
			if (!MeshComponent)
			{
				continue;
			}

			const UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
			if (!StaticMesh)
			{
				continue;
			}

			FCadChildVisual Visual;
			Visual.MeshPath = GetAssetPackagePath(StaticMesh);
			Visual.RelativeTransform = RootTransform
				? MeshComponent->GetComponentTransform().GetRelativeTransform(*RootTransform)
				: MeshComponent->GetRelativeTransform();
			FillMaterialOverride(MeshComponent, Visual);
			OutVisuals.Add(MoveTemp(Visual));
		}
	}

	void AppendSubtreeVisualsRelativeToRoot(
		AActor* CurrentActor,
		const AActor* RootActor,
		TArray<FCadChildVisual>& OutVisuals)
	{
		if (!CurrentActor || !RootActor)
		{
			return;
		}

		const FTransform RootTransform = RootActor->GetActorTransform();
		AppendVisualsFromActor(CurrentActor, &RootTransform, OutVisuals);

		TArray<AActor*> Children;
		CadActorHierarchyUtils::GetSortedAttachedChildren(CurrentActor, Children, false);
		for (AActor* ChildActor : Children)
		{
			AppendSubtreeVisualsRelativeToRoot(ChildActor, RootActor, OutVisuals);
		}
	}

	void AbsorbStaticMeshActorSubtree(
		AActor* RootActor,
		AActor* CurrentActor,
		TArray<FCadChildVisual>& OutVisuals)
	{
		if (!RootActor || !CurrentActor)
		{
			return;
		}

		if (CurrentActor->IsA<AStaticMeshActor>())
		{
			const FTransform RootTransform = RootActor->GetActorTransform();
			AppendVisualsFromActor(CurrentActor, &RootTransform, OutVisuals);
		}

		TArray<AActor*> Descendants;
		CadActorHierarchyUtils::GetSortedAttachedChildren(CurrentActor, Descendants, false);
		for (AActor* ChildActor : Descendants)
		{
			AbsorbStaticMeshActorSubtree(RootActor, ChildActor, OutVisuals);
		}
	}
}

namespace CadChildVisualCollector
{
	void CollectStaticChildVisuals(
		AActor* ChildRootActor,
		TArray<FCadChildVisual>& OutVisuals)
	{
		OutVisuals.Reset();
		if (!ChildRootActor)
		{
			return;
		}

		AppendVisualsFromActor(ChildRootActor, nullptr, OutVisuals);

		TArray<AActor*> Children;
		CadActorHierarchyUtils::GetSortedAttachedChildren(ChildRootActor, Children, false);
		for (AActor* ChildActor : Children)
		{
			AppendSubtreeVisualsRelativeToRoot(ChildActor, ChildRootActor, OutVisuals);
		}
	}

	void CollectRootLinkVisuals(
		AActor* ChildRootActor,
		TArray<FCadChildVisual>& OutVisuals)
	{
		OutVisuals.Reset();
		if (!ChildRootActor)
		{
			return;
		}

		AppendVisualsFromActor(ChildRootActor, nullptr, OutVisuals);

		TArray<AActor*> Children;
		CadActorHierarchyUtils::GetSortedAttachedChildren(ChildRootActor, Children, false);
		for (AActor* ChildActor : Children)
		{
			if (!ChildActor)
			{
				continue;
			}

			AppendSubtreeVisualsRelativeToRoot(ChildActor, ChildRootActor, OutVisuals);
		}
	}
}
