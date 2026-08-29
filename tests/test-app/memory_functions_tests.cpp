#include "compiler/compiler_warnings_control.h"

#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "utility_functions/memory_functions.h"

#include <string_view>

using namespace std::string_view_literals;

namespace {

constexpr size_t notFound = std::string_view::npos;

// The offset memfind reports the match at, or notFound when it reports none.
[[nodiscard]] size_t findOffset(const std::string_view haystack, const std::string_view needle) noexcept
{
	const void* const match = ::memfind(haystack.data(), haystack.size(), needle.data(), needle.size());
	return match ? static_cast<size_t>(static_cast<const char*>(match) - haystack.data()) : notFound;
}

} // namespace

TEST_CASE("memfind rejects a search that cannot succeed", "[memfind]")
{
	CHECK(findOffset("haystack"sv, ""sv) == notFound);
	CHECK(findOffset(""sv, "needle"sv) == notFound);
	CHECK(findOffset(""sv, ""sv) == notFound);
	CHECK(findOffset("short"sv, "longer needle"sv) == notFound);
}

TEST_CASE("memfind locates a needle wherever it sits", "[memfind]")
{
	constexpr auto haystack = "the needle is here"sv;

	CHECK(findOffset(haystack, "the"sv) == 0);
	CHECK(findOffset(haystack, "needle"sv) == 4);
	CHECK(findOffset(haystack, "here"sv) == 14);
}

TEST_CASE("memfind matches a needle that is the entire haystack", "[memfind]")
{
	CHECK(findOffset("needle"sv, "needle"sv) == 0);
	CHECK(findOffset("x"sv, "x"sv) == 0);
	CHECK(findOffset("needle"sv, "needlf"sv) == notFound);
}

TEST_CASE("memfind reaches the last offset a needle can start at", "[memfind]")
{
	// In each of these an earlier byte matches the needle's first byte and is then rejected, so arriving at the
	// final candidate depends on the scan not stopping one offset short of it.
	CHECK(findOffset("aab"sv, "ab"sv) == 1);
	CHECK(findOffset("xaxab"sv, "ab"sv) == 3);
	CHECK(findOffset("aaab"sv, "aab"sv) == 1);
}

TEST_CASE("memfind returns the first of several matches", "[memfind]")
{
	CHECK(findOffset("abcabc"sv, "abc"sv) == 0);
	CHECK(findOffset("xabcabc"sv, "abc"sv) == 1);
}

TEST_CASE("memfind reports no match when the needle is absent", "[memfind]")
{
	CHECK(findOffset("haystack"sv, "needle"sv) == notFound);
	// The needle's first byte recurs throughout but never begins a whole match.
	CHECK(findOffset("aaaaa"sv, "ab"sv) == notFound);
	// A prefix of the needle sits at the end of the haystack, leaving no room for the rest of it.
	CHECK(findOffset("xxxab"sv, "abc"sv) == notFound);
}

TEST_CASE("memfind handles a single-byte needle", "[memfind]")
{
	CHECK(findOffset("abc"sv, "a"sv) == 0);
	CHECK(findOffset("abc"sv, "c"sv) == 2);
	CHECK(findOffset("abc"sv, "d"sv) == notFound);
}

TEST_CASE("memfind searches bytes, so an embedded NUL is ordinary content", "[memfind]")
{
	constexpr auto haystack = "a\0b\0c"sv;
	REQUIRE(haystack.size() == 5); // A truncated view would silently make the rest of this case meaningless

	CHECK(findOffset(haystack, "\0b"sv) == 1);
	CHECK(findOffset(haystack, "b\0c"sv) == 2);
	CHECK(findOffset(haystack, "\0c"sv) == 3); // Also the last offset a match can start at
	CHECK(findOffset("abc"sv, "\0"sv) == notFound);
}
