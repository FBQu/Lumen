#pragma once

#include "Lumen/Assets/AssetManager.h"
#include "Lumen/Core/Base.h"
#include "Lumen/Physics/PhysicsWorld.h"
#include "Lumen/Renderer/Environment.h"
#include "Lumen/Runtime/Project.h"
#include "Lumen/Scene/Scene.h"
#include "Lumen/Scripting/ScriptEngine.h"

#include <string>

namespace Lumen {

	// Everything needed to play a project, without any window or GPU: the scene, assets, physics and scripts.
	// The windowed runtime (and tests) drive it with Update(); rendering is done by the caller.
	class GameSession
	{
	public:
		GameSession();
		~GameSession();
		GameSession(const GameSession&) = delete;
		GameSession& operator=(const GameSession&) = delete;

		// Loads `projectFile` (game.lumen): imports its models, loads its scene and prepares the environment.
		// Returns false and fills `error` on failure; the session is then unusable.
		bool Load(const std::string& projectFile, std::string* error = nullptr);

		// Starts physics and scripts. Update runs scripts first, then physics, for one frame.
		void Start();
		void Stop();
		void Update(float deltaTime);
		bool IsRunning() const { return m_Running; }
		uint64_t GetFrame() const { return m_Frame; }

		Scene& GetScene() { return m_Scene; }
		AssetManager& GetAssets() { return m_Assets; }
		ScriptEngine& GetScripts() { return m_Scripts; }
		PhysicsWorld& GetPhysics() { return m_Physics; }
		const ProjectDesc& GetProject() const { return m_Project; }
		const std::string& GetRootDirectory() const { return m_Root; }
		Ref<const Environment> GetEnvironment() const { return m_Environment; }
		Entity FindCamera(); // the first entity with a CameraComponent, or an invalid entity

		static constexpr float s_MaxFrameTime = 0.1f; // longer frames are clamped so a hitch cannot explode the simulation
	private:
		Scene m_Scene;
		AssetManager m_Assets;
		PhysicsWorld m_Physics;
		ScriptEngine m_Scripts;
		ProjectDesc m_Project;
		std::string m_Root;
		Ref<const Environment> m_Environment;
		bool m_Loaded = false;
		bool m_Running = false;
		uint64_t m_Frame = 0;
	};

}
