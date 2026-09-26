#include <algorithm>
#include <cmath>
#include <chrono>
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
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "chess/eval.hpp"
#include "chess/movegen.hpp"
#include "chess/nnue.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

enum class Command { Trace, Sweep, Compare, StaticAudit, NullTrace, OracleCurve, Corpus, RootVerify };

struct Options {
  Command command{Command::Trace};
  std::string fen;
  std::optional<std::filesystem::path> network;
  std::optional<std::filesystem::path> output;
  int depth{8};
  bool objective_only{false};
  int corpus_size{100};
  int corpus_control_size{20};
  std::uint64_t corpus_seed{0x9040c5d4ULL};
  EvalMode trace_mode{EvalMode::NNUE};
  std::optional<std::string> prepared_root;
  std::optional<int> reuse_previous_depth;
  std::optional<std::string> forced_root;
  std::vector<std::string> shadow_null_event_ids;
  std::vector<std::string> shadow_null_position_fens;
  DiagnosticNullMatchLevel shadow_match_level{DiagnosticNullMatchLevel::Exact};
  int shadow_position_depth{-1};
  int shadow_position_reduction{-1};
  DiagnosticTtMode tt_mode{DiagnosticTtMode::Normal};
  bool use_tt{true};
  bool use_null_move{true};
  bool use_lmr{true};
  bool use_pvs{true};
  bool use_see_pruning{true};
  bool use_aspiration{true};
  bool include_nmp_off_reference{false};
  DiagnosticNullMovePolicy null_move_policy{DiagnosticNullMovePolicy::Production};
  DiagnosticOracleTtMode oracle_tt_mode{DiagnosticOracleTtMode::Seeded};
  std::vector<std::string> oracle_event_ids;
  std::optional<std::pair<int, int>> oracle_window;
  int oracle_ply{0};
  int vclean_depth{3};
  std::string root_strategy{"all"};
  std::vector<std::string> root_candidates{"c5d4", "f7f5", "h5h4", "f7f6", "g6g5"};
  bool explicit_root_candidates{false};
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

std::string oracle_tt_mode_name(DiagnosticOracleTtMode mode) {
  switch (mode) {
    case DiagnosticOracleTtMode::Seeded: return "seeded";
    case DiagnosticOracleTtMode::Counterfactual: return "counterfactual";
    case DiagnosticOracleTtMode::Clean: return "clean";
    case DiagnosticOracleTtMode::CleanNoTt: return "clean-no-tt";
    case DiagnosticOracleTtMode::MoveOnly: return "move-only";
  }
  return "unknown";
}

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

std::string null_move_policy_name(DiagnosticNullMovePolicy policy) {
  switch (policy) {
    case DiagnosticNullMovePolicy::Production: return "production";
    case DiagnosticNullMovePolicy::DisableNoHeavyTwoMinors: return "a_disable_two_minors";
    case DiagnosticNullMovePolicy::DisableNoHeavySideOneMinor: return "b_disable_side_one_minor";
    case DiagnosticNullMovePolicy::ContinueOnLowMaterialFailHigh: return "c_continue_fail_high";
    case DiagnosticNullMovePolicy::VerifyLowMaterialFailHigh: return "d_verify_fail_high";
    case DiagnosticNullMovePolicy::VerifyLowMaterialFailHighNullFree: return "e_verify_null_free";
    case DiagnosticNullMovePolicy::SkipNoHeavyTwoMinorsDepthSix: return "f_skip_depth6";
    case DiagnosticNullMovePolicy::SkipNoHeavyTwoMinorsDepthThree: return "n1_skip_depth3";
    case DiagnosticNullMovePolicy::SkipNoHeavyTwoMinorsDepthFour: return "n2_skip_depth4";
    case DiagnosticNullMovePolicy::SkipNoHeavyTwoMinorsDepthFive: return "n3_skip_depth5";
    case DiagnosticNullMovePolicy::VerifyReducedLowMaterialDepthMinusReduction: return "vr_depth_minus_r";
    case DiagnosticNullMovePolicy::VerifyReducedLowMaterialDepthMinusOne: return "v1_depth_minus_1";
    case DiagnosticNullMovePolicy::VerifyReducedLowMaterialDepthMinusReductionPlusOne: return "vr1_depth_minus_r_plus_1";
    case DiagnosticNullMovePolicy::VerifyNoHeavyTwoMinorsDepthSixNullFree: return "g_verify_depth6_null_free";
    case DiagnosticNullMovePolicy::VerifyNoHeavyTwoMinorsDepthSixMargin71NullFree: return "h_verify_depth6_margin71_null_free";
    case DiagnosticNullMovePolicy::VerifyNoHeavyTwoMinorsDepthSixWidth70NullFree: return "i_verify_depth6_width70_null_free";
    case DiagnosticNullMovePolicy::AdmissionStaticEval: return "j_admission_static_eval";
    case DiagnosticNullMovePolicy::AdmissionLowMaterialStaticEval: return "k_admission_low_material_static_eval";
    case DiagnosticNullMovePolicy::AdmissionLowMaterialStaticEvalMargin16: return "l16_admission_static_eval";
    case DiagnosticNullMovePolicy::AdmissionLowMaterialStaticEvalMargin32: return "l32_admission_static_eval";
    case DiagnosticNullMovePolicy::AdmissionLowMaterialStaticEvalMargin64: return "l64_admission_static_eval";
    case DiagnosticNullMovePolicy::ReductionOneLowMaterialDepthSix: return "m_reduction_one";
    case DiagnosticNullMovePolicy::VerifyLowMaterialFailHighClean: return "vclean";
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
    else if (flag == "--objective-only") options.objective_only = true;
    else if (flag == "--include-nmp-off-reference") options.include_nmp_off_reference = true;
    else if (flag == "--corpus-size") options.corpus_size = parse_positive(next(index, "--corpus-size"), "--corpus-size");
    else if (flag == "--control-size") options.corpus_control_size = parse_positive(next(index, "--control-size"), "--control-size");
    else if (flag == "--seed") options.corpus_seed = std::stoull(next(index, "--seed"));
    else if (flag == "--shadow-null-id")
      options.shadow_null_event_ids.push_back(next(index, "--shadow-null-id"));
    else if (flag == "--shadow-null-position")
      options.shadow_null_position_fens.push_back(next(index, "--shadow-null-position"));
    else if (flag == "--position-match-level") {
      const std::string level = next(index, "--position-match-level");
      if (level == "exact") options.shadow_match_level = DiagnosticNullMatchLevel::Exact;
      else if (level == "position-depth") options.shadow_match_level = DiagnosticNullMatchLevel::PositionDepth;
      else if (level == "position") options.shadow_match_level = DiagnosticNullMatchLevel::Position;
      else throw std::runtime_error("--position-match-level accepts exact, position-depth, or position");
    } else if (flag == "--position-depth")
      options.shadow_position_depth = std::stoi(next(index, "--position-depth"));
    else if (flag == "--position-reduction")
      options.shadow_position_reduction = std::stoi(next(index, "--position-reduction"));
    else if (flag == "--tt-mode") {
      const std::string mode = next(index, "--tt-mode");
      if (mode == "normal") options.tt_mode = DiagnosticTtMode::Normal;
      else if (mode == "disabled") { options.tt_mode = DiagnosticTtMode::Disabled; options.use_tt = false; }
      else if (mode == "readonly") options.tt_mode = DiagnosticTtMode::ReadOnly;
      else if (mode == "writeonly") options.tt_mode = DiagnosticTtMode::WriteOnly;
      else if (mode == "move-hints") options.tt_mode = DiagnosticTtMode::MoveHintsOnly;
      else if (mode == "bounds-only") options.tt_mode = DiagnosticTtMode::BoundsOnly;
      else if (mode == "clear-depth") options.tt_mode = DiagnosticTtMode::ClearBetweenDepths;
      else if (mode == "clear-retry") options.tt_mode = DiagnosticTtMode::ClearBetweenRetries;
      else throw std::runtime_error("--tt-mode accepts normal, disabled, readonly, writeonly, move-hints, bounds-only, clear-depth, or clear-retry");
    }
    else if (flag == "--oracle-tt-mode") {
      const std::string mode = next(index, "--oracle-tt-mode");
      if (mode == "seeded") options.oracle_tt_mode = DiagnosticOracleTtMode::Seeded;
      else if (mode == "counterfactual") options.oracle_tt_mode = DiagnosticOracleTtMode::Counterfactual;
      else if (mode == "clean") options.oracle_tt_mode = DiagnosticOracleTtMode::Clean;
      else if (mode == "clean-no-tt") options.oracle_tt_mode = DiagnosticOracleTtMode::CleanNoTt;
      else if (mode == "move-only") options.oracle_tt_mode = DiagnosticOracleTtMode::MoveOnly;
      else throw std::runtime_error("--oracle-tt-mode accepts seeded, counterfactual, clean, clean-no-tt, or move-only");
    } else if (flag == "--oracle-event-id") {
      options.oracle_event_ids.push_back(next(index, "--oracle-event-id"));
    } else if (flag == "--oracle-window") {
      const std::string value = next(index, "--oracle-window");
      const std::size_t comma = value.find(',');
      if (comma == std::string::npos) throw std::runtime_error("--oracle-window expects alpha,beta");
      options.oracle_window = std::pair{std::stoi(value.substr(0, comma)),
                                        std::stoi(value.substr(comma + 1))};
    } else if (flag == "--oracle-ply") {
      options.oracle_ply = std::stoi(next(index, "--oracle-ply"));
    } else if (flag == "--vclean-depth") {
      options.vclean_depth = parse_positive(next(index, "--vclean-depth"), "--vclean-depth");
    } else if (flag == "--root-strategy") {
      options.root_strategy = next(index, "--root-strategy");
      if (options.root_strategy != "candidates" && options.root_strategy != "r1" &&
          options.root_strategy != "r2" && options.root_strategy != "top2" &&
          options.root_strategy != "top6" &&
          options.root_strategy != "r3" &&
          options.root_strategy != "rall" && options.root_strategy != "all")
        throw std::runtime_error("--root-strategy accepts candidates, r1, r2, top2, top6, r3, rall, or all");
    } else if (flag == "--root-candidate") {
      options.root_candidates.push_back(next(index, "--root-candidate"));
      options.explicit_root_candidates = true;
    }
    else if (flag == "--prepared-root") options.prepared_root = next(index, "--prepared-root");
    else if (flag == "--reuse-previous-depth") {
      options.reuse_previous_depth = parse_positive(
          next(index, "--reuse-previous-depth"), "--reuse-previous-depth");
    }
    else if (flag == "--command") {
      const std::string command = next(index, "--command");
      if (command == "trace") options.command = Command::Trace;
      else if (command == "sweep") options.command = Command::Sweep;
      else if (command == "compare") options.command = Command::Compare;
      else if (command == "static") options.command = Command::StaticAudit;
      else if (command == "null-trace") options.command = Command::NullTrace;
      else if (command == "oracle-curve") options.command = Command::OracleCurve;
      else if (command == "corpus") options.command = Command::Corpus;
      else if (command == "root-verify") options.command = Command::RootVerify;
      else throw std::runtime_error("--command accepts trace, sweep, compare, static, null-trace, oracle-curve, corpus, or root-verify");
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
    } else if (flag == "--nmp-policy") {
      const std::string policy = next(index, "--nmp-policy");
      if (policy == "production") options.null_move_policy = DiagnosticNullMovePolicy::Production;
      else if (policy == "a") options.null_move_policy = DiagnosticNullMovePolicy::DisableNoHeavyTwoMinors;
      else if (policy == "b") options.null_move_policy = DiagnosticNullMovePolicy::DisableNoHeavySideOneMinor;
      else if (policy == "c") options.null_move_policy = DiagnosticNullMovePolicy::ContinueOnLowMaterialFailHigh;
      else if (policy == "d") options.null_move_policy = DiagnosticNullMovePolicy::VerifyLowMaterialFailHigh;
      else if (policy == "e") options.null_move_policy = DiagnosticNullMovePolicy::VerifyLowMaterialFailHighNullFree;
      else if (policy == "f") options.null_move_policy = DiagnosticNullMovePolicy::SkipNoHeavyTwoMinorsDepthSix;
      else if (policy == "n1") options.null_move_policy = DiagnosticNullMovePolicy::SkipNoHeavyTwoMinorsDepthThree;
      else if (policy == "n2") options.null_move_policy = DiagnosticNullMovePolicy::SkipNoHeavyTwoMinorsDepthFour;
      else if (policy == "n3") options.null_move_policy = DiagnosticNullMovePolicy::SkipNoHeavyTwoMinorsDepthFive;
      else if (policy == "vr") options.null_move_policy = DiagnosticNullMovePolicy::VerifyReducedLowMaterialDepthMinusReduction;
      else if (policy == "v1") options.null_move_policy = DiagnosticNullMovePolicy::VerifyReducedLowMaterialDepthMinusOne;
      else if (policy == "vr1") options.null_move_policy = DiagnosticNullMovePolicy::VerifyReducedLowMaterialDepthMinusReductionPlusOne;
      else if (policy == "g") options.null_move_policy = DiagnosticNullMovePolicy::VerifyNoHeavyTwoMinorsDepthSixNullFree;
      else if (policy == "h") options.null_move_policy = DiagnosticNullMovePolicy::VerifyNoHeavyTwoMinorsDepthSixMargin71NullFree;
      else if (policy == "i") options.null_move_policy = DiagnosticNullMovePolicy::VerifyNoHeavyTwoMinorsDepthSixWidth70NullFree;
      else if (policy == "j") options.null_move_policy = DiagnosticNullMovePolicy::AdmissionStaticEval;
      else if (policy == "k") options.null_move_policy = DiagnosticNullMovePolicy::AdmissionLowMaterialStaticEval;
      else if (policy == "l16") options.null_move_policy = DiagnosticNullMovePolicy::AdmissionLowMaterialStaticEvalMargin16;
      else if (policy == "l32") options.null_move_policy = DiagnosticNullMovePolicy::AdmissionLowMaterialStaticEvalMargin32;
      else if (policy == "l64") options.null_move_policy = DiagnosticNullMovePolicy::AdmissionLowMaterialStaticEvalMargin64;
      else if (policy == "m") options.null_move_policy = DiagnosticNullMovePolicy::ReductionOneLowMaterialDepthSix;
      else if (policy == "vclean") options.null_move_policy = DiagnosticNullMovePolicy::VerifyLowMaterialFailHighClean;
      else throw std::runtime_error("--nmp-policy accepts production, a through i, j, k, l16, l32, l64, m, n1, n2, n3, vr, vr1, v1, or vclean");
    } else if (flag == "--force-root") {
      options.forced_root = next(index, "--force-root");
    } else if (flag == "--help") {
      std::cout << "Usage: HebiChessBlunderDiagnostic --fen <FEN> [--network model.hebinnue]"
                   " [--command trace|sweep|compare|static|null-trace|corpus|root-verify] [--depth N] [--eval nnue|hce]"
                   " [--root-strategy candidates|r1|r2|top2|top6|r3|rall|all --root-candidate uci]"
                   " [--corpus-size N --seed N]"
                   " [--force-root uci]"
                   " [--prepared-root uci --reuse-previous-depth N]"
                   " [--disable CONTROL] [--nmp-policy production|a|b|c|d|e|f|g|h|i|j|k|l16|l32|l64|m|vclean]"
                   " [--vclean-depth N] [--oracle-tt-mode seeded|counterfactual|clean|clean-no-tt|move-only]"
                   " [--oracle-event-id ID] [--oracle-window alpha,beta --oracle-ply N]"
                   " [--output result.json]\\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown option: " + flag);
    }
  }
  if (options.fen.empty() && options.command != Command::Corpus)
    throw std::runtime_error("--fen is required except for --command corpus");
  if (options.prepared_root.has_value() != options.reuse_previous_depth.has_value())
    throw std::runtime_error("--prepared-root and --reuse-previous-depth must be supplied together");
  if (options.shadow_match_level == DiagnosticNullMatchLevel::PositionDepth &&
      (options.shadow_null_position_fens.empty() || options.shadow_position_depth < 0 ||
       options.shadow_position_reduction < 0))
    throw std::runtime_error("position-depth matching requires --shadow-null-position, --position-depth, and --position-reduction");
  return options;
}

SearchLimits make_limits(const Board& board, const Options& options, EvalMode mode, int depth,
                         bool use_root_style_selection) {
  SearchLimits limits;
  limits.max_depth = depth;
  limits.eval_mode = mode;
  limits.eval_mode = mode;
  limits.use_root_style_selection = use_root_style_selection;
  limits.use_tt = options.use_tt;
  limits.use_null_move = options.use_null_move;
  limits.use_lmr = options.use_lmr;
  limits.use_pvs = options.use_pvs;
  limits.use_see_pruning = options.use_see_pruning;
  limits.use_aspiration = options.use_aspiration;
  limits.null_move_diagnostic_policy = options.null_move_policy;
  limits.null_shadow_event_ids = options.shadow_null_event_ids;
  limits.null_shadow_position_fens = options.shadow_null_position_fens;
  limits.null_shadow_match_level = options.shadow_match_level;
  limits.null_shadow_position_depth = options.shadow_position_depth;
  limits.null_shadow_position_reduction = options.shadow_position_reduction;
  limits.diagnostic_tt_mode = options.tt_mode;
  limits.diagnostic_oracle_tt_mode = options.oracle_tt_mode;
  limits.null_oracle_filter_event_ids = options.oracle_event_ids;
  limits.diagnostic_vclean_depth = options.vclean_depth;
  if (options.prepared_root) {
    const auto move = parse_uci_move(board, *options.prepared_root);
    if (!move) throw std::runtime_error("--prepared-root is not a legal root move: " +
                                        *options.prepared_root);
    // Deliberately diagnostic-only injection.  The production UCI path does
    // not parse these options and never reaches this code.
    limits.reuse_hit = true;
    limits.has_prepared_root_move = true;
    limits.prepared_root_move = *move;
    limits.reuse_previous_depth = *options.reuse_previous_depth;
  }
  // No deadline or soft deadline is ever configured in this harness.
  return limits;
}

SearchResult run_search(const Board& board, const Options& options, EvalMode mode,
                        int depth, bool use_root_style_selection) {
  clear_transposition_table();
  clear_search_heuristics();
  if (options.tt_mode == DiagnosticTtMode::ReadOnly) {
    Options seed_options = options;
    seed_options.tt_mode = DiagnosticTtMode::Normal;
    seed_options.shadow_null_event_ids.clear();
    seed_options.shadow_null_position_fens.clear();
    const SearchResult seed = search(board, make_limits(
        board, seed_options, mode, depth, use_root_style_selection));
    (void)seed;
    clear_search_heuristics();
  }
  return search(board, make_limits(board, options, mode, depth, use_root_style_selection));
}

void seed_tt_for_readonly(const Board& board, const Options& options) {
  if (options.tt_mode != DiagnosticTtMode::ReadOnly) return;
  Options seed_options = options;
  seed_options.tt_mode = DiagnosticTtMode::Normal;
  seed_options.shadow_null_event_ids.clear();
  seed_options.shadow_null_position_fens.clear();
  (void)search(board, make_limits(board, seed_options, options.trace_mode,
                                  options.depth, true));
  clear_search_heuristics();
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

std::string string_array_json(const std::vector<std::string>& values) {
  std::ostringstream out;
  out << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) out << ',';
    out << '"' << json_escape(values[index]) << '"';
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
        << "\",\"search_alpha_cp\":" << item.search_alpha
        << ",\"search_beta_cp\":" << item.search_beta
        << ",\"pvs_full_research\":" << (item.pvs_full_research ? "true" : "false")
        << ",\"style_score\":" << item.style_score
        << ",\"style_safe\":" << (item.style_safe ? "true" : "false")
        << ",\"style_tolerance_cp\":" << item.style_tolerance
        << ",\"verification\":\"" << proof_name(item.style_proof)
        << "\",\"pv\":" << pv_json(extract_principal_variation(board, item.move)) << '}';
  }
  out << ']';
}

#if defined(HEBICHESS_BLUNDER_DIAGNOSTIC)
void write_diagnostic_iterations(std::ostream& out, const SearchResult& result) {
  out << '[';
  for (std::size_t index = 0; index < result.diagnostic_iterations.size(); ++index) {
    if (index != 0) out << ',';
    const DiagnosticIterationSummary& iteration = result.diagnostic_iterations[index];
    out << "{\"depth\":" << iteration.depth << ",\"score_cp\":" << iteration.score
        << ",\"nodes\":" << iteration.nodes << ",\"qnodes\":" << iteration.qnodes
        << ",\"tt_hits\":" << iteration.tt_hits
        << ",\"tt_cutoffs\":" << iteration.tt_cutoffs
        << ",\"tt_stores\":" << iteration.tt_stores << ",\"windows\":[";
    for (std::size_t window = 0; window < iteration.aspiration_windows.size(); ++window) {
      if (window != 0) out << ',';
      out << '[' << iteration.aspiration_windows[window].first << ','
          << iteration.aspiration_windows[window].second << ']';
    }
    out << "],\"root_order\":[";
    for (std::size_t move = 0; move < iteration.root_order.size(); ++move) {
      if (move != 0) out << ',';
      out << '"' << json_escape(move_to_uci(iteration.root_order[move])) << '"';
    }
    out << "],\"alpha_after_each_root_move\":[";
    for (std::size_t move = 0; move < iteration.alpha_after_each_root_move.size(); ++move) {
      if (move != 0) out << ',';
      out << iteration.alpha_after_each_root_move[move];
    }
    out << "],\"root_candidates\":[";
    for (std::size_t move = 0; move < iteration.root_candidates.size(); ++move) {
      if (move != 0) out << ',';
      const RootMoveInfo& candidate = iteration.root_candidates[move];
      out << "{\"move\":\"" << json_escape(move_to_uci(candidate.move))
          << "\",\"score_cp\":" << candidate.search_score
          << ",\"bound\":\"" << bound_name(candidate.bound) << "\"}";
    }
    out << "]}";
  }
  out << ']';
}
#endif

void write_search_record(std::ostream& out, const Board& board, const SearchResult& result) {
  out << "{\"bestmove\":\"" << json_escape(move_to_uci(result.best_move))
      << "\",\"score_cp\":" << result.score
      << ",\"completed_depth\":" << result.completed_depth
      << ",\"attempted_depth\":" << result.attempted_depth
      << ",\"elapsed_ms\":" << result.time_elapsed_ms
      << ",\"nodes\":" << result.nodes
      << ",\"qnodes\":" << result.qnodes
      << ",\"aspiration_retries\":" << result.aspiration_retries
      << ",\"null_attempts\":" << result.null_attempts
      << ",\"null_cutoffs\":" << result.null_cutoffs
      << ",\"tt_probes\":" << result.tt_probes
      << ",\"tt_hits\":" << result.tt_hits
      << ",\"tt_cutoffs\":" << result.tt_cutoffs
      << ",\"tt_stores\":" << result.tt_stores
      << ",\"null_policy_skips\":" << result.null_policy_skips
      << ",\"null_policy_rejected_cutoffs\":" << result.null_policy_rejected_cutoffs
      << ",\"null_policy_verification_searches\":" << result.null_policy_verification_searches
      << ",\"null_policy_verified_cutoffs\":" << result.null_policy_verified_cutoffs
      << ",\"null_policy_verification_nodes\":" << result.null_policy_verification_nodes
      << ",\"null_policy_verification_qnodes\":" << result.null_policy_verification_qnodes
      << ",\"null_policy_admission_evaluations\":" << result.null_policy_admission_evaluations
      << ",\"null_policy_admission_rejections\":" << result.null_policy_admission_rejections
      << ",\"null_policy_reduction_overrides\":" << result.null_policy_reduction_overrides
      << ",\"null_shadow_matches\":" << result.null_shadow_matches
      << ",\"null_shadow_suppressions\":" << result.null_shadow_suppressions
      << ",\"null_shadow_matched_ids\":"
      << string_array_json(result.null_shadow_matched_event_ids)
      << ",\"null_shadow_propagation_event_id\":\""
      << json_escape(result.null_shadow_propagation_event_id)
      << "\",\"null_shadow_propagation\":[";
  for (std::size_t index = 0; index < result.null_shadow_propagation.size(); ++index) {
    if (index != 0) out << ',';
    const auto& step = result.null_shadow_propagation[index];
    out << "{\"ply\":" << step.ply << ",\"depth\":" << step.depth
        << ",\"alpha\":" << step.alpha << ",\"beta\":" << step.beta
        << ",\"returned_score\":" << step.returned_score
        << ",\"bound\":\"" << bound_name(step.bound) << "\"}";
  }
  out << ']'
      << ",\"root_move_ordering_first\":\""
      << json_escape(result.root_moves.empty() ? "none" : move_to_uci(result.root_moves.front().move))
      << "\",\"nps\":" << (result.time_elapsed_ms > 0
          ? result.nodes * 1000 / static_cast<std::uint64_t>(result.time_elapsed_ms) : 0)
      << ",\"stop_reason\":\"" << json_escape(result.time_stop_reason)
      << "\",\"pv\":" << pv_json(result.principal_variation)
      << ",\"root_candidates\":";
  write_root_candidates(out, board, result);
#if defined(HEBICHESS_BLUNDER_DIAGNOSTIC)
  out << ",\"diagnostic_iterations\":";
  write_diagnostic_iterations(out, result);
#endif
  out << '}';
}

std::string controls_json(const Options& options) {
  const char* tt_mode = "normal";
  switch (options.tt_mode) {
    case DiagnosticTtMode::Normal: tt_mode = "normal"; break;
    case DiagnosticTtMode::Disabled: tt_mode = "disabled"; break;
    case DiagnosticTtMode::ReadOnly: tt_mode = "read_only_seeded_from_normal_search"; break;
    case DiagnosticTtMode::WriteOnly: tt_mode = "write_only"; break;
    case DiagnosticTtMode::MoveHintsOnly: tt_mode = "move_hints_only"; break;
    case DiagnosticTtMode::BoundsOnly: tt_mode = "bounds_only"; break;
    case DiagnosticTtMode::ClearBetweenDepths: tt_mode = "clear_between_depths"; break;
    case DiagnosticTtMode::ClearBetweenRetries: tt_mode = "clear_between_retries"; break;
  }
  std::ostringstream out;
  out << "{\"tt\":" << (options.use_tt ? "true" : "false")
      << ",\"tt_mode\":\"" << tt_mode << "\""
      << ",\"null_move\":" << (options.use_null_move ? "true" : "false")
      << ",\"lmr\":" << (options.use_lmr ? "true" : "false")
      << ",\"pvs\":" << (options.use_pvs ? "true" : "false")
      << ",\"see_pruning\":" << (options.use_see_pruning ? "true" : "false")
      << ",\"aspiration\":" << (options.use_aspiration ? "true" : "false")
      << ",\"nmp_policy\":\"" << null_move_policy_name(options.null_move_policy)
      << "\"}";
  return out.str();
}

std::string reuse_json(const Options& options, const SearchResult& result) {
  std::ostringstream out;
  out << "{\"reuse_hit\":" << (result.reuse_hit ? "true" : "false")
      << ",\"has_prepared_root_move\":" << (options.prepared_root ? "true" : "false")
      << ",\"prepared_root_move\":";
  if (options.prepared_root) out << '"' << json_escape(*options.prepared_root) << '"';
  else out << "null";
  out << ",\"reuse_previous_depth\":";
  if (options.reuse_previous_depth) out << *options.reuse_previous_depth;
  else out << "null";
  return out.str() + '}';
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
      << ",\"search_controls\":" << controls_json(options)
      << ",\"reuse_state\":" << reuse_json(options, result) << ',';
  const std::string record = [&] { std::ostringstream value; write_search_record(value, board, result); return value.str(); }();
  // Keep the required trace fields top-level while sharing the record writer.
  out << record.substr(1);
  return out.str();
}

std::string root_verification_json(const Board& board, const Options& options) {
  const SearchResult production = run_search(board, options, options.trace_mode,
                                             options.depth, false);
  int queens_rooks = 0;
  int minors = 0;
  for (const Piece& piece : board.squares()) {
    queens_rooks += piece.type == PieceType::Queen || piece.type == PieceType::Rook;
    minors += piece.type == PieceType::Bishop || piece.type == PieceType::Knight;
  }
  const bool eligible = queens_rooks == 0 && minors <= 2;

  std::vector<const RootMoveInfo*> ranked;
  for (const RootMoveInfo& item : production.root_moves) ranked.push_back(&item);
  std::stable_sort(ranked.begin(), ranked.end(), [](const auto* left, const auto* right) {
    return left->search_score > right->search_score;
  });

  std::unordered_map<std::string, RootCandidateVerificationResult> verified;
  auto verify = [&](const RootMoveInfo* candidate) -> const RootCandidateVerificationResult& {
    const std::string uci = move_to_uci(candidate->move);
    auto found = verified.find(uci);
    if (found == verified.end()) {
      auto result = verify_root_candidate_null_free_for_diagnostic(
          board, candidate->move, production.completed_depth, options.trace_mode);
      found = verified.emplace(uci, std::move(result)).first;
    }
    return found->second;
  };
  auto run_order = [&](const std::string& strategy) {
    std::vector<const RootMoveInfo*> order;
    if (!eligible) return order;
    if (strategy == "r1") {
      const RootMoveInfo* best = find_root(production, production.best_move);
      if (best != nullptr) order.push_back(best);
    } else if (strategy == "r2") {
      int current_best = -MATE_SCORE;
      for (std::size_t index = 0; index < ranked.size(); ++index) {
        const RootMoveInfo* candidate = ranked[index];
        const auto& result = verify(candidate);
        order.push_back(candidate);
        current_best = std::max(current_best, result.score);
        int strongest_unverified_ceiling = -MATE_SCORE;
        for (std::size_t remaining = index + 1; remaining < ranked.size(); ++remaining) {
          const RootMoveInfo* other = ranked[remaining];
          if (verified.contains(move_to_uci(other->move))) continue;
          if (other->bound == ScoreBound::Lower) {
            strongest_unverified_ceiling = MATE_SCORE;
            break;
          }
          strongest_unverified_ceiling = std::max(strongest_unverified_ceiling,
                                                   other->search_score);
        }
        if (current_best > strongest_unverified_ceiling) break;
      }
    } else if (strategy == "r3") {
      for (std::size_t index = 0; index < std::min<std::size_t>(3, ranked.size()); ++index)
        order.push_back(ranked[index]);
    } else if (strategy == "top2") {
      for (std::size_t index = 0; index < std::min<std::size_t>(2, ranked.size()); ++index)
        order.push_back(ranked[index]);
    } else if (strategy == "top6") {
      for (std::size_t index = 0; index < std::min<std::size_t>(6, ranked.size()); ++index)
        order.push_back(ranked[index]);
    } else if (strategy == "rall") {
      order = ranked;
    }
    for (const auto* candidate : order) (void)verify(candidate);
    return order;
  };

  std::vector<std::string> strategies;
  if (options.root_strategy == "all") strategies = {"r1", "top2", "r2", "r3", "rall"};
  else if (options.root_strategy == "candidates") strategies = {"candidates"};
  else strategies = {options.root_strategy};
  std::unordered_map<std::string, std::vector<const RootMoveInfo*>> strategy_orders;
  for (const std::string& strategy : strategies)
    if (strategy != "candidates") strategy_orders.emplace(strategy, run_order(strategy));

  std::vector<const RootMoveInfo*> requested;
  if (eligible && (options.root_strategy == "candidates" || options.root_strategy == "all")) {
    for (const std::string& uci : options.root_candidates) {
      const auto parsed = parse_uci_move(board, uci);
      if (!parsed) {
        if (options.explicit_root_candidates)
          throw std::runtime_error("--root-candidate is not legal: " + uci);
        continue;
      }
      const RootMoveInfo* candidate = find_root(production, *parsed);
      if (candidate == nullptr) {
        if (options.explicit_root_candidates)
          throw std::runtime_error("candidate missing from completed root iteration: " + uci);
        continue;
      }
      requested.push_back(candidate);
      (void)verify(candidate);
    }
  }

  std::uint64_t verification_nodes = 0, verification_qnodes = 0, verification_ms = 0;
  for (const auto& [move, result] : verified) {
    (void)move;
    verification_nodes += result.nodes;
    verification_qnodes += result.qnodes;
    verification_ms += result.elapsed_ms;
  }
  std::optional<SearchResult> nmp_off_reference;
  if (options.include_nmp_off_reference) {
    Options reference_options = options;
    reference_options.use_null_move = false;
    nmp_off_reference = run_search(board, reference_options, options.trace_mode,
                                   options.depth, false);
  }
  std::ostringstream out;
  out << "{\"kind\":\"root_candidate_verification\",\"fen\":\""
      << json_escape(board.to_fen()) << "\",\"eval_mode\":\""
      << mode_name(options.trace_mode) << "\",\"depth\":" << production.completed_depth
      << ",\"eligibility\":{\"low_material_no_qr_minors_le2\":"
      << (eligible ? "true" : "false") << ",\"total_minors\":" << minors
      << ",\"heavy_pieces\":" << queens_rooks << "}"
      << ",\"production\":{\"bestmove\":\"" << move_to_uci(production.best_move)
      << "\",\"score_cp\":" << production.score << ",\"nodes\":" << production.nodes
      << ",\"qnodes\":" << production.qnodes << ",\"elapsed_ms\":" << production.time_elapsed_ms
      << ",\"aspiration_retries\":" << production.aspiration_retries
      << ",\"root_candidates\":";
  write_root_candidates(out, board, production);
  std::array<int, 5> within_gap{};
  int published_gap = 0;
  if (ranked.size() > 1) {
    published_gap = ranked[0]->search_score - ranked[1]->search_score;
    constexpr int gaps[] = {16, 32, 64, 128, 256};
    for (const auto* candidate : ranked)
      for (std::size_t index = 0; index < std::size(gaps); ++index)
        if (ranked[0]->search_score - candidate->search_score <= gaps[index])
          ++within_gap[index];
  }
  out << "},\"published_root_score_gaps\":{\"best_second_cp\":" << published_gap
      << ",\"candidate_count_within\":{\"16\":" << within_gap[0]
      << ",\"32\":" << within_gap[1] << ",\"64\":" << within_gap[2]
      << ",\"128\":" << within_gap[3] << ",\"256\":" << within_gap[4] << "}},\"verified_candidates\":[";
  bool first = true;
  for (const auto* candidate : requested) {
    if (!first) out << ',';
    first = false;
    const auto& result = verify(candidate);
    out << "{\"move\":\"" << move_to_uci(candidate->move)
        << "\",\"production_score_cp\":" << candidate->search_score
        << ",\"production_bound\":\"" << bound_name(candidate->bound)
        << "\",\"verified_score_cp\":" << result.score
        << ",\"verified_bound\":\"" << bound_name(result.bound)
        << "\",\"nodes\":" << result.nodes << ",\"qnodes\":" << result.qnodes
        << ",\"elapsed_ms\":" << result.elapsed_ms << ",\"pv\":" << pv_json(result.pv) << '}';
  }
  out << "],\"strategies\":{";
  bool first_strategy = true;
  for (const auto& [strategy, order] : strategy_orders) {
    if (!first_strategy) out << ',';
    first_strategy = false;
    int best_score = -MATE_SCORE;
    std::string best_move;
    std::uint64_t nodes = 0, qnodes = 0, elapsed = 0;
    for (const auto* candidate : order) {
      const auto& result = verify(candidate);
      if (result.score > best_score) { best_score = result.score; best_move = move_to_uci(candidate->move); }
      nodes += result.nodes; qnodes += result.qnodes; elapsed += result.elapsed_ms;
    }
    out << '"' << strategy << "\":{\"verified_count\":" << order.size()
        << ",\"verification_order\":[";
    for (std::size_t i = 0; i < order.size(); ++i) {
      if (i) out << ',';
      out << '"' << move_to_uci(order[i]->move) << '"';
    }
    out << "],\"bestmove\":";
    if (best_move.empty()) out << "null"; else out << '"' << best_move << '"';
    out << ",\"score_cp\":" << (best_move.empty() ? 0 : best_score)
        << ",\"verification_nodes\":" << nodes << ",\"verification_qnodes\":" << qnodes
        << ",\"verification_ms\":" << elapsed << ",\"candidate_results\":[";
    for (std::size_t i = 0; i < order.size(); ++i) {
      if (i) out << ',';
      const auto& result = verify(order[i]);
      out << "{\"move\":\"" << move_to_uci(order[i]->move)
          << "\",\"score_cp\":" << result.score << ",\"nodes\":" << result.nodes
          << ",\"qnodes\":" << result.qnodes << ",\"elapsed_ms\":" << result.elapsed_ms << '}';
    }
    out << "]}";
  }
  out << "},\"verification_totals\":{\"unique_candidates\":" << verified.size()
      << ",\"nodes\":" << verification_nodes << ",\"qnodes\":" << verification_qnodes
      << ",\"elapsed_ms\":" << verification_ms << "}";
  if (nmp_off_reference) {
    out << ",\"full_nmp_off_reference\":{\"bestmove\":\""
        << move_to_uci(nmp_off_reference->best_move) << "\",\"score_cp\":"
        << nmp_off_reference->score << ",\"nodes\":" << nmp_off_reference->nodes
        << ",\"qnodes\":" << nmp_off_reference->qnodes
        << ",\"elapsed_ms\":" << nmp_off_reference->time_elapsed_ms
        << ",\"aspiration_retries\":" << nmp_off_reference->aspiration_retries << '}';
  }
  out << '}';
  return out.str();
}

std::string null_trace_json(const Board& board, const Options& options) {
  clear_transposition_table();
  clear_search_heuristics();
  seed_tt_for_readonly(board, options);
  std::vector<NullMoveTrace> events;
  std::vector<NullMoveVerificationTrace> verifications;
  set_null_move_trace_callback_for_diagnostic(
      [&events](const NullMoveTrace& trace) { events.push_back(trace); });
  set_null_move_verification_trace_callback_for_diagnostic(
      [&verifications](const NullMoveVerificationTrace& trace) {
        verifications.push_back(trace);
      });
  const SearchLimits limits = make_limits(board, options, options.trace_mode,
                                          options.depth, !options.objective_only);
  SearchResult result;
  if (options.forced_root) {
    const auto move = parse_uci_move(board, *options.forced_root);
    if (!move) throw std::runtime_error("--force-root is not a legal root move: " +
                                        *options.forced_root);
    result = search_forced_root_move_for_null_diagnostic(board, *move, limits);
  } else {
    result = search(board, limits);
  }
  set_null_move_trace_callback_for_diagnostic({});
  set_null_move_verification_trace_callback_for_diagnostic({});
  std::uint64_t cutoffs = 0;
  std::uint64_t oracle_checks = 0;
  std::uint64_t false_cutoffs = 0;
  for (const NullMoveTrace& event : events) {
    cutoffs += event.cutoff;
    oracle_checks += event.oracle_score.has_value();
    false_cutoffs += event.false_cutoff;
  }
  std::ostringstream out;
  out << "{\"kind\":\"null_move_trace\",\"fen_before\":\""
      << json_escape(board.to_fen()) << "\",\"eval_mode\":\""
      << mode_name(options.trace_mode) << "\",\"oracle_tt_mode\":\""
      << oracle_tt_mode_name(options.oracle_tt_mode) << "\",\"search_controls\":"
      << controls_json(options) << ",\"objective_only\":"
      << (options.objective_only ? "true" : "false") << ",\"forced_root\":";
  if (options.forced_root) out << '\"' << json_escape(*options.forced_root) << '\"';
  else out << "null";
  out << ",\"totals\":{\"null_attempts\":" << events.size()
      << ",\"null_cutoffs\":" << cutoffs
      << ",\"oracle_checks\":" << oracle_checks
      << ",\"false_null_cutoffs\":" << false_cutoffs
      << ",\"nodes\":" << result.nodes << ",\"qnodes\":" << result.qnodes
      << ",\"aspiration_retries\":" << result.aspiration_retries
      << ",\"admission_evaluations\":" << result.null_policy_admission_evaluations
      << ",\"admission_rejections\":" << result.null_policy_admission_rejections
      << ",\"reduction_overrides\":" << result.null_policy_reduction_overrides
      << "},\"result\":";
  write_search_record(out, board, result);
  out << ",\"events\":[";
  std::uint64_t cutoff_sequence = 0;
  for (std::size_t index = 0; index < events.size(); ++index) {
    if (index != 0) out << ',';
    const NullMoveTrace& event = events[index];
    out << "{\"attempt_index\":" << (index + 1)
        << ",\"cutoff_sequence\":" << (event.cutoff ? ++cutoff_sequence : 0)
        << ",\"event_occurrence\":" << event.event_occurrence
        << ",\"fen\":\"" << json_escape(event.fen) << "\",\"event_id\":\""
        << event.event_id << "\",\"root_move\":";
    if (event.has_root_move) out << '\"' << move_to_uci(event.root_move) << '\"';
    else out << "null";
    out << ",\"ply\":" << event.ply << ",\"depth\":" << event.depth
        << ",\"alpha\":" << event.alpha << ",\"beta\":" << event.beta
        << ",\"reduction\":" << event.reduction << ",\"null_score\":"
        << event.null_score << ",\"event_id\":\"" << event.event_id
        << "\",\"cutoff\":" << (event.cutoff ? "true" : "false")
        << ",\"side_to_move\":\""
        << (event.side_to_move == Color::White ? "white" : "black")
        << "\",\"material\":\"" << json_escape(event.material)
        << "\",\"legal_move_count\":" << event.legal_move_count
        << ",\"legal_king_moves\":" << event.legal_king_moves
        << ",\"legal_pawn_moves\":" << event.legal_pawn_moves
        << ",\"legal_minor_moves\":" << event.legal_minor_moves
        << ",\"legal_non_pawn_non_king_moves\":" << event.legal_non_pawn_non_king_moves
        << ",\"legal_captures\":" << event.legal_captures
        << ",\"legal_quiet_moves\":" << event.legal_quiet_moves
        << ",\"pawn_advance_moves\":" << event.pawn_advance_moves
        << ",\"stm_has_pawn_push\":" << (event.stm_has_pawn_push ? "true" : "false")
        << ",\"stm_has_capture\":" << (event.stm_has_capture ? "true" : "false")
        << ",\"passed_pawns_white\":" << event.passed_pawns_white
        << ",\"passed_pawns_black\":" << event.passed_pawns_black
        << ",\"connected_passed_pawns_white\":" << event.connected_passed_pawns_white
        << ",\"connected_passed_pawns_black\":" << event.connected_passed_pawns_black
        << ",\"material_imbalance_cp\":" << event.material_imbalance_cp
        << ",\"stm_non_pawn_material_cp\":" << event.stm_non_pawn_material_cp
        << ",\"stm_in_check\":" << (event.stm_in_check ? "true" : "false")
        << ",\"static_eval_cp\":";
    if (event.static_eval_cp) out << *event.static_eval_cp; else out << "null";
    out << ",\"static_eval_minus_beta\":";
    if (event.static_eval_cp) out << *event.static_eval_cp - event.beta; else out << "null";
    out << ",\"static_eval_minus_alpha\":";
    if (event.static_eval_cp) out << *event.static_eval_cp - event.alpha; else out << "null";
    out << ",\"null_score_minus_static_eval\":";
    if (event.static_eval_cp) out << event.null_score - *event.static_eval_cp; else out << "null";
    out << ",\"oracle_score\":";
    if (event.oracle_score) out << *event.oracle_score; else out << "null";
    out << ",\"oracle_reaches_beta\":"
        << (event.oracle_reaches_beta ? "true" : "false")
        << ",\"false_cutoff\":" << (event.false_cutoff ? "true" : "false")
        << ",\"tt_probe_hit\":" << (event.tt_probe_hit ? "true" : "false")
        << ",\"tt_entry_depth\":" << event.tt_entry_depth
        << ",\"tt_bound\":\"" << json_escape(event.tt_bound) << "\""
        << ",\"tt_score_cp\":";
    if (event.tt_score_cp) out << *event.tt_score_cp; else out << "null";
    out << ",\"caller_alpha\":" << event.caller_alpha
        << ",\"caller_beta\":" << event.caller_beta
        << ",\"effective_alpha\":" << event.effective_alpha
        << ",\"effective_beta\":" << event.effective_beta
        << ",\"tt_raised_alpha\":" << (event.tt_raised_alpha ? "true" : "false")
        << ",\"tt_lowered_beta\":" << (event.tt_lowered_beta ? "true" : "false")
        << ",\"tt_window_changed\":" << (event.tt_window_changed ? "true" : "false")
        << ",\"tt_move_present\":" << (event.tt_move_present ? "true" : "false")
        << ",\"tt_move_ordering_only\":" << (event.tt_move_ordering_only ? "true" : "false")
        << ",\"tt_would_cutoff\":" << (event.tt_would_cutoff ? "true" : "false")
        << ",\"oracle_tt_mode\":\"" << json_escape(event.oracle_tt_mode)
        << "\",\"oracle_initial_tt_hit\":" << (event.oracle_initial_tt_hit ? "true" : "false")
        << ",\"oracle_initial_tt_entry_depth\":" << event.oracle_initial_tt_entry_depth
        << ",\"oracle_initial_tt_bound\":\"" << json_escape(event.oracle_initial_tt_bound) << "\""
        << ",\"oracle_initial_tt_score\":";
    if (event.oracle_initial_tt_score) out << *event.oracle_initial_tt_score; else out << "null";
    out << ",\"oracle_initial_tt_caused_cutoff\":"
        << (event.oracle_initial_tt_caused_cutoff ? "true" : "false")
        << ",\"oracle_tt_probes\":" << event.oracle_tt_probes
        << ",\"oracle_tt_hits\":" << event.oracle_tt_hits
        << ",\"oracle_tt_cutoffs\":" << event.oracle_tt_cutoffs
        << ",\"oracle_nodes\":" << event.oracle_nodes
        << ",\"oracle_qnodes\":" << event.oracle_qnodes
        << ",\"oracle_returned_bound\":\""
        << json_escape(event.oracle_returned_bound) << "\"}";
  }
  out << "],\"verification_events\":[";
  for (std::size_t index = 0; index < verifications.size(); ++index) {
    if (index != 0) out << ',';
    const NullMoveVerificationTrace& event = verifications[index];
    out << "{\"event_id\":\"" << event.event_id << "\",\"fen\":\""
        << json_escape(event.fen) << "\",\"root_move\":";
    if (event.has_root_move) out << '"' << move_to_uci(event.root_move) << '"';
    else out << "null";
    out << ",\"ply\":" << event.ply << ",\"depth\":" << event.depth
        << ",\"reduction\":" << event.reduction << ",\"alpha\":" << event.alpha
        << ",\"beta\":" << event.beta << ",\"null_score\":" << event.null_score
        << ",\"verification_depth\":" << event.verification_depth
        << ",\"verification_score\":" << event.verification_score
        << ",\"accepted\":" << (event.accepted ? "true" : "false")
        << ",\"verification_nodes\":" << event.verification_nodes
        << ",\"verification_qnodes\":" << event.verification_qnodes
        << ",\"full_oracle_score\":";
    if (event.full_oracle_score) out << *event.full_oracle_score;
    else out << "null";
    const bool oracle_reaches_beta = event.full_oracle_score.has_value() &&
                                     *event.full_oracle_score >= event.beta;
    out << ",\"oracle_reaches_beta\":" << (oracle_reaches_beta ? "true" : "false")
        << ",\"classification\":\"";
    if (!event.full_oracle_score) out << "missing_oracle";
    else if (oracle_reaches_beta && event.accepted) out << "correctly_confirmed_true";
    else if (oracle_reaches_beta && !event.accepted) out << "unnecessarily_rejected_true";
    else if (!oracle_reaches_beta && event.accepted) out << "incorrectly_confirmed_false";
    else out << "correctly_rejected_false";
    out << "\"}";
  }
  return out.str() + "]}";
}

std::string oracle_curve_json(const Board& board, const Options& options) {
  if (!options.oracle_window)
    throw std::runtime_error("oracle-curve requires --oracle-window alpha,beta");
  std::ostringstream out;
  out << "{\"kind\":\"null_free_oracle_curve\",\"fen\":\""
      << json_escape(board.to_fen()) << "\",\"eval_mode\":\""
      << mode_name(options.trace_mode) << "\",\"tt_mode\":\""
      << oracle_tt_mode_name(options.oracle_tt_mode) << "\",\"ply\":"
      << options.oracle_ply << ",\"alpha\":" << options.oracle_window->first
      << ",\"beta\":" << options.oracle_window->second << ",\"runs\":[";
  for (int depth = 1; depth <= options.depth; ++depth) {
    if (depth != 1) out << ',';
    const auto started = std::chrono::steady_clock::now();
    const DiagnosticOracleRunResult result = run_null_free_oracle_for_diagnostic(
        board, depth, options.oracle_window->first, options.oracle_window->second,
        options.trace_mode, options.oracle_tt_mode, options.oracle_ply);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    out << "{\"depth\":" << depth << ",\"score\":" << result.score
        << ",\"bound\":\"" << bound_name(result.bound) << "\",\"reaches_beta\":"
        << (result.reaches_beta ? "true" : "false") << ",\"nodes\":" << result.nodes
        << ",\"qnodes\":" << result.qnodes << ",\"tt_probes\":" << result.tt_probes
        << ",\"tt_hits\":" << result.tt_hits << ",\"tt_cutoffs\":"
        << result.tt_cutoffs << ",\"elapsed_ms\":" << elapsed << '}';
  }
  return out.str() + "]}";
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

struct CorpusPosition {
  Board board{};
  std::string category{};
};

struct CorpusSearch {
  SearchResult result{};
  std::vector<NullMoveTrace> events{};
};

std::array<int, 6> corpus_material(const Board& board) {
  // P, N, B, R, Q, K.
  std::array<int, 6> counts{};
  for (const Piece piece : board.squares()) {
    switch (piece.type) {
      case PieceType::Pawn: ++counts[0]; break;
      case PieceType::Knight: ++counts[1]; break;
      case PieceType::Bishop: ++counts[2]; break;
      case PieceType::Rook: ++counts[3]; break;
      case PieceType::Queen: ++counts[4]; break;
      case PieceType::King: ++counts[5]; break;
      default: break;
    }
  }
  return counts;
}

std::string corpus_category(const Board& board) {
  std::array<int, 6> w{}, b{};
  for (const Piece piece : board.squares()) {
    auto& counts = piece.color == Color::White ? w : b;
    switch (piece.type) {
      case PieceType::Pawn: ++counts[0]; break;
      case PieceType::Knight: ++counts[1]; break;
      case PieceType::Bishop: ++counts[2]; break;
      case PieceType::Rook: ++counts[3]; break;
      case PieceType::Queen: ++counts[4]; break;
      default: break;
    }
  }
  if (w[2] && b[2] && !w[1] && !b[1]) return "bishop_pawns_vs_bishop_pawns";
  if (w[1] && b[1] && !w[2] && !b[2]) return "knight_pawns_vs_knight_pawns";
  if ((w[1] && b[2] && !w[2] && !b[1]) ||
      (w[2] && b[1] && !w[1] && !b[2])) return "bishop_vs_knight_pawns";
  const int white_minors = w[1] + w[2], black_minors = b[1] + b[2];
  if ((white_minors == 1 && black_minors == 0) ||
      (black_minors == 1 && white_minors == 0)) return "one_minor_vs_pawns";
  if (white_minors + black_minors == 0) return "pawns_only";
  return "mixed_low_material";
}

std::uint64_t next_random(std::uint64_t& state) {
  state ^= state >> 12;
  state ^= state << 25;
  state ^= state >> 27;
  return state * 0x2545f4914f6cdd1dULL;
}

CorpusSearch run_corpus_search(const Board& board, DiagnosticOracleTtMode mode) {
  CorpusSearch output;
  SearchLimits limits;
  limits.max_depth = 6;
  limits.eval_mode = EvalMode::NNUE;
  limits.use_root_style_selection = false;
  limits.diagnostic_oracle_tt_mode = mode;
  limits.deadline_check_interval_nodes = 0;
  clear_transposition_table();
  clear_search_heuristics();
  set_null_move_trace_callback_for_diagnostic(
      [&output](const NullMoveTrace& event) { output.events.push_back(event); });
  try {
    output.result = search(board, limits);
    set_null_move_trace_callback_for_diagnostic({});
  } catch (...) {
    set_null_move_trace_callback_for_diagnostic({});
    throw;
  }
  return output;
}

std::string corpus_json(const Options& options) {
  std::uint64_t random_state = options.corpus_seed == 0 ? 1 : options.corpus_seed;
  std::vector<CorpusPosition> low_positions;
  std::vector<CorpusPosition> control_positions;
  std::unordered_set<ZobristKey> low_keys, control_keys;
  std::unordered_map<std::string, std::size_t> low_category_counts;
  const std::unordered_map<std::string, std::size_t> category_caps = {
      {"bishop_pawns_vs_bishop_pawns", 12},
      {"knight_pawns_vs_knight_pawns", 5},
      {"bishop_vs_knight_pawns", 8},
      {"one_minor_vs_pawns", 65},
      {"mixed_low_material", 10},
      {"pawns_only", 0},
  };
  const auto bishop_fixture = Board::from_fen(
      "8/2b2p2/2p3p1/p1k4p/K7/1B4P1/7P/8 b - - 0 1");
  if (!bishop_fixture) throw std::runtime_error("known legal bishop endgame fixture is invalid");
  const std::size_t kControlCount = static_cast<std::size_t>(options.corpus_control_size);
  for (std::uint64_t game = 0;
       (low_positions.size() < static_cast<std::size_t>(options.corpus_size) ||
        control_positions.size() < kControlCount) && game < 3000; ++game) {
    Board board = game % 2 == 0 ? Board::initial() : *bishop_fixture;
    for (int ply = 0; ply < 240; ++ply) {
      const auto material = corpus_material(board);
      const bool no_heavy = material[3] == 0 && material[4] == 0;
      const int nonking = material[0] + material[1] + material[2] + material[3] + material[4];
      const bool low_eligible = no_heavy && nonking <= 10 && material[0] >= 2 &&
                                nonking >= 3 && material[1] + material[2] >= 1;
      const bool control_eligible = !no_heavy && nonking >= 12 && ply >= 8;
      const std::uint64_t chance = next_random(random_state);
      const std::string category = corpus_category(board);
      const auto cap = category_caps.find(category);
      if (low_eligible && cap != category_caps.end() &&
          low_category_counts[category] < cap->second && (chance & 3U) == 0 &&
          low_keys.insert(board.zobrist_key()).second) {
        low_positions.push_back({board, category});
        ++low_category_counts[category];
        if (low_positions.size() >= static_cast<std::size_t>(options.corpus_size) &&
            control_positions.size() >= kControlCount) break;
      }
      if (control_positions.size() < kControlCount && control_eligible && (chance & 15U) == 0 &&
          control_keys.insert(board.zobrist_key()).second) {
        control_positions.push_back({board, "heavy_piece_control"});
      }
      auto legal = generate_legal_moves(board);
      if (legal.empty()) break;
      std::vector<Move> captures;
      for (const Move& move : legal)
        if (move.flag == MoveFlag::Capture || move.flag == MoveFlag::EnPassant ||
            move.flag == MoveFlag::PromotionCapture) captures.push_back(move);
      const std::uint64_t draw = next_random(random_state);
      const Move move = !captures.empty() && draw % 100 < 65
          ? captures[next_random(random_state) % captures.size()]
          : legal[next_random(random_state) % legal.size()];
      board.make_move(move);
    }
  }
  if (low_positions.size() < static_cast<std::size_t>(options.corpus_size))
    throw std::runtime_error("deterministic legal playout generator found only " +
        std::to_string(low_positions.size()) + " low-material positions; lower --corpus-size");

  struct JoinedEvent { const NullMoveTrace* cf{}; const NullMoveTrace* clean{}; };
  std::vector<std::string> position_json;
  std::vector<std::string> cutoff_json;
  std::unordered_map<std::string, std::uint64_t> class_counts;
  std::unordered_map<std::string, std::uint64_t> attempt_rule_counts;
  std::uint64_t total_attempts = 0;
  std::size_t sequence = 0;
  auto analyze_position = [&](const CorpusPosition& position, bool low) {
    const CorpusSearch cf = run_corpus_search(position.board,
        DiagnosticOracleTtMode::Counterfactual);
    const CorpusSearch clean = run_corpus_search(position.board,
        DiagnosticOracleTtMode::Clean);
    if (cf.result.best_move != clean.result.best_move || cf.result.score != clean.result.score)
      throw std::runtime_error("oracle mode changed primary corpus search result for " +
                               position.board.to_fen());
    for (const NullMoveTrace& attempt : cf.events) {
      ++total_attempts;
      const auto node = Board::from_fen(attempt.fen);
      if (!node) throw std::runtime_error("trace emitted an invalid position FEN");
      std::array<std::array<int, 5>, 2> counts{};
      for (const Piece piece : node->squares()) {
        int type_index = -1;
        switch (piece.type) {
          case PieceType::Pawn: type_index = 0; break;
          case PieceType::Knight: type_index = 1; break;
          case PieceType::Bishop: type_index = 2; break;
          case PieceType::Rook: type_index = 3; break;
          case PieceType::Queen: type_index = 4; break;
          default: break;
        }
        if (type_index >= 0) ++counts[piece.color == Color::White ? 0 : 1][type_index];
      }
      const int minors = counts[0][1] + counts[0][2] + counts[1][1] + counts[1][2];
      const int stm = attempt.side_to_move == Color::White ? 0 : 1;
      const int stm_minors = counts[stm][1] + counts[stm][2];
      const bool no_heavy = counts[0][3] + counts[1][3] + counts[0][4] + counts[1][4] == 0;
      const bool risk_material = no_heavy && minors <= 2;
      if (risk_material) ++attempt_rule_counts["no_qr_minors_le2"];
      if (risk_material && attempt.legal_move_count <= 8)
        ++attempt_rule_counts["no_qr_minors_le2_legal_le8"];
      if (risk_material && attempt.legal_pawn_moves <= 1)
        ++attempt_rule_counts["no_qr_minors_le2_pawn_moves_le1"];
      if (risk_material && attempt.legal_pawn_moves == 0)
        ++attempt_rule_counts["no_qr_minors_le2_pawn_moves_eq0"];
      if (no_heavy && stm_minors <= 1 && attempt.legal_move_count <= 8)
        ++attempt_rule_counts["no_qr_stm_minors_le1_legal_le8"];
    }
    std::unordered_map<std::string, const NullMoveTrace*> clean_by_id;
    for (const auto& event : clean.events) if (event.cutoff) clean_by_id[event.event_id] = &event;
    std::size_t cutoffs = 0;
    for (const auto& event : cf.events) {
      if (!event.cutoff) continue;
      ++cutoffs;
      const auto found = clean_by_id.find(event.event_id);
      if (found == clean_by_id.end() || !event.oracle_score || !found->second->oracle_score)
        continue;
      const NullMoveTrace& clean_event = *found->second;
      const bool cf_false = *event.oracle_score < event.beta;
      const bool clean_false = *clean_event.oracle_score < clean_event.beta;
      const std::string label = cf_false && clean_false ? "robust_false" :
          (!cf_false && !clean_false ? "robust_true" : "state_sensitive");
      ++class_counts[label];
      const std::string category = low ? position.category : "heavy_piece_control";
      const auto event_board = Board::from_fen(event.fen);
      if (!event_board) throw std::runtime_error("trace emitted an invalid position FEN");
      std::array<std::array<int, 5>, 2> side_counts{}; // P/N/B/R/Q.
      for (const Piece piece : event_board->squares()) {
        int type_index = -1;
        switch (piece.type) {
          case PieceType::Pawn: type_index = 0; break;
          case PieceType::Knight: type_index = 1; break;
          case PieceType::Bishop: type_index = 2; break;
          case PieceType::Rook: type_index = 3; break;
          case PieceType::Queen: type_index = 4; break;
          default: break;
        }
        if (type_index >= 0)
          ++side_counts[piece.color == Color::White ? 0 : 1][type_index];
      }
      const int stm_index = event.side_to_move == Color::White ? 0 : 1;
      const int white_minors = side_counts[0][1] + side_counts[0][2];
      const int black_minors = side_counts[1][1] + side_counts[1][2];
      std::ostringstream row;
      row << "{\"position_index\":" << sequence << ",\"category\":\""
          << category << "\",\"event_id\":\"" << event.event_id
          << "\",\"event_occurrence\":" << event.event_occurrence
          << ",\"fen\":\"" << json_escape(event.fen)
          << "\",\"root_move\":\"" << (event.has_root_move ? move_to_uci(event.root_move) : "")
          << "\",\"ply\":" << event.ply << ",\"depth\":" << event.depth
          << ",\"alpha\":" << event.alpha << ",\"beta\":" << event.beta
          << ",\"window_width\":" << event.beta - event.alpha
          << ",\"reduction\":" << event.reduction
          << ",\"null_score\":" << event.null_score
          << ",\"null_margin\":" << event.null_score - event.beta
          << ",\"white_pawns\":" << side_counts[0][0]
          << ",\"white_knights\":" << side_counts[0][1]
          << ",\"white_bishops\":" << side_counts[0][2]
          << ",\"white_rooks\":" << side_counts[0][3]
          << ",\"white_queens\":" << side_counts[0][4]
          << ",\"black_pawns\":" << side_counts[1][0]
          << ",\"black_knights\":" << side_counts[1][1]
          << ",\"black_bishops\":" << side_counts[1][2]
          << ",\"black_rooks\":" << side_counts[1][3]
          << ",\"black_queens\":" << side_counts[1][4]
          << ",\"total_pawns\":" << side_counts[0][0] + side_counts[1][0]
          << ",\"stm_pawns\":" << side_counts[stm_index][0]
          << ",\"opponent_pawns\":" << side_counts[1 - stm_index][0]
          << ",\"total_minors\":" << white_minors + black_minors
          << ",\"stm_minors\":" << side_counts[stm_index][1] + side_counts[stm_index][2]
          << ",\"total_pieces\":" << side_counts[0][0] + side_counts[0][1] +
               side_counts[0][2] + side_counts[0][3] + side_counts[0][4] +
               side_counts[1][0] + side_counts[1][1] + side_counts[1][2] +
               side_counts[1][3] + side_counts[1][4]
          << ",\"has_queens\":" << ((side_counts[0][4] + side_counts[1][4]) ? "true" : "false")
          << ",\"has_rooks\":" << ((side_counts[0][3] + side_counts[1][3]) ? "true" : "false")
          << ",\"counterfactual_score\":" << *event.oracle_score
          << ",\"counterfactual_gap\":" << event.beta - *event.oracle_score
          << ",\"clean_score\":" << *clean_event.oracle_score
          << ",\"clean_gap\":" << event.beta - *clean_event.oracle_score
          << ",\"label\":\"" << label << "\",\"legal_moves\":"
          << event.legal_move_count << ",\"king_moves\":" << event.legal_king_moves
          << ",\"pawn_moves\":" << event.legal_pawn_moves
          << ",\"minor_moves\":" << event.legal_minor_moves
          << ",\"captures\":" << event.legal_captures
          << ",\"quiet_moves\":" << event.legal_quiet_moves
          << ",\"pawn_pushes\":" << event.pawn_advance_moves
          << ",\"passed_white\":" << event.passed_pawns_white
          << ",\"passed_black\":" << event.passed_pawns_black
          << ",\"stm_non_pawn_material_cp\":" << event.stm_non_pawn_material_cp
          << ",\"static_eval_cp\":";
      if (event.static_eval_cp) row << *event.static_eval_cp;
      else row << "null";
      row << ",\"static_eval_minus_beta\":";
      if (event.static_eval_cp) row << *event.static_eval_cp - event.beta;
      else row << "null";
      row << ",\"in_check\":" << (event.stm_in_check ? "true" : "false")
          << ",\"counterfactual_nodes\":" << event.oracle_nodes
          << ",\"counterfactual_qnodes\":" << event.oracle_qnodes
          << ",\"clean_nodes\":" << clean_event.oracle_nodes
          << ",\"clean_qnodes\":" << clean_event.oracle_qnodes << '}';
      cutoff_json.push_back(row.str());
    }
    std::ostringstream row;
    row << "{\"index\":" << sequence << ",\"category\":\""
        << (low ? position.category : "heavy_piece_control") << "\",\"fen\":\""
        << json_escape(position.board.to_fen()) << "\",\"bestmove\":\""
        << move_to_uci(cf.result.best_move) << "\",\"score_cp\":" << cf.result.score
        << ",\"nodes\":" << cf.result.nodes << ",\"qnodes\":" << cf.result.qnodes
        << ",\"null_attempts\":" << cf.result.null_attempts
        << ",\"null_cutoffs\":" << cf.result.null_cutoffs
        << ",\"counterfactual_cutoffs\":" << cf.result.null_cutoffs
        << ",\"clean_cutoffs\":" << clean.result.null_cutoffs << '}';
    position_json.push_back(row.str());
    ++sequence;
  };
  for (const auto& position : low_positions) analyze_position(position, true);
  for (const auto& position : control_positions) analyze_position(position, false);

  std::ostringstream out;
  out << "{\"kind\":\"legal_playout_nmp_corpus\",\"seed\":"
      << options.corpus_seed << ",\"depth\":6,\"low_material_positions\":"
      << low_positions.size() << ",\"control_positions\":" << control_positions.size()
      << ",\"total_nmp_attempts\":" << total_attempts
      << ",\"candidate_attempt_triggers\":{"
      << "\"no_qr_minors_le2\":" << attempt_rule_counts["no_qr_minors_le2"]
      << ",\"no_qr_minors_le2_legal_le8\":" << attempt_rule_counts["no_qr_minors_le2_legal_le8"]
      << ",\"no_qr_minors_le2_pawn_moves_le1\":" << attempt_rule_counts["no_qr_minors_le2_pawn_moves_le1"]
      << ",\"no_qr_minors_le2_pawn_moves_eq0\":" << attempt_rule_counts["no_qr_minors_le2_pawn_moves_eq0"]
      << ",\"no_qr_stm_minors_le1_legal_le8\":" << attempt_rule_counts["no_qr_stm_minors_le1_legal_le8"]
      << "}"
      << ",\"classification_counts\":{\"robust_false\":" << class_counts["robust_false"]
      << ",\"robust_true\":" << class_counts["robust_true"]
      << ",\"state_sensitive\":" << class_counts["state_sensitive"]
      << "},\"positions\":[";
  for (std::size_t i = 0; i < position_json.size(); ++i) {
    if (i) out << ',';
    out << position_json[i];
  }
  out << "],\"cutoffs\":[";
  for (std::size_t i = 0; i < cutoff_json.size(); ++i) {
    if (i) out << ',';
    out << cutoff_json[i];
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
      (options.command == Command::Trace || options.command == Command::Sweep ||
       options.command == Command::NullTrace || options.command == Command::RootVerify ||
       options.command == Command::Corpus) &&
          options.trace_mode == EvalMode::NNUE;
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    const Options options = parse_options(argc, argv);
    std::optional<Board> board;
    if (options.command != Command::Corpus) {
      board = Board::from_fen(options.fen);
      if (!board) throw std::runtime_error("invalid FEN");
    }
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
      case Command::NullTrace: json = null_trace_json(*board, options); break;
      case Command::OracleCurve: json = oracle_curve_json(*board, options); break;
      case Command::Corpus: json = corpus_json(options); break;
      case Command::RootVerify: json = root_verification_json(*board, options); break;
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
