//
// Created by Dominic Kloecker on 29/08/2026.
//

#ifndef DSL_DSLU_FIXED_STRING_H_
#define DSL_DSLU_FIXED_STRING_H_

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <string_view>

namespace dsl {

template<std::size_t N>
struct FixedString {
	char buf[N + 1]{};

	constexpr FixedString() noexcept = default;

	constexpr FixedString(char const (&s)[N + 1]) noexcept {
		for (std::size_t i = 0; i < N; ++i) {
			buf[i] = s[i];
		}
		buf[N] = '\0';
	}

	[[nodiscard]] static constexpr std::size_t size() noexcept {
		return N;
	}

	[[nodiscard]] constexpr std::size_t length() const noexcept {
		return N;
	}

	[[nodiscard]] constexpr bool empty() const noexcept {
		return N == 0;
	}

	[[nodiscard]] constexpr std::string_view view() const noexcept {
		return std::string_view{buf, N};
	}

	[[nodiscard]] explicit constexpr operator std::string_view() const noexcept {
		return view();
	}

	[[nodiscard]] constexpr const char* data() const noexcept {
		return buf;
	}

	[[nodiscard]] constexpr const char* c_str() const noexcept {
		return buf;
	}

	[[nodiscard]] constexpr char operator[](std::size_t i) const noexcept {
		return buf[i];
	}

	[[nodiscard]] constexpr char& operator[](std::size_t i) noexcept {
		return buf[i];
	}

	[[nodiscard]] constexpr const char* begin() const noexcept {
		return buf;
	}

	[[nodiscard]] constexpr const char* end() const noexcept {
		return buf + N;
	}

	[[nodiscard]] constexpr char* begin() noexcept {
		return buf;
	}

	[[nodiscard]] constexpr char* end() noexcept {
		return buf + N;
	}

	template<std::size_t M>
	constexpr auto operator<=>(const FixedString<M>& other) const noexcept {
		return view() <=> other.view();
	}

	template<std::size_t M>
	constexpr bool operator==(const FixedString<M>& other) const noexcept {
		return view() == other.view();
	}

	constexpr auto operator<=>(const std::string_view other) const noexcept {
		return view() <=> other;
	}

	constexpr bool operator==(const std::string_view other) const noexcept {
		return view() == other;
	}

	constexpr auto operator<=>(const char* other) const noexcept {
		return view() <=> std::string_view{other};
	}

	constexpr bool operator==(const char* other) const noexcept {
		return view() == std::string_view{other};
	}

	friend std::ostream& operator<<(std::ostream& os, const FixedString& fs) {
		return os << fs.view();
	}
};

template<std::size_t N>
FixedString(char const (&)[N]) -> FixedString<N == 0 ? 0 : N - 1>;
}

#endif  // DSL_DSLU_FIXED_STRING_H_
