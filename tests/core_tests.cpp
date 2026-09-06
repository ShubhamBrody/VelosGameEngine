#include "core/FixedStepClock.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void testClock() {
    velos::FixedStepClock clock;
    require(clock.advance(0.0).steps == 0, "Zero elapsed time must not tick.");
    const auto half = clock.advance(clock.stepSeconds() / 2.0);
    require(half.steps == 0 && std::abs(half.alpha - 0.5) < 1e-8, "Half-step interpolation.");
    require(clock.advance(clock.stepSeconds() / 2.0).steps == 1, "Two half steps must tick once.");
    clock.reset();
    std::uint32_t steps = 0;
    for (int frame = 0; frame < 120; ++frame) {
        steps += clock.advance(1.0 / 60.0).steps;
    }
    require(steps == 120, "60 Hz simulation must not drift at 60 FPS.");
    clock.reset();
    steps = 0;
    for (int frame = 0; frame < 60; ++frame) {
        steps += clock.advance(1.0 / 30.0).steps;
    }
    require(steps == 120, "Simulation must retain its rate at 30 FPS.");
    const auto overloaded = clock.advance(0.5);
    require(overloaded.steps == 4 && overloaded.droppedSeconds > 0.4, "Catch-up must be bounded.");
    require(overloaded.alpha >= 0.0 && overloaded.alpha < 1.0, "Interpolation must remain valid.");
    clock.reset();
    require(clock.advance(0.0).alpha == 0.0, "Reset must clear accumulated time.");
    bool rejected = false;
    try { static_cast<void>(clock.advance(-1.0)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Negative time must be rejected.");
    rejected = false;
    try { static_cast<void>(clock.advance(std::numeric_limits<double>::infinity())); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Infinite time must be rejected.");
}

}

int main() {
    try {
        testClock();
        std::cout << "PASS: fixed-step clock, interpolation, overload, reset and invalid input.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}