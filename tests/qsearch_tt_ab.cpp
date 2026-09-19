#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "chess/nnue.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

#ifndef HEBICHESS_QSEARCH_TT_VARIANT
#define HEBICHESS_QSEARCH_TT_VARIANT 0
#endif

struct Position {
  std::string name;
  std::string fen;
  Board board;
  int depth;
};

std::uint64_t next_random(std::uint64_t& state) {
  state ^= state << 7;
  state ^= state >> 9;
  return state;
}

std::vector<Position> load_fixture(const std::string& path, int depth) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open fixture: " + path);
  std::vector<Position> positions;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    const std::size_t tab = line.find('\t');
    const std::string name = tab == std::string::npos
        ? std::filesystem::path(path).stem().string() : line.substr(0, tab);
    const std::string fen = tab == std::string::npos ? line : line.substr(tab + 1);
    const auto board = Board::from_fen(fen);
    if (!board) throw std::runtime_error("invalid fixture FEN: " + name);
    positions.push_back({name, fen, *board, depth});
  }
  return positions;
}

void append_targeted(std::vector<Position>& positions, int depth) {
  const std::pair<const char*, const char*> cases[] = {
      {"target-mate", "7k/5Q2/7K/8/8/8/8/8 w - - 0 1"},
      // Search currently has no history stack or fifty-move draw shortcut;
      // keep both boundary FENs in the equivalence corpus nevertheless.
      {"target-repetition-related", "4k3/8/8/8/8/8/4P3/4K3 w - - 12 42"},
      {"target-fifty-move-edge", "4k3/8/8/8/8/8/4P3/4K3 w - - 99 50"},
      {"target-in-check", "4k3/8/8/8/8/8/4r3/4K3 w - - 0 1"},
      {"target-promotion", "4k3/P7/8/8/8/8/8/4K3 w - - 0 1"},
      {"target-en-passant", "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"},
      {"target-long-capture-chain", "3qk3/8/8/3r4/3Q4/8/8/3RK3 w - - 0 1"},
  };
  for (const auto& item : cases) {
    const auto board = Board::from_fen(item.second);
    if (!board) throw std::runtime_error("invalid targeted FEN");
    positions.push_back({item.first, item.second, *board, depth});
  }
}

void append_random(std::vector<Position>& positions, int count, int depth) {
  std::uint64_t state = 0x6a09e667f3bcc909ULL;
  Board board = Board::initial();
  for (int index = 0; index < count; ++index) {
    for (int ply = 0; ply < 8 + static_cast<int>(next_random(state) % 24); ++ply) {
      const std::vector<Move> legal = generate_legal_moves(board);
      if (legal.empty()) {
        board = Board::initial();
        break;
      }
      board.make_move(legal[next_random(state) % legal.size()]);
    }
    positions.push_back({"random-" + std::to_string(index), board.to_fen(), board, depth});
    if ((index + 1) % 16 == 0) board = Board::initial();
  }
}

const char* variant_name() {
#if HEBICHESS_QSEARCH_TT_VARIANT == 0
  return "baseline";
#elif HEBICHESS_QSEARCH_TT_VARIANT == 1
  return "shadow";
#else
  return "active-noncheck-cutoff-only";
#endif
}

void write_record(std::ostream& out, const Position& position, const SearchResult& result) {
  // The first five fields form the A/B signature: name, bestmove, score,
  // nodes, qnodes.  FEN is retained for mismatch diagnostics; remaining
  // values are profile-only measurements.
  out << position.name << '\t' << move_to_uci(result.best_move) << '\t' << result.score
      << '\t' << result.nodes << '\t' << result.qnodes << '\t' << position.fen
      << '\t' << result.eval_calls
      << '\t' << result.qdelta_prunes
      << '\t' << result.qtt_probes << '\t' << result.qtt_hits
      << '\t' << result.qtt_exact_hits << '\t' << result.qtt_lower_hits
      << '\t' << result.qtt_upper_hits << '\t' << result.qtt_in_check_hits
      << '\t' << result.qtt_non_check_hits << '\t' << result.qtt_potential_reusable_eval
      << '\t' << result.qtt_potential_reusable_subtree << '\t' << result.qtt_active_cutoffs
      << '\t' << result.qtt_stores << '\t' << result.qtt_replacements
      << '\t' << result.tt_replacements << '\t' << result.qtt_in_check_probes
      << '\t' << result.qtt_non_check_probes;
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
  out << '\t' << result.qtt_cutoff_candidates << '\t' << result.qtt_cutoff_applied;
#endif
  out << '\n';
}

#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
const char* bound_name(TTBound bound) {
  switch (bound) {
    case TTBound::Exact: return "EXACT";
    case TTBound::Lower: return "LOWER";
    case TTBound::Upper: return "UPPER";
  }
  return "UNKNOWN";
}

const char* cutoff_reason(const QsearchTtCutoffTrace& event) {
  switch (event.bound) {
    case TTBound::Exact: return "exact";
    case TTBound::Lower: return "lower>=beta";
    case TTBound::Upper: return "upper<=alpha";
  }
  return "unknown";
}

const char* return_kind_name(QsearchReturnKind kind) {
  switch (kind) {
    case QsearchReturnKind::Mate: return "mate";
    case QsearchReturnKind::StandPatBeta: return "stand-pat-beta";
    case QsearchReturnKind::Stalemate: return "stalemate";
    case QsearchReturnKind::EvasionLoop: return "evasion-loop";
    case QsearchReturnKind::TacticalLoop: return "tactical-loop";
    case QsearchReturnKind::Stopped: return "stopped";
  }
  return "unknown";
}

struct QsearchWindowRequest {
  std::string name;
  int alpha{0};
  int beta{0};
  int ply{0};
};

void write_qsearch_window_trace(std::ostream& out, const std::string& window,
                                const Position& position, const QsearchWindowTrace& trace) {
  const auto optional_int = [&out](const std::optional<int>& value) {
    if (value) out << *value;
  };
  const auto optional_float = [&out](const std::optional<float>& value) {
    if (value) out << std::setprecision(9) << *value;
  };
  for (const QsearchNodeTrace& node : trace.nodes) {
    out << window << '\t' << position.name << "\tnode\t" << node.sequence << '\t'
        << std::hex << node.key << std::dec << '\t' << node.ply << '\t' << node.fen << '\t'
        << node.entry_alpha << '\t' << node.entry_beta << '\t' << node.in_check << '\t';
    optional_float(node.raw_nnue);
    out << '\t' << std::hex << node.accumulator_checksum << std::dec << '\t';
    optional_int(node.stand_pat);
    out << '\t' << node.alpha_after_stand_pat << '\t' << return_kind_name(node.return_kind)
        << '\t' << node.returned_score;
    for (int field = 0; field < 14; ++field) out << '\t';
    out << '\n';
    for (const QsearchMoveTrace& move : node.moves) {
      out << window << '\t' << position.name << "\tmove\t" << node.sequence << '\t'
          << std::hex << node.key << std::dec << '\t' << node.ply << '\t' << node.fen << '\t'
          << node.entry_alpha << '\t' << node.entry_beta << '\t' << node.in_check << '\t';
      optional_float(node.raw_nnue);
      out << '\t' << std::hex << node.accumulator_checksum << std::dec << '\t';
      optional_int(node.stand_pat);
      out << '\t' << node.alpha_after_stand_pat << '\t' << return_kind_name(node.return_kind)
          << '\t' << node.returned_score << '\t' << move.move << '\t' << move.order << '\t'
          << move.capture << '\t' << move.promotion << '\t' << move.gives_check << '\t'
          << move.see << '\t' << move.see_rejected << '\t' << move.delta_rejected << '\t'
          << move.searched << '\t' << move.child_alpha << '\t' << move.child_beta << '\t'
          << move.child_score << '\t' << move.alpha_after << '\t' << move.beta_cutoff << '\n';
    }
  }
}

void write_trace(std::ostream& out, const Position& position,
                 const std::vector<QsearchTtCutoffTrace>& trace) {
  for (const QsearchTtCutoffTrace& event : trace) {
    const auto raw = [&out](const std::optional<float>& value) {
      if (value) out << std::setprecision(9) << *value;
      else out << "null";
    };
    out << position.name << '\t' << event.cutoff_serial << '\t' << event.hit_fen
        << '\t' << std::hex << event.key << '\t' << event.stored_key << std::dec
        << '\t' << event.ply << '\t' << event.alpha << '\t' << event.beta
        << '\t' << bound_name(event.bound) << '\t' << event.stored_score
        << '\t' << event.decoded_score << '\t' << cutoff_reason(event)
        << '\t' << event.store_serial << '\t' << event.store_fen
        << '\t' << event.store_ply << '\t' << event.store_alpha << '\t' << event.store_beta
        << '\t' << event.store_result << '\t' << event.same_fen << '\t' << event.same_full_key
        << '\t';
    raw(event.hit_raw_nnue);
    out << '\t';
    raw(event.store_raw_nnue);
    out << '\t' << std::hex << event.hit_accumulator_checksum
        << '\t' << event.store_accumulator_checksum << std::dec
        << '\t' << event.qtt_off_hit_window << '\t' << event.qtt_off_store_window
        << '\t' << event.qtt_off_full_window << '\n';
  }
}

std::uint8_t parse_qtt_bounds(std::string value) {
  if (value == "OFF") return 0;
  std::uint8_t mask = 0;
  for (char raw : value) {
    const char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
    if (ch == 'E') mask |= 1;
    else if (ch == 'L') mask |= 2;
    else if (ch == 'U') mask |= 4;
    else throw std::runtime_error("--qtt-bounds accepts OFF or a combination of E, L, U");
  }
  if (mask == 0) throw std::runtime_error("--qtt-bounds accepts OFF or a combination of E, L, U");
  return mask;
}
#endif

}  // namespace

int main(int argc, char* argv[]) {
  try {
    std::string fixture = "tests/data/phase6-search-baseline.fen";
    std::string network;
    std::optional<std::string> output;
    std::optional<std::string> trace_output;
    std::optional<std::string> qsearch_window_trace_output;
    std::optional<std::uint8_t> qtt_bounds;
    std::optional<std::int64_t> qtt_cutoff_limit;
    std::optional<std::uint64_t> qtt_trace_cutoff;
    std::vector<QsearchWindowRequest> qsearch_windows;
    bool qsearch_disable_delta_pruning = false;
    std::vector<std::string> only;
    std::optional<std::string> forced_root_move;
    int depth = 4;
    int random_depth = 3;
    int random_count = 256;
    for (int index = 1; index < argc; ++index) {
      const std::string flag = argv[index];
      auto value = [&]() -> std::string {
        if (++index >= argc) throw std::runtime_error(flag + " needs a value");
        return argv[index];
      };
      if (flag == "--fixture") fixture = value();
      else if (flag == "--network") network = value();
      else if (flag == "--output") output = value();
      else if (flag == "--trace-output") trace_output = value();
      else if (flag == "--qsearch-window-trace-output") qsearch_window_trace_output = value();
      else if (flag == "--qsearch-window") {
        if (index + 4 >= argc)
          throw std::runtime_error("--qsearch-window needs NAME ALPHA BETA PLY");
        const std::string name = argv[++index];
        const int alpha = std::stoi(argv[++index]);
        const int beta = std::stoi(argv[++index]);
        const int ply = std::stoi(argv[++index]);
        if (alpha >= beta) throw std::runtime_error("--qsearch-window requires ALPHA < BETA");
        if (ply < 0) throw std::runtime_error("--qsearch-window requires PLY >= 0");
        qsearch_windows.push_back({name, alpha, beta, ply});
      }
      else if (flag == "--qsearch-disable-delta-pruning") qsearch_disable_delta_pruning = true;
      else if (flag == "--qtt-bounds") qtt_bounds = parse_qtt_bounds(value());
      else if (flag == "--qtt-cutoff-limit") qtt_cutoff_limit = std::stoll(value());
      else if (flag == "--qtt-trace-cutoff") qtt_trace_cutoff = std::stoull(value());
      else if (flag == "--only") only.push_back(value());
      else if (flag == "--forced-root-move") forced_root_move = value();
      else if (flag == "--depth") depth = std::stoi(value());
      else if (flag == "--random-depth") random_depth = std::stoi(value());
      else if (flag == "--random-count") random_count = std::stoi(value());
      else throw std::runtime_error("unknown option: " + flag);
    }
    if (network.empty()) throw std::runtime_error("--network is required");
    std::string network_error;
    if (!load_nnue_network(network, network_error)) throw std::runtime_error(network_error);
    std::vector<Position> positions = load_fixture(fixture, depth);
    append_targeted(positions, depth);
    append_random(positions, random_count, random_depth);
    if (!only.empty()) {
      positions.erase(std::remove_if(positions.begin(), positions.end(), [&](const Position& position) {
        return std::find(only.begin(), only.end(), position.name) == only.end();
      }), positions.end());
      if (positions.empty()) throw std::runtime_error("no selected cases in fixture/random corpus");
    }
    if (forced_root_move && positions.size() != 1)
      throw std::runtime_error("--forced-root-move requires exactly one --only case");
#if !(defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC)
    if (forced_root_move || trace_output || qtt_bounds || qtt_cutoff_limit || qtt_trace_cutoff ||
        qsearch_window_trace_output || !qsearch_windows.empty() || qsearch_disable_delta_pruning)
      throw std::runtime_error("forced-root and trace diagnostics require a diagnostic profile binary");
#endif
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
    if (!qsearch_windows.empty() && !qsearch_window_trace_output)
      throw std::runtime_error("--qsearch-window requires --qsearch-window-trace-output");
    if (qsearch_window_trace_output && qsearch_windows.empty())
      throw std::runtime_error("--qsearch-window-trace-output requires --qsearch-window");
    if (!qsearch_windows.empty() && positions.size() != 1)
      throw std::runtime_error("--qsearch-window requires exactly one --only case");
#endif
    std::ofstream file;
    std::ostream* out = &std::cout;
    if (output) {
      file.open(*output);
      if (!file) throw std::runtime_error("cannot write output: " + *output);
      out = &file;
    }
    *out << "# variant=" << variant_name() << " positions=" << positions.size() << '\n';
    *out << "# case\tbestmove\tscore\tnodes\tqnodes\tfen\teval_calls\tqdelta_prunes"
            "\tqtt_probes\tqtt_hits\tqtt_exact_hits\tqtt_lower_hits\tqtt_upper_hits"
            "\tqtt_in_check_hits\tqtt_non_check_hits\tqtt_potential_reusable_eval"
            "\tqtt_potential_reusable_subtree\tqtt_active_cutoffs\tqtt_stores"
            "\tqtt_replacements\ttt_replacements\tqtt_in_check_probes"
            "\tqtt_non_check_probes";
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
    *out << "\tqtt_cutoff_candidates\tqtt_cutoff_applied";
#endif
    *out << '\n';
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
    std::ofstream trace_file;
    if (trace_output) {
      trace_file.open(*trace_output);
      if (!trace_file) throw std::runtime_error("cannot write trace output: " + *trace_output);
      trace_file << "# case\tcutoff_serial\thit_fen\thit_full_key\tstored_full_key\tply\talpha\tbeta\tbound\tstored_tt_score\tdecoded_score\tcutoff_reason\tstore_serial\tstore_fen\tstore_ply\tstore_alpha\tstore_beta\tstore_result\tsame_fen\tsame_full_key\thit_raw_nnue\tstore_raw_nnue\thit_accumulator_checksum\tstore_accumulator_checksum\tqtt_off_hit_window\tqtt_off_store_window\tqtt_off_full_window\n";
    }
    std::ofstream qsearch_window_trace_file;
    if (qsearch_window_trace_output) {
      qsearch_window_trace_file.open(*qsearch_window_trace_output);
      if (!qsearch_window_trace_file)
        throw std::runtime_error("cannot write output: " + *qsearch_window_trace_output);
      qsearch_window_trace_file << "# window\tcase\trecord\tsequence\tkey\tply\tfen\tentry_alpha\tentry_beta\tin_check\traw_nnue\taccumulator_checksum\tstand_pat\talpha_after_stand_pat\treturn_kind\treturned_score\tmove\torder\tcapture\tpromotion\tgives_check\tsee\tsee_rejected\tdelta_rejected\tsearched\tchild_alpha\tchild_beta\tchild_score\talpha_after\tbeta_cutoff\n";
    }
#endif
    for (const Position& position : positions) {
      clear_transposition_table();
      clear_search_heuristics();
      SearchLimits limits;
      limits.max_depth = position.depth;
      limits.eval_mode = EvalMode::NNUE;
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
      if (qtt_bounds) {
        limits.qtt_diagnostic_override_bounds = true;
        limits.qtt_diagnostic_bound_mask = *qtt_bounds;
      }
      if (qtt_cutoff_limit) limits.qtt_cutoff_limit = *qtt_cutoff_limit;
      if (qtt_trace_cutoff) limits.qtt_trace_cutoff = *qtt_trace_cutoff;
#endif
      SearchResult result;
#if defined(HEBICHESS_QSEARCH_TT_DIAGNOSTIC) && HEBICHESS_QSEARCH_TT_DIAGNOSTIC
      if (!qsearch_windows.empty()) {
        for (const QsearchWindowRequest& window : qsearch_windows) {
          const QsearchWindowTrace trace = trace_qsearch_window_for_test(
              position.board, window.alpha, window.beta, window.ply, EvalMode::NNUE,
              qsearch_disable_delta_pruning);
          write_qsearch_window_trace(qsearch_window_trace_file, window.name, position, trace);
        }
        continue;
      }
      std::vector<QsearchTtCutoffTrace> trace;
      if (trace_output) {
        set_qsearch_tt_cutoff_trace_callback_for_test([&trace](const QsearchTtCutoffTrace& event) {
          trace.push_back(event);
        });
      }
      if (forced_root_move) {
        const auto move = parse_uci_move(position.board, *forced_root_move);
        if (!move) throw std::runtime_error("invalid forced root move for " + position.name);
        result = search_forced_root_move_for_test(position.board, *move, limits);
      } else {
        result = search(position.board, limits);
      }
      set_qsearch_tt_cutoff_trace_callback_for_test({});
      if (trace_output) write_trace(trace_file, position, trace);
#else
      result = search(position.board, limits);
#endif
      if (!result.best_move.from.is_valid())
        throw std::runtime_error("no bestmove for " + position.name);
      write_record(*out, position, result);
    }
  } catch (const std::exception& error) {
    std::cerr << "qsearch-tt-ab: " << error.what() << '\n';
    return 1;
  }
}
