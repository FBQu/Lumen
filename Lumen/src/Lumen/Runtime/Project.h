#pragma once

#include "Lumen/Renderer/Renderer.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Lumen {

	// Description of a game project, stored as `game.lumen` (JSON) next to its scene and assets.
	// All paths are relative to the project directory and may not escape it.
	struct ProjectDesc
	{
		std::string Name = "Lumen Game";
		std::string Scene = "scene.json";
		std::vector<std::string> Models;   // .gltf / .glb files imported before the scene is loaded
		std::string Environment = "sky";   // "sky", "none" or the path of an equirectangular .hdr file
		std::string WindowTitle;           // defaults to Name
		uint32_t WindowWidth = 1280;
		uint32_t WindowHeight = 720;
		bool VSync = true;
		bool QuitOnEscape = true;
		Renderer::Settings Render;         // initial renderer settings
	};

	namespace Project
	{
		// Parses and validates a project file's text. Returns nullopt and fills `error` for malformed input.
		std::optional<ProjectDesc> Parse(std::string_view json, std::string* error = nullptr);
		std::string Serialize(const ProjectDesc& project);

		// True for a relative path that stays inside the project directory (no absolute paths, drive letters or "..").
		bool IsSafeRelativePath(std::string_view path);
	}

}
