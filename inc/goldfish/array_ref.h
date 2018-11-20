#pragma once

#include <array>
#include <assert.h>
#include "common.h"
#include <iterator>
#include <vector>
#include <gsl/span>

using namespace std::literals::string_view_literals;

namespace goldfish
{
	using const_buffer_ref = gsl::span<const byte>;
	using buffer_ref = gsl::span<byte>;

	size_t copy_span(gsl::span<const byte> from, gsl::span<byte> to)
	{
		assert(from.size() == to.size());
		std::copy(from.begin(), from.end(), make_unchecked_array_iterator(to.begin()));
		return from.size();
	}
	
	template <class T> const_buffer_ref constexpr to_buffer(const T& t) { return{ reinterpret_cast<const byte*>(&t), reinterpret_cast<const byte*>(&t + 1) }; }
	template <class T> buffer_ref constexpr to_buffer(T& t) { return{ reinterpret_cast<byte*>(&t), reinterpret_cast<byte*>(&t + 1) }; }
	
	template <class T> 
	gsl::span<T> remove_front(gsl::span<T>& in, size_t n) {
		assert(n <= in.size());
		auto ret = in.subspan(0, n);
		in = in.subspan(n);
		return ret;
	}
	template <class T>
	T& pop_front(gsl::span<T>& in) { 
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
