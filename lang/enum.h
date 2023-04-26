#pragma once

#include "../assert/advanced_assert.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

template <typename EnumType>
class Enum
{
public:
	using Enum_Type = EnumType;

	struct EnumItem {
		bool operator==(EnumType value) const {
			return id == value;
		}

		bool operator!=(EnumType value) const {
			return !(*this == value);
		}

		EnumType id;
		std::string name;
	};

	// cppcheck-suppress noExplicitConstructor
	Enum(EnumType initialValue = _items[0].id) : _id(initialValue)
	{
		assert_debug_only(isKnownItem(_id));
	}

	Enum(const Enum& other) : _id(other._id)
	{
	}

	// cppcheck-suppress noExplicitConstructor
	Enum(const EnumItem& item) : _id(item.id)
	{
		assert_debug_only(isKnownItem(_id));
	}

	Enum& operator=(const Enum& other)
	{
		_id = other._id;
		return *this;
	}

	Enum& operator=(EnumType newValue)
	{
		assert_debug_only(isKnownItem(newValue));
		_id = newValue;
		return *this;
	}

	[[nodiscard]] EnumType value() const
	{
		return _id;
	}

	operator EnumType() const
	{
		return value();
	}

	[[nodiscard]] static std::string itemName(const EnumType value)
	{
		const auto item = findItem(value);

		assert_and_return_r(item != std::cend(_items), std::string());
		return item->name;
	}

	[[nodiscard]] std::string itemName() const
	{
		return itemName(_id);
	}

	[[nodiscard]] static typename std::vector<EnumItem>::const_iterator begin()
	{
		return std::begin(_items);
	}

	[[nodiscard]] static typename std::vector<EnumItem>::const_iterator end()
	{
		return std::end(_items);
	}

private:
	[[nodiscard]] inline static typename std::vector<EnumItem>::const_iterator findItem(EnumType id)
	{
		return std::find_if(_items.cbegin(), _items.cend(), [id](const EnumItem& item) {
			return item.id == id;
		});
	}

	[[nodiscard]] inline static bool isKnownItem(EnumType id)
	{
		return findItem(id) != _items.cend();
	}

protected:
	EnumType _id;
	static const std::vector<EnumItem> _items;
};

