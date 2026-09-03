#ifndef STEP_DETECTOR_H
#define STEP_DETECTOR_H

#include <stdint.h>

// Pure pedometer: feed it accelerometer-magnitude samples (in g, ~1.0 at rest)
// with their millis() timestamp and it tells you how many steps were just
// confirmed. No Arduino dependency, so it is unit-tested on PC (test_native).
//
// How it works:
//   1. low-pass the magnitude to kill sensor noise,
//   2. track a slow "gravity" baseline (the DC component),
//   3. the dynamic part (magnitude - gravity) swings +/- once per step; a
//      threshold crossing with hysteresis + a plausibility check on the peak
//      amplitude, its width and the time since the last step counts one step.
//
// It is sample-rate tolerant (all timing is in ms) but expects to be fed at a
// steady ~20-50 Hz, which is why SensorManager samples the IMU on its own timer
// rather than once per BLE measurement.
struct StepDetectorConfig {
    float lowPassAlpha   = 0.30f;   // per-sample smoothing of the raw magnitude
    float gravityAlpha   = 0.02f;   // how fast the gravity baseline follows
    float enterThreshG   = 0.30f;   // dynamic accel to start a peak
    float exitThreshG    = 0.12f;   // ...and to end it (hysteresis)
    float minPeakG       = 0.45f;   // reject peaks smaller than this (noise / tremor)
    uint32_t minStepMs   = 350;     // faster than ~3.4 steps/s is not walking
    uint32_t minPeakMs   = 80;      // a real footfall lasts at least this long
    uint32_t maxPeakMs   = 350;     // longer = a shock or the arm being moved, not a step
};

class StepDetector {
public:
    StepDetector() {}
    explicit StepDetector(const StepDetectorConfig& config) : cfg_(config) {}

    // Feed one sample, returns 1 if it confirmed a step, 0 otherwise.
    uint8_t update(float accelMagnitudeG, uint32_t tMs);

    // Reset the filter/peak state (e.g. after sensor re-init). Does not touch
    // the step total, that stays in SensorManager.
    void resetState();

private:
    StepDetectorConfig cfg_;

    bool primed_ = false;
    float lowPass_ = 1.0f;
    float gravity_ = 1.0f;

    bool inPeak_ = false;
    float peakValue_ = 0.0f;
    uint32_t peakStartMs_ = 0;
    uint32_t lastStepMs_ = 0;
};

#endif // STEP_DETECTOR_H