#include "Lumen/Runtime/ProjectExport.h"

#include "Lumen/Runtime/Project.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace Lumen {

	namespace fs = std::filesystem;

	std::string MakeExecutableName(const std::string& projectName)
	{
		std::string name;
		for (unsigned char c : projectName)
		{
			if (std::isalnum(c) || c == '_' || c == '-')
				name += static_cast<char>(c);
			else if (c == ' ' || c == '.')
				name += '_';
			// everything else is dropped
		}
		while (!name.empty() && name.front() == '_')
			name.erase(name.begin());
		while (!name.empty() && name.back() == '_')
			name.pop_back();
		return name.empty() ? "Game" : name;
	}

	bool ExportProject(const std::string& projectFile, const std::string& outputDirectory, const std::string& runtimeExecutable, std::string* error)
	{
		auto fail = [error](const std::string& message)
		{
			if (error)
				*error = message;
			return false;
		};

		std::error_code ec;
		if (!fs::is_regular_file(projectFile, ec))
			return fail("could not read project file '" + projectFile + "'");
		if (!fs::is_regular_file(runtimeExecutable, ec))
			return fail("the runtime executable '" + runtimeExecutable + "' was not found");

		// Validate before copying anything.
		std::string text;
		{
			std::ifstream file(projectFile, std::ios::binary);
			text.assign(std::istreambuf_iterator<char>(file), {});
		}
		std::string parseError;
		auto project = Project::Parse(text, &parseError);
		if (!project)
			return fail("invalid project file: " + parseError);

		const fs::path source = fs::canonical(fs::path(projectFile).parent_path().empty() ? fs::path(".") : fs::path(projectFile).parent_path(), ec);
		if (ec)
			return fail("could not resolve the project directory");
		if (!fs::is_regular_file(source / project->Scene, ec))
			return fail("the project's scene '" + project->Scene + "' does not exist");

		fs::create_directories(outputDirectory, ec);
		if (ec)
			return fail("could not create '" + outputDirectory + "': " + ec.message());
		const fs::path destination = fs::canonical(outputDirectory, ec);
		if (ec)
			return fail("could not resolve the output directory");
		if (destination == source)
			return fail("the output directory must differ from the project directory");

		// Copy the project tree, skipping hidden entries and the destination (if it lives inside the project).
		for (fs::recursive_directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec))
		{
			const fs::path& path = it->path();
			const std::string filename = path.filename().string();
			if ((!filename.empty() && filename[0] == '.') || fs::equivalent(path, destination, ec))
			{
				if (it->is_directory(ec))
					it.disable_recursion_pending();
				continue;
			}
			const fs::path relative = fs::relative(path, source, ec);
			if (it->is_directory(ec))
				fs::create_directories(destination / relative, ec);
			else if (it->is_regular_file(ec))
				fs::copy_file(path, destination / relative, fs::copy_options::overwrite_existing, ec);
			if (ec)
				return fail("could not copy '" + path.string() + "': " + ec.message());
		}
		if (ec)
			return fail("could not read the project directory: " + ec.message());

		// Install the runtime next to game.lumen.
		std::string executableName = MakeExecutableName(project->Name);
		const std::string extension = fs::path(runtimeExecutable).extension().string();
		if (!extension.empty())
			executableName += extension; // ".exe" on Windows
		const fs::path target = destination / executableName;
		fs::copy_file(runtimeExecutable, target, fs::copy_options::overwrite_existing, ec);
		if (ec)
			return fail("could not install the runtime: " + ec.message());
		fs::permissions(target, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
		return true;
	}

}
