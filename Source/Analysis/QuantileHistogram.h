#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include <cmath>

// Fixed-bucket nearest-rank quantile estimator: an allocation-free, O(1)-add
// replacement for "collect everything into a vector, sort, index" (which is
// unusable on a real-time audio thread). Bucket width trades memory/accuracy;
// 0.5 dB is well below the 0.5 dB dead-zone already used for advice display,
// so the quantization is not perceptible in the derived advice.
class QuantileHistogram
{
public:
    static constexpr float minDb   = -100.f;
    static constexpr float maxDb   =   12.f;
    static constexpr float bucketW =    0.5f;
    static constexpr int   numBuckets = static_cast<int> ((maxDb - minDb) / bucketW);

    void reset() noexcept
    {
        counts_.fill (0);
        total_ = 0;
    }

    void addSample (float valueDb) noexcept
    {
        const float clamped = std::clamp (valueDb, minDb, maxDb - bucketW);
        const int idx = std::clamp (static_cast<int> ((clamped - minDb) / bucketW), 0, numBuckets - 1);
        ++counts_[static_cast<size_t> (idx)];
        ++total_;
    }

    // p in [0, 1]. Nearest-rank: floor(p * N) 0-based index, matching the
    // offline sort-and-index convention this replaces.
    float percentile (float p) const noexcept
    {
        if (total_ == 0)
            return minDb;

        const uint64_t targetRank = static_cast<uint64_t> (
            std::clamp (std::floor (static_cast<double> (p) * static_cast<double> (total_)),
                        0.0, static_cast<double> (total_ - 1)));

        uint64_t cumulative = 0;
        for (int i = 0; i < numBuckets; ++i)
        {
            cumulative += counts_[static_cast<size_t> (i)];
            if (cumulative > targetRank)
                return minDb + (static_cast<float> (i) + 0.5f) * bucketW;
        }
        return maxDb;
    }

    uint64_t sampleCount() const noexcept { return total_; }

private:
    std::array<uint32_t, static_cast<size_t> (numBuckets)> counts_ {};
    uint64_t total_ { 0 };
};
