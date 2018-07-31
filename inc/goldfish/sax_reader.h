#pragma once

#include "tags.h"
#include "stream.h"
#include <optional>
#include "base64_stream.h"
#include "buffered_stream.h"
#include <type_traits>
#include <variant>

namespace goldfish
{
	namespace json 
	{
		struct ill_formatted_json_data : ill_formatted { using ill_formatted::ill_formatted; };
		template <class Stream> std::variant<uint64_t, int64_t, double> read_number(Stream& s, char first);
	}
	struct integer_overflow_while_casting : exception { integer_overflow_while_casting() : exception("Integer too large") {} };

	template <class DocTraitsT>
	class document_impl
	{
	public:
		using tag = tags::document;
		template <class tag> using type_with_tag_t = typename DocTraitsT::template type_with_tag_t<tag>;
		static constexpr bool does_json_conversions = DocTraitsT::does_json_conversions;

		template <class... Args> document_impl(Args&&... args)
			: m_data(std::forward<Args>(args)...)
		{}

		template <class Lambda> decltype(auto) visit(Lambda&& l) &
		{
			assert(!m_moved_from);
			return std::visit(l, m_data);
		}
		template <class Lambda> decltype(auto) visit(Lambda&& l) &&
		{
			assert(!m_moved_from);
			return std::visit(l, std::move(m_data));
		}
		type_with_tag_t<tags::string> as_string()
		{
			assert(!m_moved_from);
			#ifndef NDEBUG
			m_moved_from = true;
			#endif
			return std::get<type_with_tag_t<tags::string>>(std::move(m_data));
		}
		auto as_binary()
		{
			if constexpr(does_json_conversions) {
				return stream::decode_base64(as_string());
			}
			else {
				assert(!m_moved_from);
				#ifndef NDEBUG
				m_moved_from = true;
				#endif
				return std::get<type_with_tag_t<tags::binary>>(std::move(m_data));
			}
		}
		type_with_tag_t<tags::array> as_array()
		{
			assert(!m_moved_from);
			#ifndef NDEBUG
			m_moved_from = true;
			#endif
			return std::get<type_with_tag_t<tags::array>>(std::move(m_data));
		}
		type_with_tag_t<tags::map> as_map()
		{
			assert(!m_moved_from);
			#ifndef NDEBUG
			m_moved_from = true;
			#endif
			return std::get<type_with_tag_t<tags::map>>(std::move(m_data));
		}
		template <class... Args> auto as_object(Args&&... args) { return as_map(std::forward<Args>(args)...); }

		// Floating point can be converted from an int
		double as_double()
		{
			assert(!m_moved_from);
			double result = std::visit([&](auto&& arg) -> double {
				using Ti = std::decay_t<decltype(arg)>;
				auto& x = const_cast<Ti&>(arg);
				if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::floating_point>) { return x; }
				else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::unsigned_int>) { return static_cast<double>(x); }
				else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::signed_int>) { return static_cast<double>(x); }
				else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::string>)
				{
 					// We need to buffer the stream because read_number uses "peek<char>"
 					auto s = stream::buffer<1>(stream::ref(x));
 					try
 					{
						double result = std::visit([](auto&& x) -> double { return static_cast<double>(x); }, json::read_number(s, stream::read<char>(s)));
 						if (stream::seek(s, 1) != 0)
 							throw std::bad_variant_access{};
 						return result;
					}
					catch (const json::ill_formatted_json_data&)
					{
						throw std::bad_variant_access{};
					}
					catch (const stream::unexpected_end_of_stream&)
					{
						throw std::bad_variant_access{};
					}
 				}
				else
				{
					throw std::bad_variant_access{};
				}
			}, m_data);
			#ifndef NDEBUG
			m_moved_from = true;
			#endif
			return result;
		}

		// Unsigned ints can be converted from signed ints
		uint64_t as_uint64()
		{
			assert(!m_moved_from);
			uint64_t result = std::visit([](auto&& x) -> uint64_t {
				if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::unsigned_int>) { return x; }
				else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::signed_int>) { return cast_signed_to_unsigned(x); }
				else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::floating_point>) { return cast_double_to_unsigned(x); }
				else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::string>)
				{
					// We need to buffer the stream because read_number uses "peek<char>"
					auto s = stream::buffer<1>(stream::ref(x));
					try
					{
						auto num = json::read_number(s, stream::read<char>(s));
						uint64_t result = 0;
						if (std::holds_alternative<uint64_t>(num)) { result = std::get<int64_t>(num); }
						else if (std::holds_alternative<int64_t>(num)) { result = cast_signed_to_unsigned(std::get<int64_t>(num)); }
						else if (std::holds_alternative<double>(num)) { result = cast_double_to_unsigned(std::get<double>(num)); }
						if (stream::seek(s, 1) != 0)
							throw std::bad_variant_access{};
						return result;
					}
					catch (const json::ill_formatted_json_data&)
					{
						throw std::bad_variant_access{};
					}
					catch (const stream::unexpected_end_of_stream&)
					{
						throw std::bad_variant_access{};
					}
				}
				else
				{
					throw std::bad_variant_access{};
				}
			}, m_data);
			#ifndef NDEBUG
			m_moved_from = true;
			#endif
			return result;
		}
		uint32_t as_uint32()
		{
			auto x = as_uint64();
			if (x > std::numeric_limits<uint32_t>::max())
				throw integer_overflow_while_casting{};
			return static_cast<uint32_t>(x);
		}
		uint16_t as_uint16()
		{
			auto x = as_uint64();
			if (x > std::numeric_limits<uint16_t>::max())
				throw integer_overflow_while_casting{};
			return static_cast<uint16_t>(x);
		}
		uint8_t as_uint8()
		{
			auto x = as_uint64();
			if (x > std::numeric_limits<uint8_t>::max())
				throw integer_overflow_while_casting{};
			return static_cast<uint8_t>(x);
		}

		// Signed ints can be converted from unsigned ints
		int64_t as_int64()
		{
			assert(!m_moved_from);
			int64_t result = std::visit([](auto&& x) -> int64_t {
				if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::signed_int>) { return x; }
				else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::unsigned_int>) { return cast_unsigned_to_signed(x); }
				else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::floating_point>) { return cast_double_to_signed(x); }
				else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::string>)
				{
					// We need to buffer the stream because read_number uses "peek<char>"
					auto s = stream::buffer<1>(stream::ref(x));
					try
					{
						auto num = json::read_number(s, stream::read<char>(s));
						int64_t result = 0;
						if (std::holds_alternative<int64_t>(num)) { result = std::get<int64_t>(num); }
						else if (std::holds_alternative<uint64_t>(num)) { result = cast_unsigned_to_signed(std::get<uint64_t>(num)); }
						else if (std::holds_alternative<double>(num)) { result = cast_double_to_signed(std::get<double>(num)); }
						if (stream::seek(s, 1) != 0)
							throw std::bad_variant_access{};
						return result;
					}
					catch (const json::ill_formatted_json_data&)
					{
						throw std::bad_variant_access{};
					}
					catch (const stream::unexpected_end_of_stream&)
					{
					 	throw std::bad_variant_access{};
					}
				}
				else
				{
					throw std::bad_variant_access{};
				}
			}, m_data);
			#ifndef NDEBUG
			m_moved_from = true;
			#endif
			return result;
		}
		int32_t as_int32()
		{
			auto x = as_int64();
			if (x < std::numeric_limits<int32_t>::min() || x > std::numeric_limits<int32_t>::max())
				throw integer_overflow_while_casting{};
			return static_cast<int32_t>(x);
		}
		int16_t as_int16()
		{
			auto x = as_int64();
			if (x < std::numeric_limits<int16_t>::min() || x > std::numeric_limits<int16_t>::max())
				throw integer_overflow_while_casting{};
			return static_cast<int16_t>(x);
		}
		int8_t as_int8()
		{
			auto x = as_int64();
			if (x < std::numeric_limits<int8_t>::min() || x > std::numeric_limits<int8_t>::max())
				throw integer_overflow_while_casting{};
			return static_cast<int8_t>(x);
		}

		bool as_bool()
		{
			assert(!m_moved_from);
			bool result = std::visit([](auto&& x) -> bool{
				if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::boolean>) { return x; }
				else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::string>)
				{
					byte buffer[6];
					auto cb = read_full_buffer(x, buffer);
					if (cb == 4 && std::equal(buffer, buffer + 4, "true"))
						return true;
					else if (cb == 5 && std::equal(buffer, buffer + 5, "false"))
						return false;
					else
						throw std::bad_variant_access{};
				}
				else
				{
					throw std::bad_variant_access{};
				}
			}, m_data);
			#ifndef NDEBUG
			m_moved_from = true;
			#endif
			return result;
		}
		bool is_undefined_or_null() const { return std::holds_alternative<undefined>(m_data) || std::holds_alternative<std::nullptr_t>(m_data); }
		bool is_null() const { return std::holds_alternative<std::nullptr_t>(m_data); }

		template <class tag> bool is_exactly() { return std::holds_alternative<type_with_tag_t<tag>>(m_data); }

	private:
		static uint64_t cast_signed_to_unsigned(int64_t x)
		{
			if (x < 0)
				throw integer_overflow_while_casting{};
			return static_cast<uint64_t>(x);
		}
		static int64_t cast_unsigned_to_signed(uint64_t x)
		{
			if (x > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
				throw integer_overflow_while_casting{};
			return static_cast<int64_t>(x);
		}
		static uint64_t cast_double_to_unsigned(double x)
		{
			if (x == static_cast<uint64_t>(x))
				return static_cast<uint64_t>(x);
			else
				throw integer_overflow_while_casting{};
		}
		static int64_t cast_double_to_signed(double x)
		{
			if (x == static_cast<int64_t>(x))
				return static_cast<int64_t>(x);
			else
				throw integer_overflow_while_casting{};
		}

		#ifndef NDEBUG
		bool m_moved_from = false;
		#endif
		typename DocTraitsT::VariantT m_data;
	};

	template <class Document> std::enable_if_t<tags::has_tag<std::decay_t<Document>, tags::document>::value, void> seek_to_end(Document&& d)
	{
		d.visit([&](auto&& x) {
			if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::binary>) { stream::seek(x, std::numeric_limits<uint64_t>::max()); }
			else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::string>) { stream::seek(x, std::numeric_limits<uint64_t>::max()); }
			else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::array>)
			{
				while (auto d = x.read())
					seek_to_end(*d);
			}
			else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::map>)
			{
				while (auto d = x.read_key())
				{
					seek_to_end(*d);
					seek_to_end(x.read_value());
				}
			}
		});
	}
	template <class type> std::enable_if_t<tags::has_tag<std::decay_t<type>, tags::undefined>::value, void> seek_to_end(type&&) {}
	template <class type> std::enable_if_t<tags::has_tag<std::decay_t<type>, tags::floating_point>::value, void> seek_to_end(type&&) {}
	template <class type> std::enable_if_t<tags::has_tag<std::decay_t<type>, tags::unsigned_int>::value, void> seek_to_end(type&&) {}
	template <class type> std::enable_if_t<tags::has_tag<std::decay_t<type>, tags::signed_int>::value, void> seek_to_end(type&&) {}
	template <class type> std::enable_if_t<tags::has_tag<std::decay_t<type>, tags::boolean>::value, void> seek_to_end(type&&) {}
	template <class type> std::enable_if_t<tags::has_tag<std::decay_t<type>, tags::null>::value, void> seek_to_end(type&&) {}
	template <class type> std::enable_if_t<tags::has_tag<std::decay_t<type>, tags::binary>::value, void> seek_to_end(type&& x)
	{
		stream::seek(x, std::numeric_limits<uint64_t>::max());
	}
	template <class type> std::enable_if_t<tags::has_tag<std::decay_t<type>, tags::string>::value, void> seek_to_end(type&& x)
	{
		stream::seek(x, std::numeric_limits<uint64_t>::max());
	}
	template <class type> std::enable_if_t<tags::has_tag<std::decay_t<type>, tags::array>::value, void> seek_to_end(type&& x)
	{
		while (auto d = x.read())
			seek_to_end(*d);
	}
	template <class type> std::enable_if_t<tags::has_tag<std::decay_t<type>, tags::map>::value, void> seek_to_end(type&& x)
	{
		while (auto d = x.read_key())
		{
			seek_to_end(*d);
			seek_to_end(x.read_value());
		}
	}
}