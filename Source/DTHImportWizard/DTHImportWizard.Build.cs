using UnrealBuildTool;

public class DTHImportWizard : ModuleRules
{
	public DTHImportWizard(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"ToolMenus",
			"UnrealEd",
			"WorkspaceMenuStructure",
			"DeveloperSettings",
			"Json",
			"PropertyEditor",
			"DesktopPlatform",
			"InterchangeCore",
			"InterchangeEngine",
			"InterchangePipelines",
			"AssetTools",
			"SkeletalMeshEditor",
			"ControlRig",
			"ControlRigDeveloper",
			"ControlRigEditor",
			"RigVM",
			"RigVMDeveloper",
		});
	}
}
