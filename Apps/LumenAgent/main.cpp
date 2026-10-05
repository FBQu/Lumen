// LumenAgent: headless engine host for AI agents. Reads one JSON request per line on stdin and writes one JSON
// response per line on stdout. See Docs/AgentAPI.md.

#include "Lumen/Agent/AgentSession.h"

#include <iostream>
#include <string>

int main()
{
	std::ios::sync_with_stdio(false);

	Lumen::AgentSession session;
	std::string line;
	while (std::getline(std::cin, line))
	{
		if (line.find_first_not_of(" \t\r") == std::string::npos)
			continue;
		std::cout << session.Execute(line) << '\n' << std::flush;
	}
	return 0;
}
