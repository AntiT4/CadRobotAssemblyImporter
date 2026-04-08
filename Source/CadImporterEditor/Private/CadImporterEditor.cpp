// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Hyoseung

#include "CadImporterEditor.h"
#include "CadImporterEditorUserSettings.h"
#include "ImportService.h"
#include "UI/WorkflowWizard.h"
#include "Brushes/SlateImageBrush.h"
#include "HAL/FileManager.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "ISettingsModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

DEFINE_LOG_CATEGORY(LogCadImporter);

#define LOCTEXT_NAMESPACE "FCadImporterEditorModule"

namespace
{
const FName WorkflowTabName(TEXT("CadImporterEditor.Workflow"));
const FName CadImporterEditorStyleSetName(TEXT("CadImporterEditorStyle"));
const FName WorkflowTabIconBrushName(TEXT("CadImporterEditor.WorkflowTabIcon"));
const FName WorkflowStepsBrushName(TEXT("CadImporterEditor.WorkflowSteps"));
constexpr double DryRunFolderMaxAgeHours = 24.0;
constexpr float DryRunCleanupIntervalSeconds = 600.0f;
}

FName FCadImporterEditorModule::GetStyleSetName()
{
	return CadImporterEditorStyleSetName;
}

const FSlateBrush* FCadImporterEditorModule::GetWorkflowStepBrush()
{
	if (const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(GetStyleSetName()))
	{
		return Style->GetBrush(WorkflowStepsBrushName);
	}

	return FAppStyle::GetBrush("NoBrush");
}

const FSlateBrush* FCadImporterEditorModule::GetWorkflowTabIconBrush()
{
	if (const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(GetStyleSetName()))
	{
		return Style->GetBrush(WorkflowTabIconBrushName);
	}

	return FAppStyle::GetBrush("NoBrush");
}

void FCadImporterEditorModule::StartupModule()
{
	RegisterStyle();
	CleanupStaleDryRunFolders();
	DryRunCleanupTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FCadImporterEditorModule::TickDryRunGarbageCollector),
		DryRunCleanupIntervalSeconds);

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings(
			"Editor",
			"Plugins",
			"CadImporterEditor",
			LOCTEXT("CadImporterSettingsName", "CAD Importer"),
			LOCTEXT("CadImporterSettingsDescription", "Personal editor settings for CAD workflow previews and debugging."),
			GetMutableDefault<UCadImporterEditorUserSettings>());
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		WorkflowTabName,
		FOnSpawnTab::CreateRaw(this, &FCadImporterEditorModule::SpawnWorkflowTab))
		.SetDisplayName(LOCTEXT("CadImporterTabLabel", "CAD Master Workflow"))
		.SetTooltipText(LOCTEXT("CadImporterTabTooltip", "Open the dockable CAD master workflow tab."))
		.SetIcon(FSlateIcon(GetStyleSetName(), WorkflowTabIconBrushName))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FCadImporterEditorModule::RegisterMenus));
}

void FCadImporterEditorModule::ShutdownModule()
{
	if (DryRunCleanupTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DryRunCleanupTickerHandle);
		DryRunCleanupTickerHandle.Reset();
	}

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Editor", "Plugins", "CadImporterEditor");
	}

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(WorkflowTabName);
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	ImportService.Reset();
	UnregisterStyle();
}

void FCadImporterEditorModule::RegisterStyle()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CadRobotAssemblyImporter"));
	if (!Plugin.IsValid())
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());
	StyleSet->SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));
	StyleSet->Set(
		WorkflowTabIconBrushName,
		new FSlateVectorImageBrush(StyleSet->RootToContentDir(TEXT("WorkflowTabIcon"), TEXT(".svg")), FVector2D(20.0f, 20.0f)));
	StyleSet->Set(
		WorkflowStepsBrushName,
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("WorkflowSteps"), TEXT(".png")), FVector2D(920.0f, 180.0f)));
	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FCadImporterEditorModule::UnregisterStyle()
{
	if (!StyleSet.IsValid())
	{
		return;
	}

	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
	StyleSet.Reset();
}

void FCadImporterEditorModule::CleanupStaleDryRunFolders() const
{
	const FString DryRunRoot = FPaths::Combine(
		FPaths::ProjectIntermediateDir(),
		TEXT("CadRobotAssemblyImporter"),
		TEXT("WorkflowDryRun"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*DryRunRoot))
	{
		return;
	}

	TArray<FString> DryRunDirectories;
	IFileManager::Get().FindFiles(DryRunDirectories, *FPaths::Combine(DryRunRoot, TEXT("*")), false, true);
	const FDateTime Now = FDateTime::Now();
	const FTimespan MaxAge = FTimespan::FromHours(DryRunFolderMaxAgeHours);

	for (const FString& DirectoryName : DryRunDirectories)
	{
		const FString FullPath = FPaths::Combine(DryRunRoot, DirectoryName);
		const FDateTime Timestamp = IFileManager::Get().GetTimeStamp(*FullPath);
		if (Timestamp == FDateTime::MinValue() || (Now - Timestamp) < MaxAge)
		{
			continue;
		}

		PlatformFile.DeleteDirectoryRecursively(*FullPath);
	}
}

bool FCadImporterEditorModule::TickDryRunGarbageCollector(float DeltaTime)
{
	CleanupStaleDryRunFolders();
	return true;
}

void FCadImporterEditorModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);
	const FUIAction Action = FUIAction(FExecuteAction::CreateRaw(this, &FCadImporterEditorModule::OpenWorkflowTab));
	const FSlateIcon WorkflowIcon(GetStyleSetName(), WorkflowTabIconBrushName);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntry(
				"CadImporterAction",
				LOCTEXT("CadImporterMenuLabel", "CAD Master Workflow"),
				LOCTEXT("CadImporterMenuTooltip", "Open the dockable workflow tab for workspace, master JSON, child JSON, and build."),
				WorkflowIcon,
				Action);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry Entry = FToolMenuEntry::InitToolBarButton(
					"CadImporterAction",
					Action,
					LOCTEXT("CadImporterToolbarLabel", "CAD Master Workflow"),
					LOCTEXT("CadImporterToolbarTooltip", "Open the dockable tab for the master JSON workflow."),
					WorkflowIcon);
				Section.AddEntry(Entry);
			}
		}
	}
}

void FCadImporterEditorModule::OpenWorkflowTab()
{
	if (!FSlateApplication::IsInitialized())
	{
		UE_LOG(LogCadImporter, Warning, TEXT("Slate application is not initialized; cannot open workflow tab."));
		return;
	}

	FGlobalTabmanager::Get()->TryInvokeTab(WorkflowTabName);
}

TSharedRef<SDockTab> FCadImporterEditorModule::SpawnWorkflowTab(const FSpawnTabArgs& /*Args*/)
{
	if (!ImportService.IsValid())
	{
		ImportService = MakeShared<FCadImportService>();
	}

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("MasterWorkflowWindowTitle", "CAD Master Workflow"))
		[
			SNew(SCadWorkflowWizard)
			.Runner(ImportService)
		];
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FCadImporterEditorModule, CadImporterEditor)
