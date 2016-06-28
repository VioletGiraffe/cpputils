#pragma once

#include "../assert/advanced_assert.h"

#include <algorithm>
#include <iterator>
#include <string>

template <typename EnumType>
class Enum
{
public:
	typedef EnumType Enum_Type;

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

	Enum(EnumType initialValue = _items[0].id) : _id(initialValue)
	{
		static_assert(sizeof(_items) != 0, "_items list cannot be empty");
	}

	Enum(const Enum<EnumType>& other) : _id(other._id)
	{
		static_assert(sizeof(_items) != 0, "_items list cannot be empty");
	}

	Enum(const EnumItem& item) : _id(item.id)
	{
		static_assert(sizeof(_items) != 0, "_items list cannot be empty");
	}

	Enum& operator=(const Enum<EnumType>& other)
	{
		_id = other._id;
		return *this;
	}

	Enum& operator=(EnumType newValue)
	{
		_id = newValue;
		return *this;
	}

	EnumType value() const
	{
		return _id;
	}

	operator EnumType() const
	{
		return value();
	}

	static std::string itemName(const EnumType value)
	{
		auto item = std::find_if(std::begin(_items), std::end(_items), [value](const EnumItem& item){
			return item.id == value;
		});

		assert_and_return_r(item != std::end(_items), std::string());

		return item->name;
	}

	std::string itemName() const
	{
		return itemName(_id);
	}

	static const EnumItem* begin()
	{
		return std::begin(_items);
	}

	static const EnumItem* end()
	{
		return std::end(_items);
	}

protected:
	EnumType _id;
	static const EnumItem _items[];
};

