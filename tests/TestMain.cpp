#include <juce_core/juce_core.h>
#include "AnalysisInfrastructureTests.h"
#include "BPMAnalyzerTests.h"
#include "KeyAnalyzerTests.h"
#include "SampleEngineTests.h"
#include "TransientAnalyzerTests.h"

int main(int argc, char** argv)
{
    if (argc > 1 && juce::String(argv[1]) == "--benchmark")
        return runSampleEngineBenchmarks();
    if (argc > 1 && juce::String(argv[1]) == "--bpm-benchmark")
        return runBPMAnalyzerBenchmarks();
    if (argc > 1 && juce::String(argv[1]) == "--key-benchmark")
        return runKeyAnalyzerBenchmarks();
    if (argc > 1 && juce::String(argv[1]) == "--transient-benchmark")
        return runTransientAnalyzerBenchmarks();

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.setPassesAreLogged(true);
    juce::Array<juce::UnitTest*> tests;
    if (argc > 1 && juce::String(argv[1]) == "--transient-test")
    {
        tests.add(getTransientAnalyzerTest());
        runner.runTests(tests);
        for (auto i = 0; i < runner.getNumResults(); ++i)
            if (const auto* result = runner.getResult(i); result != nullptr && result->failures > 0)
                return 1;
        return 0;
    }
    tests.add(getAnalysisInfrastructureTest());
    tests.add(getBPMAnalyzerTest());
    tests.add(getKeyAnalyzerTest());
    tests.add(getTransientAnalyzerTest());
    tests.add(getSampleEngineTest());
    runner.runTests(tests);

    auto failures = 0;
    for (auto i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult(i))
            failures += result->failures;
    return failures == 0 ? 0 : 1;
}
