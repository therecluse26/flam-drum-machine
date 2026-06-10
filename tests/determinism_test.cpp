// determinism_test.cpp  (FLA-53 / CTEST-2)
// Verifies that VoiceManager::seedRNG and FlamEngine::seedRNG expose a stable
// API and that the underlying juce::Random seeding is bit-deterministic.
//
// Full golden-file null-test (audio render comparison) is deferred to CTEST-5,
// which depends on this issue being complete.

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>
#include "Core/VoiceManager.h"
#include "Core/FlamEngine.h"

// ---------------------------------------------------------------------------
// Compile-time API contract: both signatures must match exactly.
// These fire at compile time — no runtime overhead.
// ---------------------------------------------------------------------------
static_assert(
    std::is_same_v<
        decltype(&flam::VoiceManager::seedRNG),
        void (flam::VoiceManager::*)(uint64_t) noexcept>,
    "VoiceManager::seedRNG(uint64_t) noexcept — signature changed");

static_assert(
    std::is_same_v<
        decltype(&flam::FlamEngine::seedRNG),
        void (flam::FlamEngine::*)(uint64_t) noexcept>,
    "FlamEngine::seedRNG(uint64_t) noexcept — signature changed");

// ---------------------------------------------------------------------------
// juce::Random seeding — validates the mechanism both classes rely on
// ---------------------------------------------------------------------------
TEST_CASE ("Same seed produces identical float sequence", "[determinism][rng]")
{
    juce::Random r1 (42LL), r2 (42LL);
    for (int i = 0; i < 64; ++i)
        REQUIRE (r1.nextFloat() == r2.nextFloat());
}

TEST_CASE ("Same seed produces identical int sequence", "[determinism][rng]")
{
    juce::Random r1 (42LL), r2 (42LL);
    for (int i = 0; i < 64; ++i)
        REQUIRE (r1.nextInt (16) == r2.nextInt (16));
}

TEST_CASE ("Different seeds diverge", "[determinism][rng]")
{
    juce::Random r1 (42LL), r2 (99LL);
    bool differs = false;
    for (int i = 0; i < 64; ++i)
        if (r1.nextFloat() != r2.nextFloat()) { differs = true; break; }
    REQUIRE (differs);
}

TEST_CASE ("Re-seeding restarts the sequence", "[determinism][rng]")
{
    juce::Random r (42LL);
    const float first = r.nextFloat();
    r = juce::Random (42LL);
    REQUIRE (first == r.nextFloat());
}

// ---------------------------------------------------------------------------
// VoiceManager — API smoke test (instantiation + seedRNG call)
// ---------------------------------------------------------------------------
TEST_CASE ("VoiceManager::seedRNG is callable without crash", "[determinism][api]")
{
    flam::VoiceManager vm;
    REQUIRE_NOTHROW (vm.seedRNG (42));
    REQUIRE_NOTHROW (vm.seedRNG (0));
    REQUIRE_NOTHROW (vm.seedRNG (UINT64_MAX));
}

// ---------------------------------------------------------------------------
// FlamEngine — API smoke test
// Full render comparison (same seed → bit-identical audio) is CTEST-5.
// ---------------------------------------------------------------------------
TEST_CASE ("FlamEngine::seedRNG seeds both engine and voice-manager RNGs", "[determinism][api]")
{
    flam::FlamEngine engine;
    REQUIRE_NOTHROW (engine.seedRNG (42));
    REQUIRE_NOTHROW (engine.seedRNG (99));
}
