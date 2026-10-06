#pragma once

#include "Lumen/Assets/GltfImporter.h"
#include "Lumen/Core/UUID.h"
#include "Lumen/Renderer/Mesh.h"
#include "Lumen/Renderer/RenderDevice.h"
#include "Lumen/Scene/Entity.h"

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Lumen {

	struct MeshAsset
	{
		std::string Name;
		MeshData Data;
	};

	struct TextureAsset
	{
		std::string Name;
		ImageData Image; // 8-bit RGBA; the material slot decides whether it is sampled as sRGB or linear
	};

	struct MaterialAsset
	{
		std::string Name;
		glm::vec4 BaseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		float Metallic = 1.0f;
		float Roughness = 1.0f;
		glm::vec3 Emissive = { 0.0f, 0.0f, 0.0f };
		float NormalScale = 1.0f;
		float OcclusionStrength = 1.0f;
		UUID BaseColorTexture = UUID(0);         // sRGB
		UUID MetallicRoughnessTexture = UUID(0); // linear (G roughness, B metallic)
		UUID NormalTexture = UUID(0);            // linear
		UUID OcclusionTexture = UUID(0);         // linear (R)
		UUID EmissiveTexture = UUID(0);          // sRGB
		AlphaMode Alpha = AlphaMode::Opaque;
		float AlphaCutoff = 0.5f;
		bool DoubleSided = false;
	};

	// An imported file's node tree, referencing registered assets.
	struct ModelNode
	{
		struct Primitive
		{
			UUID Mesh = UUID(0);
			UUID Material = UUID(0);
		};

		std::string Name;
		glm::mat4 LocalTransform = glm::mat4(1.0f);
		std::vector<Primitive> Primitives;
		std::vector<int> Children;
	};

	struct ModelAsset
	{
		std::string Name;
		std::vector<ModelNode> Nodes;
		std::vector<int> RootNodes;
		std::vector<std::string> Warnings;
	};

	// Owns CPU-side assets. Imported assets get deterministic ids derived from the source path and the asset's position
	// in the file, so scenes that reference them stay valid when the project is reopened and the files are re-imported.
	// Use project-relative paths (as written in the project) so ids do not depend on the machine.
	class AssetManager
	{
	public:
		UUID AddMesh(MeshAsset mesh);
		UUID AddTexture(TextureAsset texture);
		UUID AddMaterial(MaterialAsset material);

		const MeshAsset* GetMesh(UUID id) const;
		const TextureAsset* GetTexture(UUID id) const;
		const MaterialAsset* GetMaterial(UUID id) const;
		const ModelAsset* GetModel(UUID id) const;

		// Imports a .gltf/.glb file (idempotent: importing the same path again returns the existing model).
		// Returns the model id, or nullopt with `error` set.
		std::optional<UUID> ImportGltf(const std::string& path, std::string* error = nullptr);

		// Creates one entity per mesh primitive of the model, with world transforms composed from the node tree.
		// `rootTransform` is applied on top. Returns the created entities.
		std::vector<Entity> Instantiate(Scene& scene, UUID model, const glm::mat4& rootTransform = glm::mat4(1.0f)) const;

		size_t GetMeshCount() const { return m_Meshes.size(); }
		size_t GetTextureCount() const { return m_Textures.size(); }
		size_t GetMaterialCount() const { return m_Materials.size(); }

		// Id derived from a source and a sub-asset key (stable across runs and platforms). Never zero.
		static UUID MakeAssetID(const std::string& source, const std::string& kind, size_t index);
	private:
		UUID Register(UUID id, MeshAsset mesh);
		UUID Register(UUID id, TextureAsset texture);
		UUID Register(UUID id, MaterialAsset material);

		std::unordered_map<UUID, MeshAsset> m_Meshes;
		std::unordered_map<UUID, TextureAsset> m_Textures;
		std::unordered_map<UUID, MaterialAsset> m_Materials;
		std::unordered_map<UUID, ModelAsset> m_Models;
	};

}
