using UnrealBuildTool;

public class DTHWardrobe : ModuleRules
{
	public DTHWardrobe(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HairStrandsCore",
		});
	}
}
