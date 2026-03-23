#include "UI/JsonPreview.h"

#include "Import/AssetImportUtils.h"
#include "Import/JsonWriter.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace CadJsonPreviewLocal
{
	ECadImportModelProfile SelectionExportProfile = ECadImportModelProfile::DynamicRobot;

	ECadImportModelProfile GetSelectionExportProfile()
	{
		return SelectionExportProfile;
	}

	void SetSelectionExportProfile(const ECadImportModelProfile Profile)
	{
		SelectionExportProfile = Profile;
	}

	FString GetActorDisplayName(const AActor* Actor)
	{
		return Actor ? Actor->GetActorNameOrLabel() : TEXT("(none)");
	}

	void GetSortedAttachedChildren(AActor* Actor, TArray<AActor*>& OutChildren, const bool bRecursive = true)
	{
		OutChildren.Reset();
		if (!Actor)
		{
			return;
		}

		Actor->GetAttachedActors(OutChildren, bRecursive, false);
		OutChildren.Sort([](const AActor& Left, const AActor& Right)
		{
			return Left.GetActorNameOrLabel() < Right.GetActorNameOrLabel();
		});
	}

	FString GetAssetPackagePath(const UObject* Asset)
	{
		return Asset ? Asset->GetOutermost()->GetName() : FString();
	}

	void FillVisualMaterialOverride(const UStaticMeshComponent* MeshComponent, FCadImportVisual& OutVisual)
	{
		if (!MeshComponent)
		{
			return;
		}

		const int32 MaterialCount = MeshComponent->GetNumMaterials();
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			const UMaterialInterface* Material = MeshComponent->GetMaterial(MaterialIndex);
			if (!Material)
			{
				continue;
			}

			OutVisual.MaterialPath = GetAssetPackagePath(Material);
			OutVisual.MaterialName = Material->GetName();
			return;
		}
	}

	void AppendVisualsFromActor(const AActor* Actor, FCadImportLink& OutLink)
	{
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

			FCadImportVisual Visual;
			Visual.MeshPath = GetAssetPackagePath(StaticMesh);
			Visual.Transform = MeshComponent->GetRelativeTransform();
			FillVisualMaterialOverride(MeshComponent, Visual);
			OutLink.Visuals.Add(MoveTemp(Visual));
		}
	}

	void AppendVisualsFromActorRelativeToParent(const AActor* Actor, const AActor* ParentActor, FCadImportLink& OutLink)
	{
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

			FCadImportVisual Visual;
			Visual.MeshPath = GetAssetPackagePath(StaticMesh);

			const FTransform VisualWorldTransform = MeshComponent->GetComponentTransform();
			Visual.Transform = ParentActor
				? VisualWorldTransform.GetRelativeTransform(ParentActor->GetActorTransform())
				: VisualWorldTransform;

			FillVisualMaterialOverride(MeshComponent, Visual);
			OutLink.Visuals.Add(MoveTemp(Visual));
		}
	}

	void AbsorbStaticMeshActorSubtree(AActor* ParentActor, AActor* CurrentActor, FCadImportLink& OutLink)
	{
		if (!CurrentActor)
		{
			return;
		}

		if (CurrentActor->IsA<AStaticMeshActor>())
		{
			AppendVisualsFromActorRelativeToParent(CurrentActor, ParentActor, OutLink);
		}

		TArray<AActor*> Descendants;
		GetSortedAttachedChildren(CurrentActor, Descendants);
		for (AActor* Child : Descendants)
		{
			AbsorbStaticMeshActorSubtree(ParentActor, Child, OutLink);
		}
	}

	void AbsorbDirectStaticMeshActorChildren(AActor* ParentActor, FCadImportLink& OutLink)
	{
		TArray<AActor*> Children;
		GetSortedAttachedChildren(ParentActor, Children);
		for (AActor* Child : Children)
		{
			if (Child && Child->IsA<AStaticMeshActor>())
			{
				AbsorbStaticMeshActorSubtree(ParentActor, Child, OutLink);
			}
		}
	}

	void BuildModelRecursive(
		AActor* Actor,
		AActor* ParentActor,
		FCadImportModel& OutModel)
	{
		if (!Actor)
		{
			return;
		}

		FCadImportLink Link;
		Link.Name = GetActorDisplayName(Actor);
		const bool bIsRootLink = (OutModel.Links.Num() == 0);
		if (bIsRootLink || !ParentActor)
		{
			// Root link transform is exported in world space.
			Link.Transform = Actor->GetActorTransform();
		}
		else
		{
			Link.Transform = Actor->GetActorTransform().GetRelativeTransform(ParentActor->GetActorTransform());
		}
		AppendVisualsFromActor(Actor, Link);
		AbsorbDirectStaticMeshActorChildren(Actor, Link);
		OutModel.Links.Add(Link);

		if (!ParentActor)
		{
			OutModel.RootLinkName = Link.Name;
		}
		else
		{
			if (OutModel.Profile == ECadImportModelProfile::DynamicRobot)
			{
				const FString ParentName = GetActorDisplayName(ParentActor);
				FCadImportJoint Joint;
				Joint.Name = FString::Printf(TEXT("%s_to_%s"), *ParentName, *Link.Name);
				Joint.Parent = ParentName;
				Joint.Child = Link.Name;
				Joint.Type = ECadImportJointType::Fixed;
				Joint.Axis = FVector::UpVector;
				Joint.Transform = Link.Transform;
				Joint.ComponentName1 = ParentName;
				Joint.ComponentName2 = Link.Name;
				Joint.Drive.bHasDrive = true;
				Joint.Drive.bEnabled = true;
				Joint.Drive.Mode = ECadImportJointDriveMode::Position;
				OutModel.Joints.Add(Joint);
			}
		}

		TArray<AActor*> Children;
		GetSortedAttachedChildren(Actor, Children);
		for (AActor* Child : Children)
		{
			if (Child && Child->IsA<AStaticMeshActor>())
			{
				continue;
			}

			BuildModelRecursive(Child, Actor, OutModel);
		}
	}

	void AppendSubtreeVisualsRelativeToRoot(AActor* CurrentActor, const AActor* RootActor, FCadImportLink& OutRootLink)
	{
		if (!CurrentActor || !RootActor)
		{
			return;
		}

		AppendVisualsFromActorRelativeToParent(CurrentActor, RootActor, OutRootLink);

		TArray<AActor*> Children;
		GetSortedAttachedChildren(CurrentActor, Children, false);
		for (AActor* Child : Children)
		{
			AppendSubtreeVisualsRelativeToRoot(Child, RootActor, OutRootLink);
		}
	}

	void BuildFixedAssemblyModel(AActor* RootActor, FCadImportModel& OutModel)
	{
		if (!RootActor)
		{
			return;
		}

		// TODO(test): add fixed_assembly export->import->export round-trip coverage
		// for this flattened subtree export path.
		FCadImportLink RootLink;
		RootLink.Name = GetActorDisplayName(RootActor);
		RootLink.Transform = RootActor->GetActorTransform();
		AppendVisualsFromActor(RootActor, RootLink);

		TArray<AActor*> Children;
		GetSortedAttachedChildren(RootActor, Children, false);
		for (AActor* Child : Children)
		{
			AppendSubtreeVisualsRelativeToRoot(Child, RootActor, RootLink);
		}

		OutModel.RootLinkName = RootLink.Name;
		OutModel.Links.Add(MoveTemp(RootLink));
	}

	bool TryBuildSelectionModel(FCadImportModel& OutModel, FString& OutError)
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

		AActor* RootActor = nullptr;
		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			RootActor = Cast<AActor>(*It);
			if (RootActor)
			{
				break;
			}
		}

		if (!RootActor)
		{
			OutError = TEXT("Select one actor in the level first.");
			return false;
		}

		OutModel = FCadImportModel();
		OutModel.Profile = GetSelectionExportProfile();
		OutModel.RobotName = GetActorDisplayName(RootActor);
		OutModel.Units.Length = TEXT("centimeter");
		OutModel.Units.Angle = TEXT("degree");
		OutModel.Units.UpAxis = TEXT("z");
		OutModel.Units.FrontAxis = TEXT("x");
		OutModel.Units.Handedness = TEXT("left");
		OutModel.Units.EulerOrder = TEXT("xyz");
		OutModel.Units.MeshScale = 1.0f;
		OutModel.RootPlacement.bHasWorldTransform = true;
		OutModel.RootPlacement.WorldTransform = RootActor->GetActorTransform();
		if (const AActor* ParentActor = RootActor->GetAttachParentActor())
		{
			OutModel.RootPlacement.ParentActorName = GetActorDisplayName(ParentActor);
		}
		if (OutModel.Profile == ECadImportModelProfile::FixedAssembly)
		{
			BuildFixedAssemblyModel(RootActor, OutModel);
			return true;
		}

		BuildModelRecursive(RootActor, nullptr, OutModel);
		return true;
	}
}

namespace CadJsonPreview
{
	bool TryBuildSelectionJsonPreview(FString& OutPreview, FString& OutError)
	{
		FCadImportModel Model;
		if (!CadJsonPreviewLocal::TryBuildSelectionModel(Model, OutError))
		{
			return false;
		}

		FCadImportJsonWriter Writer;
		if (!Writer.WriteToString(Model, OutPreview, OutError))
		{
			return false;
		}

		return true;
	}

	bool TrySaveSelectionJson(const FString& OutputPath, FString& OutError)
	{
		FCadImportModel Model;
		if (!CadJsonPreviewLocal::TryBuildSelectionModel(Model, OutError))
		{
			return false;
		}

		FCadImportJsonWriter Writer;
		return Writer.WriteToFile(Model, OutputPath, OutError);
	}

	TSharedRef<SWidget> BuildJsonPreviewTabContent(const TSharedRef<FString>& PreviewText, TFunction<void()> SaveJson)
	{
		return
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Build preview JSON from the currently selected actor hierarchy.")))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Export Profile:")))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([]()
					{
						return CadJsonPreviewLocal::GetSelectionExportProfile() == ECadImportModelProfile::DynamicRobot
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([](ECheckBoxState State)
					{
						if (State == ECheckBoxState::Checked)
						{
							CadJsonPreviewLocal::SetSelectionExportProfile(ECadImportModelProfile::DynamicRobot);
						}
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("dynamic_robot")))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 16.0f, 0.0f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([]()
					{
						return CadJsonPreviewLocal::GetSelectionExportProfile() == ECadImportModelProfile::FixedAssembly
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([](ECheckBoxState State)
					{
						if (State == ECheckBoxState::Checked)
						{
							CadJsonPreviewLocal::SetSelectionExportProfile(ECadImportModelProfile::FixedAssembly);
						}
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("fixed_assembly")))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Refresh Preview")))
					.OnClicked_Lambda([PreviewText]()
					{
						FString JsonText;
						FString Error;
						if (TryBuildSelectionJsonPreview(JsonText, Error))
						{
							*PreviewText = MoveTemp(JsonText);
						}
						else
						{
							*PreviewText = FString::Printf(TEXT("Failed to build JSON preview:\n%s"), *Error);
						}

						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Save To JSON")))
					.OnClicked_Lambda([SaveJson]()
					{
						if (SaveJson)
						{
							SaveJson();
						}
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Uses package paths for static meshes and materials, and writes visual-relative transforms.")))
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
					.Text_Lambda([PreviewText]()
					{
						return FText::FromString(*PreviewText);
					})
				]
			];
	}
}
