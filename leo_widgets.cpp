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

template <typename G>
auto plotline(std::span<const imu> data, const plot_trace<G> &trace) {
	ImPlot::PlotLineG(trace.name, G{}, const_cast<imu *>(data.data()),
					  int(data.size()));
}

void show_lplot(l_plot &plot_data, const char *yunits,
				std::span<const imu> data, auto... traces) {
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
	if (data.size() > 0) {
		const auto &[last_t, last_acc, last_gyro] = data.back();

		if (plot_data.history_limited) {
			ImPlot::SetNextPlotLimitsX(last_t / 1000. -
										   double(plot_data.history),
									   last_t / 1000., ImGuiCond_Always);
		}

		if (plot_data.scale_limited) {
			ImPlot::SetNextPlotLimitsY(-double(plot_data.yscale) * 1.1,
									   double(plot_data.yscale) * 1.1,
									   ImGuiCond_Always);
		}
	}
	if (ImPlot::BeginPlot("", "s", yunits, ImVec2(-1, 400))) {
		(plotline(data, traces), ...);
		ImPlot::EndPlot();
	}
}

void gyro_plot::show(std::span<const imu> data, std::span<const float, 3> dir) {
	plot_direction("x", dir[0]);
	ImGui::SameLine();
	plot_direction("y", dir[1]);
	ImGui::SameLine();
	plot_direction("z", dir[2]);
	show_lplot(plt, "nesi", data,
			   make_trace("x", getter([](const imu &im) {
							  return ImPlotPoint(im.ts / 1000., im.gyro[0]);
						  })),
			   make_trace("y", getter([](const imu &im) {
							  return ImPlotPoint(im.ts / 1000., im.gyro[1]);
						  })),
			   make_trace("z", getter([](const imu &im) {
							  return ImPlotPoint(im.ts / 1000., im.gyro[2]);
						  })));
	ImGui::End();
}

void acc_plot::show(std::span<const imu> data) {
	show_lplot(plt, "leandri", data,
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
