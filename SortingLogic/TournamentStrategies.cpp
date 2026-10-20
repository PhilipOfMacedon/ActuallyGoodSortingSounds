#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <stdexcept>
#include <string>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;

		void classicTournamentSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("classic_tournament_tree_comparisons");
			r.declareMetric("classic_tournament_tree_updates");
			r.declareMetric("classic_tournament_extractions");
			r.declareMetric("classic_tournament_output_writes");
			r.declareMetric("classic_tournament_padding_leaves");
			if (count < 2) return;

			std::size_t leafCount = 1;
			while (leafCount < count) { leafCount *= 2; r.chargeWork(); }
			const std::string treeLane = "classic_tournament_winners";
			const std::string outputLane = "classic_tournament_output";
			r.resizeCounters(treeLane, leafCount * 2);
			r.resizeAuxiliary(outputLane, count);
			const auto chooseWinner = [&r](int first, int second)
			{
				if (first < 0) return second;
				if (second < 0) return first;
				const SortItem a = r.at(static_cast<std::size_t>(first));
				const SortItem b = r.at(static_cast<std::size_t>(second));
				r.compare(a, b, static_cast<std::size_t>(first), static_cast<std::size_t>(second));
				r.addMetric("classic_tournament_tree_comparisons");
				const ComparisonResult order = compareItems(a, b, r.trace.direction);
				return order == ComparisonResult::Less || (order == ComparisonResult::Equal && first < second) ? first : second;
			};

			int buildGroup = r.beginPhase("Build padded winner tree");
			for (std::size_t leaf = 0; leaf < leafCount; ++leaf)
			{
				r.writeCounter(treeLane, leafCount + leaf, leaf < count ? static_cast<int>(leaf) : -1);
				if (leaf >= count) r.addMetric("classic_tournament_padding_leaves");
			}
			for (std::size_t node = leafCount; node-- > 1;)
			{
				const int winner = chooseWinner(r.counter(treeLane, node * 2), r.counter(treeLane, node * 2 + 1));
				r.writeCounter(treeLane, node, winner);
			}
			r.endPhase(buildGroup, "Winner tree ready");

			for (std::size_t output = 0; output < count; ++output)
			{
				const int group = r.beginPhase("Extract tournament winner " + std::to_string(output));
				const int winner = r.counter(treeLane, 1);
				if (winner < 0) throw std::logic_error("Classic Tournament Sort exhausted its winner tree early.");
				r.writeAuxiliary(outputLane, output, r.at(static_cast<std::size_t>(winner)));
				r.addMetric("classic_tournament_output_writes");
				r.writeCounter(treeLane, leafCount + static_cast<std::size_t>(winner), -1);
				for (std::size_t node = (leafCount + static_cast<std::size_t>(winner)) / 2; node > 0; node /= 2)
				{
					const int nextWinner = chooseWinner(r.counter(treeLane, node * 2), r.counter(treeLane, node * 2 + 1));
					r.writeCounter(treeLane, node, nextWinner);
					r.addMetric("classic_tournament_tree_updates");
				}
				r.endPhase(group, "Winner extracted and path rebuilt");
				r.addMetric("classic_tournament_extractions");
			}
			for (std::size_t i = 0; i < count; ++i)
				r.writeMain(i, r.auxiliaryAt(outputLane, i), SemanticRole::WriteTarget);
			r.resizeAuxiliary(outputLane, 0);
			r.resizeCounters(treeLane, 0);
		}

		void tournamentSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("tournament_matches");
			r.declareMetric("tournament_loser_replays");
			r.declareMetric("tournament_tree_updates");
			r.declareMetric("tournament_outputs");
			r.declareMetric("tournament_padding_leaves");
			if (count < 2) return;

			std::size_t leafCount = 1;
			while (leafCount < count) { leafCount *= 2; r.chargeWork(); }
			const std::string winnerLane = "tournament_winners";
			const std::string loserLane = "tournament_losers";
			const std::string outputLane = "tournament_output";
			r.resizeCounters(winnerLane, leafCount * 2);
			r.resizeCounters(loserLane, leafCount);
			r.resizeAuxiliary(outputLane, count);
			for (std::size_t i = 0; i < leafCount; ++i)
				r.writeCounter(loserLane, i, -1);
			for (std::size_t leaf = 0; leaf < leafCount; ++leaf)
			{
				r.writeCounter(winnerLane, leafCount + leaf, leaf < count ? static_cast<int>(leaf) : -1);
				if (leaf >= count) r.addMetric("tournament_padding_leaves");
			}

			const auto comparePlayers = [&r](int first, int second, int& winner, int& loser)
			{
				if (first < 0) { winner = second; loser = -1; return; }
				if (second < 0) { winner = first; loser = -1; return; }
				const SortItem a = r.at(static_cast<std::size_t>(first));
				const SortItem b = r.at(static_cast<std::size_t>(second));
				r.compare(a, b, static_cast<std::size_t>(first), static_cast<std::size_t>(second));
				r.addMetric("tournament_matches");
				const ComparisonResult order = compareItems(a, b, r.trace.direction);
				const bool firstWins = order == ComparisonResult::Less || (order == ComparisonResult::Equal && first < second);
				winner = firstWins ? first : second;
				loser = firstWins ? second : first;
			};
			int buildGroup = r.beginPhase("Build winner-loser tournament");
			for (std::size_t node = leafCount; node-- > 1;)
			{
				int winner = -1, loser = -1;
				comparePlayers(r.counter(winnerLane, node * 2), r.counter(winnerLane, node * 2 + 1), winner, loser);
				r.writeCounter(winnerLane, node, static_cast<int>(winner));
				r.writeCounter(loserLane, node, static_cast<int>(loser));
			}
			r.endPhase(buildGroup, "Winner-loser tree ready");

			for (std::size_t output = 0; output < count; ++output)
			{
				const int group = r.beginPhase("Replay loser path " + std::to_string(output));
				const int winner = r.counter(winnerLane, 1);
				if (winner < 0) throw std::logic_error("Tournament Sort exhausted its winner tree early.");
				r.writeAuxiliary(outputLane, output, r.at(static_cast<std::size_t>(winner)));
				r.addMetric("tournament_outputs");
				std::size_t node = leafCount + static_cast<std::size_t>(winner);
				r.writeCounter(winnerLane, node, -1);
				int candidate = -1;
				for (node /= 2; node > 0; node /= 2)
				{
					const int priorLoser = r.counter(loserLane, node);
					int nextWinner = -1, nextLoser = -1;
					if (candidate < 0) nextWinner = priorLoser;
					else if (priorLoser < 0) nextWinner = candidate;
					else
					{
						int selectedWinner = -1, selectedLoser = -1;
						comparePlayers(candidate, priorLoser, selectedWinner, selectedLoser);
						nextWinner = selectedWinner;
						nextLoser = selectedLoser;
					}
					r.writeCounter(loserLane, node, nextLoser);
					r.writeCounter(winnerLane, node, nextWinner);
					candidate = nextWinner;
					r.addMetric("tournament_loser_replays");
					r.addMetric("tournament_tree_updates");
				}
				r.endPhase(group, "Loser path replayed");
			}
			for (std::size_t i = 0; i < count; ++i)
				r.writeMain(i, r.auxiliaryAt(outputLane, i), SemanticRole::WriteTarget);
			r.resizeAuxiliary(outputLane, 0);
			r.resizeCounters(winnerLane, 0);
			r.resizeCounters(loserLane, 0);
		}
	}
}
