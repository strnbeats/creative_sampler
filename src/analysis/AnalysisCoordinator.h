#pragma once

#include "AnalysisTypes.h"
#include <condition_variable>
#include <mutex>
#include <thread>

// Thread-safe analysis lifecycle. Lifecycle methods belong to a control/message thread;
// no method on this class is intended to be called from processBlock().
class AnalysisCoordinator final
{
public:
    AnalysisCoordinator();
    ~AnalysisCoordinator();

    AnalysisCoordinator(const AnalysisCoordinator&) = delete;
    AnalysisCoordinator& operator=(const AnalysisCoordinator&) = delete;

    static AnalysisSampleId createSampleId() noexcept;

    // Call when the selected Sample changes. Repeated loads should receive new IDs,
    // even if they use the same file path.
    AnalysisIdentity setCurrentSample(AnalysisSampleId sampleId);

    // Re-running analysis for the current Sample advances its generation.
    std::optional<AnalysisIdentity> requestAnalysis(
        AnalysisSampleId sampleId,
        std::shared_ptr<const SampleStorage> sample,
        std::vector<std::shared_ptr<const Analyzer>> analyzers);

    void cancel();
    void shutdown();

    // Intended for UI/message-thread consumers and tests, never for the audio thread.
    AnalysisResult latestResult() const;
    AnalysisResultStamp latestStamp() const;

private:
    struct WorkItem
    {
        AnalysisIdentity identity;
        std::shared_ptr<const SampleStorage> sample;
        std::vector<std::shared_ptr<const Analyzer>> analyzers;
        std::shared_ptr<AnalysisCancellationToken> cancellation;
    };

    static bool isSampleUsable(const SampleStorage& sample) noexcept;
    static juce::String validateRequest(const SampleStorage* sample,
                                        const std::vector<std::shared_ptr<const Analyzer>>& analyzers);
    void cancelCurrentLocked();
    void workerLoop();
    AnalysisResult run(const WorkItem& item) const;

    mutable std::mutex stateMutex;
    std::mutex shutdownMutex;
    std::condition_variable workAvailable;
    bool shuttingDown = false;
    AnalysisSampleId currentSampleId = 0;
    std::uint64_t generation = 0;
    AnalysisResult result;
    std::shared_ptr<AnalysisCancellationToken> currentCancellation;
    std::optional<WorkItem> pendingWork;
    std::thread worker;
};
