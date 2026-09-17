#include "Analysis/BandConfig.h"
#include "test_runner.h"
#include <cstring>

int main() {
    CHECK(BandConfig::numBands == 7);
    CHECK_MSG(std::abs(BandConfig::crossoverHz[0] - 80.f) < 1e-6f, "first crossover should be 80 Hz");
    CHECK_MSG(std::abs(BandConfig::crossoverHz[5] - 16000.f) < 1e-6f, "last crossover should be 16 kHz");
    CHECK_MSG(std::strcmp(BandConfig::bandNames[0], "Sub") == 0, "band 0 should be Sub");
    CHECK_MSG(std::strcmp(BandConfig::bandNames[6], "Air") == 0, "band 6 should be Air");
    CHECK(BandConfig::bandIsShelf[0] == true);
    CHECK(BandConfig::bandIsShelf[3] == false);
    CHECK(BandConfig::bandIsShelf[6] == true);
    TEST_SUMMARY();
}
