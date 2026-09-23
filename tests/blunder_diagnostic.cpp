#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "chess/eval.hpp"
#include "chess/movegen.hpp"
#include "chess/nnue.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

enum class Command { Trace, Sweep, Compare, StaticAudit };

struct Options {
  Command command{Command::Trace};
  std::string fen;
  std::optional<std::filesystem::path> network;
  std::optional<std::filesystem::path> output;
  int depth{8};
  EvalMode trace_mode{EvalMode::NNUE};
  bool use_tt{true};
  bool use_null_move{true};
  bool use_lmr{true};
  bool use_pvs{true};
  bool use_see_pruning{true};
  bool use_aspiration{true};
};

std::string json_escape(std::string_view value) {
  std::string escaped;
  for (const unsigned char ch : value) {
    switch (ch) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (ch < 0x20) {
          std::ostringstream out;
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(ch);
          escaped += out.str();
        } else {
          escaped.push_back(static_cast<char>(ch));
        }
    }
  }
  return escaped;
}

std::string mode_name(EvalMode mode) { return mode == EvalMode::NNUE ? "NNUE" : "HCE"; }

std::string bound_name(ScoreBound bound) {
  switch (bound) {
    case ScoreBound::Exact: return "exact";
    case ScoreBound::Lower: return "lower";
    case ScoreBound::Upper: return "upper";
  }
  return "unknown";
}

std::string proof_name(StyleProofResult proof) {
  switch (proof) {
    case StyleProofResult::NotRun: return "not_run";
    case StyleProofResult::ExactScore: return "exact_score";
    case StyleProofResult::PrefilterSkipped: return "prefilter_skipped";
    case StyleProofResult::UpperBoundRejected: return "upper_bound_rejected";
    case StyleProofResult::ThresholdProven: return "threshold_proven";
    case StyleProofResult::ThresholdRejected: return "threshold_rejected";
  }
  return "unknown";
}

int parse_positive(const std::string& value, const char* flag) {
  try {
    const int parsed = std::stoi(value);
    if (parsed <= 0) throw std::invalid_argument("non-positive");
    return parsed;
  } catch (const std::exception&) {
    throw std::runtime_error(std::string(flag) + " needs a positive integer");
  }
}

Options parse_options(int argc, char* argv[]) {
  Options options;
  auto next = [&](int& index, const char* flag) -> std::string {
    if (++index >= argc) throw std::runtime_error(std::string(flag) + " needs a value");
    return argv[index];
  };
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    if (flag == "--fen") options.fen = next(index, "--fen");
    else if (flag == "--network") options.network = next(index, "--network");
    else if (flag == "--output") options.output = next(index, "--output");
    else if (flag == "--depth") options.depth = parse_positive(next(index, "--depth"), "--depth");
    else if (flag == "--command") {
      const std::string command = next(index, "--command");
      if (command == "trace") options.command = Command::Trace;
      else if (command == "sweep") options.command = Command::Sweep;
      else if (command == "compare") options.command = Command::Compare;
      else if (command == "static") options.command = Command::StaticAudit;
      else throw std::runtime_error("--command accepts trace, sweep, compare, or static");
    } else if (flag == "--eval") {
      const std::string mode = next(index, "--eval");
      if (mode == "nnue" || mode == "NNUE") options.trace_mode = EvalMode::NNUE;
      else if (mode == "hce" || mode == "HCE") options.trace_mode = EvalMode::HCE;
      else throw std::runtime_error("--eval accepts nnue or hce");
    } else if (flag == "--disable") {
      const std::string control = next(index, "--disable");
      if (control == "tt") options.use_tt = false;
      else if (control == "null-move") options.use_null_move = false;
      else if (control == "lmr") options.use_lmr = false;
      else if (control == "pvs") options.use_pvs = false;
      else if (control == "see-pruning") options.use_see_pruning = false;
      else if (control == "aspiration") options.use_aspiration = false;
      else throw std::runtime_error("--disable accepts tt, null-move, lmr, pvs, see-pruning, or aspiration");
    } else if (flag == "--help") {
      std::cout << "Usage: HebiChessBlunderDiagnostic --fen <FEN> [--network model.hebinnue]"
                   " [--command trace|sweep|compare|static] [--depth N] [--eval nnue|hce]"
                   " [--disable CONTROL] [--output result.json]\\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown option: " + flag);
    }
  }
  if (options.fen.empty()) throw std::runtime_error("--fen is required");
  return options;
}

SearchLimits make_limits(const Options& options, EvalMode mode, int depth,
                         bool use_root_style_selection) {
  SearchLimits limits;
  limits.max_depth = depth;
  limits.eval_mode = mode;
  limits.use_root_style_selection = use_root_style_selection;
  limits.use_tt = options.use_tt;
  limits.use_null_move = options.use_null_move;
  limits.use_lmr = options.use_lmr;
  limits.use_pvs = options.use_pvs;
  limits.use_see_pruning = options.use_see_pruning;
  limits.use_aspiration = options.use_aspiration;
  // No deadline or soft deadline is ever configured in this harness.
  return limits;
}

SearchResult run_search(const Board& board, const Options& options, EvalMode mode,
                        int depth, bool use_root_style_selection) {
  clear_transposition_table();
  clear_search_heuristics();
  return search(board, make_limits(options, mode, depth, use_root_style_selection));
}

const RootMoveInfo* objective_root(const SearchResult& result) {
  const RootMoveInfo* best = nullptr;
  for (const RootMoveInfo& item : result.root_moves) {
    if (item.bound != ScoreBound::Exact) continue;
    if (best == nullptr || item.search_score > best->search_score) best = &item;
  }
  return best;
}

const RootMoveInfo* find_root(const SearchResult& result, Move move) {
  const auto it = std::find_if(result.root_moves.begin(), result.root_moves.end(),
      [move](const RootMoveInfo& item) { return item.move == move; });
  return it == result.root_moves.end() ? nullptr : &*it;
}

std::string pv_json(const std::vector<Move>& pv) {
  std::ostringstream out;
  out << '[';
  for (std::size_t index = 0; index < pv.size(); ++index) {
    if (index != 0) out << ',';
    out << '"' << json_escape(move_to_uci(pv[index])) << '"';
  }
  return out.str() + ']';
}

void write_root_candidates(std::ostream& out, const Board& board, const SearchResult& result) {
  out << '[';
  for (std::size_t index = 0; index < result.root_moves.size(); ++index) {
    if (index != 0) out << ',';
    const RootMoveInfo& item = result.root_moves[index];
    out << "{\"move\":\"" << json_escape(move_to_uci(item.move))
        << "\",\"search_score_cp\":" << item.search_score
        << ",\"bound\":\"" << bound_name(item.bound)
        << "\",\"style_score\":" << item.style_score
        << ",\"style_safe\":" << (item.style_safe ? "true" : "false")
        << ",\"style_tolerance_cp\":" << item.style_tolerance
        << ",\"verification\":\"" << proof_name(item.style_proof)
        << "\",\"pv\":" << pv_json(extract_principal_variation(board, item.move)) << '}';
  }
  out << ']';
}

void write_search_record(std::ostream& out, const Board& board, const SearchResult& result) {
  out << "{\"bestmove\":\"" << json_escape(move_to_uci(result.best_move))
      << "\",\"score_cp\":" << result.score
      << ",\"completed_depth\":" << result.completed_depth
      << ",\"attempted_depth\":" << result.attempted_depth
      << ",\"elapsed_ms\":" << result.time_elapsed_ms
      << ",\"nodes\":" << result.nodes
      << ",\"qnodes\":" << result.qnodes
      << ",\"nps\":" << (result.time_elapsed_ms > 0
          ? result.nodes * 1000 / static_cast<std::uint64_t>(result.time_elapsed_ms) : 0)
      << ",\"stop_reason\":\"" << json_escape(result.time_stop_reason)
      << "\",\"pv\":" << pv_json(result.principal_variation)
      << ",\"root_candidates\":";
  write_root_candidates(out, board, result);
  out << '}';
}

std::string controls_json(const Options& options) {
  std::ostringstream out;
  out << "{\"tt\":" << (options.use_tt ? "true" : "false")
      << ",\"null_move\":" << (options.use_null_move ? "true" : "false")
      << ",\"lmr\":" << (options.use_lmr ? "true" : "false")
      << ",\"pvs\":" << (options.use_pvs ? "true" : "false")
      << ",\"see_pruning\":" << (options.use_see_pruning ? "true" : "false")
      << ",\"aspiration\":" << (options.use_aspiration ? "true" : "false") << '}';
  return out.str();
}

std::string trace_json(const Board& board, const Options& options) {
  const SearchResult result = run_search(board, options, options.trace_mode, options.depth, true);
  const RootMoveInfo* objective = objective_root(result);
  const RootMoveInfo* selected = find_root(result, result.best_move);
  const int objective_score = objective == nullptr ? result.score : objective->search_score;
  const int selected_score = selected == nullptr ? result.score : selected->search_score;
  const int loss = objective_score - selected_score;
  std::ostringstream out;
  out << "{\"kind\":\"blunder_trace\",\"fen_before\":\"" << json_escape(board.to_fen())
      << "\",\"eval_mode\":\"" << mode_name(options.trace_mode)
      << "\",\"selected_move\":\"" << json_escape(move_to_uci(result.best_move))
      << "\",\"objective_best_move\":\""
      << json_escape(objective == nullptr ? move_to_uci(result.best_move) : move_to_uci(objective->move))
      << "\",\"final_style_move\":\"" << json_escape(move_to_uci(result.best_move))
      << "\",\"objective_score_cp\":" << objective_score
      << ",\"style_objective_loss_cp\":" << loss
      << ",\"style_loss_score_is_bound\":"
      << (selected != nullptr && selected->bound != ScoreBound::Exact ? "true" : "false")
      << ",\"diagnostic_failure\":" << (loss > 50 ? "true" : "false")
      << ",\"search_controls\":" << controls_json(options) << ',';
  const std::string record = [&] { std::ostringstream value; write_search_record(value, board, result); return value.str(); }();
  // Keep the required trace fields top-level while sharing the record writer.
  out << record.substr(1);
  return out.str();
}

std::string sweep_json(const Board& board, const Options& options) {
  constexpr int depths[] = {4, 6, 8, 10, 12};
  std::ostringstream out;
  out << "{\"kind\":\"fixed_depth_sweep\",\"fen_before\":\"" << json_escape(board.to_fen())
      << "\",\"eval_mode\":\"" << mode_name(options.trace_mode)
      << "\",\"objective_only\":true,\"time_management\":false,\"search_controls\":"
      << controls_json(options) << ",\"runs\":[";
  for (std::size_t index = 0; index < std::size(depths); ++index) {
    if (index != 0) out << ',';
    const SearchResult result = run_search(board, options, options.trace_mode, depths[index], false);
    write_search_record(out, board, result);
  }
  return out.str() + "]}";
}

std::string compare_json(const Board& board, const Options& options) {
  const SearchResult hce = run_search(board, options, EvalMode::HCE, options.depth, false);
  const SearchResult nnue = run_search(board, options, EvalMode::NNUE, options.depth, false);
  std::ostringstream out;
  out << "{\"kind\":\"nnue_vs_hce\",\"fen_before\":\"" << json_escape(board.to_fen())
      << "\",\"depth\":" << options.depth
      << ",\"objective_only\":true,\"time_management\":false,\"search_controls\":"
      << controls_json(options) << ",\"hce\":";
  write_search_record(out, board, hce);
  out << ",\"nnue\":";
  write_search_record(out, board, nnue);
  return out.str() + '}';
}

std::string static_audit_json(const Board& board) {
  std::ostringstream out;
  out << "{\"kind\":\"static_root_child_nnue_audit\",\"fen_before\":\""
      << json_escape(board.to_fen())
      << "\",\"score_perspective\":\"child side-to-move\",\"sign_convention\":\"raw_nnue and rounded_cp are positive for the child side to move; root_perspective values negate them because each listed move changes side to move\",\"root_side_to_move\":\""
      << (board.side_to_move() == Color::White ? "white" : "black") << "\",\"moves\":[";
  const std::vector<Move> legal = generate_legal_moves(board);
  for (std::size_t index = 0; index < legal.size(); ++index) {
    if (index != 0) out << ',';
    Board child = board;
    child.make_move(legal[index]);
    const auto raw = evaluate_nnue_network_raw(child);
    out << "{\"move\":\"" << json_escape(move_to_uci(legal[index])) << "\",\"raw_nnue\":";
    if (!raw) {
      out << "null,\"rounded_cp\":null,\"root_perspective_raw_nnue\":null,\"root_perspective_rounded_cp\":null";
    } else {
      const int rounded = static_cast<int>(std::lround(*raw));
      out << std::setprecision(9) << *raw << ",\"rounded_cp\":" << rounded
          << ",\"root_perspective_raw_nnue\":" << -*raw
          << ",\"root_perspective_rounded_cp\":" << -rounded;
    }
    out << '}';
  }
  return out.str() + "]}";
}

void write_output(const std::optional<std::filesystem::path>& output, const std::string& json) {
  if (!output) return;
  if (!output->parent_path().empty()) std::filesystem::create_directories(output->parent_path());
  std::ofstream file(*output);
  if (!file) throw std::runtime_error("cannot write output: " + output->string());
  file << json << '\n';
}

bool needs_nnue(const Options& options) {
  return options.command == Command::Compare || options.command == Command::StaticAudit ||
      (options.command == Command::Trace || options.command == Command::Sweep) &&
          options.trace_mode == EvalMode::NNUE;
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    const Options options = parse_options(argc, argv);
    const auto board = Board::from_fen(options.fen);
    if (!board) throw std::runtime_error("invalid FEN");
    if (needs_nnue(options)) {
      if (!options.network) throw std::runtime_error("--network is required for NNUE diagnostic commands");
      std::string error;
      if (!load_nnue_network(options.network->string(), error))
        throw std::runtime_error("cannot load NNUE network: " + error);
    }
    std::string json;
    switch (options.command) {
      case Command::Trace: json = trace_json(*board, options); break;
      case Command::Sweep: json = sweep_json(*board, options); break;
      case Command::Compare: json = compare_json(*board, options); break;
      case Command::StaticAudit: json = static_audit_json(*board); break;
    }
    write_output(options.output, json);
    if (options.command == Command::Trace) std::cout << "[JJUGLE BLUNDER TRACE] ";
    std::cout << json << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "blunder diagnostic error: " << error.what() << '\n';
    return 2;
  }
}
