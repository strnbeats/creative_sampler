#include "TransientAnalyzer.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr size_t eventLimit = 65536;
constexpr std::array bandEdges { 180.0, 360.0, 720.0, 1440.0, 2880.0, 5760.0, 11520.0 };
using BandLevels = std::array<float, bandEdges.size() + 1>;

int floorPowerOfTwo(int value)
{
    int result = 1;
    while (result <= value / 2)
        result *= 2;
    return result;
}

float clampUnit(double value)
{
    return static_cast<float>(juce::jlimit(0.0, 1.0, value));
}

struct Impulse
{
    juce::int64 position = 0;
    double contrast = 0.0;
};

// Вторая разность выделяет короткий щелчок на фоне медленных колебаний.
Impulse findImpulse(const juce::AudioBuffer<float>& audio, juce::int64 estimate,
                    int radius, double sampleRate)
{
    const auto context = juce::roundToInt(0.014 * sampleRate);
    const auto first = std::max<juce::int64>(1, estimate - radius);
    const auto last = std::min<juce::int64>(audio.getNumSamples() - 1, estimate + radius + 1);
    if (first <= context)
        return {};

    Impulse result;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto* data = audio.getReadPointer(channel);
        const auto edge = [data](juce::int64 i)
        {
            return std::abs(2.0 * data[i] - data[i - 1] - data[i + 1]);
        };
        double power = 0.0;
        for (auto i = first - context; i < first; ++i)
        {
            const auto value = edge(i);
            power += value * value;
        }
        const auto floor = std::max(1.0e-6, std::sqrt(power / context));
        for (auto i = first; i < last; ++i)
        {
            const auto contrast = edge(i) / floor;
            if (contrast > result.contrast)
                result = { i, contrast };
        }
    }
    return result;
}

// Сравниваем несколько кадров после кандидата с локальным фоном до него.
// Максимум полосы сохраняет узкую атаку, которая теряется в полном RMS.
float measureSpectralSupport(const std::vector<BandLevels>& levels, size_t frame,
                             size_t left, size_t right, size_t after)
{
    BandLevels sums {}, squares {}, post {};
    for (auto j = left; j < right; ++j)
        for (size_t band = 0; band < sums.size(); ++band)
        {
            const auto level = levels[j][band];
            sums[band] += level;
            squares[band] += level * level;
        }
    for (auto j = frame + 1; j < after; ++j)
        for (size_t band = 0; band < sums.size(); ++band)
            post[band] += levels[j][band];
    const auto beforeCount = static_cast<float>(right - left);
    const auto afterCount = static_cast<float>(std::max<size_t>(1, after - frame - 1));
    float strongest = 0.0f, secondStrongest = 0.0f;
    for (size_t band = 0; band < sums.size(); ++band)
    {
        const auto mean = sums[band] / beforeCount;
        const auto deviation = std::sqrt(std::max(0.0f, squares[band] / beforeCount - mean * mean));
        auto contrast = (post[band] / afterCount - mean)
            / (deviation + 0.15f * mean + 0.000015f);
        // В самых низких полосах короткое окно содержит мало независимых отсчётов спектра.
        if (band < 2)
            contrast *= 0.65f;
        if (contrast > strongest)
        {
            secondStrongest = strongest;
            strongest = contrast;
        }
        else
            secondStrongest = std::max(secondStrongest, contrast);
    }
    // Для узкополосной атаки допустима одна полоса, но требуется больший контраст.
    return std::max(std::min(strongest, 2.0f * secondStrongest), 0.5f * strongest);
}
}

AnalyzerOutput TransientAnalyzer::analyze(const SampleStorage& sample,
                                          const AnalysisCancellationToken& cancellation) const
{
    const auto& audio = sample.audio();
    const auto& metadata = sample.metadata();
    const auto sampleRate = metadata.sampleRate;
    const auto sampleCount = static_cast<juce::int64>(audio.getNumSamples());
    if (sampleCount < 0 || !std::isfinite(sampleRate) || sampleRate < 8000.0
        || (audio.getNumChannels() != 1 && audio.getNumChannels() != 2)
        || metadata.sampleCount != sampleCount || parameters.maximumEvents == 0
        || !std::isfinite(parameters.frameDurationSeconds) || parameters.frameDurationSeconds <= 0.0
        || !std::isfinite(parameters.hopDurationSeconds) || parameters.hopDurationSeconds <= 0.0
        || !std::isfinite(parameters.minimumEventDistanceSeconds)
        || parameters.minimumEventDistanceSeconds < 0.0)
        return AnalyzerOutput::failure("Transient analysis requires a valid mono or stereo Sample.");

    TransientAnalysis result;
    result.sampleRate = sampleRate;
    result.sampleCount = sampleCount;
    if (sampleCount == 0)
    {
        result.detectionStatus = TransientDetectionStatus::insufficientData;
        return AnalyzerOutput::success(std::move(result));
    }
    const auto duration = static_cast<double>(sampleCount) / sampleRate;
    const auto channels = audio.getNumChannels();
    const auto frameSize = floorPowerOfTwo(juce::jlimit(128, 32768,
        juce::roundToInt(sampleRate * parameters.frameDurationSeconds)));
    const auto hop = juce::jmax(1, juce::roundToInt(sampleRate * parameters.hopDurationSeconds));
    if (duration < 0.04 || sampleCount < frameSize / 2)
    {
        result.detectionStatus = TransientDetectionStatus::insufficientData;
        return AnalyzerOutput::success(std::move(result));
    }

    const auto frameCount = static_cast<size_t>((sampleCount + hop - 1) / hop);
    std::vector<float> flux(frameCount, 0.0f);
    std::vector<float> energyRise(frameCount, 0.0f);
    std::vector<float> hfcRise(frameCount, 0.0f);
    std::vector<BandLevels> bandLevels(frameCount);
    std::array<size_t, bandEdges.size() + 1> bandEnds {};
    for (size_t i = 0; i < bandEdges.size(); ++i)
        bandEnds[i] = std::min(static_cast<size_t>(frameSize / 2 + 1),
                               static_cast<size_t>(std::ceil(bandEdges[i] * frameSize / sampleRate)));
    bandEnds.back() = static_cast<size_t>(frameSize / 2 + 1);
    const auto fftOrder = static_cast<int>(std::round(std::log2(static_cast<double>(frameSize))));
    juce::dsp::FFT fft(fftOrder);
    std::vector<float> fftBuffer(static_cast<size_t>(frameSize) * 2, 0.0f);
    std::vector<float> previous(static_cast<size_t>(frameSize / 2 + 1), 0.0f);
    std::vector<float> current(previous.size(), 0.0f);
    std::vector<double> channelMeans(static_cast<size_t>(channels), 0.0);
    for (int channel = 0; channel < channels; ++channel)
    {
        double sum = 0.0;
        for (juce::int64 i = 0; i < sampleCount; ++i)
        {
            if ((i & 65535) == 0 && cancellation.isCancellationRequested())
                return AnalyzerOutput::failure("Transient analysis cancelled.");
            const auto value = audio.getSample(channel, static_cast<int>(i));
            if (!std::isfinite(value))
                return AnalyzerOutput::failure("Transient analysis found non-finite audio data.");
            sum += value;
        }
        channelMeans[static_cast<size_t>(channel)] = sum / static_cast<double>(sampleCount);
    }
    double maximumDeviation = 0.0;
    for (int channel = 0; channel < channels; ++channel)
        for (juce::int64 i = 0; i < sampleCount; ++i)
        {
            if ((i & 65535) == 0 && cancellation.isCancellationRequested())
                return AnalyzerOutput::failure("Transient analysis cancelled.");
            maximumDeviation = std::max(maximumDeviation, std::abs(
                static_cast<double>(audio.getSample(channel, static_cast<int>(i)))
                - channelMeans[static_cast<size_t>(channel)]));
        }
    if (maximumDeviation < 1.0e-7)
    {
        result.detectionStatus = TransientDetectionStatus::insufficientData;
        return AnalyzerOutput::success(std::move(result));
    }
    std::vector<float> window(static_cast<size_t>(frameSize));
    for (int i = 0; i < frameSize; ++i)
        window[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(
            2.0f * juce::MathConstants<float>::pi * static_cast<float>(i)
            / static_cast<float>(frameSize - 1));

    float priorRms = 0.0f, priorHfc = 0.0f;
    for (size_t frame = 0; frame < frameCount; ++frame)
    {
        if ((frame & 31U) == 0U && cancellation.isCancellationRequested())
            return AnalyzerOutput::failure("Transient analysis cancelled.");

        double frameEnergy = 0.0;
        const auto center = static_cast<juce::int64>(frame) * hop;
        for (int offset = 0; offset < frameSize; ++offset)
        {
            const auto source = center + offset - frameSize / 2;
            double monoPower = 0.0;
            if (source >= 0 && source < sampleCount)
            {
                for (int channel = 0; channel < channels; ++channel)
                {
                    const auto value = static_cast<double>(audio.getSample(channel, static_cast<int>(source)))
                        - channelMeans[static_cast<size_t>(channel)];
                    monoPower += static_cast<double>(value) * value;
                }
                monoPower /= static_cast<double>(channels);
            }
            frameEnergy += monoPower;
        }
        const auto rms = static_cast<float>(std::sqrt(frameEnergy / static_cast<double>(frameSize)));

        // Transform each channel in turn and accumulate magnitudes; only fixed-size work buffers are used.
        std::fill(current.begin(), current.end(), 0.0f);
        for (int channel = 0; channel < channels; ++channel)
        {
            for (int offset = 0; offset < frameSize; ++offset)
            {
                const auto source = center + offset - frameSize / 2;
                const auto value = source >= 0 && source < sampleCount
                    ? static_cast<float>(static_cast<double>(audio.getSample(channel, static_cast<int>(source)))
                                         - channelMeans[static_cast<size_t>(channel)]) : 0.0f;
                fftBuffer[static_cast<size_t>(offset)] = value * window[static_cast<size_t>(offset)];
            }
            fft.performFrequencyOnlyForwardTransform(fftBuffer.data(), true);
            for (size_t bin = 0; bin < current.size(); ++bin)
                current[bin] += fftBuffer[bin] / static_cast<float>(channels);
        }

        double magnitudeSum = 1.0e-15;
        double weightedEnergy = 0.0;
        size_t bin = 1;
        for (size_t band = 0; band < bandEnds.size(); ++band)
        {
            double peakMagnitude = 0.0;
            for (; bin < bandEnds[band]; ++bin)
            {
                const auto magnitude = static_cast<double>(current[bin]);
                magnitudeSum += magnitude;
                weightedEnergy += magnitude * static_cast<double>(bin);
                peakMagnitude = std::max(peakMagnitude, magnitude);
            }
            bandLevels[frame][band] = static_cast<float>(peakMagnitude / frameSize);
        }
        double positiveDifference = 0.0;
        for (bin = 1; bin < current.size(); ++bin)
        {
            current[bin] = static_cast<float>(current[bin] / magnitudeSum);
            positiveDifference += std::max(0.0f, current[bin] - previous[bin]);
        }
        flux[frame] = static_cast<float>(positiveDifference);
        const auto hfc = static_cast<float>(weightedEnergy / magnitudeSum);
        if (frame > 0)
        {
            energyRise[frame] = static_cast<float>(std::max(0.0,
                (static_cast<double>(rms) - priorRms) / (static_cast<double>(rms) + 0.002)));
            hfcRise[frame] = priorHfc > 0.0f
                ? std::max(0.0f, (hfc - priorHfc) / (priorHfc + 1.0f)) : 0.0f;
        }
        priorRms = rms;
        priorHfc = hfc;
        previous.swap(current);
    }

    std::vector<float> novelty(frameCount, 0.0f);
    for (size_t i = 0; i < frameCount; ++i)
        novelty[i] = 0.72f * flux[i] + 0.20f * energyRise[i] + 0.08f * hfcRise[i];

    const auto neighborhood = static_cast<size_t>(juce::jmax(8, juce::roundToInt(0.35 * sampleRate / hop)));
    const auto exclusion = static_cast<size_t>(juce::jmax(1, juce::roundToInt(
        parameters.minimumEventDistanceSeconds * sampleRate / hop)));
    const auto localRadius = static_cast<size_t>(2);
    const auto maxEvents = std::min(parameters.maximumEvents, eventLimit);
    const auto framesForMs = [sampleRate, hop](double ms)
    {
        return static_cast<size_t>(std::max(1, juce::roundToInt(ms * 0.001 * sampleRate / hop)));
    };
    const auto preStart = framesForMs(36.0);
    const auto recentContext = framesForMs(18.0);
    const auto preEnd = framesForMs(7.0);
    const auto postEnd = framesForMs(9.0);
    const auto impulseRadius = static_cast<int>(std::min<juce::int64>(
        juce::roundToInt(0.005 * sampleRate), (static_cast<juce::int64>(exclusion) * hop - 1) / 2));
    std::vector<double> noveltyPrefix(frameCount + 1, 0.0);
    std::vector<double> squarePrefix(frameCount + 1, 0.0);
    for (size_t i = 0; i < frameCount; ++i)
    {
        if ((i & 1023U) == 0U && cancellation.isCancellationRequested())
            return AnalyzerOutput::failure("Transient analysis cancelled.");
        noveltyPrefix[i + 1] = noveltyPrefix[i] + novelty[i];
        squarePrefix[i + 1] = squarePrefix[i] + static_cast<double>(novelty[i]) * novelty[i];
    }
    struct Candidate
    {
        size_t frame;
        juce::int64 position;
        float confidence;
    };
    std::vector<Candidate> selected;
    selected.reserve(std::min(maxEvents, frameCount / 8 + 1));
    size_t effectiveCount = 0;
    size_t lastClusterFrame = 0;
    float lastClusterStrength = 0.0f;
    for (size_t i = 2; i + 2 < frameCount; ++i)
    {
        if ((i & 1023U) == 0U && cancellation.isCancellationRequested())
            return AnalyzerOutput::failure("Transient analysis cancelled.");
        const auto begin = i > neighborhood ? i - neighborhood : 0;
        const auto end = std::min(frameCount, i + neighborhood + 1);
        const auto excludedBegin = i > 3 ? std::max(begin, i - 3) : begin;
        const auto excludedEnd = std::min(end, i + 4);
        const auto total = end - begin;
        const auto excluded = excludedEnd - excludedBegin;
        const auto count = total - excluded;
        const auto mean = noveltyPrefix[end] - noveltyPrefix[begin]
            - (noveltyPrefix[excludedEnd] - noveltyPrefix[excludedBegin]);
        const auto square = squarePrefix[end] - squarePrefix[begin]
            - (squarePrefix[excludedEnd] - squarePrefix[excludedBegin]);
        if (count == 0)
            continue;
        const auto average = mean / static_cast<double>(count);
        const auto deviation = std::sqrt(std::max(0.0, square / static_cast<double>(count) - average * average));
        const auto localFloor = std::max(0.007, average + 3.4 * deviation);
        const auto hasEnergyAttack = energyRise[i] > 0.16f;
        const auto hasSpectralAttack = flux[i] > 0.035f;
        const auto priorFluxBegin = i > 8 ? i - 8 : 0;
        double priorFluxMean = 0.0;
        for (size_t j = priorFluxBegin; j < i; ++j)
            priorFluxMean += flux[j];
        priorFluxMean /= static_cast<double>(i - priorFluxBegin);
        const auto hasReattackOnDecay = flux[i] > 0.09f
            && flux[i] > priorFluxMean * 3.0
            && hfcRise[i] > 0.03f
            && novelty[i] > std::max(0.04, average * 1.6);
        const auto conventionalCandidate = (hasEnergyAttack && hasSpectralAttack && novelty[i] >= localFloor)
            || hasReattackOnDecay;
        if (novelty[i] < 0.015f || !hasSpectralAttack)
            continue;
        bool peak = true;
        for (size_t j = i - localRadius; j <= i + localRadius; ++j)
            if (j != i && (novelty[j] > novelty[i] || (j < i && novelty[j] >= novelty[i])))
                peak = false;
        if (peak)
        {
            // Компенсируем упреждение центрированного окна и отсекаем краевые эффекты padding.
            auto estimatedSample = static_cast<juce::int64>(i) * hop + frameSize / 4;
            if (estimatedSample < (conventionalCandidate ? frameSize / 2 : frameSize * 3 / 2)
                || estimatedSample >= sampleCount - frameSize / 2)
                continue;
            const auto risingAgain = energyRise[i] > 0.08f && flux[i] > priorFluxMean * 1.45;
            const auto history = risingAgain && !conventionalCandidate ? recentContext : preStart;
            const auto left = i > history ? i - history : 0;
            const auto right = std::max(left + 1, i > preEnd ? i - preEnd : 0);
            const auto after = std::min(frameCount, i + postEnd);
            const auto support = measureSpectralSupport(bandLevels, i, left, right, after);
            // Повторная атака может менять тембр на затухающем хвосте без прироста RMS.
            const auto cleanReattack = hasReattackOnDecay && priorFluxMean < 0.025
                && flux[i] > 0.12f && hfcRise[i] > 0.15f;
            auto evidence = static_cast<double>(support);
            const auto threshold = (conventionalCandidate || risingAgain) ? 4.0f : 4.75f;
            if (support < threshold && !cleanReattack)
            {
                if (!hasEnergyAttack || !hasSpectralAttack)
                    continue;
                const auto impulse = findImpulse(audio, estimatedSample, impulseRadius, sampleRate);
                if (impulse.contrast < 7.5)
                    continue;
                estimatedSample = impulse.position;
                evidence = impulse.contrast;
            }
            if (cleanReattack)
                evidence = std::max(evidence, 4.0);
            const auto confidence = clampUnit(0.75 * evidence / (evidence + 5.0)
                + 0.25 * std::min(1.0, std::max(0.0, novelty[i] - average) / (localFloor + 0.01)));
            if (effectiveCount == 0 || i - lastClusterFrame >= exclusion)
            {
                ++effectiveCount;
                if (selected.size() < maxEvents)
                {
                    selected.push_back({ i, estimatedSample, confidence });
                }
                lastClusterFrame = i;
                lastClusterStrength = novelty[i];
            }
            else if (novelty[i] > lastClusterStrength)
            {
                if (effectiveCount <= selected.size())
                {
                    selected.back() = { i, estimatedSample, confidence };
                }
                lastClusterFrame = i;
                lastClusterStrength = novelty[i];
            }
        }
    }

    const auto globalMaximum = *std::max_element(novelty.begin(), novelty.end());
    result.totalEventCount = effectiveCount;
    const auto storedCount = selected.size();
    result.positions.reserve(storedCount);
    for (size_t index = 0; index < storedCount; ++index)
    {
        const auto& candidate = selected[index];
        const auto frame = candidate.frame;
        const auto samplePosition = candidate.position;
        const auto strength = clampUnit(std::sqrt(static_cast<double>(novelty[frame])
                                                   / std::max(1.0e-9f, globalMaximum)));
        result.positions.push_back({ static_cast<double>(samplePosition) / sampleRate,
            strength, candidate.confidence, samplePosition,
            static_cast<double>(samplePosition) / static_cast<double>(std::max<juce::int64>(1, sampleCount - 1)),
            clampUnit(static_cast<double>(flux[frame]) / 0.20) });
    }

    result.detectionStatus = effectiveCount == 0 ? TransientDetectionStatus::insufficientData
        : (effectiveCount > storedCount ? TransientDetectionStatus::truncated
                                        : TransientDetectionStatus::detected);
    return AnalyzerOutput::success(std::move(result));
}
