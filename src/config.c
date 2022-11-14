#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util/common.h"
#include "inih/ini.h"
#include "failures/jupiter.h"
#include "networks/jupiter.h"
#include "risk.h"
#include "util/log.h"

#include "config.h"

static char* VERBOSITY_TEXT[] = {
  "[0] Enjoy the silence.",
  "[1] Room noise level",
  "[2] Coffeeshop noise level",
  "[3] Auditarium noise level",
  "[4] Show me everything (with cool graphs. kthx.)",
  "[5] No really, show me everything (graphs and kittens and donuts.)"
};

static struct risk_cost_func_t *risk_violation_name_to_func(char const *name) {
  return risk_cost_string_to_func(name);
}

static int _cutoff_at(struct criteria_time_t *ct, uint32_t length) {
  return ct->steps >= length;
}

static risk_cost_t _cutoff_zero_cost(struct criteria_time_t *ct, unsigned step) {
  return 0;
}

static risk_cost_t _cutoff_cost(struct criteria_time_t *ct, unsigned step) {
  if (step < 1)
    panic("Asking for the cost of a plan before it has started: %d", step);

  if (step > ct->steps) {
    panic("Cannot ask for the cost of cutoff after the number of allowed steps: %d > %d", step, ct->steps);
  }

  return ct->steps_cost[step - 1];
}

static struct criteria_time_t *risk_delay_name_to_func(char const *name) {
  char *ptr = strstr(name, "cutoff-at-");

  if (ptr == 0)
    panic("Unsupported criteria_time function: %s", name);

  ptr += strlen("cutoff-at-");
  struct criteria_time_t *ret = malloc(sizeof(struct criteria_time_t));
  ret->acceptable = _cutoff_at;

  char *tmp = strdup(ptr);
  char *cur = tmp;
  while (*cur != 0 && *cur != '/') {
    info("cur: %c", *cur);
    cur++;
  }

  if (*cur == 0) {
    info("No time cost provided assuming zero cost for time: %s", name);
    ret->steps = strtoul(tmp, 0, 0);
    ret->cost = _cutoff_zero_cost;
    free(tmp);
    //printf("ret steps: %d\n",ret->steps);
    return ret;
  }

  *cur = 0;
  ret->steps = strtoul(tmp, 0, 0);
  ret->steps_cost = malloc(sizeof(risk_cost_t) * ret->steps);

  // Read the rest of the
  int idx = 0; cur++;
  char *pch = strtok(cur, ",");
  risk_cost_t sum = 0;
  while (pch != 0 && idx < ret->steps) {
    sum += atof(pch);
    ret->steps_cost[idx] = sum;
    pch = strtok(0, ",");
    idx++;
  }

  if (idx != ret->steps)
    panic("Not enough numbers passed to cutoff time criteria cost function.  Format is: cutoff-at-[NSTEPS]/STEP1_COST,STEP2_COST,...STEPN_COST: %s", name);

  if (pch != 0)
    panic("Too many numbers passed to cutoff time criteria cost function.  Format is: cutoff-at-[NSTEPS]/STEP1_COST,STEP2_COST,...STEPN_COST: %s", name);

  ret->cost = _cutoff_cost;
  //printf("ret steps: %d\n",ret->steps);
  return ret;
}

static int _cl_maximize(unsigned a1, unsigned a2) {
  if (a1 > a2) { return 1; }
  else if (a1 == a2) { return 0; }
  else if (a1 < a2) { return -1; }
  return -1;
}

static int _cl_minimize (unsigned a1, unsigned a2) {
  if (a1 > a2) { return -1; }
  else if (a1 == a2) { return 0; }
  else if (a1 < a2) { return 1; }
  return -1;
}

static criteria_length_t risk_length_name_to_func(char const *name) {
  if (strcmp(name, "maximize") == 0) {
    return _cl_maximize;
  } else if (strcmp(name, "minimize") == 0) {
    return _cl_minimize;
  }
  panic("Unsupported length criteria: %s", name);
  return 0;
}

static struct network_t *string_to_network(struct expr_t const *expr, char const *string) {
  char *network_type;
  char network_info[201];
  int tot_read;
  memcpy(network_info,strchr(string,'-')+1,200);
  printf("string to network: %s\n",string);
  //network_info = strchr(string,'-');
  //printf("info:%s\n",network_info);
  /*if (sscanf(string, "%s-%s%n",network_type, network_info,tot_read) <= 0) {
    
    panic("Bad format specifier for jupiter: %s", string);
  }*/
  network_type = strtok(string, "-");
  printf("network type: %s\n",network_type);
  //printf("%s\n%s\n%s\n",string, network_type,network_info);
  /*if (tot_read < strlen(string)) {
    panic("Bad format specifier for jupiter: %s", string);
  }*/

  //info("Creating a jupiter network with: %u, %u, %u, %u, %f", core, pod, app, tpp, bw);
  if(strcmp(network_type,"jupiter")==0)
  {
    
    uint32_t core, pod, app, tpp;
    bw_t bw;
    printf("network info:%s\n",network_info);
    //printf("%d %d\n",strlen(network_info),network_info[15]);
    sscanf(network_info ,"%u-%u-%u-%u-%f%n", &core, &pod, &app, &tpp, &bw, &tot_read);
    //scanf("%u %u %u %u %f", &core, &pod, &app, &tpp, &bw);
    /*scanf("%u",&core);
    printf("%u\n",core);
    scanf("%u",&pod);
    printf("%u\n",pod);
    scanf("%u",&app);
    printf("%u\n",app);
    scanf("%u",&tpp);
    printf("%u\n",tpp);
    scanf("%f",&bw);
    printf("%f\n",bw);*/
    printf("%u-%u-%u-%u-%f\n", core, pod, app, tpp, bw);
    return (struct network_t *)jupiter_network_create(core, pod, app, tpp, bw);
  }
  if(strcmp(network_type,"klotski")==0)
  {
    printf("create klotski net:%s\n",network_info);
    char* path1;
    char* path2;
    char* temp_info;
    float alpha,theta;
    int flag = 0;
    uint32_t nswitch,nedge;
    path1 = strtok(network_info,"&&");
    //printf("path1: %s\n",path1);
    path2 = strtok(NULL,"&&");
    //printf("path2: %s\n",path2);
    temp_info = strtok(NULL,"&&");
    //printf("info: %s\n",temp_info);
    sscanf(temp_info,"%u|%u|%f|%f|%d%n",&nswitch,&nedge,&alpha,&theta,&flag,&tot_read);
    //printf("%d %d %f %f\n",nswitch,nedge,alpha,theta);
    return (struct network_t *)klotski_network_create(path1,path2,nswitch,nedge,alpha,theta,flag);
  }
}

static void _expr_jupiter_network_set_variables(struct expr_t *expr) {
  struct jupiter_network_t *jup = (struct jupiter_network_t *)expr->network;
  expr->network_type = NET_JUPITER;
  expr->num_cores = jup->core;
  expr->num_pods = jup->pod;
  expr->num_tors_per_pod = jup->tor;
  expr->num_aggs_per_pod = jup->agg;
}

static void _expr_klotski_network_set_variables(struct expr_t *expr) {
  struct jupiter_network_t *jup = (struct jupiter_network_t *)expr->network;
  expr->network_type = NET_KLOTSKI;
}


static struct network_t *expr_clone_network(struct expr_t const *expr) {
  return string_to_network(expr, expr->network_string);
}

static void jupiter_add_upgrade_group(char const *string, struct jupiter_sw_up_list_t *list,enum EXPR_NETWORK_TYPE net_type,int flag) {
  int tot_read;
  char sw_type[256] = {0};
  unsigned location, count, color, label = 1;
  if(net_type == NET_KLOTSKI && flag == 3)
  {
    if (sscanf(string, "%[^-]-%u-%u-%u-%u%n", sw_type, &location, &count, &color, &label, &tot_read) <= 0) {
      panic("Bad format specifier for jupiter: %s", string);
    }
    //printf("test\n");
  }
  else
  {
    if (sscanf(string, "%[^-]-%u-%u-%u%n", sw_type, &location, &count, &color, &tot_read) <= 0) {
      panic("Bad format specifier for jupiter: %s", string);
    }
    //printf("%s %u %u %u\n",sw_type, location, count, color);
  }
    
  if (tot_read < strlen(string)) {
    panic("Bad format specifier for switch-upgrade: %s", string);
  }

  list->size += 1;
  list->sw_list = realloc(list->sw_list, sizeof(struct jupiter_sw_up_t) * list->size);
  
  list->num_switches += count;
  printf("count: %d numswitch: %d\n",count,list->num_switches);

  struct jupiter_sw_up_t *up = &list->sw_list[list->size-1];
  up->count = count;
  up->location = location;
  up->color = color;
  up->label = label;
  if(net_type == NET_JUPITER)
  {
    if (strcmp(sw_type, "core") == 0) {
      up->type = JST_CORE;
    } else if (strcmp(sw_type, "pod/agg") == 0) {
      up->type = JST_AGG;
    } else {
      panic("Bad switch type specified: %s", sw_type);
    }
  }
  else{
    if (strcmp(sw_type, "EB") == 0) {
      up->type = EB;
    } else if (strcmp(sw_type, "FAUU") == 0) {
      up->type = FAUU;
    } else if (strcmp(sw_type, "FADU") == 0) {
      up->type = FADU;
    } else if (strcmp(sw_type, "SSW") == 0) {
      up->type = SSW;
    } else if (strcmp(sw_type, "FSW") == 0) {
      up->type = FSW;
    } else if (strcmp(sw_type, "RSW") == 0) {
      up->type = RSW;
    } else if (strcmp(sw_type, "UNFAUU") == 0) {
      up->type = UNFAUU;
    } else if (strcmp(sw_type, "UNFADU") == 0) {
      up->type = UNFADU;
    } else if (strcmp(sw_type, "UNFSW") == 0) {
      up->type = UNFSW;
    } else if (strcmp(sw_type, "UNSSW") == 0) {
      up->type = UNSSW;
    } else {
      panic("Bad switch type specified: %s", sw_type);
    }
  }
}
  uint32_t klotski_get_id(struct jupiter_network_t* net, enum KLOTSKI_SWITCH_TYPE type)
{
  //printf("\n find id: %d\n",type);
    //printf("find id: %d\n",type);
    for (int i = 0; i < net->num_switches; i++)
    {
        //printf("%d %d  ",net->switches[i].type,net->used[i]);
        if (type == net->klotski_switches[i].type && net->used[i] == 0)
        {
            net->used[i] = 1;
            return i;
        }
    }
    printf("didnt find id\n");
}

static uint32_t
jupiter_add_freedom_degree(char const *string,
    uint32_t **freedom) {
  unsigned ndegree = 1;
  //printf("add freedom degree: %s\n",string);
  for (uint32_t i = 0; i < strlen(string); ++i) {
    ndegree += (string[i] == '-');
  }

  *freedom = malloc(sizeof(uint32_t) * ndegree);
  char *str = strdup(string);
  char const *ptr = strtok(str, "-");
  int degree = 0;

  while (ptr != 0) {
    (*freedom)[degree++] = (uint32_t)atoi(ptr);
    ptr = strtok(0, "-");
  }

  free(str);
  printf("add freedom degree: %s degree: %d\n",string,ndegree);
  return ndegree;
}

static int config_handler(void *data,
    char const *section, char const *name,
    char const *value) {
#define MATCH_SECTION(s) strcmp(section, s) == 0
#define MATCH_NAME(n) strcmp(name, n) == 0
#define MATCH(s, n) MATCH_SECTION(s) && MATCH_NAME(n)

  struct expr_t *expr = (struct expr_t *)data;

  if        (MATCH("general", "traffic-test")) {
    expr->traffic_test = strdup(value);
  } else if (MATCH("general", "traffic-training")) {
    expr->traffic_training = strdup(value);
  } else if (MATCH("general", "mop-duration")) {
    expr->mop_duration = atoi(value);
  } else if (MATCH("predictor", "ewma-coeff")) {
    expr->ewma_coeff = atof(value);
  } else if (MATCH("predictor", "type")) {
    expr->predictor_string = strdup(value);
  } else if (MATCH("criteria", "risk-violation")) {
    expr->risk_violation_cost = risk_violation_name_to_func(value);
  } else if (MATCH("criteria", "criteria-time")) {
    expr->criteria_time = risk_delay_name_to_func(value);
  } else if (MATCH("failure", "failure-mode")) {
    expr->failure_mode = strdup(value);
  } else if (MATCH("failure", "failure-warm-cost")) {
    expr->failure_warm_cost = atof(value);
  } else if (MATCH("failure", "concurrent-switch-failure")) {
    expr->failure_max_concurrent = strtoul(value, 0, 0);
  } else if (MATCH("failure", "concurrent-switch-probability")) {
    expr->failure_switch_probability = atof(value);
  } else if (MATCH("criteria", "criteria-length")) {
    expr->criteria_plan_length = risk_length_name_to_func(value);
  } else if (MATCH("criteria", "promised-throughput")) {
    expr->promised_throughput = atof(value);
  } else if (MATCH("pug", "backtrack-traffic-count")) {
    expr->pug_backtrack_traffic_count = atoi(value);
  } else if (MATCH("pug", "backtrack-direction")) {
    if (strcmp(value, "forward") == 0) {
      expr->pug_is_backtrack = 0;
    } else if (strcmp(value, "backward") == 0){
      expr->pug_is_backtrack = 1;
    } else {
      panic("Invalid [pug]->backtrack_direction: %s", value);
    }
  } else if (MATCH("general", "network")) {
    info("Parsing jupiter config: %s", value);
    expr->network_string = strdup(value);
    printf("network string: %s %d\n",expr->network_string,sizeof(expr->network_string));
    expr->network = string_to_network(expr, value);
    printf("test in config\n");
    if(expr->network->network_type == NET_JUPITER)
    {
      _expr_jupiter_network_set_variables(expr);
      printf("prase switches\n");
    }
    if(expr->network->network_type == NET_KLOTSKI)
    {
      _expr_klotski_network_set_variables(expr);
      struct jupiter_network_t* net = expr->network;
      for(int i = 0;i<net->num_switches;i++)
      {
        printf("%d ",net->klotski_switches[i].sid);
      }
      printf("prase switches\n");
    }
  } else if (MATCH_SECTION("upgrade")) {
    struct jupiter_network_t* jn = (struct jupiter_network_t*)expr->network;
    if (MATCH_NAME("switch-group")) {
      jupiter_add_upgrade_group(value, &expr->upgrade_list,expr->network_type,jn->flag);
    } else if (MATCH_NAME("freedom")) {
      expr->upgrade_nfreedom = jupiter_add_freedom_degree(value, &expr->upgrade_freedom);
    } else {
      panic("Upgrading %s not supported.", name);
    }
  } else if (MATCH("scenario", "time-begin")) {
    expr->scenario.time_begin = strtoul(value, 0, 0);
  } else if (MATCH("scenario", "time-end")) {
    expr->scenario.time_end = strtoul(value, 0, 0);
  } else if (MATCH("scenario", "time-step")) {
    expr->scenario.time_step = strtoul(value, 0, 0);
  } else if (MATCH("cache", "rv-cache-dir")) {
    expr->cache.rvar_directory = strdup(value);
  } else if (MATCH("cache", "ewma-cache-dir")) {
    expr->cache.ewma_directory = strdup(value);
  } else if (MATCH("cache", "perfect-cache-dir")) {
    expr->cache.perfect_directory = strdup(value);
  }

  return 1;
}

static void
_jupiter_build_located_switch_group(struct expr_t *expr) {
  //TODO: Assume jupiter network for now.
  unsigned nswitches = 0;
  for (uint32_t i = 0; i < expr->upgrade_list.size; ++i) {
    nswitches += expr->upgrade_list.sw_list[i].count;
  }

  struct jupiter_located_switch_t *sws = malloc(sizeof(struct jupiter_located_switch_t) * nswitches);
  uint32_t idx = 0;
  for (uint32_t i = 0; i < expr->upgrade_list.size; ++i) {
    struct jupiter_sw_up_t *up = &expr->upgrade_list.sw_list[i];
    for (uint32_t j = 0; j < up->count; ++j) {
      sws[idx].color = up->color;
      sws[idx].type = up->type;
      sws[idx].pod = up->location;
      if (up->type == JST_CORE) {
        sws[idx].sid = jupiter_get_core(expr->network, j);
      } else if (up->type == JST_AGG) {
        sws[idx].sid = jupiter_get_agg(expr->network, up->location, j);
      } else {
        panic("Unsupported type for located_switch: %d", up->type);
      }
      idx++;
    }
  }
  expr->located_switches = sws;
  expr->nlocated_switches = nswitches;
  
}
static void
_klotski_build_located_switch_group(struct expr_t* expr) {
    //TODO: Assume klotski network for now.
    printf("build klotski group\n");
    unsigned nswitches = 0;
    unsigned label_num = 0,labels[1001];
    memset(labels,0,sizeof(labels));
    for (uint32_t i = 0; i < expr->upgrade_list.size; ++i) {
        nswitches += expr->upgrade_list.sw_list[i].count;
        //struct klotski_sw_up_t *up = &expr->upgrade_list.sw_list[i];
        //printf("%d: %d\n",i,up->type);
    }
    struct jupiter_located_switch_t* sws = (struct jupiter_located_switch_t*)malloc(sizeof(struct jupiter_located_switch_t) * nswitches);
    uint32_t idx = 0;
    for (uint32_t i = 0; i < expr->upgrade_list.size; ++i) {
        struct jupiter_sw_up_t* up = &expr->upgrade_list.sw_list[i];
        //printf("%d %d %d %d\n",up->type,up->location,up->color,up->count);
        for (uint32_t j = 0; j < up->count; ++j) {
            sws[idx].color = up->color;
            sws[idx].stat = UP;
            sws[idx].type = up->type;
            sws[idx].pod = up->location;
            sws[idx].label = up->label;
            //printf("sws labels: %d\n",up->label);
            if(labels[up->label] == 0)
            {
              labels[up->label] = 1;
              label_num++;
            }
            sws[idx].sid = klotski_get_id(expr->network, up->type);
            idx++;
        }
    }
    
    expr->located_switches = sws;
    expr->nlocated_switches = nswitches;//lhw注意expr的nswitch和net的nswitch是不同的，因为EB和RSW不在搜索的范围之内
    expr->label_num = label_num;
    for(int i = 0;i<nswitches;i++)//lhw不知道为什么，不加这段输出就会报错，太玄学了
    {
      printf("%d %d %d %d\n",sws[i].sid,sws[i].stat,sws[i].type,sws[i].pod);
    }
    expr->network->copy_switches(expr->network,nswitches);
    printf("sws get\n");
}

static enum EXPR_ACTION
parse_action(char const *arg) {
  if      (strcmp(arg, "long-term") == 0) {
    info_txt("Building long-term cache files.");
    return BUILD_LONGTERM;
  } else if (strcmp(arg, "pug") == 0) {
    info_txt("Running PUG.");
    return RUN_PUG;
  } else if (strcmp(arg, "pug-long") == 0) {
    info_txt("Running PUG LONG.");
    return RUN_PUG_LONG;
  } else if (strcmp(arg, "pug-lookback") == 0) {
    info_txt("Running PUG LOOKBACK.");
    return RUN_PUG_LOOKBACK;
  } else if (strcmp(arg, "stg") == 0) {
    info_txt("Running STG.");
    return RUN_STG;
  } else if (strcmp(arg, "ltg") == 0) {
    info_txt("Running LTG.");
    return RUN_LTG;
  } else if (strcmp(arg, "cap") == 0) {
    info_txt("Running CAP.");
    return RUN_CAP;
  } else if (strcmp(arg, "stats") == 0) {
    info_txt("Running STATS.");
    return TRAFFIC_STATS;
  }

  panic("Invalid execution option: %s.", arg);
  return RUN_UNKNOWN;
}

static int
cmd_parse(int argc, char *const *argv, struct expr_t *expr) {
  int opt = 0;
  expr->explain = 0;
  expr->verbose = 0;
  while ((opt = getopt(argc, argv, "a:r:vx")) != -1) {
    switch (opt) {
      case 'a':
        expr->action = parse_action(optarg);
        break;
      case 'x':
        expr->explain = 1;
      case 'v':
        expr->verbose += 1;
    };
  }
  //printf("explan: %d\n",expr->explain);
  return 1;
}

static void _jupiter_build_failure_model(struct expr_t *expr) {
  if (!expr->failure_mode)
    panic_txt("Please select a failure mode in the ini file.");

  if (strcmp(expr->failure_mode, "warm") == 0) {
    expr->failure = (struct failure_model_t *)
      jupiter_failure_model_warm_create(
        expr->failure_max_concurrent,
        expr->failure_switch_probability,
        expr->failure_warm_cost);
  } else if (strcmp(expr->failure_mode, "independent") == 0) {
    expr->failure = (struct failure_model_t *)
      jupiter_failure_model_independent_create(
        expr->failure_max_concurrent,
        expr->failure_switch_probability);
  } else {
    panic("Invalid failure model: %s.", expr->failure_mode);
  }
}

/* Set some sensible defaults for experiments */
static void _expr_set_default_values(struct expr_t *expr) {
  expr->verbose = 0;
  expr->pug_is_backtrack = 1;
  expr->pug_backtrack_traffic_count = 10;
  expr->failure_max_concurrent = 0;
  expr->failure_switch_probability = 0;
  expr->failure_mode = 0;
  expr->failure_warm_cost = 0;
}

void config_parse(char const *ini_file, struct expr_t *expr, int argc, char *const *argv) {
  info("Parsing config %s", ini_file);
  _expr_set_default_values(expr);

  if (ini_parse(ini_file, config_handler, expr) < 0) {
    panic_txt("Couldn't load the ini file.");
  }
  if (cmd_parse(argc, argv, expr) < 0) {
    panic_txt("Couldn't parse the command line options.");
  }

  info("Using verbosity: %s", VERBOSITY_TEXT[expr->verbose]);

  expr->clone_network = expr_clone_network;
  if (expr->network_type == NET_JUPITER) {
    _jupiter_build_located_switch_group(expr);
    _jupiter_build_failure_model(expr);
    }
  if(expr->network_type == NET_KLOTSKI)
  {
    _klotski_build_located_switch_group(expr);
    _jupiter_build_failure_model(expr);
    
  }
}

