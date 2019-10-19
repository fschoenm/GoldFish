#pragma once

#if !defined __GLIBCXX__ && !defined _WIN32
#define GOLDFISH_HAS_STD_SPAN
#endif

#ifdef _WIN32
#define GOLDFISH_HAS_DOUBLE_FROM_CHARS
#endif

#include "common.h"
#include <array>
#include <bit>
#include <cassert>
#include <cstring>
#include <iterator>
#include <string_view>
#include <vector>

#ifdef GOLDFISH_HAS_STD_SPAN
#include <span>
#else
#ifndef  span_CONFIG_INDEX_TYPE
# define span_CONFIG_INDEX_TYPE  size_t
#endif
#include <span-lite/span.hpp>
#endif

using namespace std::literals; //NOLINT(google-global-names-in-headers)

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

namespace std { // NOLINT(cert-dcl58-cpp)
	template <class To, class From>
	typename std::enable_if_t<(sizeof(To) == sizeof(From)) && std::is_trivially_copyable<From>::value && std::is_trivial<To>::value, To>
	constexpr
	bit_cast(const From &src) noexcept
	{
#if !defined __GLIBCXX__ && !defined _WIN32
		return __builtin_bit_cast(To, src);
#else
		To dst;
    	std::memcpy(&dst, &src, sizeof(To));
    	return dst;
#endif
	}
} // namespace std

namespace goldfish
{
#ifdef GOLDFISH_HAS_STD_SPAN
	template <class ElementType, size_t Extent, class = std::enable_if_t<!std::is_const<ElementType>::value>>
#else
	template <class ElementType, std::ptrdiff_t Extent, class = std::enable_if_t<!std::is_const<ElementType>::value>>
#endif
	std::span<byte, std::dynamic_extent>
	as_writeable_bytes(std::span<ElementType, Extent> s) noexcept
	{
		return {reinterpret_cast<byte*>(s.data()), s.size_bytes()}; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	}

#ifdef GOLDFISH_HAS_STD_SPAN
	template <class ElementType, size_t Extent>
#else
	template <class ElementType, std::ptrdiff_t Extent>
#endif
	std::span<const byte, std::dynamic_extent>
	as_bytes(std::span<ElementType, Extent> s) noexcept
	{
		return {reinterpret_cast<const byte*>(s.data()), s.size_bytes()}; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	}

	template <class ElementType>
	std::span<const byte>
	as_bytes(const std::vector<ElementType>& s) noexcept
	{
		return {reinterpret_cast<const byte*>(s.data()), s.size()}; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	}

	inline 
	std::span<const byte, std::dynamic_extent>
	as_bytes(std::string_view s) noexcept {
		return { reinterpret_cast<const byte*>(s.data()), s.size() }; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	}

	template <class DstT>
	std::enable_if_t<sizeof(DstT) == 1, DstT>
	as(std::span<const byte> s) noexcept {
		assert(s.size() >= sizeof(DstT));
		return static_cast<DstT>(s[0]);
	}

	template <class DstT>
	std::enable_if_t<sizeof(DstT) != 1, DstT>
	as(std::span<const byte> s) noexcept {
		assert(s.size() >= sizeof(DstT));
		DstT dst;
    	std::memcpy(&dst, s.data(), sizeof(DstT));
    	return dst;
	}

	template <>
	std::string_view inline as<std::string_view>(std::span<const byte> s) noexcept {
		return { reinterpret_cast<const char*>(s.data()), s.size() }; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	}

	inline size_t copy_span(std::span<const byte> from, std::span<byte> to)
	{
		assert(from.size() == to.size());
		std::copy(from.begin(), from.end(),to.begin());
		return from.size();
	}

	template <class T> std::span<const byte> constexpr to_buffer(const T& t) { return{ reinterpret_cast<const byte*>(&t), reinterpret_cast<const byte*>(&t + 1) }; } // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	template <class T> std::span<byte> constexpr to_buffer(T& t) { return{ reinterpret_cast<byte*>(&t), reinterpret_cast<byte*>(&t + 1) }; } // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	
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
} // namespace goldfish
