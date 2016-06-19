#pragma once

#include "../assert/advanced_assert.h"

#include <algorithm>
#include <iterator>
#include <string>

template <typename EnumType>
class Enum
{
public:
	struct EnumItem {
		EnumType id;
		std::string name;
	};

	Enum(EnumType initialValue = _items[0]) : _value(initialValue)
	{
		static_assert(sizeof(_items) != 0, "_items list cannot be empty");
	}

	Enum(const Enum<EnumType>& other) : _value(other._value)
	{
		static_assert(sizeof(_items) != 0, "_items list cannot be empty");
	}

	Enum& operator=(const Enum<EnumType>& other)
	{
		_value = other._value;
		return *this;
	}

	Enum& operator=(EnumType newValue)
	{
		_value = newValue;
		return *this;
	}

	EnumType value() const
	{
		return _value;
	}

	operator EnumType() const
	{
		return value();
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
	EnumType _value;
	static const EnumItem _items[];
};

