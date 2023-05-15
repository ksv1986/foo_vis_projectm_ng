#include "pch.h"

namespace {
	// {F9A79FD9-0DFA-40B8-A841-51FDE07E6EF7}
	static const GUID s_guid = { 0xf9a79fd9, 0xdfa, 0x40b8, { 0xa8, 0x41, 0x51, 0xfd, 0xe0, 0x7e, 0x6e, 0xf7 } };
	// {C3350A4C-A9F1-4369-96B2-33E873788A53}
	static const GUID s_path_guid = { 0xc3350a4c, 0xa9f1, 0x4369, { 0x96, 0xb2, 0x33, 0xe8, 0x73, 0x78, 0x8a, 0x53 } };
	cfg_string s_path(s_path_guid, "");

	struct ProjectMDeleter {
		void operator()(projectm* h) const {
			projectm_destroy(h);
		}
	};
	struct PlaylistDeleter {
		void operator()(projectm_playlist* h) const {
			projectm_playlist_destroy(h);
		}
	};
	using projectm_ptr = std::unique_ptr<projectm, ProjectMDeleter>;
	using playlist_ptr = std::unique_ptr<projectm_playlist, PlaylistDeleter>;

	class CProjectMWindow
		: public ui_element_instance
		, public playback_stream_capture_callback
		, public CWindowImpl<CProjectMWindow> {
	public:
		DECLARE_WND_CLASS_EX(L"{F9A79FD9-0DFA-40B8-A841-51FDE07E6EF7}", CS_VREDRAW | CS_HREDRAW | CS_OWNDC | CS_DBLCLKS, (-1));

	BEGIN_MSG_MAP_EX(CProjectMWindow)
		MSG_WM_LBUTTONDBLCLK(OnLButtonDblClk);
		MSG_WM_TIMER(OnTimer)
		MSG_WM_SIZE(OnSize)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()

		CProjectMWindow(ui_element_config::ptr, ui_element_instance_callback_ptr p_callback);
		~CProjectMWindow();

		// ui_element_instance
		HWND get_wnd() override { return *this; }
		void set_configuration(ui_element_config::ptr config) override { m_config = config; }
		ui_element_config::ptr get_configuration() override { return m_config; }

		// playback_stream_capture_callback
		void on_chunk(const audio_chunk& chunk) override;

		// ui_element_impl<>
		void initialize_window(HWND parent);
		static GUID g_get_guid() { return s_guid; }
		static GUID g_get_subclass() { return ui_element_subclass_playback_visualisation; }
		static void g_get_name(pfc::string_base& out) { out = "projectM"; }
		static ui_element_config::ptr g_get_default_configuration() { return ui_element_config::g_create_empty(g_get_guid()); }
		static const char* g_get_description() { return "projectM Visualization"; }

	private:
		void OnLButtonDblClk(UINT nFlags, CPoint point);
		void OnSize(UINT nType, CSize size);
		void OnTimer(UINT_PTR nIDEvent);
		void OnDestroy();

	private:
		void initialize_gl();
		void load_presets();
		void render();

	private:
		ui_element_config::ptr m_config;
		projectm_ptr m_p;
		playlist_ptr m_playlist;
		UINT_PTR m_t = 0;
	protected:
		// this must be declared as protected for ui_element_impl_withpopup<> to work.
		const ui_element_instance_callback_ptr m_callback;
	};

	CProjectMWindow::CProjectMWindow(ui_element_config::ptr config, ui_element_instance_callback_ptr p_callback)
		: m_callback(p_callback)
		, m_config(config)
	{}

	void CProjectMWindow::initialize_window(HWND parent) {
		WIN32_OP(Create(parent) != NULL);
		initialize_gl();
	}

	void CProjectMWindow::OnLButtonDblClk(UINT nFlags, CPoint point) {
		if (!m_p)
			return;

		modal_dialog_scope scope(*this);
		pfc::string8 path;
		if (uBrowseForFolder(*this, "Choose presets folder", path)) {
			s_path = path;
			load_presets();
			console::printf("projectM new playlist size: %u, presets directory: '%s'\n",
				projectm_playlist_size(m_playlist.get()), s_path.c_str());
		}
	}

	void CProjectMWindow::OnSize(UINT nType, CSize r) {
		if (!m_p)
			return;
		projectm_set_window_size(m_p.get(), r.cx, r.cy);
	}

	void CProjectMWindow::OnTimer(UINT_PTR nIDEvent) {
		render();
	}

	void CProjectMWindow::OnDestroy() {
		KillTimer(m_t);
	}

	void CProjectMWindow::on_chunk(const audio_chunk& chunk) {
#if audio_sample_size == 32
		projectm_pcm_add_float(m_p.get(), chunk.get_data(), chunk.get_used_size(), PROJECTM_STEREO);
#elif audio_sample_size == 64
		const double* p = chunk.get_data();
		const auto end = chunk.get_used_size() - 1;
		for (size_t i = 0; i < end; i += 2) {
			const float ch2[2] = { float(p[i]), float(p[i + 1]) };
			projectm_pcm_add_float(m_p.get(), ch2, 1, PROJECTM_STEREO);
		}
#else
#error Unsupported audio_sample_size
#endif
	}

#define _S(x) #x
#define S(x) _S(x)
#define THROW_IF_MSG(x, msg) do { if ((x)) { throw std::exception("projectM: " msg); } } while(0)
	void CProjectMWindow::initialize_gl() {
		CClientDC dc(*this);
		PIXELFORMATDESCRIPTOR pfd;
		ZeroMemory(&pfd, sizeof(pfd));
		pfd.nSize = sizeof(pfd);
		pfd.nVersion = 1;
		pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pfd.iPixelType = PFD_TYPE_RGBA;
		pfd.cColorBits = 32;
		pfd.cAlphaBits = 8;
		pfd.cDepthBits = 24;
		int n = dc.ChoosePixelFormat(&pfd);
		THROW_IF_MSG(!n, "pixel format 32bppRGBA not supported");
		THROW_IF_MSG(!dc.SetPixelFormat(n, &pfd), "failed to set 32bppRGBA pixel format");

		HGLRC rc = dc.wglCreateContext();
		THROW_IF_MSG(!rc, "wglCreateContext failed");
		THROW_IF_MSG(!dc.wglMakeCurrent(rc), "wglMakeCurrent failed");

		const auto vers = reinterpret_cast<const char*>(glGetString(GL_VERSION));
		THROW_IF_MSG(!vers, "failed to initialize OpenGL context");
		console::printf("OpenGL version: %s\n", vers);
		unsigned v1, v2;
		// check opengl version. projectM will crash if opengl doesn't support shaders
		THROW_IF_MSG(sscanf_s(vers, "%u.%u.", &v1, &v2) != 2 || v1 * 10 + v2 < 20, "unsupported OpenGL version");

		projectm_handle h = projectm_create();
		THROW_IF_MSG(!h, "projectM initialization failed");

		m_p.reset(h);
		m_playlist.reset(projectm_playlist_create(h));

		load_presets();
		console::printf("projectM created, presets directory: '%s'\n",
			s_path.c_str());

		playback_stream_capture::get()->add_callback(this);

		constexpr UINT frames_per_second = 30;
		projectm_set_fps(m_p.get(), frames_per_second);
		m_t = SetTimer(reinterpret_cast<UINT_PTR>(this), 1000 / frames_per_second, nullptr);
	}

	void CProjectMWindow::render() {
		if (!m_p)
			return;

		projectm_opengl_render_frame(m_p.get());

		CClientDC dc(*this);
		dc.SwapBuffers();
	}

	void CProjectMWindow::load_presets() {
		projectm_playlist_clear(m_playlist.get());

		if (s_path.is_empty())
			return;

		projectm_playlist_add_path(m_playlist.get(), s_path.c_str(), true, false);
	}

	CProjectMWindow::~CProjectMWindow() {
		if (m_p)
			playback_stream_capture::get()->remove_callback(this);
	}

	class CProjectMImpl : public ui_element_impl<CProjectMWindow> {};
	static service_factory_single_t<CProjectMImpl> g_ui_element_factory;
} // namespace

VALIDATE_COMPONENT_FILENAME("foo_vis_projectm_ng.dll");

// Declaration of your component's version information
// Since foobar2000 v1.0 having at least one of these in your DLL is mandatory to let the troubleshooter tell different versions of your component apart.
// Note that it is possible to declare multiple components within one DLL, but it's strongly recommended to keep only one declaration per DLL.
// As for 1.1, the version numbers are used by the component update finder to find updates; for that to work, you must have ONLY ONE declaration per DLL. If there are multiple declarations, the component is assumed to be outdated and a version number of "0" is assumed, to overwrite the component with whatever is currently on the site assuming that it comes with proper version numbers.
DECLARE_COMPONENT_VERSION("projectM Visualization",
"2.0",
"Visualization based on projectM-4\n"
"Copyright (C) 2023 Andrey Kuleshov\n"
"Copyright (C) 2003 - 2023 projectM Team\n"
);
