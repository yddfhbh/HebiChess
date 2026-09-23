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

int main() {
  std::cout << "production qsearch config: delta=1 variant=2 cutoff_mask=7\n";
  return 0;
}
