#include <doctest/doctest.h>

#include "Lumen/Core/Base64.h"

#include <string>

using namespace Lumen;

namespace {
	std::vector<uint8_t> Bytes(const std::string& text) { return { text.begin(), text.end() }; }
}

TEST_CASE("Base64 encodes the RFC 4648 test vectors")
{
	CHECK(Base64::Encode(Bytes("")) == "");
	CHECK(Base64::Encode(Bytes("f")) == "Zg==");
	CHECK(Base64::Encode(Bytes("fo")) == "Zm8=");
	CHECK(Base64::Encode(Bytes("foo")) == "Zm9v");
	CHECK(Base64::Encode(Bytes("foob")) == "Zm9vYg==");
	CHECK(Base64::Encode(Bytes("fooba")) == "Zm9vYmE=");
	CHECK(Base64::Encode(Bytes("foobar")) == "Zm9vYmFy");
}

TEST_CASE("Base64 round-trips every byte value and every length remainder")
{
	for (size_t length : { size_t(0), size_t(1), size_t(2), size_t(3), size_t(255), size_t(256), size_t(1000) })
	{
		std::vector<uint8_t> data(length);
		for (size_t i = 0; i < length; i++)
			data[i] = static_cast<uint8_t>(i * 7 + 3);
		auto decoded = Base64::Decode(Base64::Encode(data));
		REQUIRE(decoded.has_value());
		CHECK(*decoded == data);
	}
	std::vector<uint8_t> all(256);
	for (int i = 0; i < 256; i++)
		all[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
	CHECK(*Base64::Decode(Base64::Encode(all)) == all);
}

TEST_CASE("Base64 decoder ignores whitespace and rejects malformed input")
{
	CHECK(*Base64::Decode("Zm9v\nYmFy\r\n") == Bytes("foobar"));
	CHECK(*Base64::Decode(" Z g = = ") == Bytes("f"));

	CHECK_FALSE(Base64::Decode("Zm9").has_value());      // wrong length
	CHECK_FALSE(Base64::Decode("Zg=").has_value());      // padding incomplete
	CHECK_FALSE(Base64::Decode("Zg===").has_value());    // too much padding
	CHECK_FALSE(Base64::Decode("Zm9v!A==").has_value()); // invalid character
	CHECK_FALSE(Base64::Decode("Zg==Zg==").has_value()); // data after padding
	CHECK_FALSE(Base64::Decode("Zm-_").has_value());     // URL-safe alphabet is not accepted here
	CHECK(Base64::Decode("")->empty());
}
