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

# Phase 6-4B final production configuration.  Keep the accepted search
# definitions shared by every production engine target so they cannot drift.
set(HEBICHESS_PRODUCTION_QSEARCH_DEFINITIONS
  HEBICHESS_QSEARCH_DELTA_PRUNING=0
  HEBICHESS_QSEARCH_TT_VARIANT=2
  HEBICHESS_QSEARCH_TT_CUTOFF_MASK=7
  HEBICHESS_QSEARCH_TT_PROFILE=0
  HEBICHESS_QSEARCH_TT_DIAGNOSTIC=0
)
function(apply_hebichess_production_qsearch target)
  target_compile_definitions(${target} PRIVATE
    ${HEBICHESS_PRODUCTION_QSEARCH_DEFINITIONS}
  )
endfunction()

add_executable(HebiChess
  src/main.cpp
  ${CHESS_SOURCES}
)

target_include_directories(HebiChess PRIVATE src)
apply_hebichess_production_qsearch(HebiChess)

option(HEBICHESS_BUILD_WASM "Build the browser WebAssembly engine" OFF)
set(HEBICHESS_WASM_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/web/public/engine" CACHE PATH "Output directory for the browser WebAssembly engine")
if(HEBICHESS_BUILD_WASM)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "HEBICHESS_BUILD_WASM requires Emscripten (configure with emcmake cmake)")
  endif()
  add_executable(HebiChessWasm src/wasm_bridge.cpp ${CHESS_SOURCES})
  target_include_directories(HebiChessWasm PRIVATE src)
  target_compile_definitions(HebiChessWasm PRIVATE HEBICHESS_WASM=1 HEBICHESS_BROWSER_WASM=1)
  apply_hebichess_production_qsearch(HebiChessWasm)
  set_target_properties(HebiChessWasm PROPERTIES
    OUTPUT_NAME hebichess
    RUNTIME_OUTPUT_DIRECTORY "${HEBICHESS_WASM_OUTPUT_DIR}"
    SUFFIX ".js"
  LINK_FLAGS "--no-entry -O3 -sINITIAL_MEMORY=201326592 -sMAXIMUM_MEMORY=402653184 -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_NAME=createHebiChessModule -sENVIRONMENT=web,worker -sEXPORTED_FUNCTIONS=['_malloc','_free','_hebichess_initialize','_hebichess_send_command','_hebichess_take_output','_hebichess_nnue_load_bytes','_hebichess_nnue_clear','_hebichess_nnue_evaluate_fen_raw','_hebichess_book_load_bytes','_hebichess_book_clear','_hebichess_game_reset','_hebichess_game_load_fen','_hebichess_game_fen','_hebichess_game_legal_moves','_hebichess_game_apply','_hebichess_game_status'] -sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8']"
  )
endif()

option(HEBICHESS_BUILD_WASM_NODE_TEST "Build the test-only Node-compatible WebAssembly engine" OFF)
if(HEBICHESS_BUILD_WASM_NODE_TEST)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "HEBICHESS_BUILD_WASM_NODE_TEST requires Emscripten (configure with emcmake cmake)")
  endif()
  add_executable(HebiChessWasmNodeTest src/wasm_bridge.cpp ${CHESS_SOURCES})
  target_include_directories(HebiChessWasmNodeTest PRIVATE src)
  target_compile_definitions(HebiChessWasmNodeTest PRIVATE HEBICHESS_WASM=1)
  apply_hebichess_production_qsearch(HebiChessWasmNodeTest)
  set_target_properties(HebiChessWasmNodeTest PROPERTIES
    OUTPUT_NAME hebichess-node
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build-wasm/node-test"
    SUFFIX ".js"
  LINK_FLAGS "--no-entry -O3 -sINITIAL_MEMORY=201326592 -sMAXIMUM_MEMORY=402653184 -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_NAME=createHebiChessNodeTestModule -sENVIRONMENT=web,worker,node -sEXPORTED_FUNCTIONS=['_malloc','_free','_hebichess_initialize','_hebichess_send_command','_hebichess_take_output','_hebichess_nnue_load_bytes','_hebichess_book_load_bytes','_hebichess_book_clear','_hebichess_nnue_clear','_hebichess_nnue_evaluate_fen_raw','_hebichess_game_reset','_hebichess_game_load_fen','_hebichess_game_fen','_hebichess_game_legal_moves','_hebichess_game_apply','_hebichess_game_status'] -sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8']"
  )
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

add_executable(HebiChessUciTest
  tests/uci_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessUciTest PRIVATE src)

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

# Phase 6-3A architecture-neutral hidden1 A/B harness.  Each executable is
# independently compiled, so the selected dense loop is a compile-time choice
# only; the production engine has no setting or diagnostic toggle.
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
add_test(NAME chess_zobrist_tt COMMAND HebiChessZobristTtTest)
add_test(NAME chess_see COMMAND HebiChessSeeTest)
add_test(NAME chess_game_state COMMAND HebiChessGameStateTest)
add_test(NAME chess_opening_book COMMAND HebiChessOpeningBookTest)
add_test(NAME chess_nnue_clip_precompute COMMAND HebiChessNnueClipPrecomputeTest)
set_tests_properties(chess_nnue_clip_precompute PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
add_test(NAME chess_nnue_incremental_accumulator COMMAND HebiChessNnueIncrementalAccumulatorTest)
add_test(NAME chess_nnue_search_accumulator COMMAND HebiChessNnueSearchAccumulatorTest)
add_test(
  NAME chess_search_baseline
  COMMAND ${CMAKE_COMMAND}
    "-DBASELINE=$<TARGET_FILE:HebiChessSearchBaseline>"
    "-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/phase6-search-baseline.fen"
    "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/phase6-search-baseline-smoke.json"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/search_baseline_smoke.cmake"
)
