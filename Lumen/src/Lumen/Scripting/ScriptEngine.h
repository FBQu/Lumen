#pragma once

#include "Lumen/Core/Base.h"
#include "Lumen/Core/UUID.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Lumen {

	class Scene;
	class PhysicsWorld;

	// Hosts the Lua state for one running Scene and drives ScriptComponent callbacks.
	// Scripts run sandboxed: only base, math, string, table, utf8 and coroutine libraries are available.
	class ScriptEngine
	{
	public:
		ScriptEngine();
		~ScriptEngine();
		ScriptEngine(const ScriptEngine&) = delete;
		ScriptEngine& operator=(const ScriptEngine&) = delete;

		// Binds the engine to a scene. Must be paired with Stop(). If a PhysicsWorld is given, scripts
		// can use the global Physics table and entity:AddRigidbody / entity:AddCollider.
		void Start(Scene& scene, PhysicsWorld* physics = nullptr);
		// Calls OnDestroy on all live scripts and releases script state.
		void Stop();
		// Instantiates new scripts (OnCreate) and calls OnUpdate on every script instance.
		void Update(float deltaTime);

		bool IsRunning() const { return m_Scene != nullptr; }

		// Every script callback (and every Execute call) may run at most this many Lua VM instructions (approximately)
		// before it is aborted with an error, so a runaway loop cannot hang the engine. Default: 5 million.
		void SetInstructionLimit(uint64_t limit);
		uint64_t GetInstructionLimit() const;
		size_t GetInstanceCount() const;

		// Runs a Lua snippet in the engine's global environment. Returns false and logs on error.
		// If the snippet returns a value, `output` receives it as text; `error` receives the failure message.
		bool Execute(std::string_view code, std::string* output = nullptr, std::string* error = nullptr);
	private:
		struct Impl;
		Scope<Impl> m_Impl;
		Scene* m_Scene = nullptr;
	};

}
