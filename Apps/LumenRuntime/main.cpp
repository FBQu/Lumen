// LumenRuntime: plays a Lumen game project in a window.
//
//   LumenRuntime [project-dir-or-game.lumen] [options]
//
// Without a project argument it looks for game.lumen next to the executable (which is how exported games run), then in
// the current directory.

#include "Lumen/Assets/ImageIO.h"
#include "Lumen/Core/Log.h"
#include "Lumen/Platform/Window.h"
#include "Lumen/Renderer/Renderer.h"
#include "Lumen/Runtime/ProjectExport.h"
#include "Lumen/Runtime/GameSession.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

using namespace Lumen;
namespace fs = std::filesystem;

namespace {

	struct Options
	{
		std::string Project;
		std::string ScreenshotPath;
		std::string ExportDirectory;
		std::string RuntimeExecutable; // with --export: the runtime binary to copy (default: this executable)
		int Frames = 0;          // with --screenshot: number of frames to run before capturing (0 = until closed)
		bool NoVSync = false;
		bool ShowHelp = false;
	};

	void PrintHelp()
	{
		std::puts(
			"LumenRuntime - play a Lumen game project\n\n"
			"Usage: LumenRuntime [project-dir | game.lumen] [options]\n\n"
			"Options:\n"
			"  --export <dir>        export the project as a standalone game folder and exit\n"
			"  --runtime <exe>       with --export: ship this runtime binary instead (e.g. a Windows LumenRuntime.exe)\n"
			"  --screenshot <file>   run --frames frames (default 120), save the last frame as PNG, exit\n"
			"  --frames <n>          frame count for --screenshot\n"
			"  --no-vsync            do not wait for vertical sync\n"
			"  --help                show this text\n\n"
			"Without a project argument, game.lumen next to the executable (or in the current directory) is used.");
	}

	bool ParseArgs(int argc, char** argv, Options& options)
	{
		for (int i = 1; i < argc; i++)
		{
			const std::string arg = argv[i];
			auto value = [&](std::string& target) -> bool
			{
				if (i + 1 >= argc)
				{
					std::fprintf(stderr, "Missing value for %s\n", arg.c_str());
					return false;
				}
				target = argv[++i];
				return true;
			};
			if (arg == "--help" || arg == "-h") options.ShowHelp = true;
			else if (arg == "--no-vsync") options.NoVSync = true;
			else if (arg == "--screenshot") { if (!value(options.ScreenshotPath)) return false; }
			else if (arg == "--export") { if (!value(options.ExportDirectory)) return false; }
			else if (arg == "--runtime") { if (!value(options.RuntimeExecutable)) return false; }
			else if (arg == "--frames")
			{
				std::string text;
				if (!value(text))
					return false;
				options.Frames = std::atoi(text.c_str());
				if (options.Frames < 1)
				{
					std::fprintf(stderr, "--frames needs a positive number\n");
					return false;
				}
			}
			else if (!arg.empty() && arg[0] == '-') { std::fprintf(stderr, "Unknown option %s (try --help)\n", arg.c_str()); return false; }
			else options.Project = arg;
		}
		return true;
	}

	// Full path of the running executable (used to find game.lumen next to it and to export the runtime itself).
	fs::path ExecutablePath(const char* argv0)
	{
		std::error_code ec;
#ifdef _WIN32
		wchar_t buffer[32768];
		const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
		if (length > 0 && length < std::size(buffer))
			return fs::path(std::wstring(buffer, length));
#else
		fs::path exe = fs::read_symlink("/proc/self/exe", ec);
		if (!ec)
			return exe;
#endif
		return fs::absolute(argv0, ec);
	}

	std::string ExecutableDirectory(const char* argv0)
	{
		return ExecutablePath(argv0).parent_path().string();
	}

	std::string ResolveProjectFile(const Options& options, const std::string& exeDirectory)
	{
		if (!options.Project.empty())
			return fs::is_directory(options.Project) ? (fs::path(options.Project) / "game.lumen").string() : options.Project;
		const fs::path next = fs::path(exeDirectory) / "game.lumen";
		return fs::exists(next) ? next.string() : "game.lumen";
	}

}

int main(int argc, char** argv)
{
	Options options;
	if (!ParseArgs(argc, argv, options))
		return 2;
	if (options.ShowHelp)
	{
		PrintHelp();
		return 0;
	}

	const std::string exeDirectory = ExecutableDirectory(argv[0]);
	const std::string projectFile = ResolveProjectFile(options, exeDirectory);

	if (!options.ExportDirectory.empty())
	{
		// By default the running executable is the runtime to ship; --runtime picks another (e.g. a Windows build).
		const std::string runtime = options.RuntimeExecutable.empty() ? ExecutablePath(argv[0]).string() : options.RuntimeExecutable;
		std::string error;
		if (!ExportProject(projectFile, options.ExportDirectory, runtime, &error))
		{
			std::fprintf(stderr, "Export failed: %s\n", error.c_str());
			return 1;
		}
		return 0;
	}

	GameSession session;
	std::string error;
	if (!session.Load(projectFile, &error))
	{
		std::fprintf(stderr, "Could not load the project: %s\n", error.c_str());
		return 1;
	}
	const ProjectDesc& project = session.GetProject();
	if (!session.FindCamera().IsValid())
	{
		std::fprintf(stderr, "The scene has no camera.\n");
		return 1;
	}

	Window::Desc windowDesc;
	windowDesc.Title = project.WindowTitle;
	windowDesc.Width = project.WindowWidth;
	windowDesc.Height = project.WindowHeight;
	windowDesc.VSync = project.VSync && !options.NoVSync;
	Ref<Window> window = Window::Create(windowDesc);
	if (!window)
	{
		std::fprintf(stderr, "Could not open a window. A display and a Vulkan 1.3 capable GPU driver are required.\n");
		return 1;
	}
	std::printf("Rendering on: %s\n", window->GetRenderDevice()->GetDeviceName().c_str());

	Ref<Renderer> renderer;
	auto ensureRenderer = [&]() -> bool
	{
		const uint32_t width = std::max(window->GetWidth(), 64u), height = std::max(window->GetHeight(), 64u);
		if (renderer && renderer->GetWidth() == width && renderer->GetHeight() == height)
			return true;
		renderer = Renderer::Create(window->GetRenderDevice(), width, height, 2048);
		if (!renderer)
			return false;
		renderer->GetSettings() = project.Render;
		return session.GetEnvironment() == nullptr || renderer->SetEnvironment(session.GetEnvironment());
	};

	const bool capture = !options.ScreenshotPath.empty();
	window->SetCaptureEnabled(capture);
	const int frameLimit = capture ? (options.Frames > 0 ? options.Frames : 120) : 0;

	session.GetScripts().SetInput(&window->GetInput());
	session.Start();

	auto last = std::chrono::steady_clock::now();
	auto titleTime = last;
	int titleFrames = 0;
	int frame = 0;
	int exitCode = 0;

	while (!window->ShouldClose())
	{
		window->PollEvents();
		const auto now = std::chrono::steady_clock::now();
		const float dt = std::chrono::duration<float>(now - last).count();
		last = now;

		if (project.QuitOnEscape && window->GetInput().WasKeyPressed(Key::Escape))
			window->RequestClose();

		// With a fixed frame budget (screenshots, automated runs) use a fixed step so the result is reproducible.
		session.Update(frameLimit > 0 ? 1.0f / 60.0f : dt);

		if (!window->BeginFrame())
		{
			window->WaitForEvents(0.05); // minimized or resizing
			continue;
		}
		if (!ensureRenderer())
		{
			std::fprintf(stderr, "Could not create the renderer.\n");
			exitCode = 1;
			break;
		}
		Entity camera = session.FindCamera();
		if (!renderer->Render(session.GetScene(), camera, &session.GetAssets()))
		{
			exitCode = 1;
			break;
		}
		window->Present(renderer->GetOutput());

		titleFrames++;
		const float sinceTitle = std::chrono::duration<float>(now - titleTime).count();
		if (sinceTitle >= 0.5f)
		{
			const std::string title = project.WindowTitle + "  -  " + std::to_string(static_cast<int>(static_cast<float>(titleFrames) / sinceTitle + 0.5f)) + " FPS";
			window->SetTitle(title);
			titleTime = now;
			titleFrames = 0;
		}

		if (frameLimit > 0 && ++frame >= frameLimit)
			break;
	}

	if (capture && exitCode == 0)
	{
		std::string saveError;
		if (!ImageIO::WritePNG(options.ScreenshotPath, window->GetLastFrame(), &saveError))
		{
			std::fprintf(stderr, "Could not save the screenshot: %s\n", saveError.c_str());
			exitCode = 1;
		}
		else
		{
			std::printf("Saved %s\n", options.ScreenshotPath.c_str());
		}
	}

	session.Stop();
	session.GetScripts().SetInput(nullptr);
	renderer.reset();
	return exitCode;
}
