#pragma once

#include "Lumen/Renderer/Mesh.h"
#include "Lumen/Renderer/RenderDevice.h" // ImageData

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Lumen {

	// CPU-side result of importing a glTF 2.0 / GLB file. Indices into the vectors below use -1 for "none".
	struct ImportedTexture
	{
		std::string Name;
		ImageData Image; // decoded 8-bit RGBA
	};

	struct TextureRef
	{
		int Texture = -1;
		int TexCoord = 0; // only set 0 is supported by the renderer
	};

	enum class AlphaMode { Opaque, Mask, Blend };

	struct ImportedMaterial
	{
		std::string Name;
		glm::vec4 BaseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		float Metallic = 1.0f;  // glTF defaults
		float Roughness = 1.0f;
		glm::vec3 Emissive = { 0.0f, 0.0f, 0.0f };
		float NormalScale = 1.0f;
		float OcclusionStrength = 1.0f;
		TextureRef BaseColorTexture;         // sRGB
		TextureRef MetallicRoughnessTexture; // linear; G = roughness, B = metallic
		TextureRef NormalTexture;            // linear
		TextureRef OcclusionTexture;         // linear; R
		TextureRef EmissiveTexture;          // sRGB
		AlphaMode Alpha = AlphaMode::Opaque;
		float AlphaCutoff = 0.5f;
		bool DoubleSided = false;
	};

	struct ImportedPrimitive
	{
		MeshData Mesh;
		int Material = -1;
	};

	struct ImportedMesh
	{
		std::string Name;
		std::vector<ImportedPrimitive> Primitives;
	};

	struct ImportedNode
	{
		std::string Name;
		glm::mat4 LocalTransform = glm::mat4(1.0f);
		int Mesh = -1;
		std::vector<int> Children;
	};

	struct ImportedModel
	{
		std::vector<ImportedTexture> Textures;
		std::vector<ImportedMaterial> Materials;
		std::vector<ImportedMesh> Meshes;
		std::vector<ImportedNode> Nodes;
		std::vector<int> RootNodes; // nodes of the default scene
		std::vector<std::string> Warnings;
	};

	namespace GltfImporter
	{
		// `baseDirectory` resolves relative buffer/image URIs ("" if the data is self-contained).
		std::optional<ImportedModel> ImportMemory(const uint8_t* data, size_t size, const std::string& baseDirectory, std::string* error = nullptr);
		std::optional<ImportedModel> ImportFile(const std::string& path, std::string* error = nullptr);
	}

}
