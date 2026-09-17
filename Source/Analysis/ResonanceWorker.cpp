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
    cachedResult_ = ResonanceResult {};
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
    worker_->start();
}

void ResonanceWorker::suspend()
{
    if (worker_) worker_->stop();
}

void ResonanceWorker::requestReset() noexcept
{
    if (worker_) worker_->resetRequested.store (true, std::memory_order_release);
    result_.resonanceCount.store (0, std::memory_order_relaxed);
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
        }
    }

    // Publish the latest known-good peak-pick result every call (cheap: a
    // getLatest() copy of a small POD). The worker always returns its last
    // computed peak set (see Worker::processJob), so this is safe to
    // unconditionally overwrite AnalysisResult with -- between peak-pick
    // cycles it's simply republishing the same values.
    const auto latest = worker_->getLatest();
    for (int i = 0; i < latest.count; ++i)
    {
        result_.resonanceFreqHz[static_cast<size_t> (i)].store (latest.freqHz[static_cast<size_t> (i)], std::memory_order_relaxed);
        result_.resonanceQ[static_cast<size_t> (i)].store (latest.q[static_cast<size_t> (i)], std::memory_order_relaxed);
        result_.resonanceGainDb[static_cast<size_t> (i)].store (latest.gainDb[static_cast<size_t> (i)], std::memory_order_relaxed);
    }
    result_.resonanceCount.store (latest.count, std::memory_order_relaxed);
}
