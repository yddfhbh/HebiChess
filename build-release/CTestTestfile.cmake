# CMake generated Testfile for 
# Source directory: /home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e
# Build directory: /home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/build-release
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(smoke "/usr/bin/cmake" "-DENGINE=/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/build-release/HebiChess" "-DINPUT=/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/tests/smoke.in" "-P" "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/tests/smoke.cmake")
set_tests_properties(smoke PROPERTIES  _BACKTRACE_TRIPLES "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;136;add_test;/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;0;")
add_test(chess_types "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/build-release/HebiChessTypesTest")
set_tests_properties(chess_types PROPERTIES  _BACKTRACE_TRIPLES "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;143;add_test;/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;0;")
add_test(chess_movegen "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/build-release/HebiChessMovegenTest")
set_tests_properties(chess_movegen PROPERTIES  _BACKTRACE_TRIPLES "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;144;add_test;/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;0;")
add_test(chess_perft "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/build-release/HebiChessPerftTest")
set_tests_properties(chess_perft PROPERTIES  _BACKTRACE_TRIPLES "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;145;add_test;/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;0;")
add_test(chess_search "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/build-release/HebiChessSearchTest")
set_tests_properties(chess_search PROPERTIES  _BACKTRACE_TRIPLES "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;146;add_test;/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;0;")
add_test(chess_uci "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/build-release/HebiChessUciTest")
set_tests_properties(chess_uci PROPERTIES  _BACKTRACE_TRIPLES "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;147;add_test;/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;0;")
add_test(chess_zobrist_tt "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/build-release/HebiChessZobristTtTest")
set_tests_properties(chess_zobrist_tt PROPERTIES  _BACKTRACE_TRIPLES "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;148;add_test;/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;0;")
add_test(chess_see "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/build-release/HebiChessSeeTest")
set_tests_properties(chess_see PROPERTIES  _BACKTRACE_TRIPLES "/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;149;add_test;/home/ubuntu/.local/share/agent-hebi/task-worktrees/hebichess/3471a9d83b0048398c29d36b0232ea5e/CMakeLists.txt;0;")
