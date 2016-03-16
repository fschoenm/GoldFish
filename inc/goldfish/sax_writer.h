#pragma once

#include "match.h"
#include "tags.h"
#include <type_traits>

namespace goldfish
{
	template <class Stream, class Writer, class CreateWriterWithSize, class CreateWriterWithoutSize>
	auto copy_stream(Stream& s, Writer& writer, CreateWriterWithSize&& create_writer_with_size, CreateWriterWithoutSize&& create_writer_without_size)
	{
		uint8_t buffer[8 * 1024];
		auto cb = s.read_buffer(buffer);
		if (cb < sizeof(buffer))
		{
			// We read the entire stream
			auto output_stream = create_writer_with_size(cb);
			output_stream.write_buffer({ buffer, cb });
			output_stream.flush();
		}
		else
		{
			// We read only a portion of the stream
			auto output_stream = create_writer_without_size();
			output_stream.write_buffer(buffer);
			do
			{
				cb = s.read_buffer(buffer);
				output_stream.write_buffer({ buffer, cb });
			} while (cb == sizeof(buffer));
			output_stream.flush();
		}
	};

	template <class DocumentWriter, class Document>
	std::enable_if_t<tags::has_tag<std::decay_t<Document>, tags::document>::value, void> copy_sax_document(DocumentWriter&& writer, Document&& document)
	{
		document.visit(first_match(
			[&](auto&& x, tags::binary) { copy_stream(x, writer, [&](size_t cb) { return writer.start_binary(cb); }, [&] { return writer.start_binary(); }); },
			[&](auto&& x, tags::string) { copy_stream(x, writer, [&](size_t cb) { return writer.start_string(cb); }, [&] { return writer.start_string(); }); },
			[&](auto&& x, tags::array)
			{
				auto array_writer = writer.start_array();
				while (auto element = x.read())
					copy_sax_document(array_writer.append(), *element);
				array_writer.flush();
			},
			[&](auto&& x, tags::map)
			{
				auto map_writer = writer.start_map();
				while (auto key = x.read_key())
				{
					copy_sax_document(map_writer.append_key(), *key);
					copy_sax_document(map_writer.append_value(), x.read_value());
				}
				map_writer.flush();
			},
			[&](auto&& x, tags::undefined) { writer.write(x); },
			[&](auto&& x, tags::floating_point) { writer.write(x); },
			[&](auto&& x, tags::unsigned_int) { writer.write(x); },
			[&](auto&& x, tags::signed_int) { writer.write(x); },
			[&](auto&& x, tags::boolean) { writer.write(x); },
			[&](auto&& x, tags::null) { writer.write(x); }
		));
	}
}

namespace goldfish { namespace sax
{
	template <class inner> class document_writer;
	template <class inner> document_writer<std::decay_t<inner>> make_writer(inner&& writer);

	template <class inner> class array_writer
	{
	public:
		array_writer(inner&& writer)
			: m_writer(std::move(writer))
		{}

		template <class... Args> auto write(Args&&... args) { return append().write(std::forward<Args>(args)...); }
		auto start_binary(uint64_t cb) { return append().start_binary(cb); }
		auto start_binary() { return append().start_binary(); }
		auto start_string(uint64_t cb) { return append().start_string(cb); }
		auto start_string() { return append().start_string(); }
		auto start_array(uint64_t size) { return append().start_array(size); }
		auto start_array() { return append().start_array(); }
		auto start_map(uint64_t size) { return append().start_map(size); }
		auto start_map() { return append().start_map(); }

		auto append() { return make_writer(m_writer.append()); }
		void flush() { m_writer.flush(); }
	private:
		inner m_writer;
	};
	template <class inner> array_writer<std::decay_t<inner>> make_array_writer(inner&& writer) { return{ std::forward<inner>(writer) }; }

	template <class inner> class map_writer
	{
	public:
		map_writer(inner&& writer)
			: m_writer(std::move(writer))
		{}

		template <class... Args> auto write_key(Args&&... args) { return append_key().write(std::forward<Args>(args)...); }
		template <class... Args> auto write_value(Args&&... args) { return append_value().write(std::forward<Args>(args)...); }
		auto append_key() { return make_writer(m_writer.append_key()); }
		auto append_value() { return make_writer(m_writer.append_value()); }
		void flush() { m_writer.flush(); }
	private:
		inner m_writer;
	};
	template <class inner> map_writer<std::decay_t<inner>> make_map_writer(inner&& writer) { return{ std::forward<inner>(writer) }; }

	template <class inner> class document_writer
	{
	public:
		document_writer(inner&& writer)
			: m_writer(std::move(writer))
		{}

		void write(bool x)            { m_writer.write(x); }
		void write(nullptr_t x)       { m_writer.write(x); }
		void write(double x)          { m_writer.write(x); }
		void write(tags::undefined x) { m_writer.write(x); }

		void write(uint64_t x)        { m_writer.write(x); }
		void write(uint32_t x)        { m_writer.write(static_cast<uint64_t>(x)); }
		void write(uint16_t x)        { m_writer.write(static_cast<uint64_t>(x)); }
		void write(uint8_t x)         { m_writer.write(static_cast<uint64_t>(x)); }

		void write(int64_t x)         { m_writer.write(x); }
		void write(int32_t x)         { m_writer.write(static_cast<int64_t>(x)); }
		void write(int16_t x)         { m_writer.write(static_cast<int64_t>(x)); }
		void write(int8_t x)          { m_writer.write(static_cast<int64_t>(x)); }

		auto start_binary(uint64_t cb) { return m_writer.start_binary(cb); }
		auto start_binary() { return m_writer.start_binary(); }
		void write(const_buffer_ref x)
		{
			auto stream = start_binary(x.size());
			stream.write_buffer(x);
			stream.flush();
		}

		auto start_string(uint64_t cb) { return m_writer.start_string(cb); }
		auto start_string() { return m_writer.start_string(); }
		void write(const char* text) { return write(text, strlen(text)); }
		void write(const std::string& text) { return write(text.data(), text.size()); }
		void write(const char* text, size_t length)
		{
			auto stream = start_string(length);
			stream.write_buffer({ reinterpret_cast<uint8_t*>(text), length });
			stream.flush();
		}

		auto start_array(uint64_t size) { return make_array_writer(m_writer.start_array(size)); }
		auto start_array() { return make_array_writer(m_writer.start_array()); }

		auto start_map(uint64_t size) { return make_map_writer(m_writer.start_map(size)); }
		auto start_map() { return make_map_writer(m_writer.start_map()); }

		void write(const dom::document& d) { copy_dom_document(*this, d); }
	private:
		inner m_writer;
	};

	template <class inner> document_writer<std::decay_t<inner>> make_writer(inner&& writer) { return{ std::forward<inner>(writer) }; }
}}
