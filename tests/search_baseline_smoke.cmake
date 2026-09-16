if(NOT DEFINED BASELINE OR NOT DEFINED FIXTURE OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "BASELINE, FIXTURE, and OUTPUT are required")
endif()
execute_process(
  COMMAND "${BASELINE}" --fixture "${FIXTURE}" --output "${OUTPUT}"
          --depth 1 --time-ms 5 --modes hce,nnue --eval-warmup 1 --eval-iters 4 --eval-samples 1
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "search baseline smoke failed (${result}): ${stdout}${stderr}")
endif()
file(READ "${OUTPUT}" report)
string(FIND "${report}" "hebichess-phase6-search-baseline-v1" schema_at)
string(FIND "${report}" "critical-phase4-index-13" critical_at)
string(FIND "${report}" "\"eval_mode\":\"HCE\"" hce_at)
string(FIND "${report}" "NNUE unavailable" nnue_at)
if(schema_at EQUAL -1 OR critical_at EQUAL -1 OR hce_at EQUAL -1 OR nnue_at EQUAL -1)
  message(FATAL_ERROR "search baseline smoke produced an incomplete report")
endif()
