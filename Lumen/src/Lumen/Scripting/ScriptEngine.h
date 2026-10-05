#pragma once

#include "Lumen/Core/Base.h"
#include "Lumen/Core/UUID.h"

#include <string>
#include <string_view>

namespace Lumen {

	class Scene;

	// Hosts the Lua state for one running Scene and drives ScriptComponent callbacks.
	// Scripts run sandboxed: only base, math, string, table, utf8 and coroutine libraries are available.
	class ScriptEngine
	{
	public:
		ScriptEngine();
		~ScriptEngine();
		ScriptEngine(const ScriptEngine&) = delete;
		ScriptEngine& operator=(const ScriptEngine&) = delete;

		// Binds the engine to a scene. Must be paired with Stop().
		void Start(Scene& scene);
		// Calls OnDestroy on all live scripts and releases script state.
		void Stop();
		// Instantiates new scripts (OnCreate) and calls OnUpdate on every script instance.
		void Update(float deltaTime);

		bool IsRunning() const { return m_Scene != nullptr; }
		size_t GetInstanceCount() const;

		// Runs a Lua snippet in the engine's global environment. Returns false and logs on error.
		bool Execute(std::string_view code);
	private:
		struct Impl;
		Scope<Impl> m_Impl;
		Scene* m_Scene = nullptr;
	};

}
