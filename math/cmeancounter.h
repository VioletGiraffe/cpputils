#pragma once

template <typename T> class CMeanCounter
{
public:
	explicit CMeanCounter(float smoothingFactor = 0.1f) : _smoothingFactor(smoothingFactor) {}

	void process(const T& value);
	void reset();

	T geometricMean() const;
	T arithmeticMean() const;
	T smoothMean() const;

private:
	T _arithMeanAccumulator = T(0);
	T _geomMeanAccumulator = T(1);
	float _smoothMeanAccumulator = 0.0f;

	const float _smoothingFactor;
	size_t _counter = 0;
};

template <typename T>
T CMeanCounter<T>::smoothMean() const
{
	return T(_smoothMeanAccumulator);
}

template <typename T>
T CMeanCounter<T>::arithmeticMean() const
{
	return _counter > 0 ? _arithMeanAccumulator / T(_counter) : T(0);
}

template <typename T>
T CMeanCounter<T>::geometricMean() const
{
	return _counter > 0 ? T(pow(_geomMeanAccumulator, 1 / T(_counter))) : T(0);
}

template <typename T>
void CMeanCounter<T>::reset()
{
	_arithMeanAccumulator = T(0);
	_geomMeanAccumulator = T(1);
	_counter = 0;
}

template <typename T>
void CMeanCounter<T>::process(const T& value)
{
	++_counter;
	_arithMeanAccumulator += value;
	_geomMeanAccumulator *= value;

	if (_counter == 1)
		_smoothMeanAccumulator = (float)value;
	else
		_smoothMeanAccumulator += _smoothingFactor * (value - _smoothMeanAccumulator); // Equivalent to _smoothMeanAccumulator = _smoothMeanAccumulator * (1.0f - _smoothingFactor) + _smoothingFactor * value
}
