#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "algo/array.h"
#include "config.h"
#include "exec/longterm.h"
#include "exec/ltg.h"
#include "exec/pug.h"
#include "exec/stats.h"
#include "util/common.h"
#include "util/log.h"

#include<sys/time.h>
#include<time.h>

void usage(const char *fname) {
  const char *usage_message = ""
    "usage: %s <experiment-setting ini file> [OPTIONS]\n"
		"\nAvailable options:\n"
		"\t-a [ACTION]\t Choose an action: pug, pug-long, pug-lookback, ltg, stats, long-term\n"
		"\t-x\t\t Explain the action\n"
		"";

  printf(usage_message, fname);
  exit(EXIT_FAILURE);
}

struct exec_t *executor(struct expr_t *expr) {
  if (expr->action == BUILD_LONGTERM) {
    return exec_longterm_create();
  } else if (expr->action == RUN_PUG) {
    return exec_pug_create_short_and_long_term();
  } else if (expr->action == RUN_PUG_LONG) {
    return exec_pug_create_long_term_only();
  } else if (expr->action == RUN_PUG_LOOKBACK) {
    return exec_pug_create_lookback();
  } else if (expr->action == TRAFFIC_STATS) {
    return exec_traffic_stats_create();
  } else if (expr->action == RUN_LTG) {
    return exec_ltg_create();
  }

  panic_txt("Executor not implemented.");
  return 0;
}

static void logo(void) {
  const char text[] = ""
 "  _   _      _  ______ _____ \n"
 " | \\ | |    | | | ___ \\  ___|\n"
 " |  \\| | ___| |_| |_/ / |__  \n"
 " | . ` |/ _ \\ __|    /|  __| \n"
 " | |\\  |  __/ |_| |\\ \\| |___ \n"
 " \\_| \\_/\\___|\\__\\_| \\_\\____/ \n"
 "  A Network Risk Estimator   \n";

	printf("%s\n\n", text);
}

void summarize_result(struct exec_output_t *out) {
  unsigned size = 0;
  struct exec_result_t *res = array_splice(
      out->result, 0, array_size(out->result)-1, &size);

  risk_cost_t sum_cost = 0;
  double sum_len  = 0;

  risk_cost_t sqr_cost = 0;
  double sqr_len = 0;

  for (int i = 0; i < size;++i ){
    risk_cost_t cost = res[i].cost;
    unsigned steps = res[i].num_steps;

    sum_cost += cost; sqr_cost += cost * cost;
    sum_len += steps; sqr_len += steps * steps;
  }

  double average_cost = sum_cost / size;
  double average_len = sum_len / size;

  risk_cost_t std_cost = sqrt(fabs(sqr_cost / size - average_cost * average_cost));
  risk_cost_t std_len = sqrt(fabs(sqr_len / size - average_len * average_len));

  printf("Statistics - len: %f (%f), cost: %f (%f)\n", 
      average_len, std_len, average_cost, std_cost);
}

int main(int argc, char **argv) {
  logo();
  if (argc < 2) {
    usage(argv[0]);
    exit(1);
  }
  int num_threads = get_ncores() - 1;
  info("nthread: %d",num_threads);
  clock_t start, end; 
  start = clock();

  struct expr_t expr = {0};
  config_parse(argv[1], &expr, argc - 1, argv + 1);
  struct exec_t *exec = executor(&expr);
  printf("going to exec\n");
  if (!expr.explain) {
    printf("going to validate\n");
    exec->validate(exec, &expr);
    printf("validate finished\n");
    struct exec_output_t *out = exec->run(exec, &expr);
    printf("maybe get output\n");
    if (out) {
      printf("haven get output\n");
      summarize_result(out);
      array_free(out->result);
      free(out);
      out = 0;
    }
  } else {
    exec->explain(exec);
  }
  //printf("exec finished\n");
  end = clock();
  printf("time=%f\n", (double)(end - start) / CLOCKS_PER_SEC);
  info("exec time=%f\n",  (double)(end - start) / CLOCKS_PER_SEC);
  return EXIT_SUCCESS;
}
