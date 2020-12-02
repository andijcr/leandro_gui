#pragma once
#include <cstdint>
#include <string_view>

enum class lat_dir { N = 1, S = -1 };
enum class lon_dir { E = 1, W = -1 };

struct DMM {
	uint16_t deg;
	uint16_t min;
	uint32_t decimal;
	constexpr auto to_deg() const {
		return deg + (min + float(decimal) / 1'000'000.f) / 60.f;
	}
};

struct MercatorePos {
	float x;
	float y;
};

struct DegPos {
	float lat;
	float lon;
	explicit operator MercatorePos() const;
};

struct DMMPos {
	DMM lat;
	lat_dir latdir;
	DMM lon;
	lon_dir londir;
	explicit operator DegPos() const;
};

struct gps_hybrid {
	// gpsrmc; 193035; 41; 54; 829100; N; 12; 30; 96900; E; 410; 212830; 210400;
	// 1460
	uint32_t ts;
	DMMPos pos;
};

auto is_gps_hybrid(std::string_view sv) -> bool;

auto to_gps_hybrid(std::string_view sv) -> gps_hybrid;
