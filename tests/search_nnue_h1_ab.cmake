if(NOT DEFINED BASELINE OR NOT DEFINED CANDIDATE OR NOT DEFINED FIXTURE)
  message(FATAL_ERROR "BASELINE, CANDIDATE, and FIXTURE are required")
endif()

if(NOT DEFINED OUTPUT_DIR)
  set(OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/search-nnue-h1-ab")
endif()
if(NOT DEFINED DEPTH)
  set(DEPTH 4)
endif()
if(NOT DEFINED TIME_MS)
  set(TIME_MS 1)
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(BASELINE_OUTPUT "${OUTPUT_DIR}/baseline.json")
set(CANDIDATE_OUTPUT "${OUTPUT_DIR}/candidate.json")

# A model-free VM smoke still compares the full search fixture at fixed depth.
# Passing -DNETWORK=<frozen-v3.hebinnue> switches both binaries to NNUE and
# additionally compares their per-position raw NNUE scores.
set(mode_args --modes hce)
set(network_args)
if(DEFINED NETWORK AND NOT NETWORK STREQUAL "")
  set(mode_args --modes nnue)
  set(network_args --network "${NETWORK}")
endif()

foreach(binary IN ITEMS baseline candidate)
  if(binary STREQUAL "baseline")
    set(engine "${BASELINE}")
    set(report "${BASELINE_OUTPUT}")
  else()
    set(engine "${CANDIDATE}")
    set(report "${CANDIDATE_OUTPUT}")
  endif()
  execute_process(
    COMMAND "${engine}" --fixture "${FIXTURE}" --output "${report}"
            --depth "${DEPTH}" --time-ms "${TIME_MS}" ${mode_args} ${network_args}
            --eval-warmup 1 --eval-iters 1 --eval-samples 1
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${binary} H1 A/B run failed (${result}): ${stdout}${stderr}")
  endif()
endforeach()

file(READ "${BASELINE_OUTPUT}" baseline_report)
file(READ "${CANDIDATE_OUTPUT}" candidate_report)
string(REGEX MATCHALL "\"benchmark\":\"fixed_depth\"[^\n]*" baseline_records "${baseline_report}")
string(REGEX MATCHALL "\"benchmark\":\"fixed_depth\"[^\n]*" candidate_records "${candidate_report}")
list(LENGTH baseline_records baseline_count)
list(LENGTH candidate_records candidate_count)
if(NOT baseline_count EQUAL candidate_count OR baseline_count EQUAL 0)
  message(FATAL_ERROR "fixed-depth H1 A/B reports have different or empty record counts")
endif()

math(EXPR last_index "${baseline_count} - 1")
foreach(index RANGE ${last_index})
  list(GET baseline_records ${index} baseline_record)
  list(GET candidate_records ${index} candidate_record)
  foreach(record_name IN ITEMS baseline_record candidate_record)
    string(REGEX REPLACE
      ".*\"name\":\"([^\"]+)\".*\"bestmove\":\"([^\"]+)\".*\"score_cp\":(-?[0-9]+).*\"completed_depth\":([0-9]+).*\"root_nnue_raw\":([^,}]+).*"
      "\\1|\\2|\\3|\\4|\\5" ${record_name} "${${record_name}}")
  endforeach()
  if(NOT baseline_record STREQUAL candidate_record)
    message(FATAL_ERROR "fixed-depth H1 A/B mismatch at record ${index}: ${baseline_record} vs ${candidate_record}")
  endif()
endforeach()

if(DEFINED NETWORK AND NOT NETWORK STREQUAL "")
  message(STATUS "H1=4/H1=8 NNUE fixed-depth parity passed: ${baseline_count} positions; raw NNUE exact")
else()
  message(STATUS "H1=4/H1=8 fixed-depth HCE smoke passed: ${baseline_count} positions; pass -DNETWORK for NNUE/raw parity")
endif()
