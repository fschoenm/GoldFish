#pragma once

#if !defined __GLIBCXX__ && !defined _WIN32
#define GOLDFISH_HAS_STD_SPAN
#endif

#include <array>
#include <assert.h>
#include "common.h"
#include <iterator>
#include <vector>

#ifdef GOLDFISH_HAS_STD_SPAN
#include <span>
#else
#include <gsl/span>
#endif

using namespace std::literals::string_view_literals;

#ifndef GOLDFISH_HAS_STD_SPAN
namespace std {
template <class ElementT, std::ptrdiff_t Extent = gsl::dynamic_extent> using span = gsl::span<ElementT, Extent>;
}
#endif

namespace goldfish
{
	using const_buffer_ref = std::span<const byte>;
	using buffer_ref = std::span<byte>;

	template <class T, class U>
	constexpr T narrow_cast(U&& u) noexcept
	{
		return static_cast<T>(std::forward<U>(u));
	}

	inline size_t copy_span(std::span<const byte> from, std::span<byte> to)
	{
		assert(from.size() == to.size());
		std::copy(from.begin(), from.end(),to.begin());
		return from.size();
	}
	
	template <class T> const_buffer_ref constexpr to_buffer(const T& t) { return{ reinterpret_cast<const byte*>(&t), reinterpret_cast<const byte*>(&t + 1) }; }
	template <class T> buffer_ref constexpr to_buffer(T& t) { return{ reinterpret_cast<byte*>(&t), reinterpret_cast<byte*>(&t + 1) }; }
	
	template <class T> 
	std::span<T> remove_front(std::span<T>& in, size_t n) {
		assert(narrow_cast<std::ptrdiff_t>(n) <= in.size());
		auto ret = in.first(n);
		in = in.subspan(n);
		return ret;
	}
	template <class T>
	T& pop_front(std::span<T>& in) { 
		assert(!in.empty()); 
		T& ret = in[0];
		in = in.subspan(1);
		return ret;
	}
	template <size_t N> constexpr const_buffer_ref string_literal_to_non_null_terminated_buffer(const char(&text)[N])
	{
		static_assert(N > 0, "expect null terminated strings");
		assert(text[N - 1] == 0);
		return{ reinterpret_cast<const byte*>(text), N - 1 };
	}
}
