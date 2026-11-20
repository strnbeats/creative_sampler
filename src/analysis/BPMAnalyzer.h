#pragma once

#include "AnalysisTypes.h"

class BPMAnalyzer final : public Analyzer
{
public:
    struct Options
    {
        double minimumBpm = 50.0;
        double maximumBpm = 220.0;
        double minimumDurationSeconds = 1.5;
        double envelopeRateHz = 100.0;
        int minimumOnsets = 4;
        int minimumCycles = 3;
        int maximumCandidates = 5;
    };

    BPMAnalyzer();
    explicit BPMAnalyzer(Options options);

    AnalyzerKind kind() const noexcept override { return AnalyzerKind::bpm; }
    AnalyzerOutput analyze(const SampleStorage& sample,
                           const AnalysisCancellationToken& cancellation) const override;

    const Options& options() const noexcept { return settings; }

private:
    Options settings;
};
