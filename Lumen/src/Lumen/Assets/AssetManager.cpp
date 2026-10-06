#include "Lumen/Assets/AssetManager.h"

#include "Lumen/Core/Log.h"
#include "Lumen/Scene/Scene.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Lumen {

	UUID AssetManager::MakeAssetID(const std::string& source, const std::string& kind, size_t index)
	{
		// FNV-1a, 64 bit.
		uint64_t hash = 14695981039346656037ull;
		auto mix = [&hash](const std::string& text)
		{
			for (unsigned char c : text)
			{
				hash ^= c;
				hash *= 1099511628211ull;
			}
			hash ^= 0xFF; // separator
			hash *= 1099511628211ull;
		};
		mix(source);
		mix(kind);
		mix(std::to_string(index));
		return UUID(hash == 0 ? 1 : hash);
	}

	UUID AssetManager::AddMesh(MeshAsset mesh) { return Register(UUID(), std::move(mesh)); }
	UUID AssetManager::AddTexture(TextureAsset texture) { return Register(UUID(), std::move(texture)); }
	UUID AssetManager::AddMaterial(MaterialAsset material) { return Register(UUID(), std::move(material)); }

	UUID AssetManager::Register(UUID id, MeshAsset mesh) { m_Meshes[id] = std::move(mesh); return id; }
	UUID AssetManager::Register(UUID id, TextureAsset texture) { m_Textures[id] = std::move(texture); return id; }
	UUID AssetManager::Register(UUID id, MaterialAsset material) { m_Materials[id] = std::move(material); return id; }

	const MeshAsset* AssetManager::GetMesh(UUID id) const { auto it = m_Meshes.find(id); return it != m_Meshes.end() ? &it->second : nullptr; }
	const TextureAsset* AssetManager::GetTexture(UUID id) const { auto it = m_Textures.find(id); return it != m_Textures.end() ? &it->second : nullptr; }
	const MaterialAsset* AssetManager::GetMaterial(UUID id) const { auto it = m_Materials.find(id); return it != m_Materials.end() ? &it->second : nullptr; }
	const ModelAsset* AssetManager::GetModel(UUID id) const { auto it = m_Models.find(id); return it != m_Models.end() ? &it->second : nullptr; }

	std::optional<UUID> AssetManager::ImportGltf(const std::string& path, std::string* error)
	{
		const UUID modelID = MakeAssetID(path, "model", 0);
		if (m_Models.contains(modelID))
			return modelID;

		std::optional<ImportedModel> imported = GltfImporter::ImportFile(path, error);
		if (!imported)
			return std::nullopt;

		for (const std::string& warning : imported->Warnings)
			LM_WARN("{}: {}", path, warning);

		// Textures
		std::vector<UUID> textureIDs;
		for (size_t i = 0; i < imported->Textures.size(); i++)
			textureIDs.push_back(Register(MakeAssetID(path, "texture", i), TextureAsset{ imported->Textures[i].Name, std::move(imported->Textures[i].Image) }));

		auto textureID = [&](const TextureRef& ref) { return ref.Texture >= 0 ? textureIDs[static_cast<size_t>(ref.Texture)] : UUID(0); };

		// Materials
		std::vector<UUID> materialIDs;
		for (size_t i = 0; i < imported->Materials.size(); i++)
		{
			const ImportedMaterial& m = imported->Materials[i];
			MaterialAsset material;
			material.Name = m.Name;
			material.BaseColor = m.BaseColor;
			material.Metallic = m.Metallic;
			material.Roughness = m.Roughness;
			material.Emissive = m.Emissive;
			material.NormalScale = m.NormalScale;
			material.OcclusionStrength = m.OcclusionStrength;
			material.BaseColorTexture = textureID(m.BaseColorTexture);
			material.MetallicRoughnessTexture = textureID(m.MetallicRoughnessTexture);
			material.NormalTexture = textureID(m.NormalTexture);
			material.OcclusionTexture = textureID(m.OcclusionTexture);
			material.EmissiveTexture = textureID(m.EmissiveTexture);
			material.Alpha = m.Alpha;
			material.AlphaCutoff = m.AlphaCutoff;
			material.DoubleSided = m.DoubleSided;
			materialIDs.push_back(Register(MakeAssetID(path, "material", i), std::move(material)));
		}

		// Meshes: one asset per primitive, keyed by (mesh index, primitive index).
		std::vector<std::vector<ModelNode::Primitive>> meshPrimitives(imported->Meshes.size());
		for (size_t i = 0; i < imported->Meshes.size(); i++)
		{
			ImportedMesh& mesh = imported->Meshes[i];
			for (size_t p = 0; p < mesh.Primitives.size(); p++)
			{
				ImportedPrimitive& primitive = mesh.Primitives[p];
				const UUID meshID = Register(MakeAssetID(path, "mesh", i * 1000 + p),
					MeshAsset{ mesh.Name.empty() ? "Mesh" : mesh.Name, std::move(primitive.Mesh) });
				const UUID materialID = primitive.Material >= 0 ? materialIDs[static_cast<size_t>(primitive.Material)] : UUID(0);
				meshPrimitives[i].push_back({ meshID, materialID });
			}
		}

		ModelAsset model;
		const size_t slash = path.find_last_of("/\\");
		model.Name = slash == std::string::npos ? path : path.substr(slash + 1);
		model.RootNodes = imported->RootNodes;
		model.Warnings = imported->Warnings;
		for (const ImportedNode& node : imported->Nodes)
		{
			ModelNode out;
			out.Name = node.Name;
			out.LocalTransform = node.LocalTransform;
			out.Children = node.Children;
			if (node.Mesh >= 0)
				out.Primitives = meshPrimitives[static_cast<size_t>(node.Mesh)];
			model.Nodes.push_back(std::move(out));
		}

		m_Models[modelID] = std::move(model);
		return modelID;
	}

	std::vector<Entity> AssetManager::Instantiate(Scene& scene, UUID modelID, const glm::mat4& rootTransform) const
	{
		std::vector<Entity> created;
		const ModelAsset* model = GetModel(modelID);
		if (model == nullptr)
		{
			LM_ERROR("Instantiate: unknown model asset");
			return created;
		}

		// Depth-first walk; `visited` protects against malformed files with cyclic node graphs.
		std::vector<bool> visited(model->Nodes.size(), false);
		struct Item { int Node; glm::mat4 Parent; };
		std::vector<Item> stack;
		for (auto it = model->RootNodes.rbegin(); it != model->RootNodes.rend(); ++it)
			stack.push_back({ *it, rootTransform });

		while (!stack.empty())
		{
			const Item item = stack.back();
			stack.pop_back();
			if (item.Node < 0 || static_cast<size_t>(item.Node) >= model->Nodes.size() || visited[static_cast<size_t>(item.Node)])
				continue;
			visited[static_cast<size_t>(item.Node)] = true;

			const ModelNode& node = model->Nodes[static_cast<size_t>(item.Node)];
			const glm::mat4 world = item.Parent * node.LocalTransform;

			if (!node.Primitives.empty())
			{
				glm::vec3 scale, translation, skew;
				glm::quat rotation;
				glm::vec4 perspective;
				glm::decompose(world, scale, rotation, translation, skew, perspective);

				for (size_t p = 0; p < node.Primitives.size(); p++)
				{
					std::string name = node.Name.empty() ? model->Name : node.Name;
					if (node.Primitives.size() > 1)
						name += " [" + std::to_string(p) + "]";

					Entity entity = scene.CreateEntity(name);
					TransformComponent& transform = entity.GetComponent<TransformComponent>();
					transform.Translation = translation;
					transform.Rotation = glm::eulerAngles(rotation);
					transform.Scale = scale;

					MeshRendererComponent& renderer = entity.AddComponent<MeshRendererComponent>();
					renderer.MeshAsset = node.Primitives[p].Mesh;
					renderer.MaterialAsset = node.Primitives[p].Material;
					created.push_back(entity);
				}
			}

			for (auto it = node.Children.rbegin(); it != node.Children.rend(); ++it)
				stack.push_back({ *it, world });
		}
		return created;
	}

}
