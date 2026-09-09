if(NOT DEFINED ENGINE OR ENGINE STREQUAL "")
  message(FATAL_ERROR "ENGINE is required")
endif()

if(NOT DEFINED INPUT OR INPUT STREQUAL "")
  message(FATAL_ERROR "INPUT is required")
endif()

if(NOT EXISTS "${ENGINE}")
  message(FATAL_ERROR "HebiChess engine does not exist: ${ENGINE}")
endif()

if(NOT EXISTS "${INPUT}")
  message(FATAL_ERROR "Smoke input does not exist: ${INPUT}")
endif()

execute_process(
  COMMAND "${ENGINE}"
  INPUT_FILE "${INPUT}"
  OUTPUT_VARIABLE ENGINE_OUTPUT
  ERROR_VARIABLE ENGINE_ERROR
  RESULT_VARIABLE ENGINE_RESULT
  TIMEOUT 10
)

if(NOT ENGINE_RESULT EQUAL 0)
  message(FATAL_ERROR
    "HebiChess smoke process failed: result=${ENGINE_RESULT}\n"
    "stdout:\n${ENGINE_OUTPUT}\n"
    "stderr:\n${ENGINE_ERROR}"
  )
endif()

string(FIND "${ENGINE_OUTPUT}" "uciok" UCIOK_POS)
if(UCIOK_POS EQUAL -1)
  message(FATAL_ERROR
    "HebiChess smoke output is missing uciok\n"
    "stdout:\n${ENGINE_OUTPUT}"
  )
endif()

string(FIND "${ENGINE_OUTPUT}" "option name EvalMode type combo default HCE var HCE var NNUE" EVALMODE_OPTION_POS)
if(EVALMODE_OPTION_POS EQUAL -1)
  message(FATAL_ERROR "HebiChess smoke output is missing the HCE EvalMode option\nstdout:\n${ENGINE_OUTPUT}")
endif()

string(FIND "${ENGINE_OUTPUT}" "error EvalMode NNUE unavailable: no network loaded; retaining HCE" NNUE_UNAVAILABLE_POS)
if(NNUE_UNAVAILABLE_POS EQUAL -1)
  message(FATAL_ERROR "HebiChess smoke output does not report unavailable NNUE\nstdout:\n${ENGINE_OUTPUT}")
endif()

string(FIND "${ENGINE_OUTPUT}" "readyok" READYOK_POS)
if(READYOK_POS EQUAL -1)
  message(FATAL_ERROR
    "HebiChess smoke output is missing readyok\n"
    "stdout:\n${ENGINE_OUTPUT}"
  )
endif()
