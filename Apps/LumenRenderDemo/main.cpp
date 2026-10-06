// Renders a sample scene headlessly and writes it as a PNG: LumenRenderDemo [output.png] [width] [height]

#include "Lumen/Assets/ImageIO.h"
#include "Lumen/Core/Log.h"
#include "Lumen/Renderer/Renderer.h"
#include "Lumen/Scene/Entity.h"
#include "Lumen/Scene/Scene.h"

#include <cstdlib>
#include <string>

using namespace Lumen;

int main(int argc, char** argv)
{
	const std::string output = argc > 1 ? argv[1] : "lumen_demo.png";
	const uint32_t width = argc > 2 ? static_cast<uint32_t>(std::atoi(argv[2])) : 960;
	const uint32_t height = argc > 3 ? static_cast<uint32_t>(std::atoi(argv[3])) : 540;

	Ref<RenderDevice> device = RenderDevice::Create({});
	if (!device)
		return 1;
	Ref<Renderer> renderer = Renderer::Create(device, width, height);
	if (!renderer)
		return 1;
	renderer->GetSettings().Ambient = glm::vec3(0.12f, 0.13f, 0.16f);
	renderer->GetSettings().ClearColor = glm::vec3(0.06f, 0.07f, 0.10f);

	Scene scene;

	Entity camera = scene.CreateEntity("Camera");
	camera.AddComponent<CameraComponent>().FovY = glm::radians(40.0f);
	camera.GetComponent<TransformComponent>().Translation = { 0.0f, 2.2f, 7.5f };
	camera.GetComponent<TransformComponent>().Rotation = { glm::radians(-14.0f), 0.0f, 0.0f };

	Entity sun = scene.CreateEntity("Sun");
	sun.AddComponent<DirectionalLightComponent>().Intensity = 4.0f;
	sun.GetComponent<TransformComponent>().Rotation = { glm::radians(-50.0f), glm::radians(25.0f), 0.0f };

	Entity ground = scene.CreateEntity("Ground");
	ground.GetComponent<TransformComponent>().Scale = { 14.0f, 1.0f, 14.0f };
	ground.GetComponent<TransformComponent>().Translation = { 0.0f, -0.5f, 0.0f };
	auto& groundMesh = ground.AddComponent<MeshRendererComponent>();
	groundMesh.Primitive = PrimitiveType::Plane;
	groundMesh.Material = { { 0.35f, 0.36f, 0.4f, 1.0f }, 0.0f, 0.85f, { 0, 0, 0 } };

	// Roughness increases left to right; the back row is metal, the front row dielectric.
	for (int row = 0; row < 2; row++)
	{
		for (int i = 0; i < 5; i++)
		{
			Entity sphere = scene.CreateEntity("Sphere");
			sphere.GetComponent<TransformComponent>().Translation = { -3.2f + 1.6f * i, 0.0f, -1.2f + 2.4f * row };
			auto& mesh = sphere.AddComponent<MeshRendererComponent>();
			mesh.Primitive = PrimitiveType::Sphere;
			mesh.Material.Roughness = 0.1f + 0.2f * i;
			mesh.Material.Metallic = row == 0 ? 1.0f : 0.0f;
			mesh.Material.BaseColor = row == 0 ? glm::vec4(1.0f, 0.78f, 0.34f, 1.0f) : glm::vec4(0.8f, 0.12f, 0.1f, 1.0f);
		}
	}

	Entity lamp = scene.CreateEntity("Emissive cube");
	lamp.GetComponent<TransformComponent>().Translation = { 0.0f, 0.3f, -3.4f };
	lamp.GetComponent<TransformComponent>().Rotation = { 0.0f, glm::radians(30.0f), 0.0f };
	lamp.GetComponent<TransformComponent>().Scale = { 0.8f, 1.6f, 0.8f };
	auto& lampMesh = lamp.AddComponent<MeshRendererComponent>();
	lampMesh.Material.BaseColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	lampMesh.Material.Emissive = { 0.2f, 1.6f, 2.2f };

	if (!renderer->Render(scene, camera))
		return 1;

	std::string error;
	if (!ImageIO::WritePNG(output, renderer->ReadOutput(), &error))
	{
		LM_ERROR("{}", error);
		return 1;
	}
	LM_INFO("Wrote {} ({}x{}) on {}", output, width, height, device->GetDeviceName());
	return 0;
}
