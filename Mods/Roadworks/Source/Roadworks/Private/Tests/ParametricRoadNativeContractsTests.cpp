#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "ParametricRoadNativeHints.h"
#include "ParametricRoadSession.h"
#include "Blueprint/UserWidget.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadNativeContractsTest, "Roadworks.Editor.NativeKeyboardHintContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadNativeContractsTest::RunTest(const FString&)
{
    UClass* Class = LoadClass<UUserWidget>(nullptr, ParametricRoadNativeHints::BuildModePath);
    if (!TestNotNull(TEXT("The actual Build Gun widget class loads"), Class)) { return false; }
    FString Error;
    TestTrue(FString::Printf(TEXT("Native BuildMode array/function contract: %s"), *Error),
        ParametricRoadNativeHints::ResolveContract(Class, Error));
    TestNotNull(TEXT("Keyboard hint cache is present on the actual native class"), FindFProperty<FArrayProperty>(Class, TEXT("mCachedKeybinds")));
    TStrongObjectPtr<UParametricRoadSession> Session(NewObject<UParametricRoadSession>());
    const FText Normal = Session->GetShortcutLabel(5);
    TestEqual(TEXT("The native shortcut describes its action"), Normal.ToString(), FString(TEXT("Fine step")));
    Session->bFineAdjustment = true;
    const FText Fine = Session->GetShortcutLabel(5);
    TestTrue(TEXT("Changing precision preserves the cached native hint just like point selection"), Normal.EqualTo(Fine));
    const auto FineNamespace = FTextInspector::GetNamespace(Fine);
    TestTrue(TEXT("The stable label retains ownership for native cache replacement and removal"), FineNamespace.IsSet()
        && FineNamespace.GetValue() == TEXT("Roadworks.NativeHint"));
    // This checks the real reflected signature, not a substitute keyboard row,
    // input injection, runtime Blueprint execution or a gameplay UI scenario.
    return !HasAnyErrors();
}
#endif
