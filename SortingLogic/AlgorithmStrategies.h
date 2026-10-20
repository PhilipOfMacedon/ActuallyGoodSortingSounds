#pragma once

namespace sortinglogic
{
	namespace detail
	{
		class Recorder;
	}

	namespace strategies
	{
		void bingo(detail::Recorder& recorder);
		void bubble(detail::Recorder& recorder);
		void comb(detail::Recorder& recorder);
		void cocktail(detail::Recorder& recorder);
		void doubleSelection(detail::Recorder& recorder);
		void gnome(detail::Recorder& recorder);
		void insertion(detail::Recorder& recorder);
		void oddEven(detail::Recorder& recorder);
		void optimizedBubble(detail::Recorder& recorder);
		void optimizedCocktail(detail::Recorder& recorder);
		void selection(detail::Recorder& recorder);
		void stableSelection(detail::Recorder& recorder);
		void swaplessBubble(detail::Recorder& recorder);
		void unoptimizedBubble(detail::Recorder& recorder);
		void unoptimizedCocktail(detail::Recorder& recorder);
	}
}
