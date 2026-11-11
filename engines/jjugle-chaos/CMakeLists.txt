cmake_minimum_required(VERSION 3.16)
project(HebiChessChaosV497 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CHESS_SOURCES
  src/chess/board.cpp src/chess/movegen.cpp src/chess/game_state.cpp src/chess/perft.cpp
  src/chess/eval.cpp src/chess/nnue.cpp src/chess/nnue_features.cpp
  src/chess/search.cpp src/chess/uci.cpp src/chess/zobrist.cpp src/chess/tt.cpp src/chess/see.cpp
  src/chess/uci_engine.cpp src/chess/opening_book.cpp
)

# Compile the shared engine core once per meaningful macro configuration.
# The prior harness rebuilt all 14 sources for every test executable, making
# full regression runs unnecessarily slow on the ARM64 deployment VM.
add_library(ChessCoreDefault OBJECT ${CHESS_SOURCES})
target_include_directories(ChessCoreDefault PUBLIC src)

add_library(ChessCoreProduction OBJECT ${CHESS_SOURCES})
target_include_directories(ChessCoreProduction PUBLIC src)
target_compile_definitions(ChessCoreProduction PRIVATE
  HEBICHESS_QSEARCH_DELTA_PRUNING=1
  HEBICHESS_QSEARCH_TT_VARIANT=2
  HEBICHESS_QSEARCH_TT_CUTOFF_MASK=7
  HEBICHESS_QSEARCH_TT_PROFILE=0
  HEBICHESS_QSEARCH_TT_DIAGNOSTIC=0
  HEBICHESS_QSEARCH_LAZY_CHECKS=0
  HEBICHESS_NNUE_HIDDEN1_VARIANT=8
  HEBICHESS_NMP_CANDIDATE_G=0
  HEBICHESS_EVALCACHE_EXACT=1
  HEBICHESS_EVALCACHE_EXACT_WAYS=2
  HEBICHESS_EVALCACHE_EXACT_ENTRIES=8192
)

add_library(ChessCoreSearchWindow OBJECT ${CHESS_SOURCES})
target_include_directories(ChessCoreSearchWindow PUBLIC src)
target_compile_definitions(ChessCoreSearchWindow PRIVATE HEBICHESS_SEARCH_TT_WINDOW_TEST=1)

enable_testing()

add_executable(HebiChess src/main.cpp $<TARGET_OBJECTS:ChessCoreProduction>)
target_include_directories(HebiChess PRIVATE src)

add_executable(HebiChessSearchTest tests/search_test.cpp $<TARGET_OBJECTS:ChessCoreSearchWindow>)
target_include_directories(HebiChessSearchTest PRIVATE src)
target_compile_definitions(HebiChessSearchTest PRIVATE HEBICHESS_SEARCH_TT_WINDOW_TEST=1)
add_test(NAME chaos_v48_search_regressions COMMAND HebiChessSearchTest)

# V4.8 targeted queen-preservation regression on actual user PGNs.
add_executable(QueenStyleTest tests/queen_preservation_test.cpp $<TARGET_OBJECTS:ChessCoreProduction>)
target_include_directories(QueenStyleTest PRIVATE src)
add_test(NAME chaos_v48_queen_preservation COMMAND QueenStyleTest)

add_executable(V49BehaviorTest tests/v49_behavior_test.cpp $<TARGET_OBJECTS:ChessCoreDefault>)
target_include_directories(V49BehaviorTest PRIVATE src)
add_test(NAME chaos_v49_history COMMAND V49BehaviorTest)

add_executable(V491RepetitionTest tests/v491_repetition_test.cpp $<TARGET_OBJECTS:ChessCoreDefault>)
target_include_directories(V491RepetitionTest PRIVATE src)
add_test(NAME chaos_v491_threefold_search COMMAND V491RepetitionTest)

add_executable(V492PasserTest tests/v492_passer_test.cpp $<TARGET_OBJECTS:ChessCoreDefault>)
target_include_directories(V492PasserTest PRIVATE src)
add_test(NAME chaos_v492_passer_potential COMMAND V492PasserTest)

add_executable(V493PasserSafetyTest tests/v493_passer_safety_test.cpp $<TARGET_OBJECTS:ChessCoreDefault>)
target_include_directories(V493PasserSafetyTest PRIVATE src)
add_test(NAME chaos_v493_passer_safety COMMAND V493PasserSafetyTest)

add_executable(V494ConversionTest tests/v494_conversion_test.cpp $<TARGET_OBJECTS:ChessCoreDefault>)
target_include_directories(V494ConversionTest PRIVATE src)
add_test(NAME chaos_v494_win_conversion COMMAND V494ConversionTest)

add_executable(V495KingCacheTest tests/v495_king_cache_test.cpp
  src/chess/board.cpp src/chess/movegen.cpp src/chess/zobrist.cpp)
target_include_directories(V495KingCacheTest PRIVATE src)
add_test(NAME chaos_v495_king_cache COMMAND V495KingCacheTest)

add_executable(V496FightingSpiritTest tests/v496_fighting_spirit_test.cpp $<TARGET_OBJECTS:ChessCoreDefault>)
target_include_directories(V496FightingSpiritTest PRIVATE src)
add_test(NAME chaos_v496_fighting_spirit COMMAND V496FightingSpiritTest)

# V497: material imbalance, minor-piece coordination and losing-position draw defense.
add_executable(V497StrategyTest tests/v497_strategy_test.cpp $<TARGET_OBJECTS:ChessCoreDefault>)
target_include_directories(V497StrategyTest PRIVATE src)
add_test(NAME chaos_v497_strategy COMMAND V497StrategyTest)
