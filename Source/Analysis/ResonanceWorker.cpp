#include "ResonanceWorker.h"
#include "ResonancePeakMath.h"
#include "audioplugins/common/dsp/Fft.h"
#include <algorithm>
#include <cmath>

ResonanceWorker::Worker::Worker (double sampleRate)
    : sampleRate_ (sampleRate)
{
    for (int i = 0; i < kFftSize; ++i)
        hannWindow_[static_cast<size_t> (i)] = 0.5f * (1.f - std::cos (
            2.f * 3.14159265358979323846f * static_cast<float> (i) / static_cast<float> (kFftSize)));
    fftRe_.assign (kFftSize, 0.f);
    fftIm_.assign (kFftSize, 0.f);
    resetState();
}

void ResonanceWorker::Worker::resetState() noexcept
{
    avgMag_.fill (0.f);
    historyBuffer_.fill (0.f);
    windowsSinceReset_ = 0;
    hopsSincePeakPick_ = 0;

    // Preserve/advance the generation counter across the reset so callers
    // can tell "a reset was processed" apart from "nothing happened yet" --
    // see ResonanceResult::generation's comment.
    const uint64_t nextGeneration = cachedResult_.generation + 1;
    cachedResult_ = ResonanceResult {};
    cachedResult_.generation = nextGeneration;
}

ResonanceWorker::ResonanceResult ResonanceWorker::Worker::processJob (const ResonanceJob& job) noexcept
{
    if (resetRequested.exchange (false, std::memory_order_acq_rel))
        resetState();

    std::copy (historyBuffer_.begin() + kHopSize, historyBuffer_.end(), historyBuffer_.begin());
    std::copy (job.samples.begin(), job.samples.end(), historyBuffer_.end() - kHopSize);

    for (int i = 0; i < kFftSize; ++i)
        fftScratch_[static_cast<size_t> (i)] =
            historyBuffer_[static_cast<size_t> (i)] * hannWindow_[static_cast<size_t> (i)];
    std::fill (fftScratch_.begin() + kFftSize, fftScratch_.end(), 0.f);

    std::copy (fftScratch_.begin(), fftScratch_.begin() + kFftSize, fftRe_.begin());
    std::fill (fftIm_.begin(), fftIm_.end(), 0.f);
    audioplugins::common::dsp::inplaceFft (fftRe_, fftIm_);
    for (int k = 0; k < kHalfN; ++k)
        fftScratch_[static_cast<size_t> (k)] =
            std::sqrt (fftRe_[static_cast<size_t> (k)] * fftRe_[static_cast<size_t> (k)]
                     + fftIm_[static_cast<size_t> (k)] * fftIm_[static_cast<size_t> (k)]);

    ++windowsSinceReset_;
    for (int k = 0; k < kHalfN; ++k)
    {
        auto& avg = avgMag_[static_cast<size_t> (k)];
        avg += (fftScratch_[static_cast<size_t> (k)] - avg) / static_cast<float> (windowsSinceReset_);
    }

    // cachedResult_ holds the most recent peak-pick outcome and is returned on
    // every hop (not just peak-pick hops). This is deliberate: AsyncWorker's
    // lastResult_ is overwritten on *every* processJob() call, so if this
    // returned a "nothing new" sentinel on the 4 out of 5 hops that don't
    // peak-pick, a getLatest() call landing on one of those hops would
    // observe (and the caller would have to discard) the sentinel instead of
    // the still-valid previous peak set -- worse, since getLatest() calls are
    // not synchronized with hop completion, a genuine peak result can be
    // overwritten by the next hop's sentinel before any audio-thread call
    // ever observes it. Returning the cached value on every hop makes
    // getLatest() always return the latest known-good peak set, regardless
    // of how the caller's polling cadence lines up with hop completion.
    if (++hopsSincePeakPick_ >= kPeakPickEveryNHops)
    {
        hopsSincePeakPick_ = 0;
        const auto peaks = pickTrueSightResonancePeaks (avgMag_.data(), kHalfN, sampleRate_, kFftSize);
        cachedResult_.count = std::min (static_cast<int> (peaks.size()), kMaxResults);
        for (int i = 0; i < cachedResult_.count; ++i)
        {
            cachedResult_.freqHz[static_cast<size_t> (i)] = peaks[static_cast<size_t> (i)].freqHz;
            cachedResult_.q[static_cast<size_t> (i)]      = peaks[static_cast<size_t> (i)].q;
            cachedResult_.gainDb[static_cast<size_t> (i)] = peaks[static_cast<size_t> (i)].gainDb;
        }
    }
    return cachedResult_;
}

ResonanceWorker::ResonanceWorker (AnalysisResult& result) : result_ (result) {}
ResonanceWorker::~ResonanceWorker() { suspend(); }

void ResonanceWorker::prepare (double sampleRate)
{
    suspend();
    worker_ = std::make_unique<Worker> (sampleRate);
    stagingFill_ = 0;
    // A freshly-constructed Worker already starts in a reset state (its own
    // generation counter restarts independently), so any reset the old
    // Worker was still being awaited for is moot -- avoid comparing a stale
    // resetBaselineGeneration_ against the new Worker's unrelated counter.
    awaitingReset_ = false;
    worker_->start();
}

void ResonanceWorker::suspend()
{
    if (worker_) worker_->stop();
}

void ResonanceWorker::requestReset() noexcept
{
    if (worker_)
    {
        // Capture the background worker's generation *before* asking for a
        // reset, so publishLatest() can later detect "a newer generation
        // than this has now been produced" -- i.e. the reset was actually
        // processed -- instead of guessing based on elapsed time.
        resetBaselineGeneration_ = worker_->getLatest().generation;
        awaitingReset_ = true;
        worker_->resetRequested.store (true, std::memory_order_release);
    }
    result_.resonanceCount.store (0, std::memory_order_relaxed);
}

void ResonanceWorker::publishLatest (const ResonanceResult& latest) noexcept
{
    if (awaitingReset_)
    {
        if (latest.generation == resetBaselineGeneration_)
        {
            // Background hasn't processed the reset yet: leave AnalysisResult
            // as requestReset() already zeroed it, rather than republishing
            // the stale pre-reset peak set this (still old-generation) result
            // carries.
            return;
        }
        awaitingReset_ = false;   // confirmed: a new generation was produced
    }

    for (int i = 0; i < latest.count; ++i)
    {
        result_.resonanceFreqHz[static_cast<size_t> (i)].store (latest.freqHz[static_cast<size_t> (i)], std::memory_order_relaxed);
        result_.resonanceQ[static_cast<size_t> (i)].store (latest.q[static_cast<size_t> (i)], std::memory_order_relaxed);
        result_.resonanceGainDb[static_cast<size_t> (i)].store (latest.gainDb[static_cast<size_t> (i)], std::memory_order_relaxed);
    }
    result_.resonanceCount.store (latest.count, std::memory_order_relaxed);
}

void ResonanceWorker::pushSamples (const float* mono, int numSamples) noexcept
{
    if (! worker_) return;

    int src = 0;
    while (src < numSamples)
    {
        const int toCopy = std::min (numSamples - src, kHopSize - stagingFill_);
        std::copy (mono + src, mono + src + toCopy, stagingBuffer_.begin() + stagingFill_);
        stagingFill_ += toCopy;
        src += toCopy;

        if (stagingFill_ == kHopSize)
        {
            ResonanceJob job;
            job.samples = stagingBuffer_;
            worker_->submit (job);   // never blocks; drops the hop if the queue is full
            stagingFill_ = 0;

            // Only publish once a hop has actually completed (~once per
            // 43ms at 48kHz), not once per pushSamples() call (which can be
            // once per host audio block, every 1-10ms) -- see pushSamples()'s
            // doc comment for why this getLatest() call isn't fully RT-safe
            // and should be taken as infrequently as correctness allows.
            publishLatest (worker_->getLatest());
        }
    }
}
