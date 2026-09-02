#include "logic/step_detector.h"

void StepDetector::resetState() {
    primed_ = false;
    lowPass_ = 1.0f;
    gravity_ = 1.0f;
    inPeak_ = false;
    peakValue_ = 0.0f;
    peakStartMs_ = 0;
    lastStepMs_ = 0;
}

uint8_t StepDetector::update(float accelMagnitudeG, uint32_t tMs) {
    // First sample: seed the filters on it instead of on the 1.0 default, so the
    // baseline does not spend the first second converging (and firing bogus steps).
    if (!primed_) {
        primed_ = true;
        lowPass_ = accelMagnitudeG;
        gravity_ = accelMagnitudeG;
        return 0;
    }

    lowPass_ += cfg_.lowPassAlpha * (accelMagnitudeG - lowPass_);
    gravity_ += cfg_.gravityAlpha * (lowPass_ - gravity_);
    const float dynamic = lowPass_ - gravity_;

    uint8_t steps = 0;

    if (!inPeak_) {
        if (dynamic > cfg_.enterThreshG) {
            inPeak_ = true;
            peakValue_ = dynamic;
            peakStartMs_ = tMs;
        }
        return 0;
    }

    // In a peak.
    if (dynamic > peakValue_) peakValue_ = dynamic;

    if (dynamic < cfg_.exitThreshG) {
        const uint32_t peakMs = tMs - peakStartMs_;
        const uint32_t sinceLastStep = tMs - lastStepMs_;
        const bool amplitudeOk = peakValue_ >= cfg_.minPeakG;
        const bool widthOk = peakMs >= cfg_.minPeakMs && peakMs <= cfg_.maxPeakMs;
        const bool cadenceOk = lastStepMs_ == 0 || sinceLastStep >= cfg_.minStepMs;

        if (amplitudeOk && widthOk && cadenceOk) {
            steps = 1;
            lastStepMs_ = tMs;
        }
        inPeak_ = false;
        peakValue_ = 0.0f;
    } else if (tMs - peakStartMs_ > cfg_.maxPeakMs) {
        // Stuck above the threshold: a sustained shock or the arm being held up,
        // not a footfall. Drop it.
        inPeak_ = false;
        peakValue_ = 0.0f;
    }

    return steps;
}
