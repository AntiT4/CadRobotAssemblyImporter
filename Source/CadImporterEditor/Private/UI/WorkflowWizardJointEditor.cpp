#include "UI/WorkflowWizard.h"
#include "UI/WorkflowWizardSharedUtils.h"

#include "CadImportStringUtils.h"
#include "CadImporterEditorUserSettings.h"
#include "ChildDocParser.h"
#include "ImportService.h"
#include "CadImporterEditor.h"
#include "DesktopPlatformModule.h"
#include "Editor/ActorHierarchyUtils.h"
#include "Editor.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformFileManager.h"
#include "IDesktopPlatform.h"
#include "Misc/MessageDialog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "DrawDebugHelpers.h"
#include "Workflow/WorkspaceUtils.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"

using namespace CadWorkflowWizardShared;

void SCadWorkflowWizard::ResetJointEditorState()
{
	StopJointTestAnimation(true);
	EditableJointChildren.Reset();
	MovableJointPreviewText = TEXT("No movable child actors are ready for joint setup preview yet.");
	ClearJointPreviewLines();
	RebuildJointEditorRows();
}

EActiveTimerReturnType SCadWorkflowWizard::UpdateJointTestAnimation(double CurrentTime, float DeltaTime)
{
	if (!JointTestAnimation.IsSet())
	{
		return EActiveTimerReturnType::Continue;
	}

	FJointTestAnimationState& AnimationState = JointTestAnimation.GetValue();
	AActor* TargetActor = AnimationState.TargetActor.Get();
	USceneComponent* TargetRootComponent = TargetActor ? TargetActor->GetRootComponent() : nullptr;
	if (!TargetRootComponent)
	{
		JointTestAnimation.Reset();
		return EActiveTimerReturnType::Continue;
	}

	const float SegmentDurationSeconds = 0.45f;
	const float TotalDurationSeconds = SegmentDurationSeconds * 3.0f;
	AnimationState.ElapsedSeconds = FMath::Min(AnimationState.ElapsedSeconds + DeltaTime, TotalDurationSeconds);

	const auto EvaluateAnimatedValue = [SegmentDurationSeconds](const float ElapsedSeconds, const float Lower, const float Upper)
	{
		const auto EaseAlpha = [](const float Alpha)
		{
			return FMath::InterpEaseInOut(0.0f, 1.0f, FMath::Clamp(Alpha, 0.0f, 1.0f), 2.0f);
		};

		if (ElapsedSeconds <= SegmentDurationSeconds)
		{
			return FMath::Lerp(0.0f, Lower, EaseAlpha(ElapsedSeconds / SegmentDurationSeconds));
		}

		if (ElapsedSeconds <= SegmentDurationSeconds * 2.0f)
		{
			return FMath::Lerp(Lower, Upper, EaseAlpha((ElapsedSeconds - SegmentDurationSeconds) / SegmentDurationSeconds));
		}

		return FMath::Lerp(Upper, 0.0f, EaseAlpha((ElapsedSeconds - (SegmentDurationSeconds * 2.0f)) / SegmentDurationSeconds));
	};

	const FVector LocalAxis = AnimationState.LocalAxis.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const float AnimatedValue = EvaluateAnimatedValue(AnimationState.ElapsedSeconds, AnimationState.Lower, AnimationState.Upper);
	FTransform AnimatedRelativeTransform = AnimationState.OriginalRelativeTransform;

	if (AnimationState.JointType == ECadImportJointType::Prismatic)
	{
		const FVector LocalOffset = LocalAxis * AnimatedValue;
		const FVector RotatedOffset = AnimationState.OriginalRelativeTransform.GetRotation().RotateVector(LocalOffset);
		AnimatedRelativeTransform.SetLocation(AnimationState.OriginalRelativeTransform.GetLocation() + RotatedOffset);
	}
	else if (AnimationState.JointType == ECadImportJointType::Revolute)
	{
		const FQuat DeltaRotation(LocalAxis, FMath::DegreesToRadians(AnimatedValue));
		AnimatedRelativeTransform.SetRotation((AnimationState.OriginalRelativeTransform.GetRotation() * DeltaRotation).GetNormalized());
	}

	TargetRootComponent->SetRelativeTransform(AnimatedRelativeTransform);
	if (UWorld* World = TargetActor->GetWorld())
	{
		const UCadImporterEditorUserSettings* Settings = GetDefault<UCadImporterEditorUserSettings>();
		const uint8 DepthPriority = (Settings && Settings->bDrawJointPreviewLinesInForeground) ? 1 : 0;
		DrawDebugActorAxes(World, TargetActor, 12.0f, false, 0.1f, DepthPriority, 1.0f);
	}
	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}

	if (AnimationState.ElapsedSeconds >= TotalDurationSeconds - KINDA_SMALL_NUMBER)
	{
		StopJointTestAnimation(true);
	}

	return EActiveTimerReturnType::Continue;
}

void SCadWorkflowWizard::LoadEditableJointDocuments(
	const FCadMasterSelection& SelectionForGeneration,
	const TArray<FCadChildDoc>& InChildDocuments)
{
	if (InChildDocuments.Num() == 0)
	{
		MovableJointPreviewText = FString::Printf(
			TEXT("Dry-run folder: %s\nNo movable child JSON documents were produced by the dry-run preview.\nNo joint definitions were read back from temporary files."),
			DryRunPreviewFolderPath.IsEmpty() ? TEXT("(not available)") : *DryRunPreviewFolderPath);
		EditableJointChildren.Reset();
		ClearJointPreviewLines();
		RebuildJointEditorRows();
		return;
	}

	EditableJointChildren.Reset();
	for (const FCadChildDoc& InputDocument : InChildDocuments)
	{
		FEditableJointChildState ChildState;
		ChildState.ChildDocument = InputDocument;
		for (FCadChildJointDef& JointDef : ChildState.ChildDocument.Joints)
		{
			JointDef.JointName = BuildAutoJointName(JointDef);
			ChildState.JointDebugDrawEnabled.Add(false);
		}

		for (const FCadChildEntry& ChildEntry : SelectionForGeneration.Children)
		{
			if (ChildEntry.ActorName == ChildState.ChildDocument.ChildActorName)
			{
				ChildState.ChildEntry = ChildEntry;
				break;
			}
		}

		TSet<FString> UniqueLinkNames;
		for (const FCadChildLinkDef& LinkDef : ChildState.ChildDocument.Links)
		{
			const FString LinkName = LinkDef.LinkName.TrimStartAndEnd();
			if (!LinkName.IsEmpty())
			{
				UniqueLinkNames.Add(LinkName);
			}
		}

		TArray<FString> SortedLinkNames = UniqueLinkNames.Array();
		SortedLinkNames.Sort();
		ChildState.ParentLinkItems.Add(MakeShared<FString>(TEXT("World")));
		for (const FString& LinkName : SortedLinkNames)
		{
			ChildState.ParentLinkItems.Add(MakeShared<FString>(LinkName));
			ChildState.ChildLinkItems.Add(MakeShared<FString>(LinkName));
		}

		EditableJointChildren.Add(MoveTemp(ChildState));
	}

	MovableJointPreviewText = FString::Printf(
		TEXT("Dry-run folder: %s\nMovable child JSON documents parsed: %d"),
		DryRunPreviewFolderPath.IsEmpty() ? TEXT("(not available)") : *DryRunPreviewFolderPath,
		EditableJointChildren.Num());
	RebuildJointEditorRows();
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::RebuildJointEditorRows()
{
	if (!JointEditorRowsBox.IsValid())
	{
		return;
	}

	JointEditorRowsBox->ClearChildren();

	JointEditorRowsBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		SNew(STextBlock)
		.Text_Lambda([this]()
		{
			return FText::FromString(MovableJointPreviewText);
		})
	];

	if (EditableJointChildren.Num() == 0)
	{
		return;
	}

	for (int32 ChildDocIndex = 0; ChildDocIndex < EditableJointChildren.Num(); ++ChildDocIndex)
	{
		JointEditorRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 12.0f)
		[
			SNew(SBorder)
			.Padding(8.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this, ChildDocIndex]()
					{
						if (!EditableJointChildren.IsValidIndex(ChildDocIndex))
						{
							return FText::FromString(TEXT("(invalid child)"));
						}
						return FText::FromString(FString::Printf(TEXT("[Movable] %s"), *EditableJointChildren[ChildDocIndex].ChildDocument.ChildActorName));
					})
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this, ChildDocIndex]()
					{
						if (!EditableJointChildren.IsValidIndex(ChildDocIndex))
						{
							return FText::GetEmpty();
						}
						const FEditableJointChildState& ChildState = EditableJointChildren[ChildDocIndex];
						return FText::FromString(FString::Printf(
							TEXT("Source Path: %s | Available Links: %d | Joint Count: %d"),
							*ChildState.ChildDocument.SourceActorPath,
							ChildState.ChildLinkItems.Num(),
							ChildState.ChildDocument.Joints.Num()));
					})
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this, ChildDocIndex]()
					{
						if (!EditableJointChildren.IsValidIndex(ChildDocIndex))
						{
							return FText::GetEmpty();
						}

						TArray<FString> LinkNames;
						for (const TSharedPtr<FString>& LinkItem : EditableJointChildren[ChildDocIndex].ChildLinkItems)
						{
							if (LinkItem.IsValid())
							{
								LinkNames.Add(*LinkItem);
							}
						}
						return FText::FromString(FString::Printf(TEXT("Links: %s"), *FString::Join(LinkNames, TEXT(", "))));
					})
				]
			]
		];

		if (EditableJointChildren[ChildDocIndex].ChildDocument.Joints.Num() == 0)
		{
			JointEditorRowsBox->AddSlot()
			.AutoHeight()
			.Padding(12.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("No joints found in this child JSON.")))
			];
			continue;
		}

		for (int32 JointIndex = 0; JointIndex < EditableJointChildren[ChildDocIndex].ChildDocument.Joints.Num(); ++JointIndex)
		{
			JointEditorRowsBox->AddSlot()
			.AutoHeight()
			.Padding(12.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this, ChildDocIndex, JointIndex]()
						{
							if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
								!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
							{
								return FText::FromString(TEXT("Joint"));
							}

							return FText::FromString(FString::Printf(
								TEXT("Joint %d | Name: %s"),
								JointIndex + 1,
								*EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointName));
						})
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text_Lambda([this, ChildDocIndex, JointIndex]()
							{
								const bool bIsEnabled = EditableJointChildren.IsValidIndex(ChildDocIndex)
									&& EditableJointChildren[ChildDocIndex].JointDebugDrawEnabled.IsValidIndex(JointIndex)
									&& EditableJointChildren[ChildDocIndex].JointDebugDrawEnabled[JointIndex];
								return FText::FromString(bIsEnabled ? TEXT("Hide Debug Draw") : TEXT("Show Debug Draw"));
							})
							.OnClicked_Lambda([this, ChildDocIndex, JointIndex]()
							{
								const bool bCurrent = EditableJointChildren.IsValidIndex(ChildDocIndex)
									&& EditableJointChildren[ChildDocIndex].JointDebugDrawEnabled.IsValidIndex(JointIndex)
									&& EditableJointChildren[ChildDocIndex].JointDebugDrawEnabled[JointIndex];
								SetJointDebugDrawEnabled(ChildDocIndex, JointIndex, !bCurrent);
								return FReply::Handled();
							})
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
							{
								return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
									EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
									JointTypeUsesAxis(EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType);
							})
							.Text(FText::FromString(TEXT("Test")))
							.OnClicked_Lambda([this, ChildDocIndex, JointIndex]()
							{
								RunJointLimitTest(ChildDocIndex, JointIndex);
								return FReply::Handled();
							})
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Parent Link")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SComboBox<TSharedPtr<FString>>)
								.OptionsSource(&EditableJointChildren[ChildDocIndex].ParentLinkItems)
								.OnGenerateWidget_Lambda([](const TSharedPtr<FString> Item)
								{
									return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
								})
								.OnSelectionChanged_Lambda([this, ChildDocIndex, JointIndex](const TSharedPtr<FString> SelectedItem, ESelectInfo::Type)
								{
									if (SelectedItem.IsValid())
									{
										SetJointParentLink(ChildDocIndex, JointIndex, *SelectedItem);
									}
								})
								[
									SNew(STextBlock)
									.Text_Lambda([this, ChildDocIndex, JointIndex]()
									{
										if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
											!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
										{
											return FText::GetEmpty();
										}
										const FString ParentName = EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].ParentActorName.TrimStartAndEnd();
										return FText::FromString(ParentName.IsEmpty() ? TEXT("World") : ParentName);
									})
								]
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Child Link")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SComboBox<TSharedPtr<FString>>)
								.OptionsSource(&EditableJointChildren[ChildDocIndex].ChildLinkItems)
								.OnGenerateWidget_Lambda([](const TSharedPtr<FString> Item)
								{
									return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
								})
								.OnSelectionChanged_Lambda([this, ChildDocIndex, JointIndex](const TSharedPtr<FString> SelectedItem, ESelectInfo::Type)
								{
									if (SelectedItem.IsValid())
									{
										SetJointChildLink(ChildDocIndex, JointIndex, *SelectedItem);
									}
								})
								[
									SNew(STextBlock)
									.Text_Lambda([this, ChildDocIndex, JointIndex]()
									{
										if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
											!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
										{
											return FText::GetEmpty();
										}
										return FText::FromString(EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].ChildActorName);
									})
								]
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(0.8f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Joint Type")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SComboBox<TSharedPtr<FString>>)
								.OptionsSource(&JointTypeItems)
								.OnGenerateWidget_Lambda([](const TSharedPtr<FString> Item)
								{
									return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
								})
								.OnSelectionChanged_Lambda([this, ChildDocIndex, JointIndex](const TSharedPtr<FString> SelectedItem, ESelectInfo::Type)
								{
									if (SelectedItem.IsValid())
									{
										SetJointType(ChildDocIndex, JointIndex, *SelectedItem);
									}
								})
								[
									SNew(STextBlock)
									.Text_Lambda([this, ChildDocIndex, JointIndex]()
									{
										if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
											!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
										{
											return FText::FromString(TEXT("fixed"));
										}
										return FText::FromString(CadImportStringUtils::ToJointTypeString(
											EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType));
									})
								]
							]
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 18.0f, 12.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Axis")))
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("X")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.PreventThrottling(true)
								.MinValue(0.0f)
								.MaxValue(1.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(1.0f)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										JointTypeUsesAxis(EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType);
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Axis.X;
								})
								.OnValueChanged_Lambda([this, ChildDocIndex, JointIndex](const float NewValue)
								{
									SetJointAxisX(ChildDocIndex, JointIndex, NewValue);
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointAxisX(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Y")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.PreventThrottling(true)
								.MinValue(0.0f)
								.MaxValue(1.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(1.0f)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										JointTypeUsesAxis(EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType);
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Axis.Y;
								})
								.OnValueChanged_Lambda([this, ChildDocIndex, JointIndex](const float NewValue)
								{
									SetJointAxisY(ChildDocIndex, JointIndex, NewValue);
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointAxisY(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Z")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.PreventThrottling(true)
								.MinValue(0.0f)
								.MaxValue(1.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(1.0f)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										JointTypeUsesAxis(EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType);
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Axis.Z;
								})
								.OnValueChanged_Lambda([this, ChildDocIndex, JointIndex](const float NewValue)
								{
									SetJointAxisZ(ChildDocIndex, JointIndex, NewValue);
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointAxisZ(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 18.0f, 12.0f, 0.0f)
						[
							SNew(SCheckBox)
							.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
							{
								return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
									EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
									JointTypeUsesAxis(EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType);
							})
							.IsChecked_Lambda([this, ChildDocIndex, JointIndex]()
							{
								if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
									!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
								{
									return ECheckBoxState::Unchecked;
								}
								return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.bHasLimit
									? ECheckBoxState::Checked
									: ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([this, ChildDocIndex, JointIndex](const ECheckBoxState NewState)
							{
								SetJointLimitEnabled(ChildDocIndex, JointIndex, NewState == ECheckBoxState::Checked);
							})
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Use Limit")))
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Lower")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.PreventThrottling(true)
								.MinSliderValue(-270.0f)
								.MaxSliderValue(270.0f)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.bHasLimit;
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Lower;
								})
								.OnValueChanged_Lambda([this, ChildDocIndex, JointIndex](const float NewValue)
								{
									SetJointLimitLower(ChildDocIndex, JointIndex, NewValue);
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointLimitLower(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Upper")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.PreventThrottling(true)
								.MinSliderValue(-270.0f)
								.MaxSliderValue(270.0f)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.bHasLimit;
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Upper;
								})
								.OnValueChanged_Lambda([this, ChildDocIndex, JointIndex](const float NewValue)
								{
									SetJointLimitUpper(ChildDocIndex, JointIndex, NewValue);
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointLimitUpper(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Effort")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(false)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.bHasLimit;
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Effort;
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointLimitEffort(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Velocity")))
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(false)
								.IsEnabled_Lambda([this, ChildDocIndex, JointIndex]()
								{
									return EditableJointChildren.IsValidIndex(ChildDocIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex) &&
										EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.bHasLimit;
								})
								.Value_Lambda([this, ChildDocIndex, JointIndex]() -> TOptional<float>
								{
									if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
										!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
									{
										return TOptional<float>();
									}
									return EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Velocity;
								})
								.OnValueCommitted_Lambda([this, ChildDocIndex, JointIndex](const float NewValue, ETextCommit::Type)
								{
									SetJointLimitVelocity(ChildDocIndex, JointIndex, NewValue);
								})
							]
						]
					]
				]
			];
		}
	}
}

void SCadWorkflowWizard::CleanupDryRunPreviewFolder()
{
	if (DryRunPreviewFolderPath.IsEmpty())
	{
		return;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (PlatformFile.DirectoryExists(*DryRunPreviewFolderPath))
	{
		PlatformFile.DeleteDirectoryRecursively(*DryRunPreviewFolderPath);
	}

	DryRunPreviewFolderPath.Reset();
}

void SCadWorkflowWizard::SetJointParentLink(const int32 ChildDocIndex, const int32 JointIndex, const FString& ParentLinkName)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	FCadChildJointDef& JointDef = EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex];
	JointDef.ParentActorName = ParentLinkName.Equals(TEXT("World"), ESearchCase::IgnoreCase)
		? FString()
		: ParentLinkName.TrimStartAndEnd();
	JointDef.JointName = BuildAutoJointName(JointDef);
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointChildLink(const int32 ChildDocIndex, const int32 JointIndex, const FString& ChildLinkName)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	FCadChildJointDef& JointDef = EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex];
	JointDef.ChildActorName = ChildLinkName.TrimStartAndEnd();
	JointDef.JointName = BuildAutoJointName(JointDef);
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointType(const int32 ChildDocIndex, const int32 JointIndex, const FString& JointTypeName)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	ECadImportJointType ParsedType = ECadImportJointType::Fixed;
	if (!CadImportStringUtils::TryParseJointTypeString(JointTypeName, ParsedType))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].JointType = ParsedType;
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointAxisX(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Axis.X = FMath::Clamp(Value, 0.0f, 1.0f);
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointAxisY(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Axis.Y = FMath::Clamp(Value, 0.0f, 1.0f);
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointAxisZ(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Axis.Z = FMath::Clamp(Value, 0.0f, 1.0f);
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointLimitEnabled(const int32 ChildDocIndex, const int32 JointIndex, const bool bEnabled)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.bHasLimit = bEnabled;
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointLimitLower(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Lower = Value;
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointLimitUpper(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Upper = Value;
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointLimitEffort(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Effort = Value;
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::SetJointLimitVelocity(const int32 ChildDocIndex, const int32 JointIndex, const float Value)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	EditableJointChildren[ChildDocIndex].ChildDocument.Joints[JointIndex].Limit.Velocity = Value;
	RedrawJointPreviewLines();
}

bool SCadWorkflowWizard::ValidateEditedJointDefinitions(FString& OutError) const
{
	OutError.Reset();

	for (const FEditableJointChildState& ChildState : EditableJointChildren)
	{
		TSet<FString> AvailableLinks;
		for (const FCadChildLinkDef& LinkDef : ChildState.ChildDocument.Links)
		{
			const FString LinkName = LinkDef.LinkName.TrimStartAndEnd();
			if (!LinkName.IsEmpty())
			{
				AvailableLinks.Add(LinkName);
			}
		}

		for (const FCadChildJointDef& JointDef : ChildState.ChildDocument.Joints)
		{
			const FString ParentName = JointDef.ParentActorName.TrimStartAndEnd();
			const FString ChildName = JointDef.ChildActorName.TrimStartAndEnd();
			if (ChildName.IsEmpty())
			{
				OutError = FString::Printf(TEXT("Child '%s' has a joint with an empty child link."), *ChildState.ChildDocument.ChildActorName);
				return false;
			}
			if (!ParentName.IsEmpty() && ParentName == ChildName)
			{
				OutError = FString::Printf(TEXT("Child '%s' joint '%s' uses the same parent and child link."), *ChildState.ChildDocument.ChildActorName, *BuildAutoJointName(JointDef));
				return false;
			}
			if (!ParentName.IsEmpty() && !AvailableLinks.Contains(ParentName))
			{
				OutError = FString::Printf(TEXT("Child '%s' joint parent link '%s' was not found in links[]."), *ChildState.ChildDocument.ChildActorName, *ParentName);
				return false;
			}
			if (!AvailableLinks.Contains(ChildName))
			{
				OutError = FString::Printf(TEXT("Child '%s' joint child link '%s' was not found in links[]."), *ChildState.ChildDocument.ChildActorName, *ChildName);
				return false;
			}
		}
	}

	return true;
}

bool SCadWorkflowWizard::TryApplyEditedChildJsonOverrides(FString& OutError) const
{
	OutError.Reset();

	if (EditableJointChildren.Num() == 0)
	{
		return true;
	}

	const FString ChildJsonFolderPath = ChildJsonResult.ChildJsonFolderPath.TrimStartAndEnd();
	if (ChildJsonFolderPath.IsEmpty())
	{
		OutError = TEXT("Child json folder path is empty while applying edited joint overrides.");
		return false;
	}

	for (const FEditableJointChildState& ChildState : EditableJointChildren)
	{
		FString ChildFileName = ChildState.ChildEntry.ChildJsonFileName.TrimStartAndEnd();
		if (ChildFileName.IsEmpty())
		{
			const FString SafeActorName = FPaths::MakeValidFileName(ChildState.ChildEntry.ActorName);
			ChildFileName = FString::Printf(TEXT("%s.json"), SafeActorName.IsEmpty() ? TEXT("Child") : *SafeActorName);
		}

		const FString OutputPath = FPaths::Combine(ChildJsonFolderPath, ChildFileName);
		FString SerializedChildJson;
		if (!CadChildDocExporter::TrySerializeChildDocument(ChildState.ChildDocument, SerializedChildJson, OutError))
		{
			OutError = FString::Printf(TEXT("Failed to serialize edited child json for '%s': %s"), *ChildState.ChildDocument.ChildActorName, *OutError);
			return false;
		}
		if (!FFileHelper::SaveStringToFile(SerializedChildJson, *OutputPath))
		{
			OutError = FString::Printf(TEXT("Failed to write edited child json file: %s"), *OutputPath);
			return false;
		}
	}

	return true;
}

void SCadWorkflowWizard::ClearJointPreviewLines()
{
	const AActor* MasterActor = ConfirmedSelection.MasterActor.Get();
	UWorld* World = MasterActor ? MasterActor->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	FlushPersistentDebugLines(World);
	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void SCadWorkflowWizard::SetJointDebugDrawEnabled(const int32 ChildDocIndex, const int32 JointIndex, const bool bEnabled)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].JointDebugDrawEnabled.IsValidIndex(JointIndex))
	{
		return;
	}

	for (FEditableJointChildState& ChildState : EditableJointChildren)
	{
		for (bool& bJointEnabled : ChildState.JointDebugDrawEnabled)
		{
			bJointEnabled = false;
		}
	}

	if (bEnabled)
	{
		EditableJointChildren[ChildDocIndex].JointDebugDrawEnabled[JointIndex] = true;
	}

	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::RunJointLimitTest(const int32 ChildDocIndex, const int32 JointIndex)
{
	if (!EditableJointChildren.IsValidIndex(ChildDocIndex) ||
		!EditableJointChildren[ChildDocIndex].ChildDocument.Joints.IsValidIndex(JointIndex))
	{
		return;
	}

	const FEditableJointChildState& ChildState = EditableJointChildren[ChildDocIndex];
	const FCadChildJointDef& JointDef = ChildState.ChildDocument.Joints[JointIndex];
	if (!JointTypeUsesAxis(JointDef.JointType))
	{
		return;
	}

	AActor* ChildRootActor = CadActorHierarchyUtils::FindByPath(ChildState.ChildEntry.ActorPath);
	if (!ChildRootActor)
	{
		return;
	}

	const FString ChildName = JointDef.ChildActorName.TrimStartAndEnd();
	AActor* TargetActor = FindNonStaticDescendantActorByName(ChildRootActor, ChildName);
	if (!TargetActor)
	{
		return;
	}

	const float PreviewLength = 40.0f;
	const float PreviewAngle = 180.0f;

	StopJointTestAnimation(true);

	FJointTestAnimationState AnimationState;
	AnimationState.TargetActor = TargetActor;
	USceneComponent* TargetRootComponent = TargetActor->GetRootComponent();
	if (!TargetRootComponent)
	{
		return;
	}

	AnimationState.OriginalRelativeTransform = TargetRootComponent->GetRelativeTransform();
	AnimationState.LocalAxis = JointDef.Axis.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	AnimationState.JointType = JointDef.JointType;
	AnimationState.ElapsedSeconds = 0.0f;

	if (JointDef.Limit.bHasLimit)
	{
		AnimationState.Lower = JointDef.Limit.Lower;
		AnimationState.Upper = JointDef.Limit.Upper;
	}
	else if (JointDef.JointType == ECadImportJointType::Prismatic)
	{
		AnimationState.Lower = -PreviewLength;
		AnimationState.Upper = PreviewLength;
	}
	else
	{
		AnimationState.Lower = -PreviewAngle;
		AnimationState.Upper = PreviewAngle;
	}

	JointTestAnimation = AnimationState;
	RedrawJointPreviewLines();
}

void SCadWorkflowWizard::StopJointTestAnimation(const bool bRestoreTransform)
{
	if (!JointTestAnimation.IsSet())
	{
		return;
	}

	const FJointTestAnimationState AnimationState = JointTestAnimation.GetValue();
	JointTestAnimation.Reset();

	if (bRestoreTransform)
	{
		if (AActor* TargetActor = AnimationState.TargetActor.Get())
		{
			if (USceneComponent* TargetRootComponent = TargetActor->GetRootComponent())
			{
				TargetRootComponent->SetRelativeTransform(AnimationState.OriginalRelativeTransform);
			}
		}
	}

	RedrawJointPreviewLines();
	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void SCadWorkflowWizard::RedrawJointPreviewLines()
{
	ClearJointPreviewLines();

	const AActor* MasterActor = ConfirmedSelection.MasterActor.Get();
	UWorld* World = MasterActor ? MasterActor->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	const UCadImporterEditorUserSettings* Settings = GetDefault<UCadImporterEditorUserSettings>();
	if (Settings && !Settings->bDrawJointPreviewLines)
	{
		return;
	}

	const uint8 DepthPriority = (Settings && Settings->bDrawJointPreviewLinesInForeground) ? 1 : 0;
	const float LineThickness = (Settings && Settings->JointPreviewLineThickness > 0.0f)
		? Settings->JointPreviewLineThickness
		: 4.0f;
	const float AxisDrawLength = 40.0f;
	const float DefaultRevoluteRadius = 35.0f;
	const float DefaultPrismaticHalfLength = 25.0f;
	const float LimitMarkerHalfSize = 8.0f;
	const float BoundsLineThickness = 1.0f;
	const float ActorAxesLength = 12.0f;
	const FColor GuideLineColor = FColor(210, 210, 210, 255);

	for (const FEditableJointChildState& ChildState : EditableJointChildren)
	{
		AActor* ChildRootActor = CadActorHierarchyUtils::FindByPath(ChildState.ChildEntry.ActorPath);
		if (!ChildRootActor)
		{
			continue;
		}

		TMap<FString, FVector> LinkLocations;
		CollectNonStaticDescendantActorLocations(ChildRootActor, ChildRootActor, LinkLocations);
		TMap<FString, FTransform> LinkTransforms;
		TArray<AActor*> Descendants;
		CadActorHierarchyUtils::GetSortedAttachedChildren(ChildRootActor, Descendants, true);
		for (AActor* DescendantActor : Descendants)
		{
			if (!DescendantActor || DescendantActor->IsA<AStaticMeshActor>())
			{
				continue;
			}

			LinkTransforms.Add(DescendantActor->GetActorNameOrLabel(), DescendantActor->GetActorTransform());
		}
		const FVector WorldAnchorLocation = ChildRootActor->GetActorLocation();
		TSet<FString> BoundsDrawnActors;

		for (int32 JointIndex = 0; JointIndex < ChildState.ChildDocument.Joints.Num(); ++JointIndex)
		{
			if (!ChildState.JointDebugDrawEnabled.IsValidIndex(JointIndex) || !ChildState.JointDebugDrawEnabled[JointIndex])
			{
				continue;
			}

			const FCadChildJointDef& JointDef = ChildState.ChildDocument.Joints[JointIndex];
			const FString ChildName = JointDef.ChildActorName.TrimStartAndEnd();
			if (ChildName.IsEmpty())
			{
				continue;
			}

			const FVector* ChildLocation = LinkLocations.Find(ChildName);
			AActor* ChildActor = FindNonStaticDescendantActorByName(ChildRootActor, ChildName);
			if (!ChildLocation)
			{
				continue;
			}

			const FString ParentName = JointDef.ParentActorName.TrimStartAndEnd();
			const FVector* ParentLocation = ParentName.IsEmpty() ? nullptr : LinkLocations.Find(ParentName);
			AActor* ParentActor = ParentName.IsEmpty() ? nullptr : FindNonStaticDescendantActorByName(ChildRootActor, ParentName);
			const FVector Start = ParentLocation ? *ParentLocation : WorldAnchorLocation;
			const FVector End = *ChildLocation;
			const FColor JointColor = JointTypeToPreviewColor(Settings, JointDef.JointType);
			FColor ParentBoundsColor = FColor(110, 220, 140, 180);
			FColor ChildBoundsColor = FColor(90, 170, 255, 180);
			DrawDebugLine(
				World,
				Start,
				End,
				GuideLineColor,
				true,
				-1.0f,
				DepthPriority,
				LineThickness);

			if (ParentActor && !BoundsDrawnActors.Contains(ParentName))
			{
				DrawDebugActorBounds(World, ParentActor, ParentBoundsColor, true, -1.0f, BoundsLineThickness);
				DrawDebugActorAxes(World, ParentActor, ActorAxesLength, true, -1.0f, DepthPriority, BoundsLineThickness);
				BoundsDrawnActors.Add(ParentName);
			}

			if (ChildActor && !BoundsDrawnActors.Contains(ChildName))
			{
				DrawDebugActorBounds(World, ChildActor, ChildBoundsColor, true, -1.0f, BoundsLineThickness);
				if (!(JointTestAnimation.IsSet() && JointTestAnimation.GetValue().TargetActor.Get() == ChildActor))
				{
					DrawDebugActorAxes(World, ChildActor, ActorAxesLength, true, -1.0f, DepthPriority, BoundsLineThickness);
				}
				BoundsDrawnActors.Add(ChildName);
			}

			if (JointTypeUsesAxis(JointDef.JointType))
			{
				FVector AxisDirection = JointDef.Axis.GetSafeNormal();
				if (AxisDirection.IsNearlyZero())
				{
					AxisDirection = FVector::UpVector;
				}
				if (const FTransform* ChildTransform = LinkTransforms.Find(ChildName))
				{
					AxisDirection = ChildTransform->TransformVectorNoScale(AxisDirection).GetSafeNormal();
					if (AxisDirection.IsNearlyZero())
					{
						AxisDirection = FVector::UpVector;
					}
				}

				DrawDebugDirectionalArrow(
					World,
					End,
					End + (AxisDirection * AxisDrawLength),
					12.0f,
					JointColor,
					true,
					-1.0f,
					DepthPriority,
					LineThickness);

				if (JointDef.JointType == ECadImportJointType::Revolute)
				{
					const float StartAngleDegrees = JointDef.Limit.bHasLimit ? JointDef.Limit.Lower : 0.0f;
					const float EndAngleDegrees = JointDef.Limit.bHasLimit ? JointDef.Limit.Upper : 360.0f;
					DrawDebugArc(
						World,
						End,
						AxisDirection,
						DefaultRevoluteRadius,
						StartAngleDegrees,
						EndAngleDegrees,
						JointColor,
						true,
						-1.0f,
						DepthPriority,
						LineThickness);
				}
				else if (JointDef.JointType == ECadImportJointType::Prismatic)
				{
					const float Lower = JointDef.Limit.bHasLimit ? JointDef.Limit.Lower : -DefaultPrismaticHalfLength;
					const float Upper = JointDef.Limit.bHasLimit ? JointDef.Limit.Upper : DefaultPrismaticHalfLength;
					const FVector RangeStart = End + (AxisDirection * Lower);
					const FVector RangeEnd = End + (AxisDirection * Upper);
					DrawDebugLine(
						World,
						RangeStart,
						RangeEnd,
						JointColor,
						true,
						-1.0f,
						DepthPriority,
						LineThickness);

					FVector MarkerAxisX = FVector::RightVector;
					FVector MarkerAxisY = FVector::ForwardVector;
					BuildAxisBasis(AxisDirection, MarkerAxisX, MarkerAxisY);
					DrawDebugLine(
						World,
						RangeStart - (MarkerAxisX * LimitMarkerHalfSize),
						RangeStart + (MarkerAxisX * LimitMarkerHalfSize),
						JointColor,
						true,
						-1.0f,
						DepthPriority,
						LineThickness);
					DrawDebugLine(
						World,
						RangeEnd - (MarkerAxisX * LimitMarkerHalfSize),
						RangeEnd + (MarkerAxisX * LimitMarkerHalfSize),
						JointColor,
						true,
						-1.0f,
						DepthPriority,
						LineThickness);
					DrawDebugLine(
						World,
						RangeStart - (MarkerAxisY * LimitMarkerHalfSize),
						RangeStart + (MarkerAxisY * LimitMarkerHalfSize),
						JointColor,
						true,
						-1.0f,
						DepthPriority,
						LineThickness);
					DrawDebugLine(
						World,
						RangeEnd - (MarkerAxisY * LimitMarkerHalfSize),
						RangeEnd + (MarkerAxisY * LimitMarkerHalfSize),
						JointColor,
						true,
						-1.0f,
						DepthPriority,
						LineThickness);
				}
			}
		}
	}

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}


