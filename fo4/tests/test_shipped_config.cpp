// Parses FO4SMP's shipped configs.json with FSMP's GlobalConfig and checks a few values.
#include "GlobalConfig.h"
#include <doctest/doctest.h>
#include <fstream>
#include <sstream>
TEST_CASE("FO4SMP shipped configs.json parses to its own values")
{
	std::ifstream f(FO4SMP_CONFIGS_JSON);
	REQUIRE(f.good());
	std::stringstream s; s << f.rdbuf();
	auto c = hdt::parseConfigJson(s.str());
	CHECK(c.numIterations == 16);
	CHECK(c.minFps == 60);
	CHECK(c.maxSubSteps == 4);
	CHECK(c.windEnabled == true);
	CHECK(c.distanceForMaxWind == doctest::Approx(2000.0f));
	CHECK(c.maximumActiveSkeletons == 5);
	auto layered = hdt::parseConfigJson(R"({"solver":{"numIterations":8}})", c);
	CHECK(layered.numIterations == 8);
	CHECK(layered.minFps == 60);
}
