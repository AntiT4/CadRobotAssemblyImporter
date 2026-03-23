#include "UI/MasterWorkflowWizard.h"

#include "ImportRunner.h"
#include "CadImporterEditor.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FString MasterChildActorTypeToUiString(const ECadMasterChildActorType ActorType)
	{
		return ActorType == ECadMasterChildActorType::Movable ? TEXT("movable") : TEXT("static");
	}

	bool TryParseMasterChildActorTypeFromUiString(const FString& RawType, ECadMasterChildActorType& OutType)
	{
		if (RawType.Equals(TEXT("movable"), ESearchCase::IgnoreCase))
		{
			OutType = ECadMasterChildActorType::Movable;
			return true;
		}

		if (RawType.Equals(TEXT("static"), ESearchCase::IgnoreCase))
		{
			OutType = ECadMasterChildActorType::Static;
			return true;
		}

		return false;
	}

	void CollectActorHierarchyForVisibility(AActor* RootActor, TArray<AActor*>& OutActors)
	{
		OutActors.Reset();
		if (!RootActor)
		{
			return;
		}

		OutActors.Add(RootActor);

		TArray<AActor*> AttachedActors;
		RootActor->GetAttachedActors(AttachedActors, true, true);
		for (AActor* AttachedActor : AttachedActors)
		{
			if (AttachedActor)
			{
				OutActors.Add(AttachedActor);
			}
		}
	}

	struct FAnyDirectoryEntryVisitor final : IPlatformFile::FDirectoryVisitor
	{
		bool bFoundAnyEntry = false;

		virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
		{
			static_cast<void>(FilenameOrDirectory);
			static_cast<void>(bIsDirectory);
			bFoundAnyEntry = true;
			return false;
		}
	};

	bool TryNormalizeWorkspacePath(const FString& WorkspaceInput, FString& OutTrimmedWorkspace, FString& OutNormalizedWorkspace, FString& OutError)
	{
		OutTrimmedWorkspace.Reset();
		OutNormalizedWorkspace.Reset();
		OutError.Reset();

		OutTrimmedWorkspace = WorkspaceInput.TrimStartAndEnd();
		if (OutTrimmedWorkspace.IsEmpty())
		{
			OutError = TEXT("Workspace path is empty. Apply a valid workspace folder first.");
			return false;
		}

		FString NormalizedWorkspace = OutTrimmedWorkspace;
		if (FPaths::IsRelative(NormalizedWorkspace))
		{
			NormalizedWorkspace = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), NormalizedWorkspace));
		}
		else
		{
			NormalizedWorkspace = FPaths::ConvertRelativePathToFull(NormalizedWorkspace);
		}
		FPaths::NormalizeDirectoryName(NormalizedWorkspace);

		if (NormalizedWorkspace.IsEmpty())
		{
			OutError = TEXT("Workspace path normalization produced an empty value.");
			return false;
		}

		OutNormalizedWorkspace = MoveTemp(NormalizedWorkspace);
		return true;
	}

	bool TryValidateWorkspaceForApply(const FString& WorkspaceInput, FString& OutNormalizedWorkspace, FString& OutError)
	{
		FString TrimmedWorkspace;
		if (!TryNormalizeWorkspacePath(WorkspaceInput, TrimmedWorkspace, OutNormalizedWorkspace, OutError))
		{
			return false;
		}

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		const bool bWorkspaceDirectoryExists = PlatformFile.DirectoryExists(*OutNormalizedWorkspace);

		bool bWorkspaceDirectoryEmpty = true;
		if (bWorkspaceDirectoryExists)
		{
			FAnyDirectoryEntryVisitor Visitor;
			PlatformFile.IterateDirectory(*OutNormalizedWorkspace, Visitor);
			bWorkspaceDirectoryEmpty = !Visitor.bFoundAnyEntry;
		}

		UE_LOG(
			LogCadImporter,
			Display,
			TEXT("Workspace validation before apply: raw='%s', trimmed='%s', normalized='%s', exists=%s, empty=%s"),
			*WorkspaceInput,
			*TrimmedWorkspace,
			*OutNormalizedWorkspace,
			bWorkspaceDirectoryExists ? TEXT("true") : TEXT("false"),
			bWorkspaceDirectoryEmpty ? TEXT("true") : TEXT("false"));

		if (bWorkspaceDirectoryExists && !bWorkspaceDirectoryEmpty)
		{
			OutError = FString::Printf(
				TEXT("Workspace folder is not empty. Choose an empty folder or create a new one.\npath=%s"),
				*OutNormalizedWorkspace);
			return false;
		}

		return true;
	}

	bool TryValidateWorkspaceForGeneration(const FString& WorkspaceInput, FString& OutNormalizedWorkspace, FString& OutError)
	{
		FString TrimmedWorkspace;
		if (!TryNormalizeWorkspacePath(WorkspaceInput, TrimmedWorkspace, OutNormalizedWorkspace, OutError))
		{
			return false;
		}

		const bool bWorkspaceDirectoryExists = IFileManager::Get().DirectoryExists(*OutNormalizedWorkspace);
		UE_LOG(
			LogCadImporter,
			Display,
			TEXT("Workspace validation before generation: raw='%s', trimmed='%s', normalized='%s', exists=%s"),
			*WorkspaceInput,
			*TrimmedWorkspace,
			*OutNormalizedWorkspace,
			bWorkspaceDirectoryExists ? TEXT("true") : TEXT("false"));

		return true;
	}
}

void SCadMasterWorkflowWizard::Construct(const FArguments& InArgs)
{
	Runner = InArgs._Runner;
	WorkspaceFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	StatusMessage = TEXT("Step 1: Set workspace folder.");
	SelectionPreviewText = TEXT("선택된 액터 없음");
	ActiveStepIndex = 0;
	ChildTypeOptions.Reset();
	ChildTypeOptions.Add(MakeShared<FString>(TEXT("static")));
	ChildTypeOptions.Add(MakeShared<FString>(TEXT("movable")));
	ImportOptions = FCadFbxImportOptions();
	ImportOptions.bShowDialog = false;
	LastSelectionSignature.Reset();
	RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SCadMasterWorkflowWizard::HandleSelectionPolling));

	ChildSlot
	[
		SNew(SBorder)
		.Padding(12.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					static const TCHAR* Labels[] =
					{
						TEXT("1/4 Workspace"),
						TEXT("2/4 Confirm Master Actor"),
						TEXT("3/4 Child Type + Generate JSON"),
						TEXT("4/4 Build Actor"),
						TEXT("Completed")
					};
					const int32 SafeIndex = FMath::Clamp(ActiveStepIndex, 0, 4);
					return FText::FromString(FString::Printf(TEXT("Master Workflow Wizard - %s"), Labels[SafeIndex]));
				})
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]()
				{
					return ActiveStepIndex;
				})

				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Set the workspace folder where master/child json files will be created.")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SAssignNew(WorkspaceTextBox, SEditableTextBox)
						.Text_Lambda([this]()
						{
							return FText::FromString(WorkspaceFolder);
						})
						.OnTextChanged_Lambda([this](const FText& NewText)
						{
							WorkspaceFolder = NewText.ToString();
						})
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Browse Workspace")))
							.OnClicked(this, &SCadMasterWorkflowWizard::HandleBrowseWorkspace)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Apply Workspace")))
							.OnClicked(this, &SCadMasterWorkflowWizard::HandleApplyWorkspace)
						]
					]
				]

				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Select one actor that will become the master in the level, then confirm it.")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("선택 액터 / 직계 자손 (1초 간격 자동 갱신, 선택 변경시에만 자손 재검색)")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SBox)
						.MinDesiredHeight(120.0f)
						[
							SNew(SMultiLineEditableTextBox)
							.IsReadOnly(true)
							.Text_Lambda([this]()
							{
								return FText::FromString(SelectionPreviewText);
							})
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return FText::FromString(FString::Printf(TEXT("Workspace: %s"), *WorkspaceFolder));
						})
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Back")))
							.OnClicked(this, &SCadMasterWorkflowWizard::HandleBack)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Master Actor Confirm")))
							.OnClicked(this, &SCadMasterWorkflowWizard::HandleConfirmMasterActor)
						]
					]
				]

				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Set child actor types in UI, then generate master/child json files.")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							const AActor* MasterActor = ConfirmedSelectionResult.MasterCandidateActor.Get();
							const FString MasterName = MasterActor ? MasterActor->GetActorNameOrLabel() : TEXT("(not confirmed)");
							return FText::FromString(FString::Printf(TEXT("Confirmed Master: %s"), *MasterName));
						})
					]

					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(ChildTypeRowsBox, SVerticalBox)
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Back")))
							.OnClicked(this, &SCadMasterWorkflowWizard::HandleBack)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Generate JSON")))
							.OnClicked(this, &SCadMasterWorkflowWizard::HandleGenerateJson)
						]
					]
				]

				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Build blueprint from master/child json and replace actors in level.")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return FText::FromString(FString::Printf(TEXT("Child Folder: %s"), *WorkflowBuildInput.ChildJsonFolderPath));
						})
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Back")))
							.OnClicked(this, &SCadMasterWorkflowWizard::HandleBack)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("Build Actor")))
							.OnClicked(this, &SCadMasterWorkflowWizard::HandleBuildActor)
						]
					]
				]

				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Master workflow completed.")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Restart Workflow")))
						.OnClicked(this, &SCadMasterWorkflowWizard::HandleRestart)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.MinDesiredHeight(120.0f)
				[
					SNew(SMultiLineEditableTextBox)
					.IsReadOnly(true)
					.Text_Lambda([this]()
					{
						return FText::FromString(StatusMessage);
					})
				]
			]
		]
	];

	RebuildChildTypeRows();
}

EActiveTimerReturnType SCadMasterWorkflowWizard::HandleSelectionPolling(double CurrentTime, float DeltaTime)
{
	static_cast<void>(CurrentTime);
	static_cast<void>(DeltaTime);
	RefreshSelectionPreviewIfChanged();
	return EActiveTimerReturnType::Continue;
}

void SCadMasterWorkflowWizard::RefreshSelectionPreviewIfChanged()
{
	FString CurrentSignature;
	AActor* SingleSelectedActor = nullptr;
	int32 SelectedActorCount = 0;

	if (!GEditor)
	{
		CurrentSignature = TEXT("selection:no_editor");
	}
	else
	{
		USelection* SelectedActors = GEditor->GetSelectedActors();
		if (!SelectedActors)
		{
			CurrentSignature = TEXT("selection:unavailable");
		}
		else
		{
			TArray<AActor*> ActorSelection;
			for (FSelectionIterator It(*SelectedActors); It; ++It)
			{
				if (AActor* SelectedActor = Cast<AActor>(*It))
				{
					ActorSelection.Add(SelectedActor);
				}
			}

			SelectedActorCount = ActorSelection.Num();
			if (SelectedActorCount == 1)
			{
				SingleSelectedActor = ActorSelection[0];
				CurrentSignature = FString::Printf(TEXT("selection:single:%s"), *SingleSelectedActor->GetPathName());
			}
			else if (SelectedActorCount == 0)
			{
				CurrentSignature = TEXT("selection:none");
			}
			else
			{
				ActorSelection.Sort([](const AActor& Left, const AActor& Right)
				{
					return Left.GetActorNameOrLabel() < Right.GetActorNameOrLabel();
				});

				const FString FirstActorPath = ActorSelection[0] ? ActorSelection[0]->GetPathName() : TEXT("(none)");
				CurrentSignature = FString::Printf(TEXT("selection:multi:%d:%s"), SelectedActorCount, *FirstActorPath);
			}
		}
	}

	if (CurrentSignature == LastSelectionSignature)
	{
		// Skip descendant lookup when selected actor has not changed.
		return;
	}

	LastSelectionSignature = CurrentSignature;

	if (!GEditor)
	{
		SelectionPreviewText = TEXT("에디터 컨텍스트를 찾을 수 없습니다.");
		return;
	}

	if (SelectedActorCount == 0 || !SingleSelectedActor)
	{
		if (SelectedActorCount > 1)
		{
			SelectionPreviewText = FString::Printf(TEXT("여러 액터가 선택됨 (%d)\n단일 액터를 선택해 주세요."), SelectedActorCount);
		}
		else
		{
			SelectionPreviewText = TEXT("선택된 액터 없음");
		}
		return;
	}

	TArray<AActor*> DirectChildren;
	SingleSelectedActor->GetAttachedActors(DirectChildren, false, false);
	DirectChildren.Sort([](const AActor& Left, const AActor& Right)
	{
		return Left.GetActorNameOrLabel() < Right.GetActorNameOrLabel();
	});

	FString Result = FString::Printf(
		TEXT("Selected: %s\nPath: %s\nDirect Children: %d"),
		*SingleSelectedActor->GetActorNameOrLabel(),
		*SingleSelectedActor->GetPathName(),
		DirectChildren.Num());

	for (AActor* ChildActor : DirectChildren)
	{
		if (!ChildActor)
		{
			continue;
		}

		Result += FString::Printf(TEXT("\n- %s"), *ChildActor->GetActorNameOrLabel());
	}

	SelectionPreviewText = MoveTemp(Result);
}

void SCadMasterWorkflowWizard::RebuildChildTypeRows()
{
	if (!ChildTypeRowsBox.IsValid())
	{
		return;
	}

	ChildTypeRowsBox->ClearChildren();

	if (EditableChildren.Num() == 0)
	{
		ChildTypeRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No confirmed direct children.")))
		];
		return;
	}

	for (int32 ChildIndex = 0; ChildIndex < EditableChildren.Num(); ++ChildIndex)
	{
		ChildTypeRowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(EditableChildren[ChildIndex].ActorName))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&ChildTypeOptions)
				.OnGenerateWidget_Lambda([](const TSharedPtr<FString> Item)
				{
					return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
				})
				.OnSelectionChanged_Lambda([this, ChildIndex](const TSharedPtr<FString> SelectedType, ESelectInfo::Type)
				{
					if (!SelectedType.IsValid())
					{
						return;
					}
					HandleChildTypeSelectionChanged(ChildIndex, *SelectedType);
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this, ChildIndex]()
					{
						if (!EditableChildren.IsValidIndex(ChildIndex))
						{
							return FText::FromString(TEXT("static"));
						}
						return FText::FromString(MasterChildActorTypeToUiString(EditableChildren[ChildIndex].ActorType));
					})
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text_Lambda([this, ChildIndex]()
				{
					return FText::FromString(IsChildVisibilityIsolated(ChildIndex) ? TEXT("Restore") : TEXT("Show"));
				})
				.OnClicked_Lambda([this, ChildIndex]()
				{
					return HandleToggleChildVisibility(ChildIndex);
				})
			]
		];
	}
}

void SCadMasterWorkflowWizard::HandleChildTypeSelectionChanged(const int32 ChildIndex, const FString& SelectedType)
{
	if (!EditableChildren.IsValidIndex(ChildIndex))
	{
		return;
	}

	ECadMasterChildActorType ParsedType = ECadMasterChildActorType::Static;
	if (!TryParseMasterChildActorTypeFromUiString(SelectedType, ParsedType))
	{
		return;
	}

	EditableChildren[ChildIndex].ActorType = ParsedType;
}

AActor* SCadMasterWorkflowWizard::ResolveChildActorByPath(const FCadMasterChildEntry& ChildEntry) const
{
	if (ChildEntry.ActorPath.TrimStartAndEnd().IsEmpty())
	{
		return nullptr;
	}

	return FindObject<AActor>(nullptr, *ChildEntry.ActorPath);
}

void SCadMasterWorkflowWizard::CaptureChildVisibilitySnapshot()
{
	ChildVisibilitySnapshot.Reset();
	TArray<AActor*> HierarchyActors;

	for (const FCadMasterChildEntry& ChildEntry : EditableChildren)
	{
		AActor* ChildActor = ResolveChildActorByPath(ChildEntry);
		if (!ChildActor)
		{
			continue;
		}

		CollectActorHierarchyForVisibility(ChildActor, HierarchyActors);
		for (AActor* HierarchyActor : HierarchyActors)
		{
			if (!HierarchyActor)
			{
				continue;
			}

			const FString ActorPath = HierarchyActor->GetPathName();
			if (ChildVisibilitySnapshot.Contains(ActorPath))
			{
				continue;
			}

			ChildVisibilitySnapshot.Add(ActorPath, HierarchyActor->IsTemporarilyHiddenInEditor());
		}
	}
}

void SCadMasterWorkflowWizard::ApplyChildVisibilityIsolation(const int32 ChildIndex)
{
	TArray<AActor*> HierarchyActors;

	for (int32 Index = 0; Index < EditableChildren.Num(); ++Index)
	{
		AActor* ChildActor = ResolveChildActorByPath(EditableChildren[Index]);
		if (!ChildActor)
		{
			continue;
		}

		const bool bHideInEditor = (Index != ChildIndex);
		CollectActorHierarchyForVisibility(ChildActor, HierarchyActors);
		for (AActor* HierarchyActor : HierarchyActors)
		{
			if (HierarchyActor)
			{
				HierarchyActor->SetIsTemporarilyHiddenInEditor(bHideInEditor);
			}
		}
	}

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void SCadMasterWorkflowWizard::RestoreChildVisibility()
{
	for (const TPair<FString, bool>& SnapshotEntry : ChildVisibilitySnapshot)
	{
		AActor* Actor = FindObject<AActor>(nullptr, *SnapshotEntry.Key);
		if (!Actor)
		{
			continue;
		}

		Actor->SetIsTemporarilyHiddenInEditor(SnapshotEntry.Value);
	}

	ChildVisibilitySnapshot.Reset();
	IsolatedChildIndex = INDEX_NONE;

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

bool SCadMasterWorkflowWizard::IsChildVisibilityIsolated(const int32 ChildIndex) const
{
	return ChildVisibilitySnapshot.Num() > 0 && IsolatedChildIndex == ChildIndex;
}

FReply SCadMasterWorkflowWizard::HandleToggleChildVisibility(const int32 ChildIndex)
{
	if (!EditableChildren.IsValidIndex(ChildIndex))
	{
		return FReply::Handled();
	}

	if (IsChildVisibilityIsolated(ChildIndex))
	{
		RestoreChildVisibility();
		const AActor* MasterActor = ConfirmedSelectionResult.MasterCandidateActor.Get();
		SetStatusMessage(FString::Printf(
			TEXT("Visibility restored for child actors under master '%s'."),
			MasterActor ? *MasterActor->GetActorNameOrLabel() : TEXT("(none)")));
		return FReply::Handled();
	}

	if (ChildVisibilitySnapshot.Num() == 0)
	{
		CaptureChildVisibilitySnapshot();
	}

	ApplyChildVisibilityIsolation(ChildIndex);
	IsolatedChildIndex = ChildIndex;
	SetStatusMessage(FString::Printf(TEXT("Isolated child visibility: %s"), *EditableChildren[ChildIndex].ActorName));
	return FReply::Handled();
}

FReply SCadMasterWorkflowWizard::HandleBrowseWorkspace()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		SetStatusMessage(TEXT("Workspace browse failed: Desktop platform module is unavailable."));
		return FReply::Handled();
	}

	const void* ParentWindowHandle = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	}

	FString PickedFolder;
	const bool bOpened = DesktopPlatform->OpenDirectoryDialog(
		const_cast<void*>(ParentWindowHandle),
		TEXT("Select Master Workflow Workspace"),
		WorkspaceFolder,
		PickedFolder);

	if (bOpened && !PickedFolder.TrimStartAndEnd().IsEmpty())
	{
		WorkspaceFolder = PickedFolder;
		if (WorkspaceTextBox.IsValid())
		{
			WorkspaceTextBox->SetText(FText::FromString(WorkspaceFolder));
		}
		SetStatusMessage(FString::Printf(TEXT("Workspace selected: %s"), *WorkspaceFolder));
	}

	return FReply::Handled();
}

FReply SCadMasterWorkflowWizard::HandleApplyWorkspace()
{
	FString NormalizedWorkspace;
	FString WorkspaceValidationError;
	if (!TryValidateWorkspaceForApply(WorkspaceFolder, NormalizedWorkspace, WorkspaceValidationError))
	{
		SetStatusMessage(FString::Printf(TEXT("Workspace apply failed:\n%s"), *WorkspaceValidationError));
		return FReply::Handled();
	}

	WorkspaceFolder = MoveTemp(NormalizedWorkspace);
	if (WorkspaceTextBox.IsValid())
	{
		WorkspaceTextBox->SetText(FText::FromString(WorkspaceFolder));
	}

	MoveToStep(1);
	SetStatusMessage(FString::Printf(TEXT("Workspace applied: %s"), *WorkspaceFolder));
	return FReply::Handled();
}

FReply SCadMasterWorkflowWizard::HandleBack()
{
	const int32 NextStep = FMath::Max(0, ActiveStepIndex - 1);
	if (ChildVisibilitySnapshot.Num() > 0 && NextStep < 2)
	{
		RestoreChildVisibility();
	}

	MoveToStep(NextStep);
	return FReply::Handled();
}

FReply SCadMasterWorkflowWizard::HandleConfirmMasterActor()
{
	if (ChildVisibilitySnapshot.Num() > 0)
	{
		RestoreChildVisibility();
	}

	FString NormalizedWorkspace;
	FString WorkspaceValidationError;
	if (!TryValidateWorkspaceForGeneration(WorkspaceFolder, NormalizedWorkspace, WorkspaceValidationError))
	{
		SetStatusMessage(FString::Printf(TEXT("Master actor confirmation failed:\n%s"), *WorkspaceValidationError));
		return FReply::Handled();
	}

	WorkspaceFolder = MoveTemp(NormalizedWorkspace);
	if (WorkspaceTextBox.IsValid())
	{
		WorkspaceTextBox->SetText(FText::FromString(WorkspaceFolder));
	}

	FString Error;
	FCadMasterActorSelectionResult SelectionResult;
	if (!CadMasterJsonActorCollector::TryCollectFromSelection(SelectionResult, Error))
	{
		SetStatusMessage(FString::Printf(TEXT("Master actor confirmation failed:\n%s"), *Error));
		return FReply::Handled();
	}

	ConfirmedSelectionResult = SelectionResult;
	EditableChildren = SelectionResult.DirectChildren;
	ChildVisibilitySnapshot.Reset();
	IsolatedChildIndex = INDEX_NONE;
	MasterGenerationResult = FCadMasterJsonGenerationResult();
	ChildExtractionResult = FCadChildJsonExtractionResult();
	WorkflowBuildInput = FCadMasterWorkflowBuildInput();
	RebuildChildTypeRows();

	const AActor* ConfirmedMasterActor = ConfirmedSelectionResult.MasterCandidateActor.Get();
	MoveToStep(2);
	SetStatusMessage(FString::Printf(
		TEXT("Master actor confirmed.\nmaster=%s\nchildren=%d\n다음 단계에서 child actor_type을 선택 후 Generate JSON을 실행하세요."),
		ConfirmedMasterActor ? *ConfirmedMasterActor->GetActorNameOrLabel() : TEXT("(none)"),
		EditableChildren.Num()));
	return FReply::Handled();
}

FReply SCadMasterWorkflowWizard::HandleGenerateJson()
{
	if (!ConfirmedSelectionResult.IsValid())
	{
		SetStatusMessage(TEXT("Generate JSON failed: confirm master actor first."));
		return FReply::Handled();
	}

	if (EditableChildren.Num() == 0)
	{
		SetStatusMessage(TEXT("Generate JSON failed: confirmed master has no direct children."));
		return FReply::Handled();
	}

	FString NormalizedWorkspace;
	FString WorkspaceValidationError;
	if (!TryValidateWorkspaceForGeneration(WorkspaceFolder, NormalizedWorkspace, WorkspaceValidationError))
	{
		SetStatusMessage(FString::Printf(TEXT("Generate JSON failed:\n%s"), *WorkspaceValidationError));
		return FReply::Handled();
	}

	WorkspaceFolder = MoveTemp(NormalizedWorkspace);
	if (WorkspaceTextBox.IsValid())
	{
		WorkspaceTextBox->SetText(FText::FromString(WorkspaceFolder));
	}

	FCadMasterActorSelectionResult SelectionForGeneration = ConfirmedSelectionResult;
	SelectionForGeneration.DirectChildren = EditableChildren;

	FString Error;
	if (!CadMasterJsonGenerator::TryGenerateAndWriteFromSelectionResult(SelectionForGeneration, WorkspaceFolder, MasterGenerationResult, Error))
	{
		SetStatusMessage(FString::Printf(TEXT("Generate JSON failed:\n%s"), *Error));
		return FReply::Handled();
	}

	const FString MasterJsonPath = MasterGenerationResult.WorkspacePaths.MasterJsonPath;
	if (MasterJsonPath.IsEmpty())
	{
		SetStatusMessage(TEXT("Generate JSON failed: master json path is empty."));
		return FReply::Handled();
	}

	if (!CadMasterChildJsonExtractor::TryExtractChildJsonFilesFromDocument(
		MasterJsonPath,
		MasterGenerationResult.Document,
		ChildExtractionResult,
		Error))
	{
		SetStatusMessage(FString::Printf(TEXT("Generate JSON failed while creating child json files:\n%s"), *Error));
		return FReply::Handled();
	}

	WorkflowBuildInput = ChildExtractionResult.BuildInput;
	if (WorkflowBuildInput.ContentRootPath.TrimStartAndEnd().IsEmpty())
	{
		WorkflowBuildInput.ContentRootPath = MasterGenerationResult.WorkspacePaths.ContentRootPath;
	}

	if (ChildVisibilitySnapshot.Num() > 0)
	{
		RestoreChildVisibility();
	}

	MoveToStep(3);
	SetStatusMessage(FString::Printf(
		TEXT("Master/Child JSON generated.\nmaster=%s\nchild_count=%d\nchild_folder=%s"),
		*MasterGenerationResult.WorkspacePaths.MasterJsonPath,
		ChildExtractionResult.GeneratedChildJsonPaths.Num(),
		*ChildExtractionResult.ChildJsonFolderPath));
	return FReply::Handled();
}

FReply SCadMasterWorkflowWizard::HandleBuildActor()
{
	if (!Runner.IsValid())
	{
		SetStatusMessage(TEXT("Build failed: importer runner is unavailable."));
		return FReply::Handled();
	}

	if (WorkflowBuildInput.MasterJsonPath.TrimStartAndEnd().IsEmpty() ||
		WorkflowBuildInput.ChildJsonFolderPath.TrimStartAndEnd().IsEmpty())
	{
		SetStatusMessage(TEXT("Build failed: workflow input is incomplete. Run previous steps first."));
		return FReply::Handled();
	}

	if (!Runner->RunMasterWorkflowImport(WorkflowBuildInput, ImportOptions))
	{
		SetStatusMessage(TEXT("Build failed. Check message log for parser/import/replacement errors."));
		return FReply::Handled();
	}

	MoveToStep(4);
	SetStatusMessage(TEXT("Build completed and level replacement executed."));
	return FReply::Handled();
}

FReply SCadMasterWorkflowWizard::HandleRestart()
{
	if (ChildVisibilitySnapshot.Num() > 0)
	{
		RestoreChildVisibility();
	}

	ConfirmedSelectionResult = FCadMasterActorSelectionResult();
	EditableChildren.Reset();
	ChildVisibilitySnapshot.Reset();
	IsolatedChildIndex = INDEX_NONE;
	MasterGenerationResult = FCadMasterJsonGenerationResult();
	ChildExtractionResult = FCadChildJsonExtractionResult();
	WorkflowBuildInput = FCadMasterWorkflowBuildInput();
	RebuildChildTypeRows();
	MoveToStep(0);
	SetStatusMessage(TEXT("Workflow restarted. Step 1: Set workspace folder."));
	return FReply::Handled();
}

void SCadMasterWorkflowWizard::SetStatusMessage(const FString& InMessage)
{
	StatusMessage = InMessage;
	UE_LOG(LogCadImporter, Display, TEXT("%s"), *InMessage);
}

void SCadMasterWorkflowWizard::MoveToStep(const int32 StepIndex)
{
	ActiveStepIndex = FMath::Clamp(StepIndex, 0, 4);
}
