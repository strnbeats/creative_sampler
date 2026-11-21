#pragma once

#include "AnalysisTypes.h"

class TransientAnalyzer final : public Analyzer
{
public:
    struct Parameters
    {
        double frameDurationSeconds = 0.012;
        double hopDurationSeconds = 0.001;
        double minimumEventDistanceSeconds = 0.012;
        size_t maximumEvents = 65536;
    };

    TransientAnalyzer() = default;
    explicit TransientAnalyzer(Parameters settings) : parameters(settings) {}

    AnalyzerKind kind() const noexcept override { return AnalyzerKind::transients; }
    AnalyzerOutput analyze(const SampleStorage& sample,
                          const AnalysisCancellationToken& cancellation) const override;

private:
    Parameters parameters;
};
