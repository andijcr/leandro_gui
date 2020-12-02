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
#include <queue>
#include <random>
#include <ranges>
#include <span>
#include <variant>

#include "gps.hpp"
#include "imu.hpp"
#include "leo_widgets.hpp"

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

static const auto app_start = std::chrono::system_clock::now();
uint64_t get_ms() {
	auto d = std::chrono::system_clock::now() - app_start;
	return uint64_t(
		std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
}

struct lclock {
	struct Time {
		uint64_t ms;
		auto asSeconds() -> float { return float(ms) / 1000.f; }
	};

	uint64_t start;
	lclock() { restart(); }
	auto getElapsedTime() -> Time { return {get_ms() - start}; }
	void restart() { start = get_ms(); }
};

constexpr auto to_degs_per_sec(int16_t gyr_point, float fullscale) {
	return fullscale * float(gyr_point) /
		   -float(std::numeric_limits<int16_t>::min());
}
constexpr auto to_increment(float degs_per_sec, uint32_t ms) {
	return degs_per_sec * (float(ms) / 1000.f);
}

constexpr auto to_radians(float degs) {
	return (degs / 180.f) * std::numbers::pi_v<float>;
}

struct sample_data {
	std::string label = "sample_data";
	lclock test_data_clock{};
	std::vector<imu> test_data{1, imu{}};
	std::default_random_engine random_engine{std::random_device{}()};
	std::uniform_int_distribution<int> random_acc{-2, 2};
	std::uniform_int_distribution<int> random_ts{0, 1};

	std::uniform_int_distribution<int> attractor_random{-5000, 5000};
	std::pair<std::array<int16_t, 3>, std::array<int16_t, 3>> attractor{};
	lclock attractor_clock{};

	std::array<float, 3> dir{};

	void update() {
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
			return int(expected - float(test_data.size()));
		}();
		if (to_add <= 0) {
			return;
		}
		auto new_data_gen = [&, back = test_data.back()]() mutable {
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
			test_data.emplace_back(new_data_gen());

			const auto &gyr = test_data.back().gyro;
			const auto dt = test_data.back().ts - (test_data.end() - 2)->ts;
			for (auto i = 0u; i < gyr.size(); ++i) {
				dir[i] = std::remainder(
					dir[i] + to_radians(to_increment(
								 to_degs_per_sec(gyr[i], 245), dt)),
					std::numbers::pi_v<float>);
			}
		}
	}
};

constexpr auto office = DegPos{41.9134432f, 12.5010377f};
struct sample_gps {
	std::default_random_engine random_engine{std::random_device{}()};
	std::uniform_real_distribution<float> random_acc{-1 / 10'000.f,
													 1 / 10'000.f};

	lclock test_data_clock{};
	std::vector<DegPos> test_data{1, office};
	std::array<float, 2> bb_y = [] {
		auto a = MercatorePos(office);
		return std::array{a.y - 0.00001f, a.y + 0.00001f};
	}();
	std::array<float, 2> bb_x = [] {
		auto a = MercatorePos(office);
		return std::array{a.x - 0.00001f, a.x + 0.00001f};
	}();
	void update() {
		const auto to_add = [&] {
			const auto expected =
				std::floor(test_data_clock.getElapsedTime().asSeconds());
			return int(expected - float(test_data.size()));
		}();
		if (to_add <= 0) {
			return;
		}

		for (auto i = 0; i < to_add; ++i) {
			auto stp = test_data.back();
			stp.lat += random_acc(random_engine);
			stp.lon += random_acc(random_engine);
			auto mer = MercatorePos(stp);
			bb_x[0] = std::min(bb_x[0], mer.x);
			bb_x[1] = std::max(bb_x[1], mer.x);
			bb_y[0] = std::min(bb_y[0], mer.y);
			bb_y[1] = std::max(bb_y[1], mer.y);
			test_data.emplace_back(stp);
		}
	}
};

struct data_adapter {
	std::string label;
	std::function<void()> update;
	std::function<std::pair<std::span<const imu>, std::span<const float, 3>>()>
		get_imu;
	std::function<std::span<const DegPos>()> get_gps;
};

struct device_samples {
	std::vector<imu> imu_samples{};
	std::array<float, 3> imu_direction{};
	std::vector<DegPos> gps_samples{};
	std::string label;
	data_source src;
	device_samples(open_port &&p)
		: label{fmt::format(FMT_COMPILE("{}-{}"), p.name(), p.description())},
		  src{line_getter(std::move(p))} {}

	void update_direction(const imu &im, uint32_t dt) {
		for (auto i = 0u; i < im.gyro.size(); ++i) {
			imu_direction[i] = std::remainder(
				imu_direction[i] + to_radians(to_increment(
									   to_degs_per_sec(im.gyro[i], 245), dt)),
				std::numbers::pi_v<float>);
		}
	}
	void update() {
		auto sample = src();
		if (!sample) {
			return;
		}
		if (std::holds_alternative<imu>(*sample)) {
			imu_samples.emplace_back(std::get<imu>(*sample));
			return;
		}

		if (std::holds_alternative<gps_hybrid>(*sample)) {
			gps_samples.emplace_back(DegPos(std::get<gps_hybrid>(*sample).pos));
		}
	}
};
int main() {
	spdlog::cfg::load_env_levels();

	{
		auto ports = get_ports();
		fmt::print("ports: {}\n",
				   ports | std::views::transform([](auto &p) {
					   return std::pair{p.name(), p.description()};
				   }));
	}
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
	sample_data sample{};
	sample_gps sample_pos{};

	auto sources = std::vector<data_adapter>{
		{
			sample.label,
			[&] {
				sample.update();
				sample_pos.update();
			},
			[&]()
				-> std::pair<std::span<const imu>, std::span<const float, 3>> {
				return {sample.test_data, sample.dir};
			},
			[&]() -> std::span<const DegPos> { return sample_pos.test_data; },
		},
	};
	auto devices = [] {
		auto ports_p = get_ports();
		auto dev_v = ports_p | std::views::transform([](port &p) {
						 return device_samples{std::move(
							 std::move(p).open().set_config({230800}))};
						 ;
					 });
		return std::vector<device_samples>{dev_v.begin(), dev_v.end()};
	}();
	std::ranges::copy(
		devices | std::views::transform([](auto &dev) {
			return data_adapter{
				dev.label, [&] { dev.update(); },
				[&]() -> std::pair<std::span<const imu>,
								   std::span<const float, 3>> {
					return {dev.imu_samples, dev.imu_direction};
				},
				[&]() -> std::span<const DegPos> { return dev.gps_samples; }};
		}),
		std::back_insert_iterator(sources));
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

		static auto clear_bg = true;
		ImGui::Checkbox("clear_bg", &clear_bg);
		ImGui::Text("%d fps", int(std::floor(io.Framerate)));
		ImGui::Combo(
			"source", &source_item,
			[](void *data, int idx, const char **outstr) -> bool {
				auto &src = *static_cast<decltype(sources) *>(data);
				*outstr = src[size_t(idx)].label.c_str();
				return true;
			},
			&sources, int(sources.size()));

		sources[size_t(source_item)].update();

		if (ImGui::Begin("Position")) {
			auto pos = sources[size_t(source_item)].get_gps();

			/*
			 * ImPlot::SetNextPlotLimitsX(sample_pos.bb_x[0],
			 * sample_pos.bb_x[1], ImGuiCond_Appearing);
			 * ImPlot::SetNextPlotLimitsY(sample_pos.bb_y[0],
			 * sample_pos.bb_y[1], ImGuiCond_Appearing);
			 */
			if (ImPlot::BeginPlot("pos - mercatore", "longitude", "latitude",
								  ImVec2(400, 400))) {

				ImPlot::PlotLineG(
					"trace", getter([](const DegPos &p) -> ImPlotPoint {
						auto e = MercatorePos(p);
						return {e.x, e.y};
					}),
					const_cast<DegPos *>(pos.data()), int(pos.size()));
				ImPlot::EndPlot();
			}
		}
		ImGui::End();

		if (ImGui::Begin("instruments")) {
			auto [data_imu, data_dir] = sources[size_t(source_item)].get_imu();
			if (data_imu.size() > 0) {
				if (ImGui::TreeNode("acc")) {
					acc.show(data_imu);
					ImGui::TreePop();
				}
				if (ImGui::TreeNode("gyro")) {
					gyro.show(data_imu, data_dir);
					ImGui::TreePop();
				}
			}
		}
		ImGui::End();

		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);

		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x, clear_color.y, clear_color.z,
					 clear_color.w);
		if (clear_bg) {
			glClear(GL_COLOR_BUFFER_BIT);
		}

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
