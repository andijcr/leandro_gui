#include "mock_device.hpp"
#include <numbers>
#include <span>
#include <thread>

uint64_t get_ms() {
	static const auto app_start = std::chrono::system_clock::now();

	auto d = std::chrono::system_clock::now() - app_start;
	return uint64_t(
		std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
}

lclock::lclock() { restart(); }

lclock::Time lclock::getElapsedTime() { return {get_ms() - start}; }

void lclock::restart() { start = get_ms(); }

void mock_imu::update() {
	if (attractor_clock.getElapsedTime().asSeconds() > 3.f) {
		attractor_clock.restart();
		for (auto &a : attractor.first) {
			a = int16_t(attractor_random(random_engine));
		}
		for (auto &a : attractor.second) {
			a = int16_t(attractor_random(random_engine));
		}
	}
	const auto to_add = [&] {
		const auto expected = std::floor(
			test_data_clock.getElapsedTime().asSeconds() / 0.016891892f);
		return int(expected - generated);
	}();
	if (to_add <= 0) {
		return;
	}

	generated += to_add;
	auto new_data_gen = [&, back = last]() mutable {
		auto zero_attractor = [&](int v, int tgt) {
			return int16_t(
				std::clamp<int>(v + (tgt - v) * random_acc(random_engine) +
									random_acc(random_engine),
								std::numeric_limits<int16_t>::min(),
								std::numeric_limits<int16_t>::max()));
		};

		back = imu{
			back.ts += uint32_t(16 + random_ts(random_engine)),
			{
				zero_attractor(back.acc[0], attractor.first[0]),
				zero_attractor(back.acc[1], attractor.first[1]),
				zero_attractor(back.acc[2], attractor.first[2]),
			},
			{
				zero_attractor(back.gyro[0], attractor.second[0]),
				zero_attractor(back.gyro[1], attractor.second[1]),
				zero_attractor(back.gyro[2], attractor.second[2]),
			},
		};
		return back;
	};

	for (auto sam = 0; sam < to_add; ++sam) {
		imu_samples.emplace_back(new_data_gen());

		const auto &gyr = imu_samples.back().gyro;
		const auto dt =
			(imu_samples.back().ts - (imu_samples.end() - 2)->ts) / 1000.f;
		for (auto i = 0u; i < gyr.size(); ++i) {
			imu_direction[i] = std::remainder(
				imu_direction[i] +
					to_radians(to_increment(to_degs_per_sec(gyr[i], 245), dt)),
				std::numbers::pi_v<float>);
		}
	}

	last = imu_samples.back();
}

constexpr auto update_bb(std::span<DegPos, 2> old_bb, const DegPos &pos) {
	auto &ul = old_bb[0];
	auto &dr = old_bb[1];
	ul.lat = std::max(ul.lat, pos.lat);
	dr.lat = std::min(dr.lat, pos.lat);
	ul.lon = std::min(ul.lon, pos.lon);
	dr.lon = std::max(dr.lon, pos.lon);
}

void mock_gps::update() {
	const auto to_add = [&] {
		const auto expected =
			std::floor(test_data_clock.getElapsedTime().asSeconds());
		return int(expected - generated);
	}();
	if (to_add <= 0) {
		return;
	}

	generated += to_add;
	for (auto i = 0; i < to_add; ++i) {
		last.lat += random_acc(random_engine);
		last.lon += random_acc(random_engine);
		update_bb(gps_boundingbox, last);
		gps_samples.emplace_back(last);
	}
}
