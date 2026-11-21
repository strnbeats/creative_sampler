#pragma once

#include "AnalysisTypes.h"

class KeyAnalyzer final : public Analyzer
{
public:
    struct Options
    {
        double minimumDurationSeconds = 1.5;
        double frameDurationSeconds = 0.32;
        double hopFraction = 0.25;
        int minimumFrames = 4;
        int maximumCandidates = 5;
    };

    KeyAnalyzer();
    explicit KeyAnalyzer(Options options);

    AnalyzerKind kind() const noexcept override { return AnalyzerKind::key; }
    AnalyzerOutput analyze(const SampleStorage& sample,
                           const AnalysisCancellationToken& cancellation) const override;

    const Options& options() const noexcept { return settings; }

private:
    Options settings;
};
