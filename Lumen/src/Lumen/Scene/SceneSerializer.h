#pragma once

#include <string>
#include <string_view>

namespace Lumen {

	class Scene;

	// JSON scene format (version 1). UUIDs are stored as decimal strings so 64-bit values survive any JSON parser.
	class SceneSerializer
	{
	public:
		static constexpr int s_FormatVersion = 1;

		static std::string Serialize(const Scene& scene);

		// Adds the entities described by `json` to `scene`. All-or-nothing: the input is fully validated first,
		// so on failure the scene is untouched, false is returned and `error` (if given) describes the problem.
		static bool Deserialize(Scene& scene, std::string_view json, std::string* error = nullptr);
	};

}
