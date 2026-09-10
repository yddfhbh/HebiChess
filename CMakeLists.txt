cmake_minimum_required(VERSION 3.16)
project(HebiChess LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CHESS_SOURCES
  src/chess/board.cpp src/chess/movegen.cpp src/chess/perft.cpp
  src/chess/eval.cpp src/chess/nnue.cpp src/chess/nnue_features.cpp
  src/chess/search.cpp src/chess/uci.cpp src/chess/zobrist.cpp src/chess/tt.cpp src/chess/see.cpp
  src/chess/uci_engine.cpp
)

add_executable(HebiChess
  src/main.cpp
  ${CHESS_SOURCES}
)

target_include_directories(HebiChess PRIVATE src)

option(HEBICHESS_BUILD_WASM "Build the browser-only HCE WebAssembly engine" OFF)
if(HEBICHESS_BUILD_WASM)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "HEBICHESS_BUILD_WASM requires Emscripten (configure with emcmake cmake)")
  endif()
  add_executable(HebiChessWasm src/wasm_bridge.cpp ${CHESS_SOURCES})
  target_include_directories(HebiChessWasm PRIVATE src)
  target_compile_definitions(HebiChessWasm PRIVATE HEBICHESS_WASM=1)
  set_target_properties(HebiChessWasm PROPERTIES
    OUTPUT_NAME hebichess
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/web/public/engine"
    SUFFIX ".js"
    LINK_FLAGS "--no-entry -O3 -sINITIAL_MEMORY=134217728 -sMODULARIZE=1 -sEXPORT_NAME=createHebiChessModule -sENVIRONMENT=web,worker -sEXPORTED_FUNCTIONS=['_hebichess_initialize','_hebichess_send_command','_hebichess_take_output'] -sEXPORTED_RUNTIME_METHODS=['ccall']"
  )
endif()

option(HEBICHESS_BUILD_WASM_NODE_TEST "Build the test-only Node-compatible HCE WebAssembly engine" OFF)
if(HEBICHESS_BUILD_WASM_NODE_TEST)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "HEBICHESS_BUILD_WASM_NODE_TEST requires Emscripten (configure with emcmake cmake)")
  endif()
  add_executable(HebiChessWasmNodeTest src/wasm_bridge.cpp ${CHESS_SOURCES})
  target_include_directories(HebiChessWasmNodeTest PRIVATE src)
  target_compile_definitions(HebiChessWasmNodeTest PRIVATE HEBICHESS_WASM=1)
  set_target_properties(HebiChessWasmNodeTest PROPERTIES
    OUTPUT_NAME hebichess-node
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build-wasm/node-test"
    SUFFIX ".js"
    LINK_FLAGS "--no-entry -O3 -sINITIAL_MEMORY=134217728 -sMODULARIZE=1 -sEXPORT_NAME=createHebiChessNodeTestModule -sENVIRONMENT=web,worker,node -sEXPORTED_FUNCTIONS=['_hebichess_initialize','_hebichess_send_command','_hebichess_take_output'] -sEXPORTED_RUNTIME_METHODS=['ccall']"
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
