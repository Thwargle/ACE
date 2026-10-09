#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEScrollLearning.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEScrollLearningTest,"ACE.RetailParity.ScrollLearning",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEScrollLearningTest::RunTest(const FString&)
{
    FACEPlayerVitals V;
    for (uint32 Power : {1u,49u,300u,350u,400u})
        TestTrue(TEXT("I/VII/VIII can be learned without training or skill"),ACEScrollLearning::CanLearn(Power,1,V));
    const int32 Schools[] = {0,34,33,32,31,43};
    for (uint32 School=1;School<UE_ARRAY_COUNT(Schools);++School)
    {
        for (uint32 Power : {50u,100u,150u,200u,250u})
        {
            V.Skills.Empty();
            TestFalse(TEXT("Missing school cannot learn II-VI"),ACEScrollLearning::CanLearn(Power,School,V));
            FACESkillInfo Skill;Skill.SkillId=Schools[School];Skill.Current=500;Skill.AdvancementClass=1;
            V.Skills.Add(Skill);
            TestFalse(TEXT("High skill does not bypass untrained school"),ACEScrollLearning::CanLearn(Power,School,V));
            for (int32 Training : {2,3})
            {
                V.Skills[0].AdvancementClass=Training;
                V.Skills[0].Current=int32(Power)-51;
                TestFalse(TEXT("Below learning threshold is rejected"),ACEScrollLearning::CanLearn(Power,School,V));
                V.Skills[0].Current=int32(Power)-50;V.Skills[0].Base=0;
                TestTrue(TEXT("Exact learning threshold uses current skill, not base or casting difficulty"),ACEScrollLearning::CanLearn(Power,School,V));
            }
        }
    }
    TestFalse(TEXT("Unknown school cannot bypass mid-tier requirements"),ACEScrollLearning::CanLearn(100,99,V));
    return true;
}
#endif
