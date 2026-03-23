#include "UI/ActorToolsTab.h"

#include "UI/ActorInspector.h"
#include "UI/JsonPreview.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

namespace CadActorToolsTab
{
	TSharedRef<SWidget> BuildActorToolsTabContent(const FCadActorToolsTabArgs& Args)
	{
		const TSharedRef<int32> ActiveModeIndex = MakeShared<int32>(0);
		const TSharedRef<SWidget> InspectorContent = CadActorInspector::BuildInspectorTabContent(Args.InspectorText);
		const TSharedRef<SWidget> JsonContent = CadJsonPreview::BuildJsonPreviewTabContent(Args.JsonPreviewText, Args.SaveJson);

		return
			SNew(SVerticalBox)

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
					.Text(FText::FromString(TEXT("Inspector")))
					.OnClicked_Lambda([ActiveModeIndex]()
					{
						*ActiveModeIndex = 0;
						return FReply::Handled();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("JSON")))
					.OnClicked_Lambda([ActiveModeIndex]()
					{
						*ActiveModeIndex = 1;
						return FReply::Handled();
					})
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([ActiveModeIndex]()
				{
					return *ActiveModeIndex;
				})
				+ SWidgetSwitcher::Slot()
				[
					InspectorContent
				]
				+ SWidgetSwitcher::Slot()
				[
					JsonContent
				]
			];
	}
}
