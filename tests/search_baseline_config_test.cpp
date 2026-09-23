#include <iostream>

#include "chess/search.hpp"

#ifndef HEBICHESS_QSEARCH_DELTA_PRUNING
#define HEBICHESS_QSEARCH_DELTA_PRUNING 1
#endif
#ifndef HEBICHESS_QSEARCH_TT_VARIANT
#define HEBICHESS_QSEARCH_TT_VARIANT 0
#endif
#ifndef HEBICHESS_QSEARCH_TT_CUTOFF_MASK
#define HEBICHESS_QSEARCH_TT_CUTOFF_MASK 0
#endif
#ifndef HEBICHESS_QSEARCH_TT_PROFILE
#define HEBICHESS_QSEARCH_TT_PROFILE 0
#endif
#ifndef HEBICHESS_QSEARCH_TT_DIAGNOSTIC
#define HEBICHESS_QSEARCH_TT_DIAGNOSTIC 0
#endif
#ifndef HEBICHESS_QSEARCH_LAZY_CHECKS
#define HEBICHESS_QSEARCH_LAZY_CHECKS 0
#endif
#ifndef HEBICHESS_NNUE_HIDDEN1_VARIANT
#define HEBICHESS_NNUE_HIDDEN1_VARIANT 8
#endif
#ifndef HEBICHESS_EXPECTED_NNUE_HIDDEN1_VARIANT
#define HEBICHESS_EXPECTED_NNUE_HIDDEN1_VARIANT 8
#endif

static_assert(HEBICHESS_QSEARCH_DELTA_PRUNING == 1,
              "SearchBaseline must use production delta pruning");
static_assert(HEBICHESS_QSEARCH_TT_VARIANT == 2,
              "SearchBaseline must use the production QTT variant");
static_assert(HEBICHESS_QSEARCH_TT_CUTOFF_MASK == 7,
              "SearchBaseline must use all production QTT cutoff bounds");
static_assert(HEBICHESS_QSEARCH_TT_PROFILE == 0,
              "SearchBaseline must keep QTT profiling disabled");
static_assert(HEBICHESS_QSEARCH_TT_DIAGNOSTIC == 0,
              "SearchBaseline must keep QTT diagnostics disabled");
static_assert(HEBICHESS_QSEARCH_LAZY_CHECKS == 0,
              "SearchBaseline must keep lazy QSearch checks disabled");
static_assert(HEBICHESS_NNUE_HIDDEN1_VARIANT == HEBICHESS_EXPECTED_NNUE_HIDDEN1_VARIANT,
              "SearchBaseline must use its selected hidden1 loop variant");

int main() {
  std::cout << "production qsearch config: delta=1 variant=2 cutoff_mask=7 "
            << "lazy_checks=0 hidden1_variant=" << HEBICHESS_NNUE_HIDDEN1_VARIANT << '\n';
  return 0;
}
