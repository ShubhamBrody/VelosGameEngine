#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace velos {

struct TickResult {
    std::uint32_t steps = 0;
    double alpha = 0.0;
    double droppedSeconds = 0.0;
};

class FixedStepClock {
public:
    explicit FixedStepClock(double stepSeconds = 1.0 / 60.0, std::uint32_t maxSteps = 4)
        : stepSeconds_(stepSeconds), maxSteps_(maxSteps) {
        if (!std::isfinite(stepSeconds_) || stepSeconds_ <= 0.0 || maxSteps_ == 0) {
            throw std::invalid_argument("Fixed-step duration and catch-up budget must be positive.");
        }
    }

    [[nodiscard]] TickResult advance(double elapsedSeconds) {
        if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0) {
            throw std::invalid_argument("Elapsed time must be finite and nonnegative.");
        }
        TickResult result;
        const double accepted = std::min(elapsedSeconds, 0.25);
        result.droppedSeconds = elapsedSeconds - accepted;
        accumulator_ += accepted;
        while (accumulator_ + stepSeconds_ * 1e-9 >= stepSeconds_ && result.steps < maxSteps_) {
            accumulator_ = std::max(0.0, accumulator_ - stepSeconds_);
            ++result.steps;
        }
        if (accumulator_ >= stepSeconds_) {
            const double remainder = std::fmod(accumulator_, stepSeconds_);
            result.droppedSeconds += accumulator_ - remainder;
            accumulator_ = remainder;
        }
        result.alpha = std::clamp(accumulator_ / stepSeconds_, 0.0, 1.0);
        return result;
    }

    void reset() noexcept { accumulator_ = 0.0; }
    [[nodiscard]] double stepSeconds() const noexcept { return stepSeconds_; }

private:
    double stepSeconds_;
    double accumulator_ = 0.0;
    std::uint32_t maxSteps_;
};

}