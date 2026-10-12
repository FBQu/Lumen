#pragma once

#include <string>

namespace Lumen {

	// Exports a project as a self-contained, distributable folder: a copy of the project directory (hidden files and the
	// destination itself are skipped) plus the game runtime executable, named after the project, placed next to
	// game.lumen. The editor is not part of an export. Returns false and fills `error` on failure; on failure nothing
	// that was already valid in `outputDirectory` is removed.
	bool ExportProject(const std::string& projectFile, const std::string& outputDirectory, const std::string& runtimeExecutable, std::string* error = nullptr);

	// Turns a project name into a safe executable file name ("My Game!" -> "My_Game").
	std::string MakeExecutableName(const std::string& projectName);

}
