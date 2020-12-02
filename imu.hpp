#pragma once

#include <array>
#include <cstdint>
#include <string_view>

struct imu {
	uint32_t ts{};
	std::array<int16_t, 3> acc{};
	std::array<int16_t, 3> gyro{};
};

auto is_imu(std::string_view sv) -> bool;

auto to_imu(std::string_view sv) -> imu;
