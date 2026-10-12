#include "Lumen/Runtime/Project.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace Lumen::Project {

	using Json = nlohmann::json;

	namespace {

		struct ProjectError : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		std::string StringOr(const Json& json, const char* key, const std::string& fallback)
		{
			auto it = json.find(key);
			if (it == json.end())
				return fallback;
			if (!it->is_string())
				throw ProjectError(std::string("'") + key + "' must be a string");
			return it->get<std::string>();
		}

		bool BoolOr(const Json& json, const char* key, bool fallback)
		{
			auto it = json.find(key);
			if (it == json.end())
				return fallback;
			if (!it->is_boolean())
				throw ProjectError(std::string("'") + key + "' must be a boolean");
			return it->get<bool>();
		}

		void ReadNumber(const Json& json, const char* key, float& target, float minimum, float maximum)
		{
			auto it = json.find(key);
			if (it == json.end())
				return;
			if (!it->is_number() || !(it->get<float>() >= minimum && it->get<float>() <= maximum))
				throw ProjectError(std::string("'") + key + "' must be a number between " + std::to_string(minimum) + " and " + std::to_string(maximum));
			target = it->get<float>();
		}

		uint32_t ReadSize(const Json& json, const char* key, uint32_t fallback)
		{
			auto it = json.find(key);
			if (it == json.end())
				return fallback;
			if (!it->is_number_integer() || it->get<int>() < 64 || it->get<int>() > 16384)
				throw ProjectError(std::string("window '") + key + "' must be an integer between 64 and 16384");
			return static_cast<uint32_t>(it->get<int>());
		}

		glm::vec3 ReadColor(const Json& json, const char* key, const glm::vec3& fallback)
		{
			auto it = json.find(key);
			if (it == json.end())
				return fallback;
			if (!it->is_array() || it->size() != 3 || !(*it)[0].is_number() || !(*it)[1].is_number() || !(*it)[2].is_number())
				throw ProjectError(std::string("'") + key + "' must be an array of 3 numbers");
			return { (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>() };
		}

		void CheckPath(const std::string& path, const char* what)
		{
			if (!IsSafeRelativePath(path))
				throw ProjectError(std::string(what) + " '" + path + "' must be a relative path inside the project");
		}

	}

	bool IsSafeRelativePath(std::string_view path)
	{
		if (path.empty() || path[0] == '/' || path[0] == '\\' || path.find(':') != std::string_view::npos)
			return false;
		size_t start = 0;
		while (start <= path.size())
		{
			size_t end = path.find_first_of("/\\", start);
			if (end == std::string_view::npos)
				end = path.size();
			if (path.substr(start, end - start) == "..")
				return false;
			start = end + 1;
		}
		return true;
	}

	std::optional<ProjectDesc> Parse(std::string_view text, std::string* error)
	{
		auto fail = [error](const std::string& message) -> std::optional<ProjectDesc>
		{
			if (error)
				*error = message;
			return std::nullopt;
		};

		try
		{
			const Json root = Json::parse(text);
			if (!root.is_object())
				return fail("a project file must be a JSON object");

			ProjectDesc project;
			project.Name = StringOr(root, "name", project.Name);
			project.Scene = StringOr(root, "scene", project.Scene);
			CheckPath(project.Scene, "scene");

			if (auto it = root.find("models"); it != root.end())
			{
				if (!it->is_array())
					return fail("'models' must be an array of paths");
				for (const Json& model : *it)
				{
					if (!model.is_string())
						return fail("'models' must be an array of paths");
					CheckPath(model.get<std::string>(), "model");
					project.Models.push_back(model.get<std::string>());
				}
			}

			project.Environment = StringOr(root, "environment", project.Environment);
			if (project.Environment != "sky" && project.Environment != "none")
				CheckPath(project.Environment, "environment");
			project.QuitOnEscape = BoolOr(root, "quitOnEscape", project.QuitOnEscape);

			project.WindowTitle = project.Name;
			if (auto it = root.find("window"); it != root.end())
			{
				if (!it->is_object())
					return fail("'window' must be an object");
				project.WindowTitle = StringOr(*it, "title", project.Name);
				project.WindowWidth = ReadSize(*it, "width", project.WindowWidth);
				project.WindowHeight = ReadSize(*it, "height", project.WindowHeight);
				project.VSync = BoolOr(*it, "vsync", project.VSync);
			}

			if (auto it = root.find("renderer"); it != root.end())
			{
				if (!it->is_object())
					return fail("'renderer' must be an object");
				Renderer::Settings& r = project.Render;
				ReadNumber(*it, "exposure", r.Exposure, 0.0f, 1000.0f);
				ReadNumber(*it, "environmentIntensity", r.EnvironmentIntensity, 0.0f, 1000.0f);
				ReadNumber(*it, "shadowDistance", r.ShadowDistance, 1.0f, 100000.0f);
				ReadNumber(*it, "shadowSoftness", r.ShadowSoftness, 0.0f, 1.0f);
				ReadNumber(*it, "aoRadius", r.AORadius, 0.01f, 1000.0f);
				ReadNumber(*it, "aoIntensity", r.AOIntensity, 0.0f, 10.0f);
				ReadNumber(*it, "aoPower", r.AOPower, 0.01f, 10.0f);
				r.EnableShadows = BoolOr(*it, "enableShadows", r.EnableShadows);
				r.EnableAmbientOcclusion = BoolOr(*it, "enableAmbientOcclusion", r.EnableAmbientOcclusion);
				r.ShowBackground = BoolOr(*it, "showBackground", r.ShowBackground);
				r.Ambient = ReadColor(*it, "ambient", r.Ambient);
				r.ClearColor = ReadColor(*it, "clearColor", r.ClearColor);
			}
			return project;
		}
		catch (const ProjectError& e)
		{
			return fail(e.what());
		}
		catch (const Json::exception& e)
		{
			return fail(std::string("invalid JSON: ") + e.what());
		}
	}

	std::string Serialize(const ProjectDesc& project)
	{
		const Renderer::Settings& r = project.Render;
		Json root = {
			{ "name", project.Name }, { "scene", project.Scene }, { "models", project.Models }, { "environment", project.Environment },
			{ "quitOnEscape", project.QuitOnEscape },
			{ "window", { { "title", project.WindowTitle }, { "width", project.WindowWidth }, { "height", project.WindowHeight }, { "vsync", project.VSync } } },
			{ "renderer", { { "exposure", r.Exposure }, { "environmentIntensity", r.EnvironmentIntensity }, { "shadowDistance", r.ShadowDistance },
			                { "shadowSoftness", r.ShadowSoftness }, { "aoRadius", r.AORadius }, { "aoIntensity", r.AOIntensity }, { "aoPower", r.AOPower },
			                { "enableShadows", r.EnableShadows }, { "enableAmbientOcclusion", r.EnableAmbientOcclusion },
			                { "showBackground", r.ShowBackground }, { "ambient", { r.Ambient.r, r.Ambient.g, r.Ambient.b } },
			                { "clearColor", { r.ClearColor.r, r.ClearColor.g, r.ClearColor.b } } } },
		};
		return root.dump(2);
	}

}
