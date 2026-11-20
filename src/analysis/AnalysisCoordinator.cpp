#include "AnalysisCoordinator.h"
#include <cmath>
#include <exception>
#include <unordered_set>

namespace
{
std::atomic<AnalysisSampleId> nextSampleId { 1 };

AnalysisResult makeTerminalResult(AnalysisIdentity identity, AnalysisStatus status,
                                  juce::String error = {})
{
    AnalysisResult value;
    value.identity = identity;
    value.status = status;
    value.error = std::move(error);
    return value;
}
}

AnalysisCoordinator::AnalysisCoordinator()
    : worker([this] { workerLoop(); })
{
}

AnalysisCoordinator::~AnalysisCoordinator()
{
    shutdown();
}

void AnalysisCoordinator::shutdown()
{
    const std::lock_guard<std::mutex> shutdownLock(shutdownMutex);
    {
        const std::lock_guard<std::mutex> lock(stateMutex);
        if (!shuttingDown)
        {
            shuttingDown = true;
            cancelCurrentLocked();
            pendingWork.reset();
            if (result.status == AnalysisStatus::running)
            {
                ++generation;
                result.status = AnalysisStatus::cancelled;
                result.bpm.reset();
                result.key.reset();
                result.transients.reset();
                result.error.clear();
            }
        }
    }
    workAvailable.notify_one();
    if (worker.joinable())
        worker.join();
}

AnalysisSampleId AnalysisCoordinator::createSampleId() noexcept
{
    return nextSampleId.fetch_add(1, std::memory_order_relaxed);
}

AnalysisIdentity AnalysisCoordinator::setCurrentSample(AnalysisSampleId sampleId)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    if (shuttingDown)
        return {};
    cancelCurrentLocked();
    pendingWork.reset();
    currentSampleId = sampleId;
    ++generation;
    result = {};
    result.identity = currentSampleId == 0 ? AnalysisIdentity {}
                                          : AnalysisIdentity { currentSampleId, generation };
    result.status = AnalysisStatus::notAnalyzed;
    return result.identity;
}

std::optional<AnalysisIdentity> AnalysisCoordinator::requestAnalysis(
    AnalysisSampleId sampleId,
    std::shared_ptr<const SampleStorage> sample,
    std::vector<std::shared_ptr<const Analyzer>> analyzers)
{
    AnalysisIdentity identity;
    {
        const std::lock_guard<std::mutex> lock(stateMutex);
        if (shuttingDown || sampleId == 0 || sampleId != currentSampleId)
            return std::nullopt;

        cancelCurrentLocked();
        pendingWork.reset();
        identity = { sampleId, ++generation };
        result = {};
        result.identity = identity;
        result.status = AnalysisStatus::running;

        const auto validationError = validateRequest(sample.get(), analyzers);
        if (validationError.isNotEmpty())
        {
            result.status = AnalysisStatus::failed;
            result.error = validationError;
            return identity;
        }

        auto cancellation = std::make_shared<AnalysisCancellationToken>();
        currentCancellation = cancellation;
        pendingWork = WorkItem { identity, std::move(sample), std::move(analyzers),
                                 std::move(cancellation) };
    }
    workAvailable.notify_one();
    return identity;
}

void AnalysisCoordinator::cancel()
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    if (result.status != AnalysisStatus::running)
        return;

    cancelCurrentLocked();
    pendingWork.reset();
    ++generation; // Invalidates any late completion from the cancelled request.
    result.status = AnalysisStatus::cancelled;
    result.bpm.reset();
    result.key.reset();
    result.transients.reset();
    result.error.clear();
}

AnalysisResult AnalysisCoordinator::latestResult() const
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    return result;
}

bool AnalysisCoordinator::isSampleUsable(const SampleStorage& sample) noexcept
{
    const auto& audio = sample.audio();
    const auto& metadata = sample.metadata();
    const auto duration = metadata.durationSeconds();
    return audio.getNumSamples() > 0
        && (audio.getNumChannels() == 1 || audio.getNumChannels() == 2)
        && metadata.channelCount == audio.getNumChannels()
        && metadata.sampleCount == audio.getNumSamples()
        && std::isfinite(metadata.sampleRate) && metadata.sampleRate > 0.0
        && std::isfinite(duration) && duration > 0.0;
}

juce::String AnalysisCoordinator::validateRequest(
    const SampleStorage* sample,
    const std::vector<std::shared_ptr<const Analyzer>>& analyzers)
{
    if (sample == nullptr || !isSampleUsable(*sample))
        return "Analysis requires a valid, non-empty mono or stereo Sample.";
    if (analyzers.empty())
        return "No analyzers were provided.";

    std::unordered_set<int> kinds;
    for (const auto& analyzer : analyzers)
    {
        if (analyzer == nullptr)
            return "The analysis request contains an empty analyzer.";
        const auto kind = static_cast<int>(analyzer->kind());
        if (kind < static_cast<int>(AnalyzerKind::bpm)
            || kind > static_cast<int>(AnalyzerKind::transients))
            return "The analysis request contains an unknown analyzer type.";
        if (!kinds.insert(kind).second)
            return "The analysis request contains the same analyzer type more than once.";
    }
    return {};
}

void AnalysisCoordinator::cancelCurrentLocked()
{
    if (currentCancellation != nullptr)
        currentCancellation->cancel();
    currentCancellation.reset();
}

void AnalysisCoordinator::workerLoop()
{
    for (;;)
    {
        WorkItem item;
        {
            std::unique_lock<std::mutex> lock(stateMutex);
            workAvailable.wait(lock, [this] { return shuttingDown || pendingWork.has_value(); });
            if (shuttingDown)
                return;
            item = std::move(*pendingWork);
            pendingWork.reset();
        }

        auto completed = run(item);
        {
            const std::lock_guard<std::mutex> lock(stateMutex);
            const auto isCurrent = !shuttingDown
                && result.identity == item.identity
                && result.status == AnalysisStatus::running
                && !item.cancellation->isCancellationRequested();
            if (isCurrent)
                result = std::move(completed);
            if (currentCancellation == item.cancellation)
                currentCancellation.reset();
        }
    }
}

AnalysisResult AnalysisCoordinator::run(const WorkItem& item) const
{
    AnalysisResult completed;
    completed.identity = item.identity;
    completed.status = AnalysisStatus::completed;
    const auto duration = item.sample->metadata().durationSeconds();

    for (const auto& analyzer : item.analyzers)
    {
        if (item.cancellation->isCancellationRequested())
            return makeTerminalResult(item.identity, AnalysisStatus::cancelled);

        AnalyzerOutput output;
        try
        {
            output = analyzer->analyze(*item.sample, *item.cancellation);
        }
        catch (const std::exception& exception)
        {
            return makeTerminalResult(item.identity, AnalysisStatus::failed,
                                      "Analyzer failed: " + juce::String(exception.what()));
        }
        catch (...)
        {
            return makeTerminalResult(item.identity, AnalysisStatus::failed,
                                      "Analyzer failed with an unknown error.");
        }

        if (item.cancellation->isCancellationRequested())
            return makeTerminalResult(item.identity, AnalysisStatus::cancelled);
        if (output.error.isNotEmpty())
            return makeTerminalResult(item.identity, AnalysisStatus::failed, output.error);
        if (output.payload == std::nullopt)
            return makeTerminalResult(item.identity, AnalysisStatus::failed,
                                      "Analyzer returned no result.");

        const auto kindMatches = std::visit([&analyzer](const auto& value)
        {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, BpmAnalysis>)
                return analyzer->kind() == AnalyzerKind::bpm;
            else if constexpr (std::is_same_v<Value, KeyAnalysis>)
                return analyzer->kind() == AnalyzerKind::key;
            else
                return analyzer->kind() == AnalyzerKind::transients;
        }, *output.payload);
        if (!kindMatches || !isValidAnalysisPayload(*output.payload, duration))
            return makeTerminalResult(item.identity, AnalysisStatus::failed,
                                      "Analyzer returned an invalid or mismatched result.");

        std::visit([&completed](const auto& value)
        {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, BpmAnalysis>)
                completed.bpm = value;
            else if constexpr (std::is_same_v<Value, KeyAnalysis>)
                completed.key = value;
            else
                completed.transients = value;
        }, *output.payload);
    }

    if (!completed.isValid())
        return makeTerminalResult(item.identity, AnalysisStatus::failed,
                                  "Analysis produced an invalid result.");
    return completed;
}
