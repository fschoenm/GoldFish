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
#ifndef  span_CONFIG_INDEX_TYPE
# define span_CONFIG_INDEX_TYPE  size_t
#endif
#include <span-lite/span.hpp>
#endif

using namespace std::literals::string_view_literals;

#ifndef GOLDFISH_HAS_STD_SPAN
#ifndef USING_GSL_SPAN_DEFINED
namespace std {
using nonstd::as_writeable_bytes;
using nonstd::dynamic_extent;
using nonstd::span;
}
#define USING_GSL_SPAN_DEFINED
#endif
#endif

namespace goldfish
{

	template <class ElementType, std::ptrdiff_t Extent, class = std::enable_if_t<!std::is_const<ElementType>::value>>
	std::span<byte, std::dynamic_extent>
	as_writeable_bytes(std::span<ElementType, Extent> s) noexcept
	{
		return {reinterpret_cast<byte*>(s.data()), s.size_bytes()};
	}

	template <class ElementType, std::ptrdiff_t Extent>
	std::span<const byte, std::dynamic_extent>
	as_bytes(std::span<ElementType, Extent> s) noexcept
	{
		return {reinterpret_cast<const byte*>(s.data()), s.size_bytes()};
	}

	template <class ElementType>
	std::span<const byte>
	as_bytes(const std::vector<ElementType>& s) noexcept
	{
		return {reinterpret_cast<const byte*>(s.data()), s.size()};
	}

	inline 
	std::span<const byte, std::dynamic_extent>
	as_bytes(std::string_view s) noexcept {
		return {reinterpret_cast<const byte*>(s.data()), s.size()};
	}

	inline size_t copy_span(std::span<const byte> from, std::span<byte> to)
	{
		assert(from.size() == to.size());
		std::copy(from.begin(), from.end(),to.begin());
		return from.size();
	}

	template <class T> std::span<const byte> constexpr to_buffer(const T& t) { return{ reinterpret_cast<const byte*>(&t), reinterpret_cast<const byte*>(&t + 1) }; }
	template <class T> std::span<byte> constexpr to_buffer(T& t) { return{ reinterpret_cast<byte*>(&t), reinterpret_cast<byte*>(&t + 1) }; }
	
	template <class T> 
	std::span<T> remove_front(std::span<T>& in, size_t n) {
		assert(n <= in.size());
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
	template <size_t N> constexpr std::span<const byte> string_literal_to_non_null_terminated_buffer(const char(&text)[N])
	{
		static_assert(N > 0, "expect null terminated strings");
		assert(text[N - 1] == 0);
		return{ reinterpret_cast<const byte*>(text), N - 1 };
	}
}
