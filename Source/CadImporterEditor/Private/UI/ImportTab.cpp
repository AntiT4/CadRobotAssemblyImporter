#include "UI/ImportTab.h"

#include "UI/DialogUtils.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

namespace CadImportTabBuilder
{
	TSharedRef<SWidget> BuildImportTabContent(const FCadImportTabBuildArgs& Args)
	{
		return
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Load a CAD JSON config, then click Import.")))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([SelectedJsonPath = Args.SelectedJsonPath]()
				{
					if (SelectedJsonPath->IsEmpty())
					{
						return FText::FromString(TEXT("Config: (none)"));
					}

					return FText::FromString(FString::Printf(TEXT("Config: %s"), **SelectedJsonPath));
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
					.Text(FText::FromString(TEXT("Load Config")))
					.OnClicked_Lambda([Args]()
					{
						FString PickedPath;
						if (Args.SelectJsonFile && Args.SelectJsonFile(PickedPath))
						{
							*Args.SelectedJsonPath = MoveTemp(PickedPath);

							FString PreviewText;
							FString PreviewError;
							if (CadImportDialogUtils::TryBuildPreviewFromJson(*Args.SelectedJsonPath, PreviewText, PreviewError))
							{
								*Args.LinkPreviewText = MoveTemp(PreviewText);
								*Args.bHasValidPreview = true;
							}
							else
							{
								*Args.LinkPreviewText = FString::Printf(TEXT("Failed to build preview:\n%s"), *PreviewError);
								*Args.bHasValidPreview = false;
							}
						}
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Import")))
					.IsEnabled_Lambda([SelectedJsonPath = Args.SelectedJsonPath, bHasValidPreview = Args.bHasValidPreview]()
					{
						return !SelectedJsonPath->IsEmpty() && *bHasValidPreview;
					})
					.OnClicked_Lambda([Args]()
					{
						if (Args.SelectedJsonPath->IsEmpty())
						{
							FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Please load a config file first.")));
							return FReply::Handled();
						}

						FString ValidationError;
						if (!CadImportDialogUtils::TryParseImportOptionTextFields(
							*Args.UniformScaleText,
							*Args.TranslationXText,
							*Args.TranslationYText,
							*Args.TranslationZText,
							*Args.RotationPitchText,
							*Args.RotationYawText,
							*Args.RotationRollText,
							*Args.SelectedImportOptions,
							ValidationError))
						{
							FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ValidationError));
							return FReply::Handled();
						}

						if (Args.RunImport)
						{
							Args.RunImport();
						}
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Clear")))
					.OnClicked_Lambda([Args]()
					{
						if (Args.ClearState)
						{
							Args.ClearState();
						}
						return FReply::Handled();
					})
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("FBX Import Settings")))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([SelectedImportOptions = Args.SelectedImportOptions]()
						{
							return SelectedImportOptions->bConvertScene ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([SelectedImportOptions = Args.SelectedImportOptions](ECheckBoxState State)
						{
							SelectedImportOptions->bConvertScene = (State == ECheckBoxState::Checked);
						})
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Convert Scene")))
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([SelectedImportOptions = Args.SelectedImportOptions]()
						{
							return SelectedImportOptions->bForceFrontXAxis ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([SelectedImportOptions = Args.SelectedImportOptions](ECheckBoxState State)
						{
							SelectedImportOptions->bForceFrontXAxis = (State == ECheckBoxState::Checked);
						})
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Force Front X Axis")))
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([SelectedImportOptions = Args.SelectedImportOptions]()
						{
							return SelectedImportOptions->bConvertSceneUnit ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([SelectedImportOptions = Args.SelectedImportOptions](ECheckBoxState State)
						{
							SelectedImportOptions->bConvertSceneUnit = (State == ECheckBoxState::Checked);
						})
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Convert Scene Unit")))
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([SelectedImportOptions = Args.SelectedImportOptions]()
						{
							return SelectedImportOptions->bCombineMeshes ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([SelectedImportOptions = Args.SelectedImportOptions](ECheckBoxState State)
						{
							SelectedImportOptions->bCombineMeshes = (State == ECheckBoxState::Checked);
						})
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Combine Meshes")))
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([SelectedImportOptions = Args.SelectedImportOptions]()
						{
							return SelectedImportOptions->bAutoGenerateCollision ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([SelectedImportOptions = Args.SelectedImportOptions](ECheckBoxState State)
						{
							SelectedImportOptions->bAutoGenerateCollision = (State == ECheckBoxState::Checked);
						})
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Auto Generate Collision")))
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([SelectedImportOptions = Args.SelectedImportOptions]()
						{
							return SelectedImportOptions->bBuildNanite ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([SelectedImportOptions = Args.SelectedImportOptions](ECheckBoxState State)
						{
							SelectedImportOptions->bBuildNanite = (State == ECheckBoxState::Checked);
						})
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Build Nanite")))
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([SelectedImportOptions = Args.SelectedImportOptions]()
						{
							return SelectedImportOptions->bShowDialog ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([SelectedImportOptions = Args.SelectedImportOptions](ECheckBoxState State)
						{
							SelectedImportOptions->bShowDialog = (State == ECheckBoxState::Checked);
						})
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Show FBX Options Dialog")))
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Import Uniform Scale")))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SEditableTextBox)
							.Text_Lambda([UniformScaleText = Args.UniformScaleText]() { return FText::FromString(*UniformScaleText); })
							.OnTextChanged_Lambda([UniformScaleText = Args.UniformScaleText](const FText& NewText) { *UniformScaleText = NewText.ToString(); })
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Import Translation (X, Y, Z)")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SEditableTextBox)
							.Text_Lambda([TranslationXText = Args.TranslationXText]() { return FText::FromString(*TranslationXText); })
							.OnTextChanged_Lambda([TranslationXText = Args.TranslationXText](const FText& NewText) { *TranslationXText = NewText.ToString(); })
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SEditableTextBox)
							.Text_Lambda([TranslationYText = Args.TranslationYText]() { return FText::FromString(*TranslationYText); })
							.OnTextChanged_Lambda([TranslationYText = Args.TranslationYText](const FText& NewText) { *TranslationYText = NewText.ToString(); })
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SEditableTextBox)
							.Text_Lambda([TranslationZText = Args.TranslationZText]() { return FText::FromString(*TranslationZText); })
							.OnTextChanged_Lambda([TranslationZText = Args.TranslationZText](const FText& NewText) { *TranslationZText = NewText.ToString(); })
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Import Rotation (Pitch, Yaw, Roll)")))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SEditableTextBox)
							.Text_Lambda([RotationPitchText = Args.RotationPitchText]() { return FText::FromString(*RotationPitchText); })
							.OnTextChanged_Lambda([RotationPitchText = Args.RotationPitchText](const FText& NewText) { *RotationPitchText = NewText.ToString(); })
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SEditableTextBox)
							.Text_Lambda([RotationYawText = Args.RotationYawText]() { return FText::FromString(*RotationYawText); })
							.OnTextChanged_Lambda([RotationYawText = Args.RotationYawText](const FText& NewText) { *RotationYawText = NewText.ToString(); })
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SEditableTextBox)
							.Text_Lambda([RotationRollText = Args.RotationRollText]() { return FText::FromString(*RotationRollText); })
							.OnTextChanged_Lambda([RotationRollText = Args.RotationRollText](const FText& NewText) { *RotationRollText = NewText.ToString(); })
						]
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Link Structure Preview")))
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(720.0f)
				.MinDesiredHeight(420.0f)
				[
					SNew(SMultiLineEditableTextBox)
					.IsReadOnly(true)
					.Text_Lambda([LinkPreviewText = Args.LinkPreviewText]()
					{
						return FText::FromString(*LinkPreviewText);
					})
				]
			];
	}
}
