#pragma once

#include "base64_stream.h"
#include "debug_checks_reader.h"
#include "tags.h"
#include "variant.h"
#include "stream.h"
#include "optional.h"
#include "sax_reader.h"

namespace goldfish { namespace json
{
	struct ill_formatted {};

	class byte_string;
	template <class Stream> class text_string;
	template <class Stream> class array;
	template <class Stream> class map;
	template <class Stream> using document = document_impl<
		true /*does_json_conversions*/,
		bool,
		nullptr_t,
		int64_t,
		uint64_t,
		double,
		text_string<Stream>,
		byte_string,
		array<Stream>,
		map<Stream>>;
	template <class Stream> document<std::decay_t<Stream>> read_no_debug_check(Stream&& s);

	namespace details
	{
		template <class Stream> optional<char> peek_non_space(Stream& s)
		{
			for (;;)
			{
				auto c = s.peek<char>();
				if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
					return c;
				else
					stream::read<char>(s);
			}
		}
	}

	class byte_string
	{
	public:
		using tag = tags::binary;
		size_t read_buffer(buffer_ref) { std::terminate(); }
	};

	/*
	Represents a stream that ends when the end quote is found in the JSON document
	The stream provided is expected to be right after the opening quote (the quote should have
	already been read).
	*/
	template <class Stream> class text_string
	{
	public:
		text_string(Stream&& s)
			: m_stream(std::move(s))
		{}
		using tag = tags::string;

		size_t read_buffer(buffer_ref buffer)
		{
			size_t cb = 0;
			for (auto&& out : buffer)
			{
				if (auto in = read_char())
				{
					out = *in;
					cb++;
				}
				else
				{
					break;
				}
			}
			return cb;
		}

		template <class T> std::enable_if_t<sizeof(T) == 1, T> read()
		{
			auto c = x.read_char();
			if (c == nullopt)
				throw stream::unexpected_end_of_stream();
			return reinterpret_cast<T&>(c);
		}
		uint64_t seek(uint64_t x)
		{
			for (uint64_t i = 0; i < x; ++i)
			{
				if (read_char() == nullopt)
					return i;
			}
			return x;
		}
	private:
		optional<char> read_char()
		{
			if (m_cb_converted != 0)
			{
				if (m_cb_converted == 255)
					return nullopt;

				return m_converted[--m_cb_converted];
			}

			auto c = stream::read<uint8_t>(m_stream);
			if (c == '\\')
			{
				switch (stream::read<uint8_t>(m_stream))
				{
				case '"': return '"';
				case '\\': return '\\';
				case '/': return '/';
				case 'b': return '\b';
				case 'f': return '\f';
				case 'n': return '\n';
				case 'r': return '\r';
				case 't': return '\t';
				case 'u': populate_converted(read_utf32_character()); return read_char();
				default: throw ill_formatted();
				}
			}
			else if (c == '"')
			{
				m_cb_converted = 255; // Indicate we reached the end
				return nullopt;
			}
			else if (c < 0x20)
			{
				throw ill_formatted{};
			}
			else
			{
				return c;
			}
		}
		static uint8_t parse_hex(char c)
		{
			if ('0' <= c && c <= '9') return c - '0';
			else if ('a' <= c && c <= 'f') return c - 'a' + 10;
			else if ('A' <= c && c <= 'F') return c - 'A' + 10;
			else throw ill_formatted();
		}
		uint16_t read_utf16_character()
		{
			uint16_t value = 0;
			value = (value << 4) | parse_hex(stream::read<char>(m_stream));
			value = (value << 4) | parse_hex(stream::read<char>(m_stream));
			value = (value << 4) | parse_hex(stream::read<char>(m_stream));
			value = (value << 4) | parse_hex(stream::read<char>(m_stream));
			return value;
		}
		uint32_t read_utf32_character()
		{
			uint32_t a = read_utf16_character();
			if (0xD800 <= a && a <= 0xDFFF)
			{
				if (a > 0xDBFF)
					throw ill_formatted{};

				// We need a second character
				if (stream::read<char>(m_stream) != '\\') throw ill_formatted{};
				if (stream::read<char>(m_stream) != 'u') throw ill_formatted{};
				uint32_t b = read_utf16_character();
				if (b < 0xDC00 || b > 0xDFFF)
					throw ill_formatted{};

				a -= 0xD800;
				b -= 0xDC00;
				return 0x10000 | (a << 10) | b;
			}
			else
			{
				return a;
			}
		}
		void populate_converted(uint32_t codepoint)
		{
			auto get_6_bits = [&](uint32_t codepoint, int offset)
			{
				return static_cast<uint8_t>(0b10000000 | ((codepoint >> offset) & 0b111111));
			};
			
			if (codepoint <= 0x7F)
			{
				m_cb_converted = 1;
				m_converted = { static_cast<uint8_t>(codepoint), 0, 0, 0 };
			}
			else if (codepoint <= 0x7FF)
			{
				m_cb_converted = 2;
				m_converted = { get_6_bits(codepoint, 0), static_cast<uint8_t>(0b11000000 | (codepoint >> 6)), 0, 0 };
			}
			else if (codepoint <= 0xFFFF)
			{
				m_cb_converted = 3;
				m_converted = { get_6_bits(codepoint, 0), get_6_bits(codepoint, 6), static_cast<uint8_t>(0b11100000 | static_cast<uint8_t>(codepoint >> 12)), 0 };
			}
			else if (codepoint <= 0x10FFFF)
			{
				m_cb_converted = 4;
				m_converted = { get_6_bits(codepoint, 0), get_6_bits(codepoint, 6), get_6_bits(codepoint, 12), static_cast<uint8_t>(0b11110000 | static_cast<uint8_t>(codepoint >> 18)) };
			}
			else
			{
				throw ill_formatted{};
			}
		}

		// 0xFF is an invalid UTF-8 character
		// The list of characters that were converted from a \u command (that is in UTF16) are in m_converted as non 0xFF bytes
		Stream m_stream;
		std::array<uint8_t, 4> m_converted{ 0xFF, 0xFF, 0xFF, 0xFF };
		uint8_t m_cb_converted = 0;
	public:
		uint8_t padding_for_variant;
	};

	template <class Stream, char end_character> class comma_separated_reader
	{
	public:
		comma_separated_reader(Stream&& s)
			: m_stream(std::move(s))
		{}
		optional<document<stream::reader_ref_type_t<Stream>>> read_comma_separated()
		{
			switch (m_state)
			{
				case state::first:
				{
					auto c = details::peek_non_space(m_stream);
					if (c == nullopt)
						throw stream::unexpected_end_of_stream{};

					if (c == end_character)
					{
						stream::read<char>(m_stream);
						m_state = state::ended;
						return nullopt;
					}
					else
					{
						m_state = state::middle;
						return read_no_debug_check(stream::ref(m_stream));
					}
				}

				case state::middle:
				{
					auto c = details::peek_non_space(m_stream);
					if (c == nullopt)
						throw stream::unexpected_end_of_stream{};

					switch (*c)
					{
					case ',': stream::read<char>(m_stream); return read_no_debug_check(stream::ref(m_stream));
					case end_character: stream::read<char>(m_stream); m_state = state::ended; return nullopt;
					default: throw ill_formatted{};
					}
				}

				default:
					return nullopt;
			}
		}

		Stream m_stream;
		enum class state
		{
			first,
			middle,
			ended,
		} m_state = state::first;
	public:
		uint8_t padding_for_variant;
	};
	template <class Stream> class array : public comma_separated_reader<Stream, ']'>
	{
	public:
		using tag = tags::array;
		using comma_separated_reader<Stream, ']'>::comma_separated_reader;
		auto read() { return read_comma_separated(); }
	};
	template <class Stream> class map : public comma_separated_reader<Stream, '}'>
	{
	public:
		using tag = tags::map;
		using comma_separated_reader<Stream, '}'>::comma_separated_reader;

		auto read_key() { return read_comma_separated(); }
		document<stream::reader_ref_type_t<Stream>> read_value()
		{
			auto c = details::peek_non_space(m_stream);
			if (c == nullopt)
				throw stream::unexpected_end_of_stream{};
			else if (*c != ':')
				throw ill_formatted{};
			stream::read<char>(m_stream);
			return read_no_debug_check(stream::ref(m_stream));
		}
	};

	template <class Stream> text_string<std::decay_t<Stream>> read_text(Stream&& s)
	{
		if (stream::read<char>(s) != '"') std::terminate();
		return{ std::forward<Stream>(s) };
	}
	template <class Stream> array<std::decay_t<Stream>> read_array(Stream&& s)
	{
		if (stream::read<char>(s) != '[') std::terminate();
		return{ std::forward<Stream>(s) };
	}
	template <class Stream> map<std::decay_t<Stream>> read_map(Stream&& s)
	{
		if (stream::read<char>(s) != '{') std::terminate();
		return{ std::forward<Stream>(s) };
	}
	template <class Stream> bool read_true(Stream& s)
	{
		for (auto c : { 't', 'r', 'u', 'e' })
		{
			if (stream::read<uint8_t>(s) != c)
				throw ill_formatted{};
		}
		return true;
	}
	template <class Stream> bool read_false(Stream& s)
	{
		for (auto c : { 'f', 'a', 'l', 's', 'e' })
		{
			if (stream::read<uint8_t>(s) != c)
				throw ill_formatted{};
		}
		return false;
	}
	template <class Stream> std::nullptr_t read_null(Stream& s)
	{
		for (auto c : { 'n', 'u', 'l', 'l' })
		{
			if (stream::read<uint8_t>(s) != c)
				throw ill_formatted{};
		}
		return nullptr;
	}
	template <class Stream> uint64_t read_unsigned_integer(Stream& s, char first, bool allow_leading_zeroes)
	{
		if (allow_leading_zeroes)
		{
			if (first < '0' || first > '9')
				throw ill_formatted{};
		}
		else
		{
			if (first == '0')
				return 0u;

			if (first < '1' || first > '9')
				throw ill_formatted{};
		}

		uint64_t result = (first - '0');
		for (;;)
		{
			auto c = s.peek<char>();
			if (c != nullopt && '0' <= *c && *c <= '9')
			{
				if (result > (std::numeric_limits<uint64_t>::max() - (*c - '0')) / 10)
					throw integer_overflow{};
				result = (result * 10) + *c - '0';
			}
			else
				return result;
			stream::read<char>(s);
		}
	}
	template <class Stream> double read_decimals(Stream& s)
	{
		auto first = stream::read<char>(s);
		if (first < '0' || first > '9')
			throw ill_formatted{};

		double result = (first - '0') / 10.;
		double divider = 100;
		for (;;)
		{
			auto c = s.peek<char>();
			if (c != nullopt && '0' <= *c && *c <= '9')
			{
				result += (*c - '0') / divider;
				divider *= 10;
			}
			else
				return result;
			stream::read<char>(s);
		}
	}
	template <class Stream> variant<uint64_t, int64_t, double> read_number(Stream& s)
	{
		bool negative = false;
		auto first = stream::read<char>(s);
		if (first == '-')
		{
			negative = true;
			first = stream::read<char>(s);
		}

		auto integer = read_unsigned_integer(s, first, false /*allow_leading_zeroes*/);

		auto floating_point_marker = s.peek<char>();
		if (floating_point_marker != '.' && floating_point_marker != 'e' && floating_point_marker != 'E')
		{
			if (negative)
			{
				if (integer > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1)
					throw integer_overflow{};
				return -static_cast<int64_t>(integer);
			}
			else
			{
				return integer;
			}
		}

		double decimals = 0;
		if (floating_point_marker == '.')
		{
			stream::read<char>(s);
			decimals = read_decimals(s);
			floating_point_marker = s.peek<char>();
		}

		double multiplier = (negative ? -1. : 1.);
		if (floating_point_marker == 'e' || floating_point_marker == 'E')
		{
			stream::read<char>(s);
			first = stream::read<char>(s);
			bool negative_exponent = false;
			if (first == '+' || first == '-')
			{
				negative_exponent = (first == '-');
				first = stream::read<char>(s);
			}
			auto exponent_value = read_unsigned_integer(s, first, true /*allow_leading_zeroes*/);
			multiplier *= pow(negative_exponent ? .1 : 10., exponent_value);
		}

		return multiplier * ((double)integer + decimals);
	}
	template <class Stream> document<std::decay_t<Stream>> read_no_debug_check(Stream&& s)
	{
		auto optC = details::peek_non_space(s);
		if (optC == nullopt)
			throw stream::unexpected_end_of_stream{};

		switch (*optC)
		{
			case '[': return read_array(std::forward<Stream>(s));
			case '{': return read_map(std::forward<Stream>(s));
			case 't': return read_true(s);
			case 'f': return read_false(s);
			case 'n': return read_null(s);
			case '"': return read_text(std::forward<Stream>(s));
			case '-':
			case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
				return read_number(s).visit([](auto&& x) -> document<Stream> { return x; });

			default: throw ill_formatted();
		}
	}

	template <class Stream, class error_handler> auto read(Stream&& s, error_handler e)
	{
		return debug_checks::add_read_checks(read_no_debug_check(std::forward<Stream>(s)), e);
	}
	template <class Stream> auto read(Stream&& s) { return read(std::forward<Stream>(s), debug_checks::default_error_handler{}); }
}}