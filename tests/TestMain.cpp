#include <juce_core/juce_core.h>
#include "AnalysisInfrastructureTests.h"
#include "SampleEngineTests.h"

int main(int argc, char** argv)
{
    if (argc > 1 && juce::String(argv[1]) == "--benchmark")
        return runSampleEngineBenchmarks();

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.setPassesAreLogged(true);
    juce::Array<juce::UnitTest*> tests;
    tests.add(getAnalysisInfrastructureTest());
    tests.add(getSampleEngineTest());
    runner.runTests(tests);

    auto failures = 0;
    for (auto i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult(i))
            failures += result->failures;
    return failures == 0 ? 0 : 1;
}
