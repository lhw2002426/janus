#ifndef _EXEC_PUG_LONG_H_
#define _EXEC_PUG_LONG_H_
#include "exec.h"

struct plan_long_repo_t {
  int *plans;
  int plan_count;
  int initial_plan_count;
  int max_plan_size;
  int plan_size_in_bytes;
  int cap;

  int _cur_index;
  int _subplan_count;
};

struct exec_pug_long_t {
  struct exec_t;

  struct plan_long_repo_t     *plans;
  struct plan_t          *planner;
  struct plan_iterator_t *iter;

  struct predictor_t *pred;
  struct rvar_t      **steady_packet_loss;
  struct rvar_t      **steady_cost;

  int shortterm_samples;
};

struct exec_t *exec_pug_long_create(void);

#endif // _EXEC_PUG_LONG_H_