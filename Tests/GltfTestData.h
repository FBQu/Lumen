#pragma once

// Shared builders for glTF test data: a single triangle with embedded (base64) buffers.

#include "Lumen/Renderer/RenderDevice.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace GltfTestData {

	using Lumen::ImageData;

	inline std::string Base64(const std::vector<uint8_t>& bytes)
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
	inline void Append(std::vector<uint8_t>& bytes, std::initializer_list<T> values)
	{
		for (T value : values)
		{
			const size_t at = bytes.size();
			bytes.resize(at + sizeof(T));
			std::memcpy(&bytes[at], &value, sizeof(T));
		}
	}

	inline void Pad4(std::vector<uint8_t>& bytes) { while (bytes.size() % 4) bytes.push_back(0); }

	// One counter-clockwise triangle in the XY plane facing +Z: buffer layout positions | normals | uvs | indices(u16).
	inline std::vector<uint8_t> TriangleBuffer()
	{
		std::vector<uint8_t> b;
		Append<float>(b, { 0, 0, 0,  1, 0, 0,  0, 1, 0 });
		Append<float>(b, { 0, 0, 1,  0, 0, 1,  0, 0, 1 });
		Append<float>(b, { 0, 1,  1, 1,  0, 0 });
		Append<uint16_t>(b, { 0, 1, 2 });
		Pad4(b);
		return b;
	}

	inline const char* kTriangleViews = R"(
		"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},
		               {"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],
		"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
		             {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
		             {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
		             {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],)";

	inline std::string TriangleGltf(const std::string& extra = "", const std::string& attributes = R"("POSITION":0,"NORMAL":1,"TEXCOORD_0":2)",
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

	inline ImageData SolidImage(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b)
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


}
