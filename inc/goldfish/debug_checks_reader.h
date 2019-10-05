#pragma once

#include "array_ref.h"
#include "debug_checks.h"
#include "sax_reader.h"
#include "tags.h"
#include <type_traits>

namespace goldfish::debug_checks
{
	template <class error_handler, class T, class _tag> class string;
	template <class error_handler, class T> class array;
	template <class error_handler, class T> class map;

	template <bool does_json_conversions_, class Document, class error_handler>
	struct DocTraits {
		using VariantT = std::variant<bool, std::nullptr_t, uint64_t, int64_t, double, undefined,
			string<error_handler, typename Document::template type_with_tag_t<tags::string>, tags::string>,
			string<error_handler, typename Document::template type_with_tag_t<tags::binary>, tags::binary>,
			array<error_handler, typename Document::template type_with_tag_t<tags::array>>,
			map<error_handler, typename Document::template type_with_tag_t<tags::map>>>;

		template <class tag> using type_with_tag_t = typename ::goldfish::tags::type_with_tag_t<tag,
			bool, std::nullptr_t, uint64_t, int64_t, double, undefined,
			string<error_handler, typename Document::template type_with_tag_t<tags::string>, tags::string>,
			string<error_handler, typename Document::template type_with_tag_t<tags::binary>, tags::binary>,
			array<error_handler, typename Document::template type_with_tag_t<tags::array>>,
			map<error_handler, typename Document::template type_with_tag_t<tags::map>>>;

		static constexpr bool does_json_conversions = does_json_conversions_;
	};

	template <class error_handler, class Document> struct document : document_impl<DocTraits<Document::does_json_conversions, Document, error_handler>>
	{
		using document_impl<DocTraits<Document::does_json_conversions, Document, error_handler>>::document_impl;
	};

	template <class error_handler, class Document> document<error_handler, std::decay_t<Document>> add_read_checks_impl(container_base<error_handler>* parent, Document&& t);

	template <class error_handler, class T, class _tag> class string : private container_base<error_handler>
	{
		using container_base<error_handler>::unlock_parent;
		using container_base<error_handler>::err_if_locked;
	public:
		using tag = _tag;
		string(container_base<error_handler>* parent, T&& inner)
			: container_base<error_handler>(parent)
			, m_inner(std::move(inner))
		{}

		size_t read_partial_buffer(std::span<byte> buffer)
		{
			if (buffer.empty())
				return 0;

			auto result = m_inner.read_partial_buffer(buffer);
			if (result == 0)
				unlock_parent();
			return result;
		}
		uint64_t seek(uint64_t cb)
		{
			auto skipped = stream::seek(m_inner, cb);
			if (skipped < cb)
				unlock_parent();
			return skipped;
		}
	private:
		T m_inner;
	};
	template <class error_handler, class Tag, class T> string<error_handler, std::decay_t<T>, Tag> make_string(container_base<error_handler>* parent, T&& inner) { return{ parent, std::forward<T>(inner) }; }

	template <class error_handler, class T> class array : private container_base<error_handler>
	{
		using container_base<error_handler>::unlock_parent;
		using container_base<error_handler>::err_if_locked;
	public:
		using tag = tags::array;

		array(container_base<error_handler>* parent, T&& inner)
			: container_base<error_handler>(parent)
			, m_inner(std::move(inner))
		{}

		std::optional<decltype(add_read_checks_impl(static_cast<container_base<error_handler>*>(nullptr) /*parent*/, *std::declval<T>().read()))> read()
		{
			err_if_locked();

			auto d = m_inner.read();
			if (d)
			{
				return add_read_checks_impl(this /*parent*/, std::move(*d));
			}
			else
			{
				unlock_parent();
				return std::nullopt;
			}
		}
	private:
		T m_inner;
	};
	template <class error_handler, class T> array<error_handler, std::decay_t<T>> make_array(container_base<error_handler>* parent, T&& inner) { return{ parent, std::forward<T>(inner) }; }

	// The inheritance is public so that schema.h can use container_base to more aggressively lock the parent
	template <class error_handler, class T> class map : public container_base<error_handler>
	{
		using container_base<error_handler>::unlock_parent;
		using container_base<error_handler>::err_if_locked;
		using container_base<error_handler>::err_if_flag_set;
		using container_base<error_handler>::err_if_flag_not_set;
		using container_base<error_handler>::set_flag;
		using container_base<error_handler>::clear_flag;
	public:
		using tag = tags::map;

		map(container_base<error_handler>* parent, T&& inner)
			: container_base<error_handler>(parent)
			, m_inner(std::move(inner))
		{}

		std::optional<decltype(add_read_checks_impl(static_cast<container_base<error_handler>*>(nullptr) /*parent*/, *std::declval<T>().read_key()))> read_key()
		{
			err_if_locked();
			err_if_flag_set();

			if (auto d = m_inner.read_key())
			{
				set_flag();
				return add_read_checks_impl(this /*parent*/, std::move(*d));
			}
			else
			{
				unlock_parent();
				return std::nullopt;
			}
		}
		auto read_value()
		{
			err_if_locked();
			err_if_flag_not_set();
			clear_flag();
			return add_read_checks_impl(this /*parent*/, m_inner.read_value());
		}
	private:
		T m_inner;
	};
	template <class error_handler, class T> map<error_handler, std::decay_t<T>> make_map(container_base<error_handler>* parent, T&& inner) { return{ parent, std::forward<T>(inner) }; }

	template <class error_handler, class Document> document<error_handler, std::decay_t<Document>> add_read_checks_impl(container_base<error_handler>* parent, Document&& t)
	{
		return std::forward<Document>(t).visit([&](auto&& x) -> document<error_handler, std::decay_t<Document>> {
			if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::map>)
			{
				return make_map<error_handler>(parent, std::forward<decltype(x)>(x));
			}
			else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::array>)
			{
				return make_array<error_handler>(parent, std::forward<decltype(x)>(x));
			}
			else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::binary>)
			{
				return make_string<error_handler, tags::binary>(parent, std::forward<decltype(x)>(x));
			}
			else if constexpr (std::is_same_v<decltype(tags::get_tag(x)), tags::string>)
			{
				return make_string<error_handler, tags::string>(parent, std::forward<decltype(x)>(x));
			}
			else
			{
				return std::forward<decltype(x)>(x);
			}
		});
	}

	template <class error_handler, class Document> auto add_read_checks(Document&& t, error_handler)
	{
		return add_read_checks_impl(static_cast<container_base<error_handler>*>(nullptr) /*parent*/, std::forward<Document>(t));
	}
	template <class Document> auto add_read_checks(Document&& t, no_check)
	{
		return std::forward<Document>(t);
	}
} // namespace goldfish::debug_checks
