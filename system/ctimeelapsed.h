#ifndef CTIMEELAPSED_H
#define CTIMEELAPSED_H

#include <memory>

class CTimePrivate;
class CTimeElapsed
{
public:
	CTimeElapsed();

	CTimeElapsed(const CTimeElapsed&) = delete;
	CTimeElapsed& operator=(const CTimeElapsed&) = delete;

	void     start();
	void     pause();
	void     resume();
	int      elapsed() const;

private:
	std::shared_ptr<CTimePrivate> _time;
	int                           _elapsedBeforePause;
	bool                          _paused;

};

#endif // CTIMEELAPSED_H
