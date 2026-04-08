// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Hyoseung

using UnrealBuildTool;

public class CadImporterEditor : ModuleRules
{
	public CadImporterEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CadImporter",
				"Projects",
				"InputCore",
				"Json",
				"AssetRegistry",
				"DesktopPlatform",
				"EditorFramework",
				"UnrealEd",
				"StaticMeshEditor",
				"ToolMenus",
				"CoreUObject",
				"Engine",
				"Settings",
				"Slate",
				"SlateCore",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
