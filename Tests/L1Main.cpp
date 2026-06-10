// Entry point for CTEST-3 (L1 unit tests: FlamKitLoader, VoiceManager, DSP).
// Runs all juce::UnitTest instances registered via static construction.
//
// No explicit JUCE init is required: juce_core tests work without a message
// thread, and juce::Thread (used by SampleStreamingManager) is a direct
// pthread wrapper that doesn't need the JUCE event loop.

#include <juce_core/juce_core.h>

int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult(i);
        if (!r) continue;
        failures += r->failures;
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
    return failures > 0 ? 1 : 0;
}
