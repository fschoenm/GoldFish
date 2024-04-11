#pragma once

#include <cstdint>
#include <iterator>

#ifdef _MSC_VER
#include <stdlib.h>
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#elif __APPLE__
#include <libkern/OSByteOrder.h>
#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)
#else
#include <byteswap.h>
#endif

namespace goldfish
{
	using byte = uint8_t;

	inline uint16_t from_big_endian(uint16_t x) { return bswap_16(x); }
	inline uint32_t from_big_endian(uint32_t x) { return bswap_32(x); }
	inline uint64_t from_big_endian(uint64_t x) { return bswap_64(x); }

	inline uint16_t to_big_endian(uint16_t x) { return from_big_endian(x); }
	inline uint32_t to_big_endian(uint32_t x) { return from_big_endian(x); }
	inline uint64_t to_big_endian(uint64_t x) { return from_big_endian(x); }

	// All goldfish exceptions subclass this exception
	class exception : public std::exception
	{
	public:
		exception(const char* w)
			: m_what(w)
		{}
		const char* what() const noexcept override { return m_what; }
	private:
		const char* m_what;
	};

	// Base class for all formatting errors that happen while parsing a document
	struct ill_formatted : exception { using exception::exception; };

	// Specifically for IO errors, thrown by file_reader/writer and istream_reader/writer
	struct io_exception : exception { using exception::exception; };
	struct io_exception_with_error_code : io_exception
	{
		io_exception_with_error_code(const char* w, int _error_code)
			: io_exception(w), error_code(_error_code)
		{}

		int error_code;
	};

	// CBOR supports an "undefined" type, which is represented at runtime by the C++ type below
	struct undefined {};
	inline bool operator == (const undefined&, const undefined&) { return true; }
	inline bool operator < (const undefined&, const undefined&) { return false; }

	template <size_t...> struct largest {};
	template <size_t x> struct largest<x> { enum { value = x }; };
	template <size_t x, size_t y> struct largest<x, y> { enum { value = x > y ? x : y }; };
	template <size_t x, size_t y, size_t... z> struct largest<x, y, z...> { enum { value = largest<x, largest<y, z...>::value>::value }; };

	template <class...> struct conjunction {};
	template <> struct conjunction<> { enum { value = true }; };
	template <class Head, class... Tail> struct conjunction<Head, Tail...> { enum { value = Head::value && conjunction<Tail...>::value }; };

	template <class...> struct disjunction {};
	template <> struct disjunction<> { enum { value = false }; };
	template <class Head, class... Tail> struct disjunction<Head, Tail...> { enum { value = Head::value || disjunction<Tail...>::value }; };

	// Used for all cases where we need to read from a stream in chunks (default implementation of seek, stream copy, etc...)
	static const int typical_buffer_length = 8 * 1024;
}
