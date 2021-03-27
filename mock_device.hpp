#pragma once
#include "gps.hpp"
#include "imu.hpp"
#include <cstdint>
#include <random>
#include <vector>

uint64_t get_ms();

struct lclock {
	struct Time {
		uint64_t ms;
		auto asSeconds() -> float { return float(ms) / 1000.f; }
	};

	uint64_t start;
	lclock();
	auto getElapsedTime() -> Time;
	void restart();
};

struct mock_imu {
	lclock test_data_clock{};
	imu last{};
	size_t generated{0};
	std::vector<imu> imu_samples{};

	std::default_random_engine random_engine{std::random_device{}()};
	std::uniform_int_distribution<int> random_acc{-2, 2};
	std::uniform_int_distribution<int> random_ts{0, 1};

	std::uniform_int_distribution<int> attractor_random{-5000, 5000};
	std::pair<std::array<int16_t, 3>, std::array<int16_t, 3>> attractor{};
	lclock attractor_clock{};

	std::array<float, 3> imu_direction{};

	void update();
};

constexpr auto office = DegPos{41.9134432f, 12.5010377f};
struct mock_gps {
	std::default_random_engine random_engine{std::random_device{}()};
	std::uniform_real_distribution<float> random_acc{-1 / 10'000.f,
													 1 / 10'000.f};

	lclock test_data_clock{};
	DegPos last = office;
	size_t generated{0};
	std::vector<DegPos> gps_samples{};
	std::array<DegPos, 2> gps_boundingbox{office, office};
	void update();
};

struct mock_device : mock_gps, mock_imu {
	using mock_gps::gps_samples;
	using mock_imu::imu_direction;
	using mock_imu::imu_samples;
	std::string label{"mock_data"};

	void update() {
		mock_gps::update();
		mock_imu::update();
	}
};
