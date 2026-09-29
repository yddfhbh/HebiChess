cmake_minimum_required(VERSION 3.16)
project(HebiChess LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CHESS_SOURCES
  src/chess/board.cpp src/chess/movegen.cpp src/chess/game_state.cpp src/chess/perft.cpp
  src/chess/eval.cpp src/chess/nnue.cpp src/chess/nnue_features.cpp
  src/chess/search.cpp src/chess/uci.cpp src/chess/zobrist.cpp src/chess/tt.cpp src/chess/see.cpp
  src/chess/uci_engine.cpp src/chess/opening_book.cpp
)

# Phase 6-4B final production configuration.  Keep the accepted NNUE/search
# definitions shared by production engine targets so they cannot drift.
set(HEBICHESS_PRODUCTION_QSEARCH_DEFINITIONS
  HEBICHESS_QSEARCH_DELTA_PRUNING=1
  HEBICHESS_QSEARCH_TT_VARIANT=2
  HEBICHESS_QSEARCH_TT_CUTOFF_MASK=7
  HEBICHESS_QSEARCH_TT_PROFILE=0
  HEBICHESS_QSEARCH_TT_DIAGNOSTIC=0
)
set(HEBICHESS_PRODUCTION_NNUE_SEARCH_DEFINITIONS
  HEBICHESS_QSEARCH_LAZY_CHECKS=0
  HEBICHESS_NNUE_HIDDEN1_VARIANT=8
  HEBICHESS_NMP_CANDIDATE_G=0
)
set(HEBICHESS_PRODUCTION_EVALCACHE_DEFINITIONS
  HEBICHESS_EVALCACHE_EXACT=1
  HEBICHESS_EVALCACHE_EXACT_WAYS=2
  HEBICHESS_EVALCACHE_EXACT_ENTRIES=8192
)
function(apply_hebichess_production_qsearch target)
  target_compile_definitions(${target} PRIVATE
    ${HEBICHESS_PRODUCTION_QSEARCH_DEFINITIONS}
  )
endfunction()
function(apply_hebichess_production_nnue_search target)
  apply_hebichess_production_qsearch(${target})
  target_compile_definitions(${target} PRIVATE
    ${HEBICHESS_PRODUCTION_NNUE_SEARCH_DEFINITIONS}
  )
endfunction()
function(apply_hebichess_production_evalcache target)
  target_compile_definitions(${target} PRIVATE
    ${HEBICHESS_PRODUCTION_EVALCACHE_DEFINITIONS}
  )
endfunction()

# Production-candidate A/B target.  It shares every accepted production
# configuration value with the default engine and differs only in the
# compile-time Null Move safeguard under investigation.
function(apply_hebichess_production_nnue_search_candidate_g target)
  apply_hebichess_production_qsearch(${target})
  target_compile_definitions(${target} PRIVATE
    HEBICHESS_QSEARCH_LAZY_CHECKS=0
    HEBICHESS_NNUE_HIDDEN1_VARIANT=8
    HEBICHESS_NMP_CANDIDATE_G=1
  )
endfunction()

# Fail configuration when the browser production target and its H1=8
# production-equivalent benchmark ever diverge in NNUE/search settings. Their
# module ABI and browser/Node definitions intentionally differ and are not
# compared here.
function(assert_hebichess_nnue_search_config_matches reference candidate)
  get_target_property(_reference_definitions ${reference} COMPILE_DEFINITIONS)
  get_target_property(_candidate_definitions ${candidate} COMPILE_DEFINITIONS)
  foreach(_required IN LISTS HEBICHESS_PRODUCTION_QSEARCH_DEFINITIONS
                             HEBICHESS_PRODUCTION_NNUE_SEARCH_DEFINITIONS
                             HEBICHESS_PRODUCTION_EVALCACHE_DEFINITIONS)
    list(FIND _reference_definitions "${_required}" _reference_index)
    list(FIND _candidate_definitions "${_required}" _candidate_index)
    if(_reference_index EQUAL -1 OR _candidate_index EQUAL -1)
      message(FATAL_ERROR
        "${reference} and ${candidate} must share ${_required} in their production NNUE/search/cache configuration")
    endif()
  endforeach()
endfunction()

add_executable(HebiChess
  src/main.cpp
  ${CHESS_SOURCES}
)

target_include_directories(HebiChess PRIVATE src)
apply_hebichess_production_nnue_search(HebiChess)
apply_hebichess_production_evalcache(HebiChess)

# Profiling-only native engine. Sampling timers and per-component clocks are
# compiled out of production/WASM targets; this target keeps the exact
# production search configuration while exposing sampled subsystem timing.
add_executable(HebiChessProfile
  src/main.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessProfile PRIVATE src)
apply_hebichess_production_nnue_search(HebiChessProfile)
target_compile_definitions(HebiChessProfile PRIVATE HEBICHESS_SEARCH_PROFILE=1)

add_executable(HebiChessProfileCounters
  src/main.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessProfileCounters PRIVATE src)
apply_hebichess_production_nnue_search(HebiChessProfileCounters)
target_compile_definitions(HebiChessProfileCounters PRIVATE
  HEBICHESS_SEARCH_PROFILE=1
  HEBICHESS_SEARCH_PROFILE_TIMING=0
)

# H1=4 native A/B companions. These are benchmark-only and deliberately match
# the production search/compiler configuration except for the dense H1 loop.
add_executable(HebiChessH1Variant4
  src/main.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessH1Variant4 PRIVATE src)
apply_hebichess_production_qsearch(HebiChessH1Variant4)
target_compile_definitions(HebiChessH1Variant4 PRIVATE
  HEBICHESS_QSEARCH_LAZY_CHECKS=0
  HEBICHESS_NNUE_HIDDEN1_VARIANT=4
  HEBICHESS_NMP_CANDIDATE_G=0
)

add_executable(HebiChessH1Variant4Profile
  src/main.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessH1Variant4Profile PRIVATE src)
apply_hebichess_production_qsearch(HebiChessH1Variant4Profile)
target_compile_definitions(HebiChessH1Variant4Profile PRIVATE
  HEBICHESS_QSEARCH_LAZY_CHECKS=0
  HEBICHESS_NNUE_HIDDEN1_VARIANT=4
  HEBICHESS_NMP_CANDIDATE_G=0
  HEBICHESS_SEARCH_PROFILE=1
)

add_executable(HebiChessNmpCandidateG
  src/main.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessNmpCandidateG PRIVATE src)
apply_hebichess_production_nnue_search_candidate_g(HebiChessNmpCandidateG)

option(HEBICHESS_BUILD_WASM "Build the browser WebAssembly engine" OFF)
set(HEBICHESS_WASM_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/web/public/engine" CACHE PATH "Output directory for the browser WebAssembly engine")
# Keep the WASM memory contract in one place. All browser, Node-test, and H1
# A/B artifacts use the same explicit stack reservation, preventing a stack
# configuration difference from contaminating an A/B result.
set(HEBICHESS_WASM_INITIAL_MEMORY 201326592)
set(HEBICHESS_WASM_MAXIMUM_MEMORY 402653184)
set(HEBICHESS_WASM_STACK_SIZE 2097152 CACHE STRING
    "WebAssembly stack size in bytes for every HebiChess WASM engine")
set(HEBICHESS_WASM_COMMON_LINK_FLAGS
    "-sINITIAL_MEMORY=${HEBICHESS_WASM_INITIAL_MEMORY} -sMAXIMUM_MEMORY=${HEBICHESS_WASM_MAXIMUM_MEMORY} -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=${HEBICHESS_WASM_STACK_SIZE}")
if(HEBICHESS_BUILD_WASM)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "HEBICHESS_BUILD_WASM requires Emscripten (configure with emcmake cmake)")
  endif()
  add_executable(HebiChessWasm src/wasm_bridge.cpp ${CHESS_SOURCES})
  target_include_directories(HebiChessWasm PRIVATE src)
  target_compile_definitions(HebiChessWasm PRIVATE HEBICHESS_WASM=1 HEBICHESS_BROWSER_WASM=1)
  apply_hebichess_production_nnue_search(HebiChessWasm)
  apply_hebichess_production_evalcache(HebiChessWasm)
  set_target_properties(HebiChessWasm PROPERTIES
    OUTPUT_NAME hebichess
    RUNTIME_OUTPUT_DIRECTORY "${HEBICHESS_WASM_OUTPUT_DIR}"
    SUFFIX ".js"
  LINK_FLAGS "--no-entry -O3 ${HEBICHESS_WASM_COMMON_LINK_FLAGS} -sMODULARIZE=1 -sEXPORT_NAME=createHebiChessModule -sENVIRONMENT=web,worker -sEXPORTED_FUNCTIONS=['_malloc','_free','_hebichess_initialize','_hebichess_send_command','_hebichess_take_output','_hebichess_nnue_load_bytes','_hebichess_nnue_clear','_hebichess_nnue_evaluate_fen_raw','_hebichess_book_load_bytes','_hebichess_book_clear','_hebichess_game_reset','_hebichess_game_load_fen','_hebichess_game_fen','_hebichess_game_legal_moves','_hebichess_game_apply','_hebichess_game_status'] -sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8']"
  )
endif()

option(HEBICHESS_BUILD_WASM_NODE_TEST "Build the test-only Node-compatible WebAssembly engine" OFF)
set(HEBICHESS_WASM_NODE_TEST_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build-wasm/node-test" CACHE PATH
    "Output directory for the Node-compatible production-equivalent WASM test engine")
if(HEBICHESS_BUILD_WASM_NODE_TEST)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "HEBICHESS_BUILD_WASM_NODE_TEST requires Emscripten (configure with emcmake cmake)")
  endif()
  add_executable(HebiChessWasmNodeTest src/wasm_bridge.cpp ${CHESS_SOURCES})
  target_include_directories(HebiChessWasmNodeTest PRIVATE src)
  target_compile_definitions(HebiChessWasmNodeTest PRIVATE HEBICHESS_WASM=1)
  apply_hebichess_production_nnue_search(HebiChessWasmNodeTest)
  apply_hebichess_production_evalcache(HebiChessWasmNodeTest)
  set_target_properties(HebiChessWasmNodeTest PROPERTIES
    OUTPUT_NAME hebichess-node
    RUNTIME_OUTPUT_DIRECTORY "${HEBICHESS_WASM_NODE_TEST_OUTPUT_DIR}"
    SUFFIX ".js"
  LINK_FLAGS "--no-entry -O3 ${HEBICHESS_WASM_COMMON_LINK_FLAGS} -sMODULARIZE=1 -sEXPORT_NAME=createHebiChessNodeTestModule -sENVIRONMENT=web,worker,node -sEXPORTED_FUNCTIONS=['_malloc','_free','_hebichess_initialize','_hebichess_send_command','_hebichess_take_output','_hebichess_nnue_load_bytes','_hebichess_book_load_bytes','_hebichess_book_clear','_hebichess_nnue_clear','_hebichess_nnue_evaluate_fen_raw','_hebichess_game_reset','_hebichess_game_load_fen','_hebichess_game_fen','_hebichess_game_legal_moves','_hebichess_game_apply','_hebichess_game_status'] -sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8']"
  )
endif()

# Windows-only H1 A/B artifacts.  Both targets use the production WASM
# memory/optimization settings and production QSearch configuration.  Their
# compile definitions intentionally differ in exactly one value: the H1 dense
# loop interleave width.  Keeping the JavaScript output name and factory name
# identical makes the Node harness exercise the same module ABI for both
# variants; separate directories keep the two artifacts side by side.
option(HEBICHESS_BUILD_WASM_H1_AB "Build Node-compatible H1=4/H1=8 WASM A/B engines" OFF)
set(HEBICHESS_WASM_H1_AB_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build-wasm/h1-ab" CACHE PATH
    "Output directory for Node-compatible WASM H1 A/B engines")
option(HEBICHESS_BUILD_WASM_H1_AB_DIAGNOSTIC
    "Add Emscripten assertions and stack-overflow checks to test-only H1 A/B WASM artifacts" OFF)
if(HEBICHESS_BUILD_WASM_H1_AB)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "HEBICHESS_BUILD_WASM_H1_AB requires Emscripten (configure with emcmake cmake)")
  endif()
  set(HEBICHESS_WASM_H1_AB_LINK_FLAGS "--no-entry -O3 ${HEBICHESS_WASM_COMMON_LINK_FLAGS} -sMODULARIZE=1 -sEXPORT_NAME=createHebiChessWasmH1BenchmarkModule -sENVIRONMENT=node -sEXPORTED_FUNCTIONS=['_malloc','_free','_hebichess_initialize','_hebichess_send_command','_hebichess_take_output','_hebichess_nnue_load_bytes','_hebichess_nnue_clear','_hebichess_nnue_evaluate_fen_raw','_hebichess_nnue_benchmark_fens','_hebichess_book_load_bytes','_hebichess_book_clear','_hebichess_game_reset','_hebichess_game_load_fen','_hebichess_game_fen','_hebichess_game_legal_moves','_hebichess_game_apply','_hebichess_game_status'] -sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8']")
  set(HEBICHESS_WASM_H1_AB_DIAGNOSTIC_LINK_FLAGS "")
  if(HEBICHESS_BUILD_WASM_H1_AB_DIAGNOSTIC)
    # Keep these checks out of every production artifact.  Both A/B variants
    # receive the identical instrumentation so their only variant-specific
    # compilation definition remains the H1 loop interleave width.
    set(HEBICHESS_WASM_H1_AB_DIAGNOSTIC_LINK_FLAGS
      "-sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2")
  endif()
  function(add_hebichess_wasm_h1_ab target variant)
    add_executable(${target} src/wasm_bridge.cpp ${CHESS_SOURCES})
    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PRIVATE
      HEBICHESS_WASM=1
      HEBICHESS_WASM_H1_AB_BENCHMARK=1
    )
    if(variant EQUAL 8)
      apply_hebichess_production_nnue_search(${target})
      apply_hebichess_production_evalcache(${target})
    else()
      # Retain the H1=4 reference artifact as an explicit baseline.  It must
      # not inherit the production H1=8 default.
      target_compile_definitions(${target} PRIVATE
        HEBICHESS_QSEARCH_LAZY_CHECKS=0
        HEBICHESS_NNUE_HIDDEN1_VARIANT=4
      )
      target_compile_definitions(${target} PRIVATE
        ${HEBICHESS_PRODUCTION_QSEARCH_DEFINITIONS}
      )
    endif()
    set_target_properties(${target} PROPERTIES
      OUTPUT_NAME hebichess
      RUNTIME_OUTPUT_DIRECTORY "${HEBICHESS_WASM_H1_AB_OUTPUT_DIR}/h1-${variant}"
      SUFFIX ".js"
      LINK_FLAGS "${HEBICHESS_WASM_H1_AB_LINK_FLAGS} ${HEBICHESS_WASM_H1_AB_DIAGNOSTIC_LINK_FLAGS}"
    )
  endfunction()
  add_hebichess_wasm_h1_ab(HebiChessWasmH1Interleaved4 4)
  add_hebichess_wasm_h1_ab(HebiChessWasmH1Interleaved8 8)
  if(HEBICHESS_BUILD_WASM)
    assert_hebichess_nnue_search_config_matches(
      HebiChessWasm HebiChessWasmH1Interleaved8)
    if(HEBICHESS_BUILD_WASM_NODE_TEST)
      assert_hebichess_nnue_search_config_matches(
        HebiChessWasm HebiChessWasmNodeTest)
    endif()
  endif()
endif()

add_executable(HebiChessTypesTest
  tests/chess_types_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessTypesTest PRIVATE src)

add_executable(HebiChessMovegenTest
  tests/movegen_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessMovegenTest PRIVATE src)

add_executable(HebiChessPerftTest
  tests/perft_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessPerftTest PRIVATE src)

add_executable(HebiChessSearchTest
  tests/search_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessSearchTest PRIVATE src)
target_compile_definitions(HebiChessSearchTest PRIVATE HEBICHESS_SEARCH_TT_WINDOW_TEST=1)

add_executable(HebiChessUciTest
  tests/uci_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessUciTest PRIVATE src)

add_executable(HebiChessTimeManagementTest tests/time_management_test.cpp)
target_include_directories(HebiChessTimeManagementTest PRIVATE src)

add_executable(HebiChessZobristTtTest
  tests/zobrist_tt_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessZobristTtTest PRIVATE src)

add_executable(HebiChessSeeTest
  tests/see_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessSeeTest PRIVATE src)

add_executable(HebiChessSeeBenchmark
  tests/see_benchmark.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessSeeBenchmark PRIVATE src)

add_executable(HebiChessSearchBaseline
  tests/search_baseline.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessSearchBaseline PRIVATE src)
apply_hebichess_production_qsearch(HebiChessSearchBaseline)
target_compile_definitions(HebiChessSearchBaseline PRIVATE
  HEBICHESS_QSEARCH_LAZY_CHECKS=0
  HEBICHESS_NNUE_HIDDEN1_VARIANT=4
)

# Separate diagnostic executable: its JSON harness is never linked into the
# production UCI target and introduces no production options or output.
add_executable(HebiChessBlunderDiagnostic
  tests/blunder_diagnostic.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessBlunderDiagnostic PRIVATE src)
apply_hebichess_production_nnue_search(HebiChessBlunderDiagnostic)
target_compile_definitions(HebiChessBlunderDiagnostic PRIVATE
  HEBICHESS_BLUNDER_DIAGNOSTIC=1
)

add_executable(HebiChessBlunderDiagnosticRegression
  tests/blunder_diagnostic_regression.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessBlunderDiagnosticRegression PRIVATE src)
apply_hebichess_production_nnue_search(HebiChessBlunderDiagnosticRegression)
target_compile_definitions(HebiChessBlunderDiagnosticRegression PRIVATE
  HEBICHESS_BLUNDER_DIAGNOSTIC=1
)

# Search-level H1 A/B targets.  These deliberately share the complete
# production QSearch configuration; only the compile-time hidden1 loop width
# differs.  Keep these as direct-search harnesses rather than engine targets.
add_executable(HebiChessSearchNnueH1Interleaved8
  tests/search_baseline.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessSearchNnueH1Interleaved8 PRIVATE src)
apply_hebichess_production_nnue_search(HebiChessSearchNnueH1Interleaved8)

add_executable(HebiChessSearchLazyChecks
  tests/search_baseline.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessSearchLazyChecks PRIVATE src)
target_compile_definitions(HebiChessSearchLazyChecks PRIVATE
  ${HEBICHESS_PRODUCTION_QSEARCH_DEFINITIONS}
)
target_compile_definitions(HebiChessSearchLazyChecks PRIVATE HEBICHESS_QSEARCH_LAZY_CHECKS=1)

add_executable(HebiChessSearchBaselineConfigTest
  tests/search_baseline_config_test.cpp
)
target_include_directories(HebiChessSearchBaselineConfigTest PRIVATE src)
apply_hebichess_production_qsearch(HebiChessSearchBaselineConfigTest)
target_compile_definitions(HebiChessSearchBaselineConfigTest PRIVATE
  HEBICHESS_QSEARCH_LAZY_CHECKS=0
  HEBICHESS_NNUE_HIDDEN1_VARIANT=4
  HEBICHESS_EXPECTED_NNUE_HIDDEN1_VARIANT=4
)

add_executable(HebiChessProductionNnueSearchConfigTest
  tests/search_baseline_config_test.cpp
)
target_include_directories(HebiChessProductionNnueSearchConfigTest PRIVATE src)
apply_hebichess_production_nnue_search(HebiChessProductionNnueSearchConfigTest)
apply_hebichess_production_evalcache(HebiChessProductionNnueSearchConfigTest)
target_compile_definitions(HebiChessProductionNnueSearchConfigTest PRIVATE
  HEBICHESS_EXPECTED_NNUE_HIDDEN1_VARIANT=8
  HEBICHESS_EXPECTED_NMP_CANDIDATE_G=0
  HEBICHESS_EXPECTED_EVALCACHE_EXACT=1
  HEBICHESS_EXPECTED_EVALCACHE_EXACT_WAYS=2
  HEBICHESS_EXPECTED_EVALCACHE_EXACT_ENTRIES=8192
)

add_executable(HebiChessNmpCandidateGTest
  tests/nmp_candidate_g_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessNmpCandidateGTest PRIVATE src)
apply_hebichess_production_nnue_search_candidate_g(HebiChessNmpCandidateGTest)

add_executable(HebiChessNmpProductionBenchmark
  tests/nmp_candidate_g_benchmark.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessNmpProductionBenchmark PRIVATE src)
apply_hebichess_production_nnue_search(HebiChessNmpProductionBenchmark)

add_executable(HebiChessNmpCandidateGBenchmark
  tests/nmp_candidate_g_benchmark.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessNmpCandidateGBenchmark PRIVATE src)
apply_hebichess_production_nnue_search_candidate_g(HebiChessNmpCandidateGBenchmark)

add_executable(HebiChessNmpCandidateGDiagnostic
  tests/blunder_diagnostic.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessNmpCandidateGDiagnostic PRIVATE src)
apply_hebichess_production_nnue_search_candidate_g(HebiChessNmpCandidateGDiagnostic)
target_compile_definitions(HebiChessNmpCandidateGDiagnostic PRIVATE
  HEBICHESS_BLUNDER_DIAGNOSTIC=1
)

add_executable(HebiChessSearchBaselineLegacy
  tests/search_baseline.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessSearchBaselineLegacy PRIVATE src)
target_compile_definitions(HebiChessSearchBaselineLegacy PRIVATE
  HEBICHESS_NNUE_HIDDEN1_VARIANT=1)

add_executable(HebiChessNnueClipPrecomputeTest
  tests/nnue_clip_precompute_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessNnueClipPrecomputeTest PRIVATE src)
target_compile_definitions(HebiChessNnueClipPrecomputeTest PRIVATE HEBICHESS_NNUE_TEST_REFERENCE=1)

add_executable(HebiChessNnueIncrementalAccumulatorTest
  tests/nnue_incremental_accumulator_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessNnueIncrementalAccumulatorTest PRIVATE src)
target_compile_definitions(HebiChessNnueIncrementalAccumulatorTest
  PRIVATE HEBICHESS_NNUE_TEST_PROFILE=1)

# Trace-only evaluator reuse diagnostics. This target records search events
# and accumulator fingerprints; normal engine targets do not define the macro.
add_executable(HebiChessEvalReuseProfile
  tests/eval_reuse_profile.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessEvalReuseProfile PRIVATE src)
apply_hebichess_production_nnue_search(HebiChessEvalReuseProfile)
target_compile_definitions(HebiChessEvalReuseProfile PRIVATE
  HEBICHESS_EVAL_REUSE_PROFILE=1
  HEBICHESS_QSEARCH_TT_PROFILE=1
)

# Diagnostic-only exact static NNUE evaluation cache A/B. The production
# engine targets above and below do not define HEBICHESS_EVALCACHE_EXACT.
foreach(_eval_cache_variant IN ITEMS Baseline Exact1 Exact2 Exact4)
  add_executable(HebiChessEvalCache${_eval_cache_variant}
    tests/eval_cache_ab_benchmark.cpp
    ${CHESS_SOURCES}
  )
  target_include_directories(HebiChessEvalCache${_eval_cache_variant} PRIVATE src)
  apply_hebichess_production_nnue_search(HebiChessEvalCache${_eval_cache_variant})
endforeach()
foreach(_eval_cache_ways IN ITEMS 1 2 4)
  target_compile_definitions(HebiChessEvalCacheExact${_eval_cache_ways} PRIVATE
    HEBICHESS_EVALCACHE_EXACT=1
    HEBICHESS_EVALCACHE_EXACT_WAYS=${_eval_cache_ways}
    HEBICHESS_EVALCACHE_EXACT_TELEMETRY=1)
endforeach()
foreach(_eval_cache_entries IN ITEMS 1024 2048 8192)
  set(_eval_cache_target HebiChessEvalCacheExact2Way${_eval_cache_entries})
  add_executable(${_eval_cache_target} tests/eval_cache_ab_benchmark.cpp ${CHESS_SOURCES})
  target_include_directories(${_eval_cache_target} PRIVATE src)
  apply_hebichess_production_nnue_search(${_eval_cache_target})
  target_compile_definitions(${_eval_cache_target} PRIVATE
    HEBICHESS_EVALCACHE_EXACT=1
    HEBICHESS_EVALCACHE_EXACT_WAYS=2
    HEBICHESS_EVALCACHE_EXACT_ENTRIES=${_eval_cache_entries}
    HEBICHESS_EVALCACHE_EXACT_TELEMETRY=1)
endforeach()

option(HEBICHESS_BUILD_WASM_EVALCACHE_AB
  "Build diagnostic-only Node WASM baseline and exact-eval-cache A/B engines" OFF)
if(HEBICHESS_BUILD_WASM_EVALCACHE_AB)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "HEBICHESS_BUILD_WASM_EVALCACHE_AB requires Emscripten")
  endif()
  foreach(_eval_cache_variant IN ITEMS Baseline Exact1 Exact2 Exact4)
    string(TOLOWER "${_eval_cache_variant}" _eval_cache_dir)
    add_executable(HebiChessWasmEvalCache${_eval_cache_variant}
      src/wasm_bridge.cpp ${CHESS_SOURCES})
    target_include_directories(HebiChessWasmEvalCache${_eval_cache_variant} PRIVATE src)
    target_compile_definitions(HebiChessWasmEvalCache${_eval_cache_variant} PRIVATE HEBICHESS_WASM=1)
    apply_hebichess_production_nnue_search(HebiChessWasmEvalCache${_eval_cache_variant})
    if(_eval_cache_variant MATCHES "^Exact([124])$")
      set(_eval_cache_ways "${CMAKE_MATCH_1}")
      target_compile_definitions(HebiChessWasmEvalCache${_eval_cache_variant} PRIVATE
        HEBICHESS_EVALCACHE_EXACT=1
        HEBICHESS_EVALCACHE_EXACT_WAYS=${_eval_cache_ways}
        HEBICHESS_EVALCACHE_EXACT_TELEMETRY=1)
    endif()
    set_target_properties(HebiChessWasmEvalCache${_eval_cache_variant} PROPERTIES
      OUTPUT_NAME hebichess-eval-cache
      RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/artifacts/eval-cache/${_eval_cache_dir}"
      SUFFIX ".js")
    set_target_properties(HebiChessWasmEvalCache${_eval_cache_variant} PROPERTIES
      LINK_FLAGS "--no-entry -O3 ${HEBICHESS_WASM_COMMON_LINK_FLAGS} -sMODULARIZE=1 -sEXPORT_NAME=createHebiChessEvalCacheModule -sENVIRONMENT=node -sEXPORTED_FUNCTIONS=['_malloc','_free','_hebichess_initialize','_hebichess_send_command','_hebichess_take_output','_hebichess_nnue_load_bytes','_hebichess_nnue_clear','_hebichess_book_load_bytes','_hebichess_book_clear','_hebichess_game_reset','_hebichess_game_load_fen','_hebichess_game_fen','_hebichess_game_legal_moves','_hebichess_game_apply','_hebichess_game_status'] -sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8']")
  endforeach()
  foreach(_eval_cache_entries IN ITEMS 1024 2048 8192)
    set(_eval_cache_variant Exact2Way${_eval_cache_entries})
    set(_eval_cache_dir exact2way-${_eval_cache_entries})
    add_executable(HebiChessWasmEvalCache${_eval_cache_variant}
      src/wasm_bridge.cpp ${CHESS_SOURCES})
    target_include_directories(HebiChessWasmEvalCache${_eval_cache_variant} PRIVATE src)
    target_compile_definitions(HebiChessWasmEvalCache${_eval_cache_variant} PRIVATE
      HEBICHESS_WASM=1
      HEBICHESS_EVALCACHE_EXACT=1
      HEBICHESS_EVALCACHE_EXACT_WAYS=2
      HEBICHESS_EVALCACHE_EXACT_ENTRIES=${_eval_cache_entries}
      HEBICHESS_EVALCACHE_EXACT_TELEMETRY=1)
    apply_hebichess_production_nnue_search(HebiChessWasmEvalCache${_eval_cache_variant})
    set_target_properties(HebiChessWasmEvalCache${_eval_cache_variant} PROPERTIES
      OUTPUT_NAME hebichess-eval-cache
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/artifacts/eval-cache/${_eval_cache_dir}"
      SUFFIX ".js"
      LINK_FLAGS "--no-entry -O3 ${HEBICHESS_WASM_COMMON_LINK_FLAGS} -sMODULARIZE=1 -sEXPORT_NAME=createHebiChessEvalCacheModule -sENVIRONMENT=node -sEXPORTED_FUNCTIONS=['_malloc','_free','_hebichess_initialize','_hebichess_send_command','_hebichess_take_output','_hebichess_nnue_load_bytes','_hebichess_nnue_clear','_hebichess_book_load_bytes','_hebichess_book_clear','_hebichess_game_reset','_hebichess_game_load_fen','_hebichess_game_fen','_hebichess_game_legal_moves','_hebichess_game_apply','_hebichess_game_status'] -sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8']")
  endforeach()
endif()

# Architecture-neutral NNUE evaluator A/B harness. Each executable is
# independently compiled, so the selected dense loop is a compile-time choice
# only; the production engine has no setting or diagnostic toggle. By default
# it uses a deterministic v3 network; Windows runs may pass --network with the
# frozen production 256/128/128 v3 file.
function(add_nnue_hidden1_dense_variant suffix interleave)
  add_executable(HebiChessNnueHidden1Dense${suffix}
    tests/nnue_hidden1_dense_benchmark.cpp
    ${CHESS_SOURCES}
  )
  target_include_directories(HebiChessNnueHidden1Dense${suffix} PRIVATE src)
  target_compile_definitions(HebiChessNnueHidden1Dense${suffix} PRIVATE
    HEBICHESS_NNUE_TEST_PROFILE=1
    HEBICHESS_NNUE_TEST_REFERENCE=1
    HEBICHESS_NNUE_HIDDEN1_VARIANT=${interleave}
  )
endfunction()

add_nnue_hidden1_dense_variant(Legacy 1)
add_nnue_hidden1_dense_variant(Interleaved2 2)
add_nnue_hidden1_dense_variant(Interleaved4 4)
add_nnue_hidden1_dense_variant(Interleaved8 8)

function(add_nnue_incremental_accumulator_variant suffix interleave)
  add_executable(HebiChessNnueIncrementalAccumulator${suffix}
    tests/nnue_incremental_accumulator_test.cpp
    ${CHESS_SOURCES}
  )
  target_include_directories(HebiChessNnueIncrementalAccumulator${suffix} PRIVATE src)
  target_compile_definitions(HebiChessNnueIncrementalAccumulator${suffix} PRIVATE
    HEBICHESS_NNUE_TEST_PROFILE=1
    HEBICHESS_NNUE_HIDDEN1_VARIANT=${interleave}
  )
endfunction()

add_nnue_incremental_accumulator_variant(Legacy 1)
add_nnue_incremental_accumulator_variant(Interleaved2 2)
add_nnue_incremental_accumulator_variant(Interleaved4 4)
add_nnue_incremental_accumulator_variant(Interleaved8 8)

add_executable(HebiChessNnueSearchAccumulatorTest
  tests/nnue_search_accumulator_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessNnueSearchAccumulatorTest PRIVATE src)
target_compile_definitions(HebiChessNnueSearchAccumulatorTest
  PRIVATE HEBICHESS_NNUE_SEARCH_TEST=1)

add_executable(HebiChessGameStateTest
  tests/game_state_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessGameStateTest PRIVATE src)

add_executable(HebiChessOpeningBookTest
  tests/opening_book_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessOpeningBookTest PRIVATE src)

# Phase 6-4B-2: compile-time-only QSearch TT A/B binaries.  Diagnostic and
# strength targets remain separately named so the accepted production path
# stays free of profiling instrumentation.
function(add_qsearch_tt_variant suffix variant profile source cutoff_mask diagnostic)
  add_executable(${suffix}
    ${source}
    ${CHESS_SOURCES}
  )
  target_include_directories(${suffix} PRIVATE src)
  target_compile_definitions(${suffix} PRIVATE
    HEBICHESS_QSEARCH_TT_VARIANT=${variant}
    HEBICHESS_QSEARCH_TT_PROFILE=${profile}
    HEBICHESS_QSEARCH_TT_CUTOFF_MASK=${cutoff_mask}
    HEBICHESS_QSEARCH_TT_DIAGNOSTIC=${diagnostic}
  )
endfunction()

# Windows acceptance targets.  They compile identical sources and differ only
# in QTT enablement; profiling is compiled out of the timing path.
add_qsearch_tt_variant(HebiChessSearchQttBaseline 0 0 tests/qsearch_tt_benchmark.cpp 0 0)
add_qsearch_tt_variant(HebiChessSearchQttActive 2 0 tests/qsearch_tt_benchmark.cpp 7 0)

# Profile-only companions retain the C1 shadow measurements without affecting
# the acceptance binaries above.
add_qsearch_tt_variant(HebiChessSearchQttBaselineProfile 0 1 tests/qsearch_tt_ab.cpp 0 1)
add_qsearch_tt_variant(HebiChessSearchQttShadowProfile 1 1 tests/qsearch_tt_ab.cpp 0 1)
add_qsearch_tt_variant(HebiChessSearchQttActiveProfile 2 1 tests/qsearch_tt_ab.cpp 7 1)

# Phase 6-4B-3 diagnosis only.  These are not performance binaries and are
# never built by the production target.  The mask enables Exact=1, Lower=2,
# Upper=4 QTT cutoffs while retaining identical QTT probing and storing.
add_qsearch_tt_variant(HebiChessSearchQttDiagExact 2 1 tests/qsearch_tt_ab.cpp 1 1)
add_qsearch_tt_variant(HebiChessSearchQttDiagLower 2 1 tests/qsearch_tt_ab.cpp 2 1)
add_qsearch_tt_variant(HebiChessSearchQttDiagUpper 2 1 tests/qsearch_tt_ab.cpp 4 1)
add_qsearch_tt_variant(HebiChessSearchQttDiagExactLower 2 1 tests/qsearch_tt_ab.cpp 3 1)
add_qsearch_tt_variant(HebiChessSearchQttDiagExactUpper 2 1 tests/qsearch_tt_ab.cpp 5 1)
add_qsearch_tt_variant(HebiChessSearchQttDiagLowerUpper 2 1 tests/qsearch_tt_ab.cpp 6 1)

# Phase 6-4B-6: delta-pruning removal candidates.  The compile definition is
# attached only to these separately named executables; HebiChess and every
# existing QTT target keep the default legacy rule.
add_qsearch_tt_variant(HebiChessSearchNoDeltaQttOffProfile 0 1 tests/qsearch_tt_ab.cpp 0 1)
target_compile_definitions(HebiChessSearchNoDeltaQttOffProfile PRIVATE
  HEBICHESS_QSEARCH_DELTA_PRUNING=0)
add_qsearch_tt_variant(HebiChessSearchNoDeltaQttOnProfile 2 1 tests/qsearch_tt_ab.cpp 7 1)
target_compile_definitions(HebiChessSearchNoDeltaQttOnProfile PRIVATE
  HEBICHESS_QSEARCH_DELTA_PRUNING=0)

add_qsearch_tt_variant(HebiChessSearchNoDeltaQttOffBenchmark 0 0 tests/qsearch_tt_benchmark.cpp 0 0)
target_compile_definitions(HebiChessSearchNoDeltaQttOffBenchmark PRIVATE
  HEBICHESS_QSEARCH_DELTA_PRUNING=0)
add_qsearch_tt_variant(HebiChessSearchNoDeltaQttOnBenchmark 2 0 tests/qsearch_tt_benchmark.cpp 7 0)
target_compile_definitions(HebiChessSearchNoDeltaQttOnBenchmark PRIVATE
  HEBICHESS_QSEARCH_DELTA_PRUNING=0)

# Counter companions run outside the timed sample set.  This preserves the
# existing release timing path while still reporting eval calls and QTT work.
add_qsearch_tt_variant(HebiChessSearchQttBaselineCounters 0 1 tests/qsearch_tt_benchmark.cpp 0 0)
add_qsearch_tt_variant(HebiChessSearchNoDeltaQttOffCounters 0 1 tests/qsearch_tt_benchmark.cpp 0 0)
target_compile_definitions(HebiChessSearchNoDeltaQttOffCounters PRIVATE
  HEBICHESS_QSEARCH_DELTA_PRUNING=0)
add_qsearch_tt_variant(HebiChessSearchNoDeltaQttOnCounters 2 1 tests/qsearch_tt_benchmark.cpp 7 0)
target_compile_definitions(HebiChessSearchNoDeltaQttOnCounters PRIVATE
  HEBICHESS_QSEARCH_DELTA_PRUNING=0)

# Phase 6-4B-7b strength-only UCI engines.  These are release-equivalent
# timing targets: detailed QTT/evaluation profiling is compiled out.  The
# separately named *Counters targets above are the only companion binaries
# allowed to collect those detailed measurements.
function(add_qsearch_strength_variant target variant cutoff_mask delta_pruning)
  add_executable(${target}
    src/main.cpp
    ${CHESS_SOURCES}
  )
  target_include_directories(${target} PRIVATE src)
  target_compile_definitions(${target} PRIVATE
    HEBICHESS_QSEARCH_TT_VARIANT=${variant}
    HEBICHESS_QSEARCH_TT_PROFILE=0
    HEBICHESS_QSEARCH_TT_CUTOFF_MASK=${cutoff_mask}
    HEBICHESS_QSEARCH_TT_DIAGNOSTIC=0
    HEBICHESS_QSEARCH_DELTA_PRUNING=${delta_pruning}
    HEBICHESS_STRENGTH_RUNNER=1
  )
endfunction()

add_qsearch_strength_variant(HebiChessStrengthCurrent 0 0 1)
add_qsearch_strength_variant(HebiChessStrengthNoDeltaQtt 2 7 0)

enable_testing()
set(HEBICHESS_BLUNDER_DIAGNOSTIC_NETWORK
    "${CMAKE_SOURCE_DIR}/hebinnue-v3-4c815d54bc6c9fbf.hebinnue" CACHE FILEPATH
    "Optional frozen NNUE network for the local blunder diagnostic regression")
if(EXISTS "${HEBICHESS_BLUNDER_DIAGNOSTIC_NETWORK}")
  add_test(NAME chess_blunder_diagnostic_reuse_regression
    COMMAND HebiChessBlunderDiagnosticRegression
      --network "${HEBICHESS_BLUNDER_DIAGNOSTIC_NETWORK}")
  add_test(NAME chess_nmp_candidate_g_regression
    COMMAND HebiChessNmpCandidateGTest
      --network "${HEBICHESS_BLUNDER_DIAGNOSTIC_NETWORK}")
  set_tests_properties(chess_nmp_candidate_g_regression PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
add_test(
  NAME smoke
  COMMAND ${CMAKE_COMMAND}
    "-DENGINE=$<TARGET_FILE:HebiChess>"
    "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.in"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.cmake"
)
add_test(NAME chess_types COMMAND HebiChessTypesTest)
add_test(NAME chess_movegen COMMAND HebiChessMovegenTest)
add_test(NAME chess_perft COMMAND HebiChessPerftTest)
add_test(NAME chess_search COMMAND HebiChessSearchTest)
add_test(NAME chess_uci COMMAND HebiChessUciTest)
add_test(NAME chess_time_management COMMAND HebiChessTimeManagementTest)
add_test(NAME chess_zobrist_tt COMMAND HebiChessZobristTtTest)
add_test(NAME chess_see COMMAND HebiChessSeeTest)
add_test(NAME chess_game_state COMMAND HebiChessGameStateTest)
add_test(NAME chess_opening_book COMMAND HebiChessOpeningBookTest)
add_test(NAME chess_nnue_clip_precompute COMMAND HebiChessNnueClipPrecomputeTest)
set_tests_properties(chess_nnue_clip_precompute PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
add_test(NAME chess_nnue_incremental_accumulator COMMAND HebiChessNnueIncrementalAccumulatorTest)
foreach(nnue_variant Legacy Interleaved2 Interleaved4 Interleaved8)
  add_test(
    NAME chess_nnue_hidden1_dense_${nnue_variant}
    COMMAND HebiChessNnueHidden1Dense${nnue_variant} --verify-only
  )
  set_tests_properties(chess_nnue_hidden1_dense_${nnue_variant} PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )
endforeach()
add_test(NAME chess_nnue_search_accumulator COMMAND HebiChessNnueSearchAccumulatorTest)
add_test(
  NAME chess_search_baseline
  COMMAND ${CMAKE_COMMAND}
    "-DBASELINE=$<TARGET_FILE:HebiChessSearchBaseline>"
    "-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/phase6-search-baseline-smoke.fen"
    "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/phase6-search-baseline-smoke.json"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/search_baseline_smoke.cmake"
)
add_test(NAME chess_search_baseline_config COMMAND HebiChessSearchBaselineConfigTest)
add_test(NAME chess_production_nnue_search_config
  COMMAND HebiChessProductionNnueSearchConfigTest
)
add_test(
  NAME chess_search_nnue_h1_ab
  COMMAND ${CMAKE_COMMAND}
    "-DBASELINE=$<TARGET_FILE:HebiChessSearchBaseline>"
    "-DCANDIDATE=$<TARGET_FILE:HebiChessSearchNnueH1Interleaved8>"
    "-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/phase6-search-baseline.fen"
    "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/search-nnue-h1-ab-smoke"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/search_nnue_h1_ab.cmake"
)
add_test(NAME chess_qsearch_lazy_ab
  COMMAND ${CMAKE_COMMAND}
    -DBASELINE=$<TARGET_FILE:HebiChessSearchBaseline>
    -DLAZY=$<TARGET_FILE:HebiChessSearchLazyChecks>
    -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/phase6-search-baseline.fen
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/qsearch_lazy_ab.cmake
)

# Isolated experimental Stockfish NNUE reference backend.  It is not linked
# into the production engine or production WASM target.
option(HEBICHESS_BUILD_STOCKFISH_NNUE_EXPERIMENTAL
  "Build isolated Stockfish 19 NNUE reference evaluator targets" OFF)
option(HEBICHESS_BUILD_STOCKFISH14_NNUE_EXPERIMENTAL
  "Include isolated Stockfish 14 NNUE in the Stockfish evaluator A/B targets" OFF)
option(HEBICHESS_STOCKFISH_NNUE_NATIVE_AVX2
  "Use Stockfish AVX2 kernels in experimental native targets" OFF)
set(HEBICHESS_STOCKFISH_NNUE_TEST_NETWORK "" CACHE FILEPATH
  "Optional local Stockfish network used by the evaluator parity test")
set(HEBICHESS_STOCKFISH14_NNUE_TEST_NETWORK "" CACHE FILEPATH
  "Optional local Stockfish 14 network used by its evaluator parity test")
set(HEBICHESS_STOCKFISH_NNUE_UPSTREAM_SOURCES
  third_party/stockfish/src/attacks.cpp
  third_party/stockfish/src/bitboard.cpp
  third_party/stockfish/src/misc.cpp
  third_party/stockfish/src/movegen.cpp
  third_party/stockfish/src/position.cpp
  third_party/stockfish/src/nnue/nnue_accumulator.cpp
  third_party/stockfish/src/nnue/network.cpp
  third_party/stockfish/src/nnue/features/half_ka_v2_hm.cpp
  third_party/stockfish/src/nnue/features/full_threats.cpp
  third_party/stockfish/src/nnue/features/pp_3wide.cpp
)
set(HEBICHESS_STOCKFISH14_NNUE_UPSTREAM_SOURCES
  third_party/stockfish14/src/bitboard.cpp
  third_party/stockfish14/src/movegen.cpp
  third_party/stockfish14/src/position.cpp
  third_party/stockfish14/src/psqt.cpp
  third_party/stockfish14/src/nnue/evaluate_nnue.cpp
  third_party/stockfish14/src/nnue/features/half_ka_v2.cpp
)

if(HEBICHESS_BUILD_STOCKFISH14_NNUE_EXPERIMENTAL AND
   NOT HEBICHESS_BUILD_STOCKFISH_NNUE_EXPERIMENTAL)
  message(FATAL_ERROR
    "The Stockfish 14 A/B backend requires HEBICHESS_BUILD_STOCKFISH_NNUE_EXPERIMENTAL for shared search baselines")
endif()

if(HEBICHESS_BUILD_STOCKFISH_NNUE_EXPERIMENTAL)
  set(_stockfish_nnue_sources
    ${HEBICHESS_STOCKFISH_NNUE_UPSTREAM_SOURCES}
    src/chess/stockfish_nnue.cpp
  )
  set(_stockfish_nnue_engine_sources ${_stockfish_nnue_sources})
  if(HEBICHESS_BUILD_STOCKFISH14_NNUE_EXPERIMENTAL)
    list(APPEND _stockfish_nnue_engine_sources
      ${HEBICHESS_STOCKFISH14_NNUE_UPSTREAM_SOURCES}
      src/chess/stockfish14_nnue.cpp)
  endif()
  add_executable(HebiChessStockfishNNUE
    src/main.cpp ${CHESS_SOURCES} ${_stockfish_nnue_engine_sources}
  )
  target_include_directories(HebiChessStockfishNNUE PRIVATE src third_party)
  target_compile_definitions(HebiChessStockfishNNUE PRIVATE
    HEBICHESS_STOCKFISH_NNUE_EXPERIMENTAL=1
    HEBICHESS_STOCKFISH_NNUE_ONLY=1
    NNUE_EMBEDDING_OFF=1
  )
  if(HEBICHESS_BUILD_STOCKFISH14_NNUE_EXPERIMENTAL)
    target_compile_definitions(HebiChessStockfishNNUE PRIVATE
      HEBICHESS_STOCKFISH14_NNUE_EXPERIMENTAL=1
      HEBICHESS_STOCKFISH14_EVAL_ONLY=1)
  endif()
  apply_hebichess_production_nnue_search(HebiChessStockfishNNUE)
  apply_hebichess_production_evalcache(HebiChessStockfishNNUE)
  if(HEBICHESS_STOCKFISH_NNUE_NATIVE_AVX2 AND MSVC AND CMAKE_GENERATOR_PLATFORM MATCHES "x64")
    target_compile_options(HebiChessStockfishNNUE PRIVATE /arch:AVX2)
    target_compile_definitions(HebiChessStockfishNNUE PRIVATE USE_AVX2=1)
  endif()

  add_executable(HebiChessStockfishNnueTest
    tests/stockfish_nnue_reference_test.cpp ${CHESS_SOURCES} ${_stockfish_nnue_sources}
  )
  target_include_directories(HebiChessStockfishNnueTest PRIVATE src third_party)
  target_compile_definitions(HebiChessStockfishNnueTest PRIVATE
    HEBICHESS_STOCKFISH_NNUE_EXPERIMENTAL=1
    HEBICHESS_STOCKFISH_NNUE_ONLY=1
    HEBICHESS_STOCKFISH_NNUE_TESTS=1
    NNUE_EMBEDDING_OFF=1
  )
  apply_hebichess_production_nnue_search(HebiChessStockfishNnueTest)
  apply_hebichess_production_evalcache(HebiChessStockfishNnueTest)
  if(HEBICHESS_STOCKFISH_NNUE_NATIVE_AVX2 AND MSVC AND CMAKE_GENERATOR_PLATFORM MATCHES "x64")
    target_compile_options(HebiChessStockfishNnueTest PRIVATE /arch:AVX2)
    target_compile_definitions(HebiChessStockfishNnueTest PRIVATE USE_AVX2=1)
  endif()

  if(HEBICHESS_STOCKFISH_NNUE_TEST_NETWORK AND
     EXISTS "${HEBICHESS_STOCKFISH_NNUE_TEST_NETWORK}")
    add_test(NAME stockfish_nnue_reference
      COMMAND HebiChessStockfishNnueTest "${HEBICHESS_STOCKFISH_NNUE_TEST_NETWORK}")
  endif()

  if(HEBICHESS_BUILD_STOCKFISH14_NNUE_EXPERIMENTAL)
    add_executable(HebiChessStockfish14NnueTest
      tests/stockfish14_nnue_reference_test.cpp ${CHESS_SOURCES}
      ${HEBICHESS_STOCKFISH14_NNUE_UPSTREAM_SOURCES}
      src/chess/stockfish14_nnue.cpp)
    target_include_directories(HebiChessStockfish14NnueTest PRIVATE src third_party)
    target_compile_definitions(HebiChessStockfish14NnueTest PRIVATE
      HEBICHESS_STOCKFISH14_NNUE_EXPERIMENTAL=1
      HEBICHESS_STOCKFISH14_NNUE_TESTS=1
      HEBICHESS_STOCKFISH14_EVAL_ONLY=1)
    apply_hebichess_production_nnue_search(HebiChessStockfish14NnueTest)
    apply_hebichess_production_evalcache(HebiChessStockfish14NnueTest)
    if(HEBICHESS_STOCKFISH14_NNUE_TEST_NETWORK AND
       EXISTS "${HEBICHESS_STOCKFISH14_NNUE_TEST_NETWORK}")
      add_test(NAME stockfish14_nnue_reference
        COMMAND HebiChessStockfish14NnueTest
          "${HEBICHESS_STOCKFISH14_NNUE_TEST_NETWORK}")
    endif()
  endif()
endif()

option(HEBICHESS_BUILD_STOCKFISH_NNUE_WASM_NODE
  "Build isolated scalar and WASM SIMD128 Stockfish NNUE Node candidates" OFF)
if(HEBICHESS_BUILD_STOCKFISH_NNUE_WASM_NODE)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "HEBICHESS_BUILD_STOCKFISH_NNUE_WASM_NODE requires Emscripten")
  endif()
  foreach(_variant IN ITEMS scalar simd128)
    set(_target HebiChessStockfishNNUEWasm${_variant})
    set(_out_dir "${CMAKE_BINARY_DIR}/stockfish-nnue-wasm-${_variant}")
    set(_wasm_stockfish_sources ${HEBICHESS_STOCKFISH_NNUE_UPSTREAM_SOURCES}
      src/chess/stockfish_nnue.cpp)
    if(HEBICHESS_BUILD_STOCKFISH14_NNUE_EXPERIMENTAL)
      list(APPEND _wasm_stockfish_sources
        ${HEBICHESS_STOCKFISH14_NNUE_UPSTREAM_SOURCES}
        src/chess/stockfish14_nnue.cpp)
    endif()
    add_executable(${_target} src/wasm_bridge.cpp ${CHESS_SOURCES}
      ${_wasm_stockfish_sources})
    target_include_directories(${_target} PRIVATE src third_party)
    target_compile_definitions(${_target} PRIVATE HEBICHESS_WASM=1
      HEBICHESS_STOCKFISH_NNUE_EXPERIMENTAL=1
      HEBICHESS_STOCKFISH_NNUE_ONLY=1 NNUE_EMBEDDING_OFF=1)
    if(HEBICHESS_BUILD_STOCKFISH14_NNUE_EXPERIMENTAL)
      target_compile_definitions(${_target} PRIVATE
        HEBICHESS_STOCKFISH14_NNUE_EXPERIMENTAL=1
        HEBICHESS_STOCKFISH14_EVAL_ONLY=1)
    endif()
    apply_hebichess_production_nnue_search(${_target})
    apply_hebichess_production_evalcache(${_target})
    set(_simd_compile_flags "")
    if(_variant STREQUAL "simd128")
      target_compile_options(${_target} PRIVATE -msimd128 -msse4.1)
      target_compile_definitions(${_target} PRIVATE USE_SSE2=1 USE_SSSE3=1 USE_SSE41=1)
      set(_simd_compile_flags "-msimd128 -msse4.1")
    endif()
    set_target_properties(${_target} PROPERTIES
      OUTPUT_NAME "hebichess-stockfish-${_variant}"
      RUNTIME_OUTPUT_DIRECTORY "${_out_dir}"
      SUFFIX ".js"
      LINK_FLAGS "--no-entry -O3 ${_simd_compile_flags} ${HEBICHESS_WASM_COMMON_LINK_FLAGS} -sMODULARIZE=1 -sEXPORT_NAME=createHebiChessStockfishModule -sENVIRONMENT=node -sEXPORTED_FUNCTIONS=['_malloc','_free','_hebichess_initialize','_hebichess_send_command','_hebichess_take_output','_hebichess_nnue_load_bytes','_hebichess_stockfish_nnue_load_bytes','_hebichess_stockfish_nnue_evaluate_fen','_hebichess_stockfish14_nnue_load_bytes','_hebichess_stockfish14_nnue_evaluate_fen','_hebichess_stockfish14_nnue_stats_json','_hebichess_stockfish14_nnue_reset_stats','_hebichess_book_load_bytes','_hebichess_book_clear','_hebichess_nnue_clear','_hebichess_nnue_evaluate_fen_raw','_hebichess_game_reset','_hebichess_game_load_fen','_hebichess_game_legal_moves','_hebichess_game_apply','_hebichess_game_status'] -sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8']"
    )
  endforeach()
endif()
