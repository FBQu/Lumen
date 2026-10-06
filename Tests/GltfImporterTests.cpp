#include <doctest/doctest.h>

#include "Lumen/Assets/GltfImporter.h"
#include "Lumen/Assets/ImageIO.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Lumen;

namespace {

	std::string Base64(const std::vector<uint8_t>& bytes)
	{
		static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		for (size_t i = 0; i < bytes.size(); i += 3)
		{
			const uint32_t n = (bytes[i] << 16) | (i + 1 < bytes.size() ? bytes[i + 1] << 8 : 0) | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
			out += table[(n >> 18) & 63];
			out += table[(n >> 12) & 63];
			out += i + 1 < bytes.size() ? table[(n >> 6) & 63] : '=';
			out += i + 2 < bytes.size() ? table[n & 63] : '=';
		}
		return out;
	}

	template<typename T>
	void Append(std::vector<uint8_t>& bytes, std::initializer_list<T> values)
	{
		for (T value : values)
		{
			const size_t at = bytes.size();
			bytes.resize(at + sizeof(T));
			std::memcpy(&bytes[at], &value, sizeof(T));
		}
	}

	void Pad4(std::vector<uint8_t>& bytes) { while (bytes.size() % 4) bytes.push_back(0); }

	// One counter-clockwise triangle in the XY plane facing +Z: buffer layout positions | normals | uvs | indices(u16).
	std::vector<uint8_t> TriangleBuffer()
	{
		std::vector<uint8_t> b;
		Append<float>(b, { 0, 0, 0,  1, 0, 0,  0, 1, 0 });
		Append<float>(b, { 0, 0, 1,  0, 0, 1,  0, 0, 1 });
		Append<float>(b, { 0, 1,  1, 1,  0, 0 });
		Append<uint16_t>(b, { 0, 1, 2 });
		Pad4(b);
		return b;
	}

	const char* kTriangleViews = R"(
		"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},
		               {"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],
		"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
		             {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
		             {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
		             {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],)";

	std::string TriangleGltf(const std::string& extra = "", const std::string& attributes = R"("POSITION":0,"NORMAL":1,"TEXCOORD_0":2)",
	                         const std::string& primitiveExtra = R"(,"indices":3,"material":0)")
	{
		std::vector<uint8_t> buffer = TriangleBuffer();
		return std::string(R"({"asset":{"version":"2.0"},)") + R"("buffers":[{"byteLength":)" + std::to_string(buffer.size())
			+ R"(,"uri":"data:application/octet-stream;base64,)" + Base64(buffer) + R"("}],)" + kTriangleViews
			+ R"("materials":[{"name":"Red","pbrMetallicRoughness":{"baseColorFactor":[1,0,0,0.5],"metallicFactor":0.25,"roughnessFactor":0.75},)"
			  R"("emissiveFactor":[0,1,0],"doubleSided":true,"alphaMode":"MASK","alphaCutoff":0.3}],)"
			+ R"("meshes":[{"name":"Tri","primitives":[{"attributes":{)" + attributes + "}" + primitiveExtra + R"(}]}],)"
			+ R"("nodes":[{"name":"Root","mesh":0,"translation":[1,2,3],"children":[1]},{"name":"Child","scale":[2,2,2]}],)"
			+ R"("scenes":[{"nodes":[0]}],"scene":0)" + extra + "}";
	}

	std::optional<ImportedModel> Import(const std::string& json, std::string* error = nullptr)
	{
		return GltfImporter::ImportMemory(reinterpret_cast<const uint8_t*>(json.data()), json.size(), "", error);
	}

	bool Fails(const std::string& json, const std::string& fragment)
	{
		std::string error;
		auto model = Import(json, &error);
		if (model || error.find(fragment) == std::string::npos)
			MESSAGE("expected an error containing '" << fragment << "', got '" << error << "'");
		return !model && error.find(fragment) != std::string::npos;
	}

	ImageData SolidImage(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b)
	{
		ImageData image;
		image.Width = w;
		image.Height = h;
		image.BytesPerPixel = 4;
		for (uint32_t i = 0; i < w * h; i++)
			for (uint8_t v : { r, g, b, uint8_t(255) })
				image.Pixels.push_back(v);
		return image;
	}

	// Replaces the first occurrence of `needle`; fails the test if it is not present.
	void ReplaceOnce(std::string& text, const std::string& needle, const std::string& replacement)
	{
		const size_t at = text.find(needle);
		REQUIRE_MESSAGE(at != std::string::npos, "missing '" << needle << "'");
		text.replace(at, needle.size(), replacement);
	}

	bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) { return glm::length(a - b) < eps; }

}

TEST_CASE("glTF: triangle with material, node hierarchy and attributes")
{
	std::string error;
	auto model = Import(TriangleGltf(), &error);
	REQUIRE_MESSAGE(model.has_value(), error);
	CHECK(model->Warnings.empty());

	REQUIRE(model->Meshes.size() == 1);
	REQUIRE(model->Meshes[0].Primitives.size() == 1);
	const MeshData& mesh = model->Meshes[0].Primitives[0].Mesh;
	CHECK(mesh.IsValid());
	REQUIRE(mesh.Vertices.size() == 3);
	CHECK(mesh.Indices == std::vector<uint32_t>{ 0, 1, 2 });
	CHECK(Near(mesh.Vertices[1].Position, { 1, 0, 0 }));
	CHECK(Near(mesh.Vertices[2].Normal, { 0, 0, 1 }));
	CHECK(mesh.Vertices[2].UV == glm::vec2(0, 0));
	// Tangents are computed when the file has none: +X is the direction of increasing U.
	CHECK(Near(glm::vec3(mesh.Vertices[0].Tangent), { 1, 0, 0 }));
	CHECK(model->Meshes[0].Primitives[0].Material == 0);

	REQUIRE(model->Materials.size() == 1);
	const ImportedMaterial& m = model->Materials[0];
	CHECK(m.Name == "Red");
	CHECK(m.BaseColor == glm::vec4(1, 0, 0, 0.5f));
	CHECK(m.Metallic == doctest::Approx(0.25f));
	CHECK(m.Roughness == doctest::Approx(0.75f));
	CHECK(m.Emissive == glm::vec3(0, 1, 0));
	CHECK(m.Alpha == AlphaMode::Mask);
	CHECK(m.AlphaCutoff == doctest::Approx(0.3f));
	CHECK(m.DoubleSided);
	CHECK(m.BaseColorTexture.Texture == -1);

	REQUIRE(model->Nodes.size() == 2);
	CHECK(model->RootNodes == std::vector<int>{ 0 });
	CHECK(model->Nodes[0].Name == "Root");
	CHECK(model->Nodes[0].Mesh == 0);
	CHECK(model->Nodes[0].Children == std::vector<int>{ 1 });
	CHECK(Near(glm::vec3(model->Nodes[0].LocalTransform[3]), { 1, 2, 3 }));
	CHECK(model->Nodes[1].Mesh == -1);
	CHECK(model->Nodes[1].LocalTransform[0][0] == doctest::Approx(2.0f));
}

TEST_CASE("glTF: missing normals give flat shading with unwelded vertices")
{
	// A quad of two triangles sharing two vertices, no NORMAL attribute.
	std::vector<uint8_t> b;
	Append<float>(b, { 0, 0, 0,  1, 0, 0,  1, 1, 0,  0, 1, 0 });
	Append<uint16_t>(b, { 0, 1, 2, 0, 2, 3 });
	Pad4(b);
	const std::string json = std::string(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":)") + std::to_string(b.size())
		+ R"(,"uri":"data:application/octet-stream;base64,)" + Base64(b) + R"("}],)"
		R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":12}],)"
		R"("accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
		R"({"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],)"
		R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"nodes":[{"mesh":0}]})";

	std::string error;
	auto model = Import(json, &error);
	REQUIRE_MESSAGE(model.has_value(), error);
	const MeshData& mesh = model->Meshes[0].Primitives[0].Mesh;
	CHECK(mesh.Vertices.size() == 6); // unwelded
	CHECK(mesh.Indices.size() == 6);
	CHECK(mesh.IsValid());
	for (const Vertex& v : mesh.Vertices)
		CHECK(Near(v.Normal, { 0, 0, 1 }));
	CHECK(model->RootNodes == std::vector<int>{ 0 }); // no scene: parentless nodes are roots
	CHECK(model->Meshes[0].Primitives[0].Material == -1);
}

TEST_CASE("glTF: primitives without indices use sequential indices")
{
	std::string error;
	auto model = Import(TriangleGltf("", R"("POSITION":0,"NORMAL":1,"TEXCOORD_0":2)", ""), &error);
	REQUIRE_MESSAGE(model.has_value(), error);
	CHECK(model->Meshes[0].Primitives[0].Mesh.Indices == std::vector<uint32_t>{ 0, 1, 2 });
	CHECK(model->Meshes[0].Primitives[0].Material == -1);
}

TEST_CASE("glTF: embedded PNG textures are decoded and referenced by materials")
{
	std::vector<uint8_t> png = ImageIO::EncodePNG(SolidImage(4, 2, 10, 20, 30));
	REQUIRE_FALSE(png.empty());

	std::string json = TriangleGltf();
	const std::string images = std::string(R"(,"images":[{"name":"Albedo","uri":"data:image/png;base64,)") + Base64(png) + R"("}],)"
		R"("textures":[{"source":0}])";
	json.insert(json.rfind('}'), images);
	ReplaceOnce(json, R"("pbrMetallicRoughness":{)", R"("pbrMetallicRoughness":{"baseColorTexture":{"index":0,"texCoord":0},)");

	std::string error;
	auto model = Import(json, &error);
	REQUIRE_MESSAGE(model.has_value(), error);
	REQUIRE(model->Textures.size() == 1);
	CHECK(model->Warnings.empty());
	CHECK(model->Textures[0].Name == "Albedo");
	CHECK(model->Textures[0].Image.Width == 4);
	CHECK(model->Textures[0].Image.Height == 2);
	CHECK(model->Textures[0].Image.PixelAt(3, 1)[1] == 20);
	CHECK(model->Materials[0].BaseColorTexture.Texture == 0);
	CHECK(model->Materials[0].NormalTexture.Texture == -1);
}

TEST_CASE("GLB: binary chunk with geometry and a buffer-view image")
{
	std::vector<uint8_t> bin = TriangleBuffer();
	const size_t imageOffset = bin.size();
	std::vector<uint8_t> png = ImageIO::EncodePNG(SolidImage(2, 2, 200, 100, 50));
	bin.insert(bin.end(), png.begin(), png.end());
	Pad4(bin);

	std::string json = std::string(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":)") + std::to_string(bin.size()) + R"(}],)"
		+ kTriangleViews;
	// Add an image view after the 4 geometry views.
	ReplaceOnce(json, R"({"buffer":0,"byteOffset":96,"byteLength":6})",
		R"({"buffer":0,"byteOffset":96,"byteLength":6},{"buffer":0,"byteOffset":)" + std::to_string(imageOffset) + R"(,"byteLength":)" + std::to_string(png.size()) + "}");
	json += R"("images":[{"bufferView":4,"mimeType":"image/png"}],"textures":[{"source":0}],)"
		R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],)"
		R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}]})";
	while (json.size() % 4) json += ' ';

	std::vector<uint8_t> glb;
	Append<uint32_t>(glb, { 0x46546C67, 2, uint32_t(12 + 8 + json.size() + 8 + bin.size()) });
	Append<uint32_t>(glb, { uint32_t(json.size()), 0x4E4F534A });
	glb.insert(glb.end(), json.begin(), json.end());
	Append<uint32_t>(glb, { uint32_t(bin.size()), 0x004E4942 });
	glb.insert(glb.end(), bin.begin(), bin.end());

	std::string error;
	auto model = GltfImporter::ImportMemory(glb.data(), glb.size(), "", &error);
	REQUIRE_MESSAGE(model.has_value(), error);
	CHECK(model->Warnings.empty());
	REQUIRE(model->Meshes[0].Primitives.size() == 1);
	CHECK(model->Meshes[0].Primitives[0].Mesh.Vertices.size() == 3);
	REQUIRE(model->Textures.size() == 1);
	CHECK(model->Textures[0].Image.Width == 2);
	CHECK(model->Textures[0].Image.PixelAt(1, 1)[0] == 200);
	CHECK(model->Materials[0].Metallic == doctest::Approx(1.0f)); // glTF defaults
	CHECK(model->Materials[0].Roughness == doctest::Approx(1.0f));
}

TEST_CASE("glTF: emissive strength extension, non-triangle primitives and missing PBR block")
{
	std::string json = TriangleGltf(R"(,"extensionsUsed":["KHR_materials_emissive_strength"])");
	ReplaceOnce(json, R"("doubleSided":true)", R"("doubleSided":false,"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":5}})");
	std::string error;
	auto model = Import(json, &error);
	REQUIRE_MESSAGE(model.has_value(), error);
	CHECK(model->Materials[0].Emissive.g == doctest::Approx(5.0f));
	CHECK_FALSE(model->Materials[0].DoubleSided);

	// A line-list primitive is skipped with a warning; the mesh survives with no primitives.
	std::string lines = TriangleGltf("", R"("POSITION":0)", R"(,"mode":1)");
	auto lineModel = Import(lines, &error);
	REQUIRE_MESSAGE(lineModel.has_value(), error);
	CHECK(lineModel->Meshes[0].Primitives.empty());
	REQUIRE(lineModel->Warnings.size() == 1);
	CHECK(lineModel->Warnings[0].find("not a triangle list") != std::string::npos);
}

TEST_CASE("glTF: bad input is rejected with a useful error")
{
	CHECK(Fails("", "no data"));
	CHECK(Fails("not a gltf file", "cannot parse"));
	CHECK(Fails(R"({"asset":{"version":"2.0"},)", "cannot parse"));
	CHECK(Fails(R"({"asset":{"version":"1.0"}})", "legacy"));
	CHECK(Fails(TriangleGltf(R"(,"extensionsRequired":["KHR_draco_mesh_compression"])"), "unsupported required extension KHR_draco_mesh_compression"));
	CHECK(Fails(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":4,"uri":"missing.bin"}]})", "buffers"));

	// Index outside the vertex range is caught by validation.
	std::string error;
	CHECK_FALSE(Import(TriangleGltf("", R"("POSITION":0,"NORMAL":1)", R"(,"indices":99)"), &error).has_value());
	CHECK_FALSE(error.empty());
}

TEST_CASE("glTF: unsafe or unreadable image URIs become placeholders with warnings")
{
	std::string json = TriangleGltf();
	json.insert(json.rfind('}'), R"(,"images":[{"uri":"../secret.png"},{"uri":"/etc/passwd"},{"uri":"missing.png"},{"uri":"data:image/png;base64,AAAA"}],"textures":[{"source":0}])");
	std::string error;
	auto model = Import(json, &error);
	REQUIRE_MESSAGE(model.has_value(), error);
	REQUIRE(model->Textures.size() == 4);
	CHECK(model->Warnings.size() == 4);
	CHECK(model->Warnings[0].find("safe relative path") != std::string::npos);
	CHECK(model->Warnings[1].find("safe relative path") != std::string::npos);
	CHECK(model->Warnings[2].find("could not read image file") != std::string::npos);
	CHECK(model->Warnings[3].find("could not decode") != std::string::npos);
	for (const ImportedTexture& texture : model->Textures)
	{
		CHECK(texture.Image.Width == 1);
		CHECK(texture.Image.PixelAt(0, 0)[0] == 255);
	}
}

TEST_CASE("glTF: ImportFile resolves external buffers and images relative to the file")
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "lumen_gltf_test";
	fs::create_directories(dir / "textures");

	std::vector<uint8_t> buffer = TriangleBuffer();
	{
		std::ofstream bin(dir / "tri.bin", std::ios::binary);
		bin.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
	}
	std::vector<uint8_t> png = ImageIO::EncodePNG(SolidImage(3, 3, 1, 2, 3));
	{
		std::ofstream image(dir / "textures" / "my tex.png", std::ios::binary);
		image.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
	}

	std::string json = TriangleGltf();
	const size_t uriStart = json.find("data:application/octet-stream;base64,");
	const size_t uriEnd = json.find('"', uriStart);
	json.replace(uriStart, uriEnd - uriStart, "tri.bin");
	json.insert(json.rfind('}'), R"(,"images":[{"uri":"textures/my%20tex.png"}],"textures":[{"source":0}])");
	{
		std::ofstream file(dir / "tri.gltf");
		file << json;
	}

	std::string error;
	auto model = GltfImporter::ImportFile((dir / "tri.gltf").string(), &error);
	fs::remove_all(dir);
	REQUIRE_MESSAGE(model.has_value(), error);
	CHECK(model->Warnings.empty());
	CHECK(model->Meshes[0].Primitives[0].Mesh.Vertices.size() == 3);
	REQUIRE(model->Textures.size() == 1);
	CHECK(model->Textures[0].Image.Width == 3);

	CHECK_FALSE(GltfImporter::ImportFile((dir / "nope.gltf").string(), &error).has_value());
	CHECK(error.find("could not read") != std::string::npos);
}
