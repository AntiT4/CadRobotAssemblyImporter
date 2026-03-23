#include "UI/ActorInspector.h"

#include "UI/DialogUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FString GetActorDisplayName(const AActor* Actor)
	{
		return Actor ? Actor->GetActorNameOrLabel() : TEXT("(none)");
	}

	void GetSortedAttachedChildren(AActor* Actor, TArray<AActor*>& OutChildren)
	{
		OutChildren.Reset();
		if (!Actor)
		{
			return;
		}

		Actor->GetAttachedActors(OutChildren, true, false);
		OutChildren.Sort([](const AActor& Left, const AActor& Right)
		{
			return Left.GetActorNameOrLabel() < Right.GetActorNameOrLabel();
		});
	}

	int32 CountDescendantsRecursive(AActor* Actor)
	{
		TArray<AActor*> DirectChildren;
		GetSortedAttachedChildren(Actor, DirectChildren);

		int32 DescendantCount = DirectChildren.Num();
		for (AActor* ChildActor : DirectChildren)
		{
			DescendantCount += CountDescendantsRecursive(ChildActor);
		}

		return DescendantCount;
	}

	void AppendStaticMeshComponentSummary(AActor* Actor, int32 Depth, FString& OutText)
	{
		TInlineComponentArray<UStaticMeshComponent*> MeshComponents(Actor);
		if (MeshComponents.Num() == 0)
		{
			return;
		}

		const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
		OutText += FString::Printf(TEXT("%s  Mesh Components: %d\n"), *Indent, MeshComponents.Num());

		for (UStaticMeshComponent* MeshComponent : MeshComponents)
		{
			if (!MeshComponent)
			{
				continue;
			}

			const UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
			const FString MeshPath = StaticMesh ? StaticMesh->GetOutermost()->GetName() : TEXT("(none)");
			OutText += FString::Printf(TEXT("%s    - %s | mesh=%s\n"), *Indent, *MeshComponent->GetName(), *MeshPath);

			const int32 MaterialCount = MeshComponent->GetNumMaterials();
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				const UMaterialInterface* Material = MeshComponent->GetMaterial(MaterialIndex);
				const FString MaterialPath = Material ? Material->GetOutermost()->GetName() : TEXT("(none)");
				OutText += FString::Printf(TEXT("%s      material[%d]=%s\n"), *Indent, MaterialIndex, *MaterialPath);
			}
		}
	}

	void AppendActorHierarchyRecursive(AActor* Actor, int32 Depth, FString& OutText)
	{
		if (!Actor)
		{
			return;
		}

		const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
		AActor* ParentActor = Actor->GetAttachParentActor();
		const FTransform WorldTransform = Actor->GetActorTransform();

		OutText += FString::Printf(TEXT("%s- %s [%s]\n"), *Indent, *GetActorDisplayName(Actor), *Actor->GetClass()->GetName());
		OutText += FString::Printf(TEXT("%s  Parent: %s\n"), *Indent, *GetActorDisplayName(ParentActor));

		if (ParentActor)
		{
			const FTransform RelativeTransform = WorldTransform.GetRelativeTransform(ParentActor->GetActorTransform());
			OutText += FString::Printf(TEXT("%s  Relative Location: %s\n"), *Indent, *CadImportDialogUtils::FormatVector(RelativeTransform.GetLocation()));
			OutText += FString::Printf(TEXT("%s  Relative Rotation: %s\n"), *Indent, *CadImportDialogUtils::FormatRotator(RelativeTransform.Rotator()));
		}
		else
		{
			OutText += FString::Printf(TEXT("%s  Relative Location: (root)\n"), *Indent);
			OutText += FString::Printf(TEXT("%s  Relative Rotation: (root)\n"), *Indent);
		}

		AppendStaticMeshComponentSummary(Actor, Depth, OutText);

		TArray<AActor*> DirectChildren;
		GetSortedAttachedChildren(Actor, DirectChildren);
		OutText += FString::Printf(TEXT("%s  Children: %d\n"), *Indent, DirectChildren.Num());

		for (AActor* ChildActor : DirectChildren)
		{
			AppendActorHierarchyRecursive(ChildActor, Depth + 1, OutText);
		}
	}
}

namespace CadActorInspector
{
	bool TryBuildSelectionHierarchyPreview(FString& OutPreview, FString& OutError)
	{
		if (!GEditor)
		{
			OutError = TEXT("Editor context is not available.");
			return false;
		}

		USelection* SelectedActors = GEditor->GetSelectedActors();
		if (!SelectedActors)
		{
			OutError = TEXT("Selected actors are not available.");
			return false;
		}

		TArray<AActor*> ActorSelection;
		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			if (AActor* SelectedActor = Cast<AActor>(*It))
			{
				ActorSelection.Add(SelectedActor);
			}
		}

		if (ActorSelection.Num() == 0)
		{
			OutError = TEXT("Select one actor in the level first.");
			return false;
		}

		AActor* RootActor = ActorSelection[0];
		OutPreview = FString::Printf(
			TEXT("Selected Actor: %s\nClass: %s\nSelected Count: %d (showing first selected actor)\nDescendants: %d\n\n"),
			*GetActorDisplayName(RootActor),
			*RootActor->GetClass()->GetName(),
			ActorSelection.Num(),
			CountDescendantsRecursive(RootActor));
		AppendActorHierarchyRecursive(RootActor, 0, OutPreview);
		return true;
	}

	TSharedRef<SWidget> BuildInspectorTabContent(const TSharedRef<FString>& InspectorText)
	{
		return
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Inspect the currently selected actor and its descendants.")))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Refresh From Selection")))
					.OnClicked_Lambda([InspectorText]()
					{
						FString PreviewText;
						FString PreviewError;
						if (TryBuildSelectionHierarchyPreview(PreviewText, PreviewError))
						{
							*InspectorText = MoveTemp(PreviewText);
						}
						else
						{
							*InspectorText = FString::Printf(TEXT("Failed to inspect selection:\n%s"), *PreviewError);
						}

						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Shows actor hierarchy, relative transform, and static mesh references.")))
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(720.0f)
				.MinDesiredHeight(520.0f)
				[
					SNew(SMultiLineEditableTextBox)
					.IsReadOnly(true)
					.Text_Lambda([InspectorText]()
					{
						return FText::FromString(*InspectorText);
					})
				]
			];
	}
}
