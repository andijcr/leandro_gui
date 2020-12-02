#include "imu.hpp"
#include <algorithm>
#include <charconv>
#include <ranges>
constexpr auto is_num(std::string_view sv) {
	auto ltrim = sv | std::views::drop_while(isspace);
	auto to_skip = ltrim[0] == '-' ? 1 : 0;
	return std::ranges::all_of(ltrim.begin() + to_skip, ltrim.end(), isdigit);
}

auto is_imu(std::string_view sv) -> bool {
	if (std::ranges::count(sv, ';') != 7) {
		return false;
	}

	auto colums =
		sv | std::views::split(';') | std::views::transform([](auto &&rng) {
			return std::string_view(&*rng.begin(),
									size_t(std::ranges::distance(rng)));
		});
	if (colums.front() != "imu") {
		return false;
	}

	auto nums = colums.begin();
	++nums;
	return std::ranges::all_of(nums, colums.end(), is_num);
}

imu to_imu(std::string_view sv) {
	auto colums =
		sv | std::views::split(';') | std::views::transform([](auto &&rng) {
			return std::string_view(&*rng.begin(),
									size_t(std::ranges::distance(rng)));
		});

	auto nums =
		colums | std::views::drop(1) | std::views::transform([](auto strnum) {
			int64_t res;
			std::from_chars(strnum.begin(), strnum.end(), res);
			return res;
		});

	auto it = nums.begin();
	auto ts = *it;
	++it;
	auto ax = *it;
	++it;
	auto ay = *it;
	++it;
	auto az = *it;
	++it;
	auto gx = *it;
	++it;
	auto gy = *it;
	++it;
	auto gz = *it;
	return imu{uint32_t(ts),
			   {int16_t(ax), int16_t(ay), int16_t(az)},
			   {int16_t(gy), int16_t(gx), int16_t(gz)}};
}
