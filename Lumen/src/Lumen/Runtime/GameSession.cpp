#include "Lumen/Runtime/GameSession.h"

#include "Lumen/Assets/ImageIO.h"
#include "Lumen/Core/Log.h"
#include "Lumen/Scene/SceneSerializer.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace Lumen {

	namespace {

		bool ReadTextFile(const std::string& path, std::string& out)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file)
				return false;
			out.assign(std::istreambuf_iterator<char>(file), {});
			return true;
		}

	}

	GameSession::GameSession() = default;

	GameSession::~GameSession()
	{
		Stop();
	}

	bool GameSession::Load(const std::string& projectFile, std::string* error)
	{
		auto fail = [error](const std::string& message)
		{
			if (error)
				*error = message;
			return false;
		};
		if (m_Loaded)
			return fail("a project is already loaded in this session");

		std::string text;
		if (!ReadTextFile(projectFile, text))
			return fail("could not read project file '" + projectFile + "'");

		std::string parseError;
		std::optional<ProjectDesc> project = Project::Parse(text, &parseError);
		if (!project)
			return fail("invalid project file: " + parseError);

		const std::filesystem::path root = std::filesystem::path(projectFile).parent_path();
		m_Root = root.empty() ? std::string(".") : root.generic_string();

		for (const std::string& model : project->Models)
		{
			std::string importError;
			if (!m_Assets.ImportGltf(model, m_Root, &importError))
				return fail("could not import model '" + model + "': " + importError);
		}

		std::string sceneText;
		if (!ReadTextFile(m_Root + "/" + project->Scene, sceneText))
			return fail("could not read scene '" + project->Scene + "'");
		std::string sceneError;
		if (!SceneSerializer::Deserialize(m_Scene, sceneText, &sceneError))
			return fail("invalid scene '" + project->Scene + "': " + sceneError);

		if (project->Environment != "none")
		{
			ImageIO::HdrImage image;
			if (project->Environment == "sky")
			{
				image = EnvironmentBuilder::MakeProceduralSky();
			}
			else
			{
				std::string bytes;
				if (!ReadTextFile(m_Root + "/" + project->Environment, bytes))
					return fail("could not read environment '" + project->Environment + "'");
				std::string decodeError;
				auto decoded = ImageIO::DecodeHDR(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &decodeError);
				if (!decoded)
					return fail("could not decode environment '" + project->Environment + "': " + decodeError);
				image = std::move(*decoded);
			}
			std::string environmentError;
			auto environment = EnvironmentBuilder::FromEquirect(image, {}, &environmentError);
			if (!environment)
				return fail("invalid environment: " + environmentError);
			m_Environment = CreateRef<const Environment>(std::move(*environment));
		}

		m_Project = std::move(*project);
		m_Loaded = true;
		if (!FindCamera().IsValid())
			LM_WARN("The scene '{}' has no camera; nothing can be rendered until one is added", m_Project.Scene);
		return true;
	}

	Entity GameSession::FindCamera()
	{
		for (auto [handle, camera] : m_Scene.GetRegistry().view<CameraComponent>().each())
			return Entity(handle, &m_Scene);
		return Entity{};
	}

	void GameSession::Start()
	{
		LM_ASSERT(m_Loaded, "GameSession::Start needs a loaded project");
		if (m_Running)
			return;
		m_Physics.Start(m_Scene);
		m_Scripts.Start(m_Scene, &m_Physics);
		m_Running = true;
		m_Frame = 0;
	}

	void GameSession::Stop()
	{
		if (!m_Running)
			return;
		m_Scripts.Stop();
		m_Physics.Stop();
		m_Running = false;
	}

	void GameSession::Update(float deltaTime)
	{
		if (!m_Running)
			return;
		deltaTime = std::clamp(deltaTime, 0.0f, s_MaxFrameTime);
		m_Scripts.Update(deltaTime);
		m_Physics.Step(deltaTime);
		m_Frame++;
	}

}
