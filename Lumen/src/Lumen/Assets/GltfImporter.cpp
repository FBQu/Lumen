#include "Lumen/Assets/GltfImporter.h"

#include "Lumen/Assets/ImageIO.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <set>

namespace Lumen::GltfImporter {

	namespace {

		void SetError(std::string* error, const std::string& message)
		{
			if (error)
				*error = message;
		}

		std::string ResultName(cgltf_result result)
		{
			switch (result)
			{
				case cgltf_result_data_too_short:    return "data too short";
				case cgltf_result_unknown_format:    return "unknown file format";
				case cgltf_result_invalid_json:      return "invalid JSON";
				case cgltf_result_invalid_gltf:      return "invalid glTF";
				case cgltf_result_invalid_options:   return "invalid options";
				case cgltf_result_file_not_found:    return "file not found";
				case cgltf_result_io_error:          return "I/O error";
				case cgltf_result_out_of_memory:     return "out of memory";
				case cgltf_result_legacy_gltf:       return "legacy glTF 1.0 is not supported";
				default:                             return "error";
			}
		}

		// Extensions that change how geometry or textures must be decoded; ignoring them would give wrong results.
		bool IsSupportedRequiredExtension(const std::string& name)
		{
			static const std::set<std::string> supported = { "KHR_materials_emissive_strength", "KHR_lights_punctual", "KHR_texture_transform", "KHR_mesh_quantization" };
			return supported.contains(name);
		}

		std::vector<uint8_t> DecodeBase64(const char* text, size_t length)
		{
			std::vector<uint8_t> out;
			out.reserve(length / 4 * 3);
			uint32_t accumulator = 0;
			int bits = 0;
			for (size_t i = 0; i < length; i++)
			{
				const char c = text[i];
				int value;
				if (c >= 'A' && c <= 'Z') value = c - 'A';
				else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
				else if (c >= '0' && c <= '9') value = c - '0' + 52;
				else if (c == '+' || c == '-') value = 62;
				else if (c == '/' || c == '_') value = 63;
				else if (c == '=') break;
				else continue; // ignore whitespace
				accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
				bits += 6;
				if (bits >= 8)
				{
					bits -= 8;
					out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFF));
				}
			}
			return out;
		}

		// Relative paths only: refuse absolute paths, drive letters and parent-directory escapes.
		bool IsSafeRelativeUri(const std::string& uri)
		{
			if (uri.empty() || uri[0] == '/' || uri[0] == '\\' || uri.find(':') != std::string::npos)
				return false;
			size_t start = 0;
			while (start <= uri.size())
			{
				size_t end = uri.find_first_of("/\\", start);
				if (end == std::string::npos)
					end = uri.size();
				if (uri.compare(start, end - start, "..") == 0)
					return false;
				start = end + 1;
			}
			return true;
		}

		bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file)
				return false;
			out.assign(std::istreambuf_iterator<char>(file), {});
			return true;
		}

		std::string JoinPath(const std::string& directory, const std::string& file)
		{
			return directory.empty() ? file : directory + "/" + file;
		}

		// Loads the encoded bytes (PNG/JPEG/...) of one glTF image.
		bool LoadImageBytes(const cgltf_image& image, const std::string& baseDirectory, std::vector<uint8_t>& out, std::string& problem)
		{
			if (image.buffer_view != nullptr)
			{
				const cgltf_buffer_view* view = image.buffer_view;
				if (view->buffer == nullptr || view->buffer->data == nullptr || view->offset + view->size > view->buffer->size)
				{
					problem = "image buffer view is out of range";
					return false;
				}
				const uint8_t* bytes = static_cast<const uint8_t*>(view->buffer->data) + view->offset;
				out.assign(bytes, bytes + view->size);
				return true;
			}

			if (image.uri == nullptr)
			{
				problem = "image has neither a buffer view nor a URI";
				return false;
			}

			const std::string uri = image.uri;
			if (uri.rfind("data:", 0) == 0)
			{
				const size_t comma = uri.find(',');
				if (comma == std::string::npos)
				{
					problem = "malformed data URI";
					return false;
				}
				out = DecodeBase64(uri.c_str() + comma + 1, uri.size() - comma - 1);
				return true;
			}

			std::string decoded = uri;
			cgltf_decode_uri(decoded.data());
			decoded.resize(std::strlen(decoded.c_str()));
			if (!IsSafeRelativeUri(decoded))
			{
				problem = "image URI '" + uri + "' is not a safe relative path";
				return false;
			}
			if (!ReadFileBytes(JoinPath(baseDirectory, decoded), out))
			{
				problem = "could not read image file '" + decoded + "'";
				return false;
			}
			return true;
		}

		TextureRef MakeTextureRef(const cgltf_data* data, const cgltf_texture_view& view)
		{
			TextureRef ref;
			if (view.texture != nullptr && view.texture->image != nullptr)
			{
				ref.Texture = static_cast<int>(cgltf_image_index(data, view.texture->image));
				ref.TexCoord = view.texcoord;
			}
			return ref;
		}

		bool ReadPrimitive(const cgltf_data* data, const cgltf_primitive& primitive, ImportedPrimitive& out, std::vector<std::string>& warnings, std::string& problem)
		{
			const cgltf_accessor* positions = nullptr;
			const cgltf_accessor* normals = nullptr;
			const cgltf_accessor* uvs = nullptr;
			const cgltf_accessor* tangents = nullptr;
			for (cgltf_size i = 0; i < primitive.attributes_count; i++)
			{
				const cgltf_attribute& attribute = primitive.attributes[i];
				if (attribute.type == cgltf_attribute_type_position && attribute.index == 0) positions = attribute.data;
				else if (attribute.type == cgltf_attribute_type_normal && attribute.index == 0) normals = attribute.data;
				else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) uvs = attribute.data;
				else if (attribute.type == cgltf_attribute_type_tangent && attribute.index == 0) tangents = attribute.data;
			}
			if (positions == nullptr || positions->count == 0)
			{
				problem = "primitive has no POSITION data";
				return false;
			}

			const cgltf_size vertexCount = positions->count;
			MeshData& mesh = out.Mesh;
			mesh.Vertices.resize(vertexCount);
			for (cgltf_size i = 0; i < vertexCount; i++)
			{
				Vertex& v = mesh.Vertices[i];
				float tmp[4] = { 0, 0, 0, 0 };
				if (!cgltf_accessor_read_float(positions, i, tmp, 3))
				{
					problem = "could not read POSITION data";
					return false;
				}
				v.Position = { tmp[0], tmp[1], tmp[2] };
				if (normals != nullptr && normals->count == vertexCount && cgltf_accessor_read_float(normals, i, tmp, 3))
					v.Normal = glm::normalize(glm::vec3(tmp[0], tmp[1], tmp[2]));
				if (uvs != nullptr && uvs->count == vertexCount && cgltf_accessor_read_float(uvs, i, tmp, 2))
					v.UV = { tmp[0], tmp[1] };
				if (tangents != nullptr && tangents->count == vertexCount && cgltf_accessor_read_float(tangents, i, tmp, 4))
					v.Tangent = { tmp[0], tmp[1], tmp[2], tmp[3] < 0.0f ? -1.0f : 1.0f };
			}

			if (primitive.indices != nullptr)
			{
				mesh.Indices.resize(primitive.indices->count);
				for (cgltf_size i = 0; i < primitive.indices->count; i++)
					mesh.Indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
			}
			else
			{
				mesh.Indices.resize(vertexCount);
				for (cgltf_size i = 0; i < vertexCount; i++)
					mesh.Indices[i] = static_cast<uint32_t>(i);
			}

			if (mesh.Indices.size() % 3 != 0)
			{
				problem = "primitive index count is not a multiple of 3";
				return false;
			}
			for (uint32_t index : mesh.Indices)
			{
				if (index >= vertexCount)
				{
					problem = "primitive index out of range";
					return false;
				}
			}

			const bool hasNormals = normals != nullptr && normals->count == vertexCount;
			if (!hasNormals)
			{
				// glTF requires flat shading when normals are absent: give each triangle its own vertices.
				std::vector<Vertex> unwelded;
				unwelded.reserve(mesh.Indices.size());
				for (uint32_t index : mesh.Indices)
					unwelded.push_back(mesh.Vertices[index]);
				mesh.Vertices = std::move(unwelded);
				for (size_t i = 0; i < mesh.Indices.size(); i++)
					mesh.Indices[i] = static_cast<uint32_t>(i);
				mesh.ComputeNormals();
			}
			if (tangents == nullptr || tangents->count != vertexCount || !hasNormals)
				mesh.ComputeTangents();

			if (!mesh.IsValid())
			{
				problem = "primitive contains non-finite values";
				return false;
			}

			out.Material = primitive.material != nullptr ? static_cast<int>(cgltf_material_index(data, primitive.material)) : -1;
			(void)warnings;
			return true;
		}

	}

	std::optional<ImportedModel> ImportMemory(const uint8_t* bytes, size_t size, const std::string& baseDirectory, std::string* error)
	{
		if (bytes == nullptr || size == 0)
		{
			SetError(error, "no data");
			return std::nullopt;
		}

		cgltf_options options{};
		cgltf_data* data = nullptr;
		if (cgltf_result result = cgltf_parse(&options, bytes, size, &data); result != cgltf_result_success)
		{
			SetError(error, "cannot parse glTF: " + ResultName(result));
			return std::nullopt;
		}
		struct Cleanup { cgltf_data* Data; ~Cleanup() { cgltf_free(Data); } } cleanup{ data };

		for (cgltf_size i = 0; i < data->extensions_required_count; i++)
		{
			if (!IsSupportedRequiredExtension(data->extensions_required[i]))
			{
				SetError(error, std::string("unsupported required extension ") + data->extensions_required[i]);
				return std::nullopt;
			}
		}

		const std::string gltfPath = JoinPath(baseDirectory, "model.gltf"); // cgltf resolves relative buffer URIs against this
		if (cgltf_result result = cgltf_load_buffers(&options, data, gltfPath.c_str()); result != cgltf_result_success)
		{
			SetError(error, "cannot load glTF buffers: " + ResultName(result));
			return std::nullopt;
		}
		if (cgltf_result result = cgltf_validate(data); result != cgltf_result_success)
		{
			SetError(error, "invalid glTF: " + ResultName(result));
			return std::nullopt;
		}

		ImportedModel model;

		// --- Textures (one per glTF image) -----------------------------------------------------------------------------
		for (cgltf_size i = 0; i < data->images_count; i++)
		{
			ImportedTexture texture;
			texture.Name = data->images[i].name != nullptr ? data->images[i].name : "";

			std::vector<uint8_t> encoded;
			std::string problem, decodeError;
			std::optional<ImageData> decoded;
			if (LoadImageBytes(data->images[i], baseDirectory, encoded, problem))
			{
				decoded = ImageIO::DecodeLDR(encoded.data(), encoded.size(), &decodeError);
				if (!decoded)
					problem = "could not decode image: " + decodeError;
			}

			if (decoded)
			{
				texture.Image = std::move(*decoded);
			}
			else
			{
				model.Warnings.push_back("image " + std::to_string(i) + ": " + problem + "; using a white placeholder");
				texture.Image.Width = texture.Image.Height = 1;
				texture.Image.BytesPerPixel = 4;
				texture.Image.Pixels = { 255, 255, 255, 255 };
			}
			model.Textures.push_back(std::move(texture));
		}

		// --- Materials -------------------------------------------------------------------------------------------------
		for (cgltf_size i = 0; i < data->materials_count; i++)
		{
			const cgltf_material& source = data->materials[i];
			ImportedMaterial material;
			material.Name = source.name != nullptr ? source.name : "";

			if (source.has_pbr_metallic_roughness)
			{
				const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
				material.BaseColor = { pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3] };
				material.Metallic = pbr.metallic_factor;
				material.Roughness = pbr.roughness_factor;
				material.BaseColorTexture = MakeTextureRef(data, pbr.base_color_texture);
				material.MetallicRoughnessTexture = MakeTextureRef(data, pbr.metallic_roughness_texture);
			}
			else if (source.has_pbr_specular_glossiness)
			{
				model.Warnings.push_back("material '" + material.Name + "' uses KHR_materials_pbrSpecularGlossiness, which is not supported; using defaults");
			}

			material.NormalTexture = MakeTextureRef(data, source.normal_texture);
			material.NormalScale = source.normal_texture.scale;
			material.OcclusionTexture = MakeTextureRef(data, source.occlusion_texture);
			material.OcclusionStrength = source.occlusion_texture.scale;
			material.EmissiveTexture = MakeTextureRef(data, source.emissive_texture);
			material.Emissive = { source.emissive_factor[0], source.emissive_factor[1], source.emissive_factor[2] };
			if (source.has_emissive_strength)
				material.Emissive *= source.emissive_strength.emissive_strength;

			switch (source.alpha_mode)
			{
				case cgltf_alpha_mode_mask:  material.Alpha = AlphaMode::Mask; break;
				case cgltf_alpha_mode_blend: material.Alpha = AlphaMode::Blend; break;
				default:                     material.Alpha = AlphaMode::Opaque; break;
			}
			material.AlphaCutoff = source.alpha_cutoff;
			material.DoubleSided = source.double_sided != 0;
			model.Materials.push_back(std::move(material));
		}

		// --- Meshes ----------------------------------------------------------------------------------------------------
		for (cgltf_size i = 0; i < data->meshes_count; i++)
		{
			const cgltf_mesh& source = data->meshes[i];
			ImportedMesh mesh;
			mesh.Name = source.name != nullptr ? source.name : "";

			for (cgltf_size p = 0; p < source.primitives_count; p++)
			{
				const cgltf_primitive& primitive = source.primitives[p];
				if (primitive.type != cgltf_primitive_type_triangles)
				{
					model.Warnings.push_back("mesh '" + mesh.Name + "' primitive " + std::to_string(p) + " is not a triangle list and was skipped");
					continue;
				}
				if (primitive.has_draco_mesh_compression)
				{
					SetError(error, "Draco-compressed meshes are not supported");
					return std::nullopt;
				}

				ImportedPrimitive imported;
				std::string problem;
				if (!ReadPrimitive(data, primitive, imported, model.Warnings, problem))
				{
					SetError(error, "mesh '" + mesh.Name + "' primitive " + std::to_string(p) + ": " + problem);
					return std::nullopt;
				}
				mesh.Primitives.push_back(std::move(imported));
			}
			model.Meshes.push_back(std::move(mesh));
		}

		// --- Nodes -----------------------------------------------------------------------------------------------------
		for (cgltf_size i = 0; i < data->nodes_count; i++)
		{
			const cgltf_node& source = data->nodes[i];
			ImportedNode node;
			node.Name = source.name != nullptr ? source.name : "";
			node.Mesh = source.mesh != nullptr ? static_cast<int>(cgltf_mesh_index(data, source.mesh)) : -1;

			float matrix[16];
			cgltf_node_transform_local(&source, matrix);
			std::memcpy(&node.LocalTransform[0][0], matrix, sizeof(matrix)); // both are column-major

			for (cgltf_size c = 0; c < source.children_count; c++)
				node.Children.push_back(static_cast<int>(cgltf_node_index(data, source.children[c])));
			model.Nodes.push_back(std::move(node));
		}

		const cgltf_scene* scene = data->scene != nullptr ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
		if (scene != nullptr)
		{
			for (cgltf_size i = 0; i < scene->nodes_count; i++)
				model.RootNodes.push_back(static_cast<int>(cgltf_node_index(data, scene->nodes[i])));
		}
		else
		{
			// No scene: every node that is nobody's child is a root.
			std::vector<bool> isChild(model.Nodes.size(), false);
			for (const ImportedNode& node : model.Nodes)
				for (int child : node.Children)
					isChild[static_cast<size_t>(child)] = true;
			for (size_t i = 0; i < model.Nodes.size(); i++)
				if (!isChild[i])
					model.RootNodes.push_back(static_cast<int>(i));
		}

		return model;
	}

	std::optional<ImportedModel> ImportFile(const std::string& path, std::string* error)
	{
		std::vector<uint8_t> bytes;
		if (!ReadFileBytes(path, bytes))
		{
			SetError(error, "could not read '" + path + "'");
			return std::nullopt;
		}

		const size_t slash = path.find_last_of("/\\");
		const std::string directory = slash == std::string::npos ? "" : path.substr(0, slash);
		return ImportMemory(bytes.data(), bytes.size(), directory, error);
	}

}
