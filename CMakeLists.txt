cmake_minimum_required(VERSION 3.16)
project(HebiChess LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CHESS_SOURCES
  src/chess/board.cpp src/chess/movegen.cpp src/chess/game_state.cpp src/chess/perft.cpp
  src/chess/eval.cpp src/chess/nnue.cpp src/chess/nnue_features.cpp
  src/chess/search.cpp src/chess/uci.cpp src/chess/zobrist.cpp src/chess/tt.cpp src/chess/see.cpp
  src/chess/uci_engine.cpp
)

add_executable(HebiChess
  src/main.cpp
  ${CHESS_SOURCES}
)

target_include_directories(HebiChess PRIVATE src)
# Explicitly pin the normal native engine to the accepted portable layout.
target_compile_definitions(HebiChess PRIVATE HEBICHESS_NNUE_HIDDEN1_LAYOUT=0)

option(HEBICHESS_BUILD_WASM "Build the browser WebAssembly engine" OFF)
if(HEBICHESS_BUILD_WASM)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "HEBICHESS_BUILD_WASM requires Emscripten (configure with emcmake cmake)")
  endif()
  add_executable(HebiChessWasm src/wasm_bridge.cpp ${CHESS_SOURCES})
  target_include_directories(HebiChessWasm PRIVATE src)
  target_compile_definitions(HebiChessWasm PRIVATE HEBICHESS_WASM=1 HEBICHESS_BROWSER_WASM=1
    HEBICHESS_NNUE_HIDDEN1_LAYOUT=0)
  set_target_properties(HebiChessWasm PROPERTIES
    OUTPUT_NAME hebichess
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/web/public/engine"
    SUFFIX ".js"
  LINK_FLAGS "--no-entry -O3 -sINITIAL_MEMORY=201326592 -sMAXIMUM_MEMORY=402653184 -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_NAME=createHebiChessModule -sENVIRONMENT=web,worker -sEXPORTED_FUNCTIONS=['_malloc','_free','_hebichess_initialize','_hebichess_send_command','_hebichess_take_output','_hebichess_nnue_load_bytes','_hebichess_nnue_clear','_hebichess_nnue_evaluate_fen_raw','_hebichess_game_reset','_hebichess_game_load_fen','_hebichess_game_fen','_hebichess_game_legal_moves','_hebichess_game_apply','_hebichess_game_status'] -sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8']"
  )
endif()

option(HEBICHESS_BUILD_WASM_NODE_TEST "Build the test-only Node-compatible WebAssembly engine" OFF)
if(HEBICHESS_BUILD_WASM_NODE_TEST)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "HEBICHESS_BUILD_WASM_NODE_TEST requires Emscripten (configure with emcmake cmake)")
  endif()
  add_executable(HebiChessWasmNodeTest src/wasm_bridge.cpp ${CHESS_SOURCES})
  target_include_directories(HebiChessWasmNodeTest PRIVATE src)
  target_compile_definitions(HebiChessWasmNodeTest PRIVATE HEBICHESS_WASM=1
    HEBICHESS_NNUE_HIDDEN1_LAYOUT=0)
  set_target_properties(HebiChessWasmNodeTest PROPERTIES
    OUTPUT_NAME hebichess-node
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build-wasm/node-test"
    SUFFIX ".js"
  LINK_FLAGS "--no-entry -O3 -sINITIAL_MEMORY=201326592 -sMAXIMUM_MEMORY=402653184 -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_NAME=createHebiChessNodeTestModule -sENVIRONMENT=web,worker,node -sEXPORTED_FUNCTIONS=['_malloc','_free','_hebichess_initialize','_hebichess_send_command','_hebichess_take_output','_hebichess_nnue_load_bytes','_hebichess_nnue_clear','_hebichess_nnue_evaluate_fen_raw','_hebichess_game_reset','_hebichess_game_load_fen','_hebichess_game_fen','_hebichess_game_legal_moves','_hebichess_game_apply','_hebichess_game_status'] -sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8']"
  )
endif()

# Test-only WASM evaluator A/B binaries.  They are emitted outside web/public,
# have no production route, and are selected solely at compile time.
option(HEBICHESS_BUILD_WASM_NNUE_LAYOUT_VARIANTS
  "Build test-only Original4/Prepacked4/Prepacked8 Node WASM evaluators" OFF)
if(HEBICHESS_BUILD_WASM_NNUE_LAYOUT_VARIANTS)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "HEBICHESS_BUILD_WASM_NNUE_LAYOUT_VARIANTS requires Emscripten")
  endif()
  function(add_wasm_nnue_layout_variant suffix layout export_name)
    add_executable(HebiChessWasmNnue${suffix} src/wasm_bridge.cpp ${CHESS_SOURCES})
    target_include_directories(HebiChessWasmNnue${suffix} PRIVATE src)
    target_compile_definitions(HebiChessWasmNnue${suffix} PRIVATE
      HEBICHESS_WASM=1 HEBICHESS_NNUE_HIDDEN1_LAYOUT=${layout})
    set_target_properties(HebiChessWasmNnue${suffix} PROPERTIES
      OUTPUT_NAME "hebichess-nnue-${suffix}"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build-wasm/nnue-layouts"
      SUFFIX ".js"
      LINK_FLAGS "--no-entry -O3 -sINITIAL_MEMORY=201326592 -sMAXIMUM_MEMORY=402653184 -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_NAME=${export_name} -sENVIRONMENT=node -sEXPORTED_FUNCTIONS=['_malloc','_free','_hebichess_initialize','_hebichess_send_command','_hebichess_take_output','_hebichess_nnue_load_bytes','_hebichess_nnue_clear','_hebichess_nnue_evaluate_fen_raw'] -sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8']"
    )
  endfunction()
  add_wasm_nnue_layout_variant(Original4 0 createHebiChessNnueOriginal4Module)
  add_wasm_nnue_layout_variant(Prepacked4 4 createHebiChessNnuePrepacked4Module)
  add_wasm_nnue_layout_variant(Prepacked8 8 createHebiChessNnuePrepacked8Module)
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

add_executable(HebiChessNnueSearchAccumulatorTest
  tests/nnue_search_accumulator_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessNnueSearchAccumulatorTest PRIVATE src)
target_compile_definitions(HebiChessNnueSearchAccumulatorTest
  PRIVATE HEBICHESS_NNUE_SEARCH_TEST=1)

# Cross-platform fixed-depth search diagnostics.  All three targets share the
# same benchmark/search source; only the hidden1 dense data layout differs.
function(add_nnue_search_layout_variant suffix layout)
  add_executable(HebiChessSearchBaseline${suffix}
    tests/search_nnue_layout_benchmark.cpp
    ${CHESS_SOURCES}
  )
  target_include_directories(HebiChessSearchBaseline${suffix} PRIVATE src)
  target_compile_definitions(HebiChessSearchBaseline${suffix} PRIVATE
    HEBICHESS_NNUE_VARIANT_TEST=1
    HEBICHESS_NNUE_HIDDEN1_LAYOUT=${layout}
  )
endfunction()

add_nnue_search_layout_variant(Original4 0)
add_nnue_search_layout_variant(Prepacked4 4)
add_nnue_search_layout_variant(Prepacked8 8)

add_executable(HebiChessGameStateTest
  tests/game_state_test.cpp
  ${CHESS_SOURCES}
)
target_include_directories(HebiChessGameStateTest PRIVATE src)

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
