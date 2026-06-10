// Entry point for CTEST-3 (L1 unit tests: FlamKitLoader, VoiceManager, DSP).
// Runs all juce::UnitTest instances registered via static construction.

#include <juce_core/juce_core.h>

int main()
{
    juce::initialiseJuce_NonGUI();

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    const int failures = runner.getNumFailures();
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult(i);
        if (!r) continue;
        const juce::String status = r->failures > 0 ? "FAILED" : "passed";
        juce::Logger::writeToLog("[" + status + "] " + r->unitTestName
            + " (" + juce::String(r->passes) + " passed, "
            + juce::String(r->failures) + " failed)");
        for (const auto& msg : r->messages)
            juce::Logger::writeToLog("  " + msg);
    }

    juce::Logger::writeToLog(failures > 0
        ? "FAILED: " + juce::String(failures) + " test failure(s)"
        : "All L1 unit tests passed.");

    juce::shutdownJuce_NonGUI();
    return failures > 0 ? 1 : 0;
}
