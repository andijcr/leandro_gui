#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "fmt/compile.h"
#include "fmt/ranges.h"
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "magic_enum.hpp"
#pragma GCC diagnostic pop

#include "spdlog/cfg/env.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <numbers>
#include <ranges>
#include <span>
#include <variant>

#include "gps.hpp"
#include "imu.hpp"
#include "leo_widgets.hpp"
#include "mock_device.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"

#include <glad/glad.h>

#include <GLFW/glfw3.h>

#include "imgui.h"
#include "implot.h"

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#pragma GCC diagnostic pop

extern "C" {
#include "libserialport.h"
}

struct arg_exception : public std::exception {};
struct mem_exception : public std::exception {};
struct supp_exception : public std::exception {};
struct sys_exception : public std::system_error {
	using std::system_error::system_error;
};

struct boh_exception : public std::exception {};

int wrap(auto ret_code) {
	if (ret_code >= 0) {
		[[likely]] return ret_code;
	}
	switch (ret_code) {
	case sp_return::SP_OK:
		[[likely]] return 0;
	case sp_return::SP_ERR_ARG:
		throw arg_exception{};
	case sp_return::SP_ERR_MEM:
		[[unlikely]] throw mem_exception{};
	case sp_return::SP_ERR_SUPP:
		throw supp_exception{};
	case sp_return::SP_ERR_FAIL:
		throw sys_exception(sp_last_error_code(), std::system_category(),
							sp_last_error_message());
	}
	{ [[unlikely]] throw boh_exception{}; }
}

template <typename T>
struct fmt::formatter<T, std::enable_if_t<std::is_enum_v<T>>> {

	constexpr auto parse(format_parse_context &ctx) { return ctx.end(); }

	auto format(auto t, auto &fc) {
		return fmt::format_to(fc.out(), FMT_COMPILE("{}"),
							  magic_enum::enum_name(t));
	}
};

constexpr auto sp_config_deleter = [](sp_port_config *p) { sp_free_config(p); };
struct config {
	using sp_config_p =
		std::unique_ptr<sp_port_config, decltype(sp_config_deleter)>;

	sp_config_p p;
	config() {
		sp_port_config *dest;
		sp_new_config(&dest);
		p.reset(dest);
	}

	config(int baudrate) : config() {
		sp_set_config_baudrate(p.get(), baudrate);
		sp_set_config_bits(p.get(), 8);
		sp_set_config_parity(p.get(), sp_parity::SP_PARITY_NONE);
		sp_set_config_stopbits(p.get(), 1);

		sp_set_config_cts(p.get(), sp_cts::SP_CTS_IGNORE);
		sp_set_config_dsr(p.get(), sp_dsr::SP_DSR_IGNORE);
		sp_set_config_dtr(p.get(), sp_dtr::SP_DTR_OFF);
		sp_set_config_flowcontrol(p.get(), sp_flowcontrol::SP_FLOWCONTROL_NONE);
		sp_set_config_rts(p.get(), sp_rts::SP_RTS_OFF);
		sp_set_config_xon_xoff(p.get(), sp_xonxoff::SP_XONXOFF_DISABLED);
	}
	operator const sp_port_config *() const { return p.get(); }
	operator sp_port_config *() { return p.get(); }
};

constexpr auto sp_port_deleter = [](sp_port *p) { sp_free_port(p); };
using sp_port_p = std::shared_ptr<sp_port>; //, decltype(sp_port_deleter)>;

struct open_port;
struct port {
	sp_port_p p;

	// explicit port(sp_port_p &&p_) : p{p_} {}
	explicit port(sp_port *source) {
		sp_port *dest;
		sp_copy_port(source, &dest);
		p = {dest, sp_free_port};
	}

	auto name() const noexcept -> std::string {
		const auto res = sp_get_port_name(p.get());
		return res ? res : "";
	}
	auto description() const noexcept -> std::string {
		const auto res = sp_get_port_description(p.get());
		return res ? res : "";
	}

	auto open() & -> open_port;
	auto open() && -> open_port;
};

struct open_port {
	sp_port_p p;

	auto name() const noexcept -> std::string {
		const auto res = sp_get_port_name(p.get());
		return res ? res : "";
	}
	auto description() const noexcept -> std::string {
		const auto res = sp_get_port_description(p.get());
		return res ? res : "";
	}

	auto set_config(const config &cfg) -> open_port & {
		wrap(sp_set_config(p.get(), cfg));
		return *this;
	}

	auto get_config() const noexcept -> config {
		struct config c {};
		wrap(sp_get_config(p.get(), c));
		return c;
	}
	operator const sp_port *() const { return p.get(); }
	operator sp_port *() { return p.get(); }
};

auto port::open() & -> open_port {
	wrap(sp_open(p.get(), sp_mode::SP_MODE_READ));
	return open_port{p};
}
auto port::open() && -> open_port {
	wrap(sp_open(p.get(), sp_mode::SP_MODE_READ));
	return open_port{std::move(p)};
}

auto get_ports() -> std::vector<port> {
	spdlog::debug("Getting port list");

	sp_port **port_list;
	const auto result = sp_list_ports(&port_list);

	if (result != SP_OK) {
		spdlog::warn("sp_list_ports() failed: {}", result);
		return {};
	}

	/* Iterate through the ports. When port_list[i] is NULL
	 * this indicates the end of the list. */
	auto res = std::vector<port>{};
	for (auto i = 0; port_list[i] != NULL; ++i) {
		res.emplace_back(port_list[i]);
		spdlog::debug(FMT_COMPILE("Found port: {}"),
					  sp_get_port_name(port_list[i]));
	}
	spdlog::debug(FMT_COMPILE("Found {} ports"), res.size());

	sp_free_port_list(port_list);
	return res;
}

struct line_getter {
	std::string rest{};
	size_t check_from = 0;
	open_port p;
	line_getter(open_port op) : p{std::move(op)} {}
	auto operator()() -> std::optional<std::string> {
		// find a line inside
		auto nl = rest.find('\n', check_from);
		if (nl != rest.npos) {
			auto line = rest.substr(0, nl);
			rest.erase(0, nl + 1);
			check_from = 0;
			return line;
		}
		check_from = rest.size();

		// try to pull
		if (const auto in_count = size_t(wrap(sp_input_waiting(p)));
			in_count > 0) {
			spdlog::debug(FMT_COMPILE("pulling {} bytes from {}"), in_count,
						  p.name());
			// reserve space inside the string
			rest.append(in_count, 0);
			// and append directly there (tanks to the fact that the old size is
			// in check_from - not the best
			wrap(sp_nonblocking_read(p, rest.data() + check_from, in_count));
		}
		return {};
	}
};

struct data_source {
	line_getter get;
	auto operator()() -> std::optional<std::variant<imu, gps_hybrid>> {
		const auto line = get();
		if (!line) {
			return {};
		}

		spdlog::debug("line: {}", *line);
		if (is_imu(*line)) {
			return to_imu(*line);
		}
		if (is_gps_hybrid(*line)) {
			return to_gps_hybrid(*line);
		}
		spdlog::debug(R"(unhandled: "{}")", *line);
		return {};
	}
};

struct device_samples {
	std::vector<imu> imu_samples{};
	std::array<float, 3> imu_direction{};
	std::vector<DegPos> gps_samples{};
	std::array<DegPos, 2> gps_boundingbox{};
	std::string label;
	data_source src;
	device_samples(open_port &&p)
		: label{fmt::format(FMT_COMPILE("{}-{}"), p.name(), p.description())},
		  src{line_getter(std::move(p))} {}

	void update_direction(const imu &im, float dt) {
		for (auto i = 0u; i < im.gyro.size(); ++i) {
			imu_direction[i] = std::remainder(
				imu_direction[i] + to_radians(to_increment(
									   to_degs_per_sec(im.gyro[i], 245), dt)),
				std::numbers::pi_v<float>);
		}
	}

	void update_bb(const DegPos &pos) {
		auto &[ul, dr] = gps_boundingbox;
		ul.lat = std::max(ul.lat, pos.lat);
		dr.lat = std::min(dr.lat, pos.lat);
		ul.lon = std::min(ul.lon, pos.lon);
		dr.lon = std::max(dr.lon, pos.lon);
	}
	void update() {
		auto sample = src();
		if (!sample) {
			return;
		}
		if (std::holds_alternative<imu>(*sample)) {
			update_direction(std::get<imu>(*sample), 1 / 59.8f);
			imu_samples.emplace_back(std::get<imu>(*sample));
			return;
		}

		if (std::holds_alternative<gps_hybrid>(*sample)) {
			auto pos = DegPos(std::get<gps_hybrid>(*sample).pos);
			update_bb(pos);
			gps_samples.emplace_back(pos);
		}
	}
};

struct data_adapter {
	std::string label;
	std::function<void()> update;
	std::function<std::pair<std::span<const imu>, std::span<const float, 3>>()>
		get_imu;
	std::function<
		std::pair<std::span<const DegPos>, std::span<const DegPos, 2>>()>
		get_gps;
	std::function<void()> clear_imu;
	std::function<void()> clear_gps;
};

struct samples_cache {
	data_adapter src;
	std::vector<acc_plot::sample> acc{};
	std::vector<gyro_plot::sample> gyro{};
	std::array<float, 3> imu_direction{};
	std::vector<ImPlotPoint> gps{};
	std::array<DegPos, 2> gps_boundingbox{};

	void update() {
		src.update();
		auto [imu_s, dir] = src.get_imu();
		if (!imu_s.empty()) {
			for (const auto &s : imu_s) {
				acc.emplace_back(s.ts / 1000., std::array{
												   float(s.acc[0]),
												   float(s.acc[1]),
												   float(s.acc[2]),
											   });
				gyro.emplace_back(s.ts / 1000., std::array{
													float(s.gyro[0]),
													float(s.gyro[1]),
													float(s.gyro[2]),
												});
			}
			src.clear_imu();
			std::copy_n(dir.begin(), dir.size(), imu_direction.begin());
		}

		auto [gps_s, bb] = src.get_gps();
		if (!gps_s.empty()) {
			for (const auto &s : gps_s) {
				auto e = MercatorePos(s);
				gps.emplace_back(e.x, e.y);
			}
			src.clear_gps();

			std::copy_n(bb.begin(), bb.size(), gps_boundingbox.begin());
			spdlog::info("bb: {}:{} {}:{}", bb[0].lat, bb[0].lon, bb[1].lat,
						 bb[1].lon);
		}
	}
};

int main() {
	spdlog::cfg::load_env_levels();

	glfwSetErrorCallback([](int e, auto str) {
		spdlog::error("glfw err: {} - {}", e, str);
		exit(1);
	});
	if (!glfwInit()) {
		spdlog::error("@glfw init");
		return 1;
	}

	// this should be the minimum version supported everywhere
	constexpr auto glsl_version = "#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	// glfwWindowHint(GLFW_OPENGL_PROFILE,
	//			   GLFW_OPENGL_CORE_PROFILE);            // 3.2+ only
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required on Mac

	// Create window with graphics context
	GLFWwindow *window = glfwCreateWindow(1280, 720, "aGfL", NULL, NULL);
	if (window == NULL) {
		spdlog::error("@gflfCreateWindow");
		return 1;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // Enable vsync

	if (gladLoadGL() == 0) {
		spdlog::error("Failed to initialize opengl loader!");
		return 1;
	}
	// Loaded OpenGL successfully.
	spdlog::info("OpenGL version loaded: {}.{}", GLVersion.major,
				 GLVersion.minor);

	//
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	auto &io = ImGui::GetIO();
	ImPlot::CreateContext();
	// Setup Platform/Renderer bindings
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	const auto clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	int source_item = 0;

	mock_device mock_dev{};

	auto sources = std::vector<samples_cache>{{
		data_adapter{
			.label = mock_dev.label,
			.update = [&] { mock_dev.update(); },
			.get_imu =
				[&] {
					return std::pair{
						std::span<const imu>(mock_dev.imu_samples),
						std::span<const float, 3>(mock_dev.imu_direction),
					};
				},
			.get_gps =
				[&] {
					return std::pair{
						std::span<const DegPos>(mock_dev.gps_samples),
						std::span<const DegPos, 2>(mock_dev.gps_boundingbox)};
				},
			.clear_imu = [&] { mock_dev.imu_samples.clear(); },
			.clear_gps = [&] { mock_dev.gps_samples.clear(); },
		},
	}};
	auto devices = [] {
		auto ports_p = get_ports();
		auto dev_v = ports_p | std::views::transform([](port &p) {
						 auto o_port = std::move(p).open().set_config({230400});
						 return device_samples{std::move(o_port)};
					 });
		return std::vector<device_samples>{dev_v.begin(), dev_v.end()};
	}();
	std::ranges::copy(
		devices | std::views::transform([](auto &dev) {
			return samples_cache{{
				.label = dev.label,
				.update = [&] { dev.update(); },
				.get_imu =
					[&] {
						return std::pair{
							std::span<const imu>(dev.imu_samples),
							std::span<const float, 3>(dev.imu_direction),
						};
					},
				.get_gps =
					[&] {
						return std::pair{
							std::span<const DegPos>(dev.gps_samples),
							std::span<const DegPos, 2>(dev.gps_boundingbox)};
					},
				.clear_imu = [&] { dev.imu_samples.clear(); },
				.clear_gps = [&] { dev.gps_samples.clear(); },
			}};
		}),
		std::back_inserter(sources));
	acc_plot acc{};
	gyro_plot gyro{};

	// Main loop
	while (!glfwWindowShouldClose(window)) {

		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard
		// flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input
		// data to your main application.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard
		// input data to your main application. Generally you may always
		// pass all inputs to dear imgui, and hide them from your
		// application based on those two flags.
		glfwPollEvents();

		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImGui::Text("%d fps", int(std::floor(io.Framerate)));
		ImGui::Combo(
			"source", &source_item,
			[](void *data, int idx, const char **outstr) -> bool {
				auto &src = *static_cast<decltype(sources) *>(data);
				*outstr = src[size_t(idx)].src.label.c_str();
				return true;
			},
			&sources, int(sources.size()));

		auto &source = sources[size_t(source_item)];
		source.update();

		if (ImGui::Begin("Position")) {
			const auto &pos = source.gps;
			if (pos.size() > 0) {
				if (ImPlot::BeginPlot("pos - mercatore", "longitude",
									  "latitude", ImVec2(800, 800))) {

					ImPlot::PlotLineG(
						"trace",
						getter([](const ImPlotPoint &p) -> const ImPlotPoint & {
							return p;
						}),
						const_cast<ImPlotPoint *>(pos.data()), int(pos.size()));
					ImPlot::EndPlot();
				}
			}
		}
		ImGui::End();

		if (ImGui::Begin("instruments")) {
			if (source.acc.size() > 0 && ImGui::TreeNode("acc")) {
				acc.show(source.acc);
				ImGui::TreePop();
			}
			if (source.gyro.size() > 0 && ImGui::TreeNode("gyro")) {
				gyro.show(source.gyro, source.imu_direction);
				ImGui::TreePop();
			}
		}
		ImGui::End();

		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);

		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x, clear_color.y, clear_color.z,
					 clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
	}

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();
}
