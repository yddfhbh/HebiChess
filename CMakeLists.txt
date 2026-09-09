cmake_minimum_required(VERSION 3.16)
project(HebiChess LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CHESS_SOURCES
  src/chess/board.cpp src/chess/movegen.cpp src/chess/perft.cpp
  src/chess/eval.cpp src/chess/nnue.cpp src/chess/nnue_features.cpp
  src/chess/search.cpp src/chess/uci.cpp src/chess/zobrist.cpp src/chess/tt.cpp src/chess/see.cpp
)

add_executable(HebiChess
  src/main.cpp
  ${CHESS_SOURCES}
)

target_include_directories(HebiChess PRIVATE src)

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
