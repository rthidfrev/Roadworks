using UnrealBuildTool;

public class Roadworks : ModuleRules
{
    public Roadworks(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PrivateDependencyModuleNames.Add("EnhancedInput");

        // The FactoryGame public headers require these SDK dependencies.
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "FactoryGame", "SML",
            "DeveloperSettings", "PhysicsCore", "InputCore", "GeometryCollectionEngine",
            "AnimGraphRuntime", "AssetRegistry", "NavigationSystem", "AIModule",
            "GameplayTasks", "SlateCore", "Slate", "UMG", "RenderCore",
            "CinematicCamera", "Foliage", "NetCore", "GameplayTags",
            "Json", "JsonUtilities", "DummyHeaders"
        });
        if (Target.Type == TargetRules.TargetType.Editor)
        {
            PublicDependencyModuleNames.Add("AnimGraph");
            PrivateDependencyModuleNames.Add("RHI");
            DynamicallyLoadedModuleNames.Add("AdpcmAudioDecoder");
        }
    }
}
