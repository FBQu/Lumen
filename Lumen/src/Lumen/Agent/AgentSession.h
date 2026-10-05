#pragma once

#include "Lumen/Core/Base.h"

#include <string>
#include <string_view>

namespace Lumen {

	class Scene;

	// Headless, fully scriptable control surface for AI agents (and tools). Owns a Scene, a PhysicsWorld and a
	// ScriptEngine, and exposes them through a JSON request/response protocol:
	//
	//   request:  {"cmd": "entity.create", "args": {...}, "requestId": <any JSON value, optional>}
	//   response: {"ok": true, "result": ...}  or  {"ok": false, "error": "message"}  (requestId is echoed)
	//
	// Execute never throws. See Docs/AgentAPI.md for the command list. Only one session should exist at a time
	// because it captures the global log into its own buffer (command "log.get").
	class AgentSession
	{
	public:
		AgentSession();
		~AgentSession();
		AgentSession(const AgentSession&) = delete;
		AgentSession& operator=(const AgentSession&) = delete;

		std::string Execute(std::string_view requestJson);

		Scene& GetScene();
		bool IsPlaying() const;
	private:
		struct Impl;
		Scope<Impl> m_Impl;
	};

}
