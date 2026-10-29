#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "ParametricRoadSession.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Input/FGChildInputMappingContext.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricRoadInputBindingTest, "Roadworks.Editor.EquipmentInputBindingOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricRoadInputBindingTest::RunTest(const FString&)
{
    TStrongObjectPtr<UEnhancedInputComponent> Component(NewObject<UEnhancedInputComponent>());
    TStrongObjectPtr<UParametricRoadSession> First(NewObject<UParametricRoadSession>());
    TStrongObjectPtr<UParametricRoadSession> Other(NewObject<UParametricRoadSession>());
    // Exercise the actual Enhanced Input binding API, without world, pawn,
    // controller, input injection or a gameplay session.
    for (int32 I = 0; I < 8; ++I)
    {
        UInputAction* Action = NewObject<UInputAction>();
        First->InputActions.Add(Action); Other->InputActions.Add(Action);
    }
    First->BindActionHandlers(Component.Get());
    TestEqual(TEXT("Eight authored actions and one selection wheel bound to equipment"), Component->GetActionEventBindings().Num(), 9);
    TestTrue(TEXT("Wheel context is transient and scoped to the road session"), First->WheelSelectionContext->HasAnyFlags(RF_Transient));
    TestFalse(TEXT("Native manual binding receives an ordinary context, never a child context"), First->WheelSelectionContext->IsA<UFGChildInputMappingContext>());
    TestFalse(TEXT("A native input mode tag cannot silently filter the session wheel"), First->WheelSelectionContext->ShouldFilterMappingByInputMode());
    const auto& WheelMappings = First->WheelSelectionContext->GetMappings();
    TestEqual(TEXT("Scoped context maps only the wheel"), WheelMappings.Num(), 1);
    if (!WheelMappings.IsEmpty())
    {
        TestEqual(TEXT("Wheel is independent of native modified Scroll routing"), WheelMappings[0].Key, EKeys::MouseWheelAxis);
        TestTrue(TEXT("Only the transient selection action is mapped"), WheelMappings[0].Action == First->WheelSelectionAction);
        TestTrue(TEXT("Wheel mapping adds no trigger or chord blockers to native input"), WheelMappings[0].Triggers.IsEmpty());
    }
    TestFalse(TEXT("Wheel selection does not consume sprint or native mappings"), First->WheelSelectionAction->bConsumeInput);
    TestFalse(TEXT("Wheel selection never triggers while paused"), First->WheelSelectionAction->bTriggerWhenPaused);
    TestEqual(TEXT("Wheel direction is a signed axis"), First->WheelSelectionAction->ValueType, EInputActionValueType::Axis1D);
    TestTrue(TEXT("Wheel observation is independent of another action's trigger state"), First->WheelSelectionAction->Triggers.IsEmpty());
    int32 SelectionWheelBindings = 0;
    for (const auto& Binding : Component->GetActionEventBindings())
    {
        if (Binding->GetAction() == First->WheelSelectionAction)
        {
            ++SelectionWheelBindings;
            TestEqual(TEXT("Wheel selection receives current triggered axis values"), Binding->GetTriggerEvent(), ETriggerEvent::Triggered);
        }
    }
    TestEqual(TEXT("Only one delegate owns wheel selection"), SelectionWheelBindings, 1);
    First->bAssessed = true;
    First->Phase = EParametricRoadPhase::Ready;
    First->SelectSettingWheel(FInputActionValue(1.0f));
    TestEqual(TEXT("An observer event without an active native input owner cannot select a parameter"),
        First->Adjustment, EParametricRoadAdjustment::Heading);
    First->ApplyWheel(1, true);
    TestEqual(TEXT("Selection routing changes the selection once"), First->Adjustment, EParametricRoadAdjustment::Grade);
    TestEqual(TEXT("Selection leaves the selected value unchanged"), First->StartGrade, 0.0);
    First->ApplyWheel(1, false);
    TestEqual(TEXT("Value adjustment edits the selection without cycling"), First->StartGrade, 1.0);
    TestEqual(TEXT("Value adjustment retains the selected parameter"), First->Adjustment, EParametricRoadAdjustment::Grade);
    TestEqual(TEXT("Value adjustment retains the other endpoint"), First->EndGrade, 0.0);
    First->BindActionHandlers(Component.Get());
    TestEqual(TEXT("Rebinding has no duplicate delegates"), Component->GetActionEventBindings().Num(), 9);
    Other->BindActionHandlers(Component.Get());
    TestEqual(TEXT("Other owner can share the equipment component"), Component->GetActionEventBindings().Num(), 18);
    First->UnbindControls();
    TestEqual(TEXT("Teardown removes only this session's delegates"), Component->GetActionEventBindings().Num(), 9);
    for (const auto& Binding : Component->GetActionEventBindings())
    {
        TestTrue(TEXT("Other owner's binding survives"), Binding->IsBoundToObject(Other.Get()));
    }
    Other->UnbindControls();
    TestTrue(TEXT("Last owner's teardown leaves no stale bindings"), Component->GetActionEventBindings().IsEmpty());
    TestNull(TEXT("Binding checks do not create a game world"), First->GetWorld());
    return true;
}
#endif
