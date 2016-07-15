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
		assert(isKnownItem(_id));
	}

	Enum(const Enum<EnumType>& other) : _id(other._id)
	{
	}

	Enum(const EnumItem& item) : _id(item.id)
	{
		assert(isKnownItem(_id));
	}

	Enum& operator=(const Enum<EnumType>& other)
	{
		_id = other._id;
		return *this;
	}

	Enum& operator=(EnumType newValue)
	{
		assert(isKnownItem(newValue));
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

	static typename std::vector<EnumItem>::const_iterator begin()
	{
		return std::begin(_items);
	}

	static typename std::vector<EnumItem>::const_iterator end()
	{
		return std::end(_items);
	}

private:
	static bool isKnownItem(EnumType id)
	{
		for (size_t i = 0, end = _items.size(); i != end; ++i)
			if (_items[i].id == id)
				return true;

		return false;
	}

protected:
	EnumType _id;
	static const std::vector<EnumItem> _items;
};

