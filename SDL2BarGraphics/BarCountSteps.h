#pragma once

inline int adjustBarCount(int current, bool increase, bool coarse, int minimum, int maximum)
{
	current = current < minimum ? minimum : (current > maximum ? maximum : current);
	if (!coarse)
	{
		const int stepped = current + (increase ? 1 : -1);
		return stepped < minimum ? minimum : (stepped > maximum ? maximum : stepped);
	}

	if (increase)
	{
		const int nextDecade = (current / 10 + 1) * 10;
		return nextDecade < maximum ? nextDecade : maximum;
	}

	const int previousDecade = ((current - 1) / 10) * 10;
	return previousDecade > minimum ? previousDecade : minimum;
}
