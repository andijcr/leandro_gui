#include "leo_widgets.hpp"
#include <cmath>
#include <numbers>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include "implot.h"
#pragma GCC diagnostic pop

static void plot_direction(const char *label, float p_value) {
	auto &style = ImGui::GetStyle();

	const auto radius_outer = 20.0f;
	const auto pos = ImGui::GetCursorScreenPos();
	const auto center = ImVec2(pos.x + radius_outer, pos.y + radius_outer);
	const auto line_height = ImGui::GetTextLineHeight();
	auto *draw_list = ImGui::GetWindowDrawList();

	const auto angle = 2 * std::numbers::pi_v<float> * p_value;
	const auto angle_cos = std::cos(angle);
	const auto angle_sin = std::sin(angle);
	const auto radius_inner = radius_outer * 0.40f;
	ImGui::InvisibleButton(
		label, ImVec2(radius_outer * 2, radius_outer * 2 + line_height +
											style.ItemInnerSpacing.y));
	const auto is_active = ImGui::IsItemActive();
	const auto is_hovered = ImGui::IsItemHovered();

	draw_list->AddCircleFilled(center, radius_outer,
							   ImGui::GetColorU32(ImGuiCol_FrameBg), 16);
	draw_list->AddLine(ImVec2(center.x + angle_cos * radius_inner,
							  center.y + angle_sin * radius_inner),
					   ImVec2(center.x + angle_cos * (radius_outer - 2),
							  center.y + angle_sin * (radius_outer - 2)),
					   ImGui::GetColorU32(ImGuiCol_SliderGrabActive), 2.0f);
	draw_list->AddCircleFilled(center, radius_inner,
							   ImGui::GetColorU32(ImGuiCol_FrameBgActive), 16);
	draw_list->AddText(
		ImVec2(pos.x, pos.y + radius_outer * 2 + style.ItemInnerSpacing.y),
		ImGui::GetColorU32(ImGuiCol_Text), label);

	if (is_active || is_hovered) {
		ImGui::SetNextWindowPos(
			ImVec2(pos.x - style.WindowPadding.x, pos.y - line_height -
													  style.ItemInnerSpacing.y -
													  style.WindowPadding.y));
		ImGui::BeginTooltip();
		ImGui::Text("%.3f", double(p_value));
		ImGui::EndTooltip();
	}
}

template <typename Get> struct plot_trace {
	const char *name;
	using getter_t = Get;
};

auto make_trace(const char *name, auto getter) {
	return plot_trace<decltype(getter)>{name};
}

template <typename G, typename T>
auto plotline(std::span<const T> data, const plot_trace<G> &trace) {
	ImPlot::PlotLineG(trace.name, G{}, const_cast<T *>(data.data()),
					  int(data.size()));
}

template <typename T, typename... TR>
void show_lplot(l_plot &plot_data, const char *yunits, double time,
				std::span<const T> data, TR... traces) {
	ImGui::SetNextWindowSize(ImVec2(600, 600), ImGuiCond_FirstUseEver);
	ImGui::Checkbox("lock##hist", &plot_data.history_limited);
	if (plot_data.history_limited) {
		ImGui::SameLine();
		ImGui::SliderFloat("History", &plot_data.history, 1, 30, "%.1f s");
	}

	ImGui::Checkbox("lock##scale", &plot_data.scale_limited);
	if (plot_data.scale_limited) {
		ImGui::SameLine();
		ImGui::SliderFloat("Scale", &plot_data.yscale, 1,
						   std::numeric_limits<int16_t>::max(), "%.1f");
	}
	if (plot_data.history_limited) {
		ImPlot::SetNextPlotLimitsX(time - double(plot_data.history), time,
								   ImGuiCond_Always);
	}

	if (plot_data.scale_limited) {
		ImPlot::SetNextPlotLimitsY(-double(plot_data.yscale) * 1.1,
								   double(plot_data.yscale) * 1.1,
								   ImGuiCond_Always);
	}
	if (ImPlot::BeginPlot("", "s", yunits, ImVec2(-1, 400))) {
		(plotline(data, traces), ...);
		ImPlot::EndPlot();
	}
}

static auto last_ts(std::span<const imu> data) {
	if (data.size() > 0) {
		return data.back().ts / 1000.;
	}
	return 0.;
}
static auto last_ts(std::span<const acc_plot::sample> data) {
	if (data.size() > 0) {
		return float(data.back().ts);
	}
	return 0.f;
}

static auto last_ts(std::span<const gyro_plot::sample> data) {
	if (data.size() > 0) {
		return float(data.back().ts);
	}
	return 0.f;
}

void gyro_plot::show(std::span<const imu> data, std::span<const float, 3> dir) {
	plot_direction("x", dir[0]);
	ImGui::SameLine();
	plot_direction("y", dir[1]);
	ImGui::SameLine();
	plot_direction("z", dir[2]);
	show_lplot(plt, "nesi", last_ts(data), data,
			   make_trace("x", getter([](const imu &im) {
							  return ImPlotPoint(im.ts / 1000., im.gyro[0]);
						  })),
			   make_trace("y", getter([](const imu &im) {
							  return ImPlotPoint(im.ts / 1000., im.gyro[1]);
						  })),
			   make_trace("z", getter([](const imu &im) {
							  return ImPlotPoint(im.ts / 1000., im.gyro[2]);
						  })));
}

void gyro_plot::show(std::span<const sample> data,
					 std::span<const float, 3> dir) {
	plot_direction("x", dir[0]);
	ImGui::SameLine();
	plot_direction("y", dir[1]);
	ImGui::SameLine();
	plot_direction("z", dir[2]);
	show_lplot(plt, "nesi", last_ts(data), data,
			   make_trace("x", getter([](const sample &im) {
							  return ImPlotPoint{im.ts, im.values[0]};
						  })),
			   make_trace("y", getter([](const sample &im) {
							  return ImPlotPoint{im.ts, im.values[1]};
						  })),
			   make_trace("z", getter([](const sample &im) {
							  return ImPlotPoint{im.ts, im.values[2]};
						  })));
}

void acc_plot::show(std::span<const sample> d) {
	show_lplot(plt, "leandri", last_ts(d), d,
			   make_trace("x", getter([](const acc_plot::sample &dt) {
							  return ImPlotPoint{dt.ts, dt.values[0]};
						  })),
			   make_trace("y", getter([](const acc_plot::sample &dt) {
							  return ImPlotPoint{dt.ts, dt.values[1]};
						  })),
			   make_trace("z", getter([](const acc_plot::sample &dt) {
							  return ImPlotPoint{dt.ts, dt.values[2]};
						  })));
}

void acc_plot::show(std::span<const imu> data) {
	show_lplot(plt, "leandri", last_ts(data), data,
			   make_trace("x", getter([](const imu &im) {
							  return ImPlotPoint(im.ts / 1000., im.acc[0]);
						  })),
			   make_trace("y", getter([](const imu &im) {
							  return ImPlotPoint(im.ts / 1000., im.acc[1]);
						  })),
			   make_trace("z", getter([](const imu &im) {
							  return ImPlotPoint(im.ts / 1000., im.acc[2]);
						  })));
}
