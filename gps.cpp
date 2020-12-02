#include "gps.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <numeric>

constexpr auto deg2rad(auto a) {
	return a / (180.f / std::numbers::pi_v<float>);
}

constexpr auto rad2deg(auto a) {
	return a * (180.f / std::numbers::pi_v<float>);
}

constexpr auto earth_radius = 6378137.f;

/* The following functions take their parameter and return their result in
 * degrees */

auto y2lat_d(auto y) {
	return rad2deg(std::atan(std::exp(deg2rad(y))) * 2 -
				   std::numbers::pi_v<float> / 2);
}

constexpr auto x2lon_d(auto x) { return x; }

auto lat2y_d(auto lat) {
	return rad2deg(
		std::log(std::tan(deg2rad(lat) / 2 + std::numbers::pi_v<float> / 4)));
}

constexpr auto lon2x_d(auto lon) { return lon; }

/* The following functions take their parameter in something close to meters,
 * along the equator, and return their result in degrees */

auto y2lat_m(auto y) {
	return rad2deg(2 * std::atan(std::exp(y / earth_radius)) -
				   std::numbers::pi_v<float> / 2);
}

auto x2lon_m(auto x) { return rad2deg(x / earth_radius); }

/* The following functions take their parameter in degrees, and return their
 * result in something close to meters, along the equator */

auto lat2y_m(auto lat) {
	return std::log(
			   std::tan(deg2rad(lat) / 2 + std::numbers::pi_v<float> / 4)) *
		   earth_radius;
}

auto lon2x_m(auto lon) { return deg2rad(lon) * earth_radius; }

DMMPos::operator DegPos() const {
	return {
		.lat = float(latdir) * lat.to_deg(),
		.lon = float(londir) * lon.to_deg(),
	};
}

DegPos::operator MercatorePos() const {
	return {
		.x = lon2x_d(lon),
		.y = lat2y_d(lat),
	};
}

constexpr auto is_num(std::string_view sv) -> bool {
	auto ltrim = sv | std::views::drop_while(isspace);
	auto to_skip = ltrim[0] == '-' ? 1 : 0;
	return std::ranges::all_of(ltrim.begin() + to_skip, ltrim.end(), isdigit);
}

constexpr auto is_latdir(std::string_view sv) -> bool {
	auto ltrim = sv | std::views::drop_while(isspace);
	return ltrim.size() == 1 && (ltrim[0] == 'N' || ltrim[0] == 'S');
}

constexpr auto to_latdir(std::string_view sv) -> lat_dir {
	auto ltrim = sv | std::views::drop_while(isspace);
	return ltrim[0] == 'N' ? lat_dir::N : lat_dir::S;
}
constexpr auto to_londir(std::string_view sv) -> lon_dir {
	auto ltrim = sv | std::views::drop_while(isspace);
	return ltrim[0] == 'W' ? lon_dir::W : lon_dir::E;
}
constexpr auto is_londir(std::string_view sv) -> bool {
	auto ltrim = sv | std::views::drop_while(isspace);
	return ltrim.size() == 1 && (ltrim[0] == 'E' || ltrim[0] == 'W');
}

/*
 * 		repl::out.printscl(
			"gpsrmc", print_time, int(ad), int(am), int32_t(add), latd, int(od),
			int(om), int32_t(odd), lond, int32_t(store.v->ground_speed_kmh),
			int32_t(store.r->heading), int32_t(store.g->altitude_mm),
			int32_t(store.g->hdop));
 */
bool is_gps_hybrid(std::string_view sv) {
	if (std::ranges::count(sv, ';') != 23) {
		return false;
	}

	auto colums =
		sv | std::views::split(';') | std::views::transform([](auto &&rng) {
			return std::string_view(&*rng.begin(),
									size_t(std::ranges::distance(rng)));
		});

	auto col = colums.begin();
	for (auto fn : {
			 +[](std::string_view in) { return in == "gpsrmc"; },
			 is_num,
			 is_num,
			 is_num,
			 is_num,
			 is_latdir,
			 is_num,
			 is_num,
			 is_num,
			 is_londir,
			 is_num,
			 is_num,
			 is_num,
			 is_num,
		 }) {
		if (!fn(*col++)) {
			return false;
		}
	}
	return true;

	return *col++ == "gpsrmc" && is_num(*col++) && is_num(*col++) &&
		   is_num(*col++) && is_num(*col++) && is_latdir(*col++) &&
		   is_num(*col++) && is_num(*col++) && is_num(*col++) &&
		   is_londir(*col++) && is_num(*col++) && is_num(*col++) &&
		   is_num(*col++) && is_num(*col);
}

auto to_gps_hybrid(std::string_view sv) -> gps_hybrid {
	constexpr auto to_num = [](auto strnum) {
		int64_t res = 0;
		std::from_chars(strnum.begin(), strnum.end(), res);
		return res;
	};
	auto colums =
		sv | std::views::split(';') | std::views::transform([](auto &&rng) {
			return std::string_view(&*rng.begin(),
									size_t(std::ranges::distance(rng)));
		});

	auto col = colums.begin();
	++col;
	return {
		.ts = uint32_t(to_num(*col++)),
		.pos =
			{
				.lat =
					{
						.deg = uint16_t(to_num(*col++)),
						.min = uint16_t(to_num(*col++)),
						.decimal = uint32_t(to_num(*col++)),
					},
				.latdir = to_latdir(*col++),
				.lon =
					{
						.deg = uint16_t(to_num(*col++)),
						.min = uint16_t(to_num(*col++)),
						.decimal = uint32_t(to_num(*col++)),
					},
				.londir = to_londir(*col),
			},
	};
}
