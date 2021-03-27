#pragma once
#include "imu.hpp"
#include <functional>
#include <limits>
#include <span>

template <typename T> struct decompose;

template <typename Ret, typename T, typename Arg>
struct decompose<Ret (T::*)(Arg) const> {
	using arg = std::remove_cvref_t<Arg>;
};

auto getter(auto f) {
	return [](void *data, int idx) {
		using U = typename decompose<decltype(&decltype(f)::operator())>::arg;
		auto dataT = static_cast<const U *>(data);
		return std::invoke(decltype(f){}, dataT[idx]);
	};
}
struct l_plot {
	float history = 10.f;
	bool history_limited = true;
	float yscale = float(std::numeric_limits<int16_t>::max());
	bool scale_limited = true;
};

struct gyro_plot {
	struct sample {
		float ts;
		std::array<float, 3> values;
	};
	l_plot plt{};
	void show(std::span<const imu> data, std::span<const float, 3> dir);
	void show(std::span<const sample> data, std::span<const float, 3> dir);
};

struct acc_plot {
	struct sample {
		float ts;
		std::array<float, 3> values;
	};
	l_plot plt{};
	void show(std::span<const imu> data);
	void show(std::span<const sample> d);
};
