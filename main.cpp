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
#include <exception>
#include <limits>
#include <memory>
#include <queue>
#include <random>
#include <ranges>

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

struct line_spliter {
	std::string rest{};
	open_port p;
	line_spliter(open_port op) : p{std::move(op)} {}
	auto operator()(std::queue<std::string> &out) {
		const auto in_count = size_t(wrap(sp_input_waiting(p)));
		if (in_count == 0) {
			return;
		}

		auto buff = std::vector<char>(in_count, 0);
		wrap(sp_nonblocking_read(p, buff.data(), in_count));

		const auto separators = std::ranges::count(buff, '\n');
		if (separators == 0) {
			// no complete line, save and drop
			std::ranges::copy(buff, std::back_inserter(rest));
			spdlog::debug("noline");
			return;
		}

		auto last_partial = [&] {
			auto tmp =
				buff | std::views::reverse |
				std::views::take_while([](auto c) { return c != '\n'; }) |
				std::views::reverse | std::views::common;

			return std::string(tmp.begin(), tmp.end());
		}();

		buff.erase(buff.end() - ptrdiff_t(last_partial.size()) - 1, buff.end());

		std::ranges::copy(
			buff | std::views::take_while([](auto c) { return c != '\n'; }),
			std::back_inserter(rest));
		out.emplace(std::exchange(rest, std::move(last_partial)));

		const auto line_list = buff | std::views::split('\n');
		for (auto line : line_list | std::views::drop(1)) {
			const auto tmp = line | std::views::common;
			out.emplace(tmp.begin(), tmp.end());
		}

		spdlog::debug(FMT_COMPILE("received {} full and {} partial"),
					  separators, rest.size() == 0 ? 0 : 1);
	}
};

struct imu {
	uint32_t ts{};
	std::array<int16_t, 3> acc{};
	std::array<int16_t, 3> gyro{};
};

auto isimu_num(std::string_view sv) {
	auto ltrim = sv | std::views::drop_while(isspace);
	auto to_skip = ltrim.front() == '-' ? 1 : 0;
	return std::ranges::all_of(ltrim.begin() + to_skip, ltrim.end(), isdigit);
}

auto is_imu(std::string_view sv) {
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
	return std::ranges::all_of(nums, colums.end(), isimu_num);
}

auto to_imu(std::string_view sv) {
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

static const auto app_start = std::chrono::system_clock::now();
uint64_t get_ms() {
	auto d = std::chrono::system_clock::now() - app_start;
	return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

struct lclock {
	struct Time {
		uint64_t ms;
		auto asSeconds() -> float { return ms / 1000.f; }
	};

	uint64_t start;
	lclock() { restart(); }
	auto getElapsedTime() -> Time { return {get_ms() - start}; }
	void restart() { start = get_ms(); }
};

struct sample_data {
	lclock test_data_clock{};
	std::vector<imu> test_data{1, imu{}};
	std::default_random_engine random_engine{std::random_device{}()};
	std::uniform_int_distribution<int> random_acc{-2, 2};
	std::uniform_int_distribution<int> random_ts{0, 1};

	std::uniform_int_distribution<int> attractor_random{-5000, 5000};
	std::pair<std::array<int16_t, 3>, std::array<int16_t, 3>> attractor{};
	lclock attractor_clock{};

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
		if (const auto to_add =
				[&] {
					const auto expected = std::floor(
						test_data_clock.getElapsedTime().asSeconds() /
						0.016891892f);
					return int(expected - float(test_data.size()));
				}();
			to_add > 0) {

			auto new_data_gen = [&, back = test_data.back()]() mutable {
				auto zero_attractor = [&](int v, int tgt) {
					return int16_t(std::clamp<int>(
						v + (tgt - v) * random_acc(random_engine) +
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
			for (auto i = 0; i < to_add; ++i) {
				test_data.emplace_back(new_data_gen());
			}
		}
	}

	bool sample_data = true;

	float history = 10.f;
	bool history_limited = true;
	float yscale = float(std::numeric_limits<int16_t>::max());
	bool scale_limited = true;
	void show() {
		ImGui::Begin("sample data", &sample_data);
		if (sample_data) {
			ImGui::SliderFloat("History", &history, 1, 30, "%.1f s");
			ImGui::SameLine();
			ImGui::Checkbox("lock x", &history_limited);

			ImGui::SliderFloat("Scale", &yscale, 1,
							   std::numeric_limits<int16_t>::max(),
							   "%.1f leandri");
			ImGui::SameLine();
			ImGui::Checkbox("lock y", &scale_limited);

			const auto &[last_t, last_acc, last_gyro] = test_data.back();

			if (history_limited) {
				ImPlot::SetNextPlotLimitsX(last_t / 1000. - history,
										   last_t / 1000., ImGuiCond_Always);
			}

			if (scale_limited) {
				ImPlot::SetNextPlotLimitsY(-yscale * 1.1, yscale * 1.1,
										   ImGuiCond_Always);
			}
			if (ImPlot::BeginPlot("imu", "s", "leandri", ImVec2(-1, -1))) {
				ImPlot::PlotLineG("acc_x", getter([](const imu &im) {
									  return ImPlotPoint(im.ts / 1000.,
														 im.acc[0]);
								  }),
								  test_data.data(), int(test_data.size()));
				ImPlot::PlotLineG("acc_y", getter([](const imu &im) {
									  return ImPlotPoint(im.ts / 1000.,
														 im.acc[1]);
								  }),
								  test_data.data(), int(test_data.size()));
				ImPlot::PlotLineG("acc_z", getter([](const imu &im) {
									  return ImPlotPoint(im.ts / 1000.,
														 im.acc[2]);
								  }),
								  test_data.data(), int(test_data.size()));
				ImPlot::EndPlot();
			}

			ImGui::End();
		}
	}
};

int main() {
	spdlog::cfg::load_env_levels();

	auto ports = get_ports();
	fmt::print("ports: {}\n", ports | std::views::transform([](auto &p) {
								  return std::pair{p.name(), p.description()};
							  }));

	glfwSetErrorCallback([](int e, auto str) {
		spdlog::error("glfw err: {} - {}", e, str);
		exit(1);
	});
	if (!glfwInit()) {
		spdlog::error("@glfw init");
		return 1;
	}

	constexpr auto glsl_version = "#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	//glfwWindowHint(GLFW_OPENGL_PROFILE,
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

		static auto sample_data = true;
		static struct sample_data sample {};

		if (sample_data) {
			sample.update();
		}

		static auto clear_bg = true;
		ImGui::Checkbox("clear_bg", &clear_bg);
		ImGui::Text("%d fps", int(std::floor(io.Framerate)));
		ImGui::Checkbox("sample data", &sample_data);

		if (sample_data) {
			sample.show();
		}

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

	if (ports.size() == 0) {
		return -1;
	}
	auto lines_gen =
		line_spliter{std::move(ports[0]).open().set_config({230800})};

	std::queue<std::string> lines{};

	auto processed = size_t(0);
	while (processed < 1000) {
		lines_gen(lines);
		processed += lines.size();
		while (!lines.empty()) {
			fmt::print("{} {}\n", is_imu(lines.front()) ? "III" : "___",
					   lines.front());
			lines.pop();
		}
	}
}
