#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>

#include "algo/array.h"
#include "algo/maxmin.h"
#include "algo/judge_automorphism.h"
#include "util/common.h"
#include "util/debug.h"
#include "config.h"
#include "dataplane.h"
#include "failure.h"
#include "network.h"
#include "plan.h"
#include "predictor.h"
#include "risk.h"

#include "exec/pug.h"


#include <math.h>

#include "algo/group_gen.h"
#include "util/common.h"
#include "util/log.h"
#include "networks/jupiter.h"

#include "plan.h"
#include "plans/jupiter.h"

#include<sys/time.h>
#include<time.h>

#define TO_PUG(e) struct exec_pug_t *pug = (struct exec_pug_t *)e;
#define EXP(p) ((p)->expected((struct rvar_t *)(p)))
// #define ROLLBACK_EXPERIMENT 1
#undef ROLLBACK_EXPERIMENT

#define TO_JITER(p) struct jupiter_switch_plan_enumerator_iterator_t *jiter =\
     ((struct jupiter_switch_plan_enumerator_iterator_t *)(p))

#if DEBUG_MODE == 1
#define DEBUG(txt, ...) info(txt, __VA_ARGS__);
#else 
#define DEBUG(txt, ...) {}
#endif

#define BUCKET_SIZE 1

// TODO: Criteria are in effect here ... Can add new criteria here or
// .. change later.  Too messy at the moment.
//
// - Omid 1/25/2019
float klotski_plan_cost(struct exec_t *exec, struct expr_t const *expr,unsigned* subplan, unsigned plan_len);
static inline int
_best_plan_criteria(
    struct expr_t const *expr,
    risk_cost_t p1_cost, unsigned p1_length, double p1_perf,
    risk_cost_t p2_cost, unsigned p2_length, double p2_perf) {

  return ( (p1_cost  < p2_cost) ||  // If cost was lower

          ((p1_cost == p2_cost) &&  // Or the length criteria was trumping
           (expr->criteria_plan_length(p1_length, p2_length) >  1)) ||

          ((p1_cost == p2_cost) &&  // Or the pref score criteria was trumping
           (expr->criteria_plan_length(p1_length, p2_length) == 0) &&
           (p1_perf > p2_perf)));
}

void print_subplan(struct exec_t *exec, struct expr_t const *expr,int id)
{
  TO_PUG(exec);
  TO_JITER(pug->iter);
  jiter->state->to_tuple(jiter->state, id, jiter->_tuple_tmp);
      for (uint32_t i = 0; i < jiter->state->tuple_size; ++i) {
        printf("%d ",jiter->_tuple_tmp[i]);
      }
      printf("list subplan: %d\n",id);
}

static bw_t __attribute__((unused))
_tm_sum(struct traffic_matrix_t *tm) {
  bw_t tot_bw = 0;
  for (uint32_t j = 0; j < tm->num_pairs; ++j) {
    tot_bw += tm->bws[j].bw;
  }

  return tot_bw;
}

/* Invalidates/removes plans that don't have "subplan" in "step"'s subplan (or after)
 * This prunes the subplan search space to subplans */
static void __attribute__((unused))
_plan_invalidate_not_equal(struct plan_repo_t *repo, unsigned subplan, unsigned step) {
  uint32_t index = 0;
  if (repo->plan_count == 0)
    return;
  //printf("plan count before invalidate: %d\n",repo->plan_count);
  uint32_t last_index = repo->plan_count - 1;

  unsigned *ptr = repo->plans;
  unsigned *last_ptr = repo->plans + repo->max_plan_size * last_index;
  unsigned *tmp = malloc(sizeof(uint32_t) * repo->max_plan_size);// _malloc 1

  int removed = 0;
  for (uint32_t i = 0; i < repo->plan_count; ++i ){
    int do_continue = 0;

    // Try to find the subplan in the remaining (step:max_plan_size) of this plan
    for (uint32_t j = step; j < repo->max_plan_size; ++j) {
      if (ptr[j] == subplan) {
        /* Put the subplan forward in the plan */
        ptr[j] = ptr[step];
        ptr[step] = subplan;

        /* Move the plan forward */
        ptr += repo->max_plan_size;
        index += 1;
        do_continue = 1;
        break;
      }
    }

    if (do_continue) {
      continue;
    }

    // Remove plan by moving it to the end and reducing the num plans
    memcpy(tmp, last_ptr, repo->plan_size_in_bytes);
    memcpy(last_ptr, ptr, repo->plan_size_in_bytes);
    memcpy(ptr, tmp, repo->plan_size_in_bytes);

    last_index -= 1;
    last_ptr -= repo->max_plan_size;
    removed += 1;
  }

  //DEBUG("removed: %d plans", removed);
  repo->plan_count = last_index + 1;
  //printf("plan count after invalidate: %d\n",repo->plan_count);
  free(tmp);//_free 1
}

/* Returns the list of remaining subplans at step pug->plans->_cur_index. This
 * assumes that we have "fixed" the first pug->plans->_cur_index subplans,
 * i.e., we have taken those subplans */
static unsigned *
_plans_remaining_subplans(struct exec_t *exec) {
  TO_PUG(exec);
  unsigned idx = pug->plans->_cur_index;
  size_t size = sizeof(int) * pug->plans->_subplan_count;
  unsigned *ret = malloc(size);//_malloc 2
  memset(ret, 0, size);

  unsigned *ptr = pug->plans->plans;
  unsigned plan_size = pug->plans->max_plan_size;
  unsigned plan_count = pug->plans->plan_count;

  for (uint32_t i = 0; i < plan_count; ++i) {
    for (uint32_t j = idx; j < plan_size; ++j) {
      ret[ptr[j]] = 1;
    }
    ptr += plan_size;
  }

  return ret;
}

struct _network_dp_t {
  struct dataplane_t dp;
  struct network_t *net;
};


unsigned long label_tabel[1001],label_tabel_size,represent_subplan[1001];
double risk_table[1001];

/*int judge_automorphism(struct exec_t *exec, struct expr_t const *expr,int id,double *risk_cache,unsigned *eq_subplan)
{
  
  TO_PUG(exec);
  TO_JITER(pug->iter);
  unsigned label_count[1001];
  memset(label_count,0,sizeof(label_count));
  jiter->state->to_tuple(jiter->state, id, jiter->_tuple_tmp);
  struct jupiter_group_t *groups = jiter->planner->multigroup.groups;
  
  for (uint32_t i = 0; i < jiter->state->tuple_size; ++i) {
    printf("%d ",jiter->_tuple_tmp[i]);
    struct jupiter_group_t *group = &groups[i];
    float portion = (float)jiter->_tuple_tmp[i] / (float)group->group_size;
    for (uint32_t j = 0; j < group->nclasses; ++j) {
      struct jupiter_class_t *class = &group->classes[j];
      unsigned sw_to_up = (unsigned)(ceil(class->nswitches * portion));
      //printf("sw to up: %d\n",sw_to_up);
      if (sw_to_up >= class->nswitches)
        sw_to_up = class->nswitches;
      for (uint32_t k = 0; k < sw_to_up; ++k) {
        printf("switch label %d\n",class->switches[k]->label);
        label_count[class->switches[k]->label]++;
      }
    }
  }
  printf("test in judge labels\n");
  struct jupiter_network_t* jn = (struct jupiter_network_t*)expr->network;
  unsigned long label_number = 0;
  for(int i = 1;i<=expr->label_num;i++)
  {
    label_number = label_number*jn->num_switches+label_count[i];
    printf("%d ",label_count[i]);
  }
  printf("label count\n");
  for(int i = 0;i<label_tabel_size;i++)
  {
    if(label_tabel[i] == label_number)
    {
      risk_cache[id] = risk_table[i];
      eq_subplan[id] = represent_subplan[i];
      return 0;
    }
      
  }
  represent_subplan[label_tabel_size] = id;
  label_tabel[label_tabel_size++] = label_number;
  eq_subplan[id] = id;
  return 1;
}*/
int judge_automorphism(struct exec_t *exec, struct expr_t const *expr,int id,double *risk_cache,unsigned *eq_subplan)
{
  //printf("  judge automorphism\n");
  TO_PUG(exec);
  struct jupiter_network_t* jn = (struct jupiter_network_t*)expr->network;
  for(int i = 0;i<label_tabel_size;i++)
  {
    struct mop_t *mop1 = pug->iter->mop_for(pug->iter, id);
    struct mop_t *mop2 = pug->iter->mop_for(pug->iter, represent_subplan[i]);
    struct jupiter_switch_mop_t *jop1 = (struct jupiter_switch_mop_t *)mop1;
    struct jupiter_switch_mop_t *jop2 = (struct jupiter_switch_mop_t *)mop2;
    unsigned drain1[1001],drain2[1001];
    memset(drain1,0,sizeof(drain1));
    memset(drain2,0,sizeof(drain2));
    if(jop1->nswitches!=jop2->nswitches)
    {
      free(mop1);
      free(mop2);
      continue;
    }
    for (uint32_t i = 0; i < jop1->nswitches; ++i) {
      struct jupiter_located_switch_t *sw1 = jop1->switches[i];
      drain1[i] = sw1->sid;
      struct jupiter_located_switch_t *sw2 = jop2->switches[i];
      drain2[i] = sw2->sid;
    }
    if(jugde_equal(jn,drain1,drain2,jop1->nswitches))
    {
      //printf("equal subplan\n");
      //print_subplan(exec,expr,id);
      //print_subplan(exec,expr,represent_subplan[i]);
      return 0;
    }
  }
  represent_subplan[label_tabel_size++] = id;
  eq_subplan[id] = id;
  //printf("unique subplan\n");
  //print_subplan(exec,expr,id);
  return 1;
}
void klotski_plans_get(struct exec_t *exec, struct expr_t const *expr,double* risk,struct plan_repo_t *out,int now,int *sk,int subplan_count)
{
  double cost = klotski_plan_cost(exec,expr,sk,now);
  /*for(int i = 0;i<now;i++)
    printf("%d ",sk[i]);
  printf("cost in klotski plan get: %f\n",cost);*/
  if(cost<0)
    return;
  if(cost>0)
  {
    /*for(int i = 0;i<now;i++)
    {
      printf("%d ",sk[i]);
    }
    printf("klotski plan get : %f\n",cost);*/
    out->plan_count++;
    if(cost<out->best_cost)
    {
      //printf("cost update\n");
      out->best_cost = cost;
      out->best_plan_len = now;
    }
    return;
  }
  int begin;
  if(now == 0)
  {
    begin = 1;
  }
  else{
    for(begin = 0;begin<label_tabel_size&&represent_subplan[begin]!=sk[now-1];begin++);
  }
  for(int i = begin;i<label_tabel_size;i++)
  {
    int id = represent_subplan[i];
    if(risk[id] == 0)
      continue;
    sk[now] = id;
    klotski_plans_get(exec,expr,risk,out,now+1,sk,subplan_count);
  }
}

/* Returns the list of plans matching the exec->criteria_time requirements
 * TODO: Make the criteria more streamlined.  Right now it's hidden here and
 * only deals with time criteria.
 *
 * Omid - 1/25/2019
 * */

static struct plan_repo_t * __attribute__((unused)) 
_plans_get(struct exec_t *exec, struct expr_t const *expr) {
  int break_number = 0;
  /*printf("input break number");
  scanf("%d",&break_number);*/
  
  TO_PUG(exec);
  TO_JITER(pug->iter);
  uint32_t cap = 1000;
  clock_t start, end; 
  start = clock();
  
  struct jupiter_switch_plan_enumerator_t *en = 
    (struct jupiter_switch_plan_enumerator_t *)pug->planner;
  struct plan_iterator_t *iter = pug->iter;
  struct jupiter_network_t* jn = (struct jupiter_network_t*)expr->network;

  printf("test in plans get: %d\n",jn->flag);
  // Number of plans collected so far.
  unsigned plan_count = 0;
  // Maximum length of a plan.
  unsigned max_plan_size = 0;
  for (uint32_t i = 0; i < en->multigroup.ngroups; ++i) {
    max_plan_size += en->multigroup.groups[i].group_size;
  }

  size_t plan_size_in_bytes = sizeof(unsigned) * max_plan_size;
  unsigned *plans = malloc(cap * plan_size_in_bytes);//_malloc 3 maybe no free
  unsigned *subplans = 0; unsigned subplan_count;
  unsigned *plan_ptr = plans;

  struct plan_repo_t *out = malloc(sizeof(struct plan_repo_t)); //_malloc 4 maybe no free
  out->_subplan_count = iter->subplan_count(iter);
  printf("subplan count:%d\n",out->_subplan_count);
 
  out->best_cost = 32.0;
  out->best_plan_len = 16;
  out->plan_count = 1;
  out->initial_plan_count = 1;
  out->plans = plans;
  for(int i = 0;i<max_plan_size;i++)
  {
    plans[i] = 0;
  }
  out->max_plan_size = 16;
  out->plan_size_in_bytes = 64;
  out->cap = 100;
  out->_cur_index = 0;
  //printf("directly return\n");

  struct rvar_t **rcache = malloc(sizeof(struct rvar_t *) * out->_subplan_count);//_malloc 5
  double *risk_cache = malloc(sizeof(double)*out->_subplan_count);//_malloc 6
  unsigned *eq_subplan = malloc(sizeof(unsigned)*out->_subplan_count);
  struct mrcplans
  {
    unsigned index;
    double traffic;
  };
  struct mrcplans* myplans = malloc(sizeof(struct mrcplans) * out->_subplan_count);

  if(jn->flag == 2)//MRC
  {
    unsigned plan_len = 0;
    unsigned subplan_now[100];
    for (uint32_t i = 0; i < out->_subplan_count; ++i) {
      myplans[i].index = i;
      rcache[i] = pug->short_term_risk(exec, expr, i, 0);
      risk_cache[i] = rcache[i]->expected(rcache[i]);
      if(risk_cache[i] == 0)
      {
        myplans[i].traffic = -1;
      }
      else{
        myplans[i].traffic  = expr->mrc_traffic;
      }
      //printf("mrc subplan : %d %f\n",myplans[i].index,myplans[i].traffic);
    }
    for (uint32_t i = 0; i < out->_subplan_count; ++i)
    {
      for (uint32_t j = 0; j < out->_subplan_count - i; ++j)
      {
        if(myplans[j].traffic > myplans[j+1].traffic)
        {
          struct mrcplans tmp;
          tmp.index = myplans[j].index;
          tmp.traffic = myplans[j].traffic;
          myplans[j].index = myplans[j+1].index;
          myplans[j].traffic = myplans[j+1].traffic;
          myplans[j+1].index = tmp.index;
          myplans[j+1].traffic = tmp.traffic;
        }
      }
    }
    /*for(int i = 0;i<out->_subplan_count;i++)
    {
      printf("%f ",myplans[i].traffic);
      if(i%20 == 0)
        printf("\n");
    }
    printf("sorted\n");*/
    unsigned sum_switch[1001];
    memset(sum_switch,0,sizeof(sum_switch));
    for (uint32_t id = 0; id < out->_subplan_count; ++id)
    {
      if(myplans[id].traffic<=0)
        continue;
      jiter->state->to_tuple(jiter->state, myplans[id].index, jiter->_tuple_tmp);
      struct jupiter_group_t *groups = jiter->planner->multigroup.groups;
      int valid_subplan = 1,finished = 1;
      for (uint32_t i = 0; i < jiter->state->tuple_size; ++i) {
        struct jupiter_group_t *group = &groups[i];
        if(sum_switch[i]<group->group_size)
          finished = 0;
        if(jiter->_tuple_tmp[i] + sum_switch[i]>group->group_size)
        {
          valid_subplan = 0;
          break;
        }
      }
      if(finished)
        break;
      if(valid_subplan)
      {
        //printf("valid subplan\n");
        //print_subplan(exec,expr,myplans[id].index);
        subplan_now[plan_len++] = myplans[id].index;
        for (uint32_t i = 0; i < jiter->state->tuple_size; ++i)
        {
          sum_switch[i] += jiter->_tuple_tmp[i];
        }
      }
    }
    float cost = klotski_plan_cost(exec,expr,&subplan_now,plan_len);
    //printf("test plan len: %d\n",plan_len);
    out->best_cost = cost;
    out->best_plan_len = plan_len;
    goto end;
  }
  
  if(jn->flag == 1||jn->flag == 3)
  {
    out->best_cost = INFINITY;
    //printf("count risk: %d\n",out->_subplan_count);
    for (uint32_t i = 0; i < out->_subplan_count; ++i) {
      if(!judge_automorphism(exec,expr,i,risk_cache,eq_subplan))
      {
        //printf("symmetry subplans\n");
        continue;
      }
      rcache[i] = pug->short_term_risk(exec, expr, i, 0);
      risk_cache[i] = rcache[i]->expected(rcache[i]);
      risk_table[label_tabel_size-1] = risk_cache[i];
      //printf("i:%d risk cache: %f\n",i,risk_cache[i]);
    }
    //printf("represent size:%d\n",label_tabel_size);
    /*for(int i = 0;i<label_tabel_size;i++)
    {
      printf("represent subplan %d %f",represent_subplan[i],risk_cache[represent_subplan[i]]);
      print_subplan(exec,expr,represent_subplan[i]);
    }*/
    int sk[1300];
    end = clock();
    printf("subplan time=%f\n", (double)(end - start) / CLOCKS_PER_SEC);
    info("subplan time=%f\n",  (double)(end - start) / CLOCKS_PER_SEC);
    klotski_plans_get(exec,expr,risk_cache,out,0,sk,out->_subplan_count);
    goto end;
  }
  /*printf("return after short term risk");
  free(rcache);//_free 5
  free(risk_cache);//_free 6
  while(1);*/ //v3
  //return out;

  for (iter->begin(iter); !iter->end(iter); iter->next(iter)) {//&&jn->flag != 1
    
    iter->plan(iter, &subplans, &subplan_count);
    /*for(int j = 0;j<subplan_count;j++)
    {
      printf("%d ",subplans[j]);
    }
    printf("subplans in planget%d\n",plan_count);*/
    if (!expr->criteria_time->acceptable(expr->criteria_time, subplan_count)&&(expr->criteria_time->steps > 0)) {//lhw change长度为0的时候为不限制长度
      free(subplans);
      continue;
    }
    plan_count++;
    
    if(jn->flag == 1||jn->flag == 3)
    {
      unsigned plan_len = 0;
      unsigned subplan_now[100];
      unsigned violation = 0;
      memset(subplan_now,0,sizeof(subplan_now));
      
      //printf("max plan len: %d\n",max_plan_length);
      for (uint32_t j = 0; j < subplan_count; ++j) {  
        subplan_now[j] = subplans[j];
        if(risk_cache[subplans[j]] == 0)
          violation = 1;
        plan_len++;
      }
      if(violation)
      {
        free(subplans);
        continue;
      }
      //printf("test in count cost\n");
      //printf("before cost\n");
      //goto end; //v7
      float cost = klotski_plan_cost(exec,expr,&subplan_now,plan_len);
      //float cost = 0;
      //printf("after cost\n");
      //goto end;//v8
      //printf("get klotski cost: %f\n",cost);
      if(cost<out->best_cost)
      {
        out->best_cost = cost;
        out->best_plan_len = plan_len;
        /*if(cost <= 4.0)
        {
          for (uint32_t j = 0; j < subplan_count; ++j) {  
            printf("%d ",subplans[j]);
          }
          printf("get best 4.0\n");
          goto end;
        }*/
        //printf("first return\n");
        //free(subplan);
        //goto end;
      }
      //break;
      free(subplans);
      end = clock();
      if((double)(end - start) / CLOCKS_PER_SEC >= 32400.0)
        goto end;
      if(plan_count == break_number)
      {
        goto end;
      }
      continue;
    }

    memset(plan_ptr, 0, plan_size_in_bytes);
    memcpy(plan_ptr, subplans, sizeof(unsigned) * subplan_count);
    plan_ptr += max_plan_size;

    if (plan_count >= cap) {
      cap *= 2;
      plans = realloc(plans, cap * plan_size_in_bytes);
      plan_ptr = plans + max_plan_size * plan_count;
    }
    free(subplans);
  }
  end:
  printf("best plan cost: %d\n",out->best_cost);
  printf("best plan len: %d\n",out->best_plan_len);
  if(out->plan_count<=0)
    out->plan_count = plan_count;
  printf("plan count:%d\n",plan_count);
  out->initial_plan_count = plan_count;
  out->plans = plans;
  for(int i = 0;i<max_plan_size;i++)
  {
    printf("%d ",plans[i]);
  }
  out->max_plan_size = max_plan_size;
  printf("max plan size: %d\n",max_plan_size);
  out->plan_size_in_bytes = plan_size_in_bytes;
  printf("plan size in bytes %d\n",plan_size_in_bytes);
  out->cap = cap;
  printf("cap: %d\n",cap);
  out->_cur_index = 0;
  printf("end plans get\n");
  free(rcache);//_free 5
  free(risk_cache);//_free 6
  return out;
}

static struct rvar_t *
_short_term_risk_using_long_term_cache(struct exec_t *exec, 
    struct expr_t const *expr, unsigned subplan, trace_time_t now) {
      //printf("short term pred!\n"); 
  TO_PUG(exec);
  struct rvar_t *rv = pug->steady_cost[subplan];
  struct rvar_t *ret = rv->copy(rv);
  return ret;
}

static struct rvar_t *
_short_term_risk_using_predictor(struct exec_t *exec, struct expr_t const *expr,
    unsigned subplan, trace_time_t now) {
     //printf("short term pred!\n"); 
     
  TO_PUG(exec);
  //printf("test in short term pred\n");
  struct mop_t *mop = pug->iter->mop_for(pug->iter, subplan);

  struct predictor_t *pred = pug->pred;
  //printf("now: %d\n",now);
  struct predictor_iterator_t *iter = pred->predict(pred, now, now + expr->mop_duration);

  unsigned num_samples = iter->length(iter);
  unsigned tm_count = num_samples * expr->mop_duration;
  struct traffic_matrix_t **tms = malloc(//_malloc 7
      sizeof(struct traffic_matrix_t *) * tm_count);
  unsigned index = 0;

  for (iter->begin(iter); !iter->end(iter); iter->next(iter)) {
    struct traffic_matrix_trace_iter_t *titer = iter->cur(iter);
    for (titer->begin(titer); !titer->end(titer); titer->next(titer)) {
      struct traffic_matrix_t *tm = 0;
      titer->get(titer, &tm);
      tms[index++] = tm;
      /*for(int ii = 0;ii<tm->num_pairs;ii++)
      {
        printf("%f ",tm->bws[ii].bw);
      }
      printf("\n monte carlo num: %d\n",tm->num_pairs);*/
    }
    titer->free(titer);
  }
  //printf("tm count: %d\n",tm_count);
  iter->free(iter);
  assert(tm_count == index);
  
  /* The returned rvar_types are in the order they were passed to exec_simulate_ordered */
  rvar_type_t *vals = exec_simulate_ordered(exec, expr, mop, tms, tm_count);
  
  /* Free the allocated traffic matrices */
  for (uint32_t i = 0; i < tm_count; ++i) {
    traffic_matrix_free(tms[i]);//_free 7
  }

  /* Build the rvar for the cost of the short term planner */
  index = 0;
  struct risk_cost_func_t *func = expr->risk_violation_cost;
  //printf("num_sample: %d\n",num_samples);
  rvar_type_t *costs = malloc(sizeof(rvar_type_t) * num_samples);//_malloc 8
  for (uint32_t i = 0; i < num_samples; ++i) {
    costs[i] = 0;
    for (uint32_t j = 0; j < expr->mop_duration; ++j) {
      costs[i] += func->cost(func, vals[index]);
      index++;
    }
  }

  struct rvar_t *cost_rv = rvar_sample_create_with_vals(costs, num_samples);
  struct rvar_t *ret = (struct rvar_t *)cost_rv->to_bucket(cost_rv, BUCKET_SIZE);
  //info("Prob failure for %d before: %f", subplan, cost_rv->expected(cost_rv));
  cost_rv->free(cost_rv);//_free 8?
  mop->free(mop);
  //printf("short term end\n");
  return ret;
}
float debug_best_cost = 0;
float klotski_plan_cost(struct exec_t *exec, struct expr_t const *expr,unsigned* subplan, unsigned plan_len)
{
  //return 0;
  TO_PUG(exec);
  TO_JITER(pug->iter);
  float cost = 0;
  int plan_id = 0;
  int pre_type = -1,now_type = 0;
  int debug_flag = 0;
  struct jupiter_group_t *groups = jiter->planner->multigroup.groups;
  struct jupiter_network_t* jn = (struct jupiter_network_t*)expr->network;
  double judge[1300];
  int label[1001],label_sum[1001];
  memset(judge,0,sizeof(judge));
  memset(label,0,sizeof(label));
  memset(label_sum,0,sizeof(label_sum));
  
  //printf("\nklotski plan count: %d\n",plan_len);
  for(int id = 0;id<plan_len;id++)
  {
    if(subplan[id] == 1039)
      debug_flag = 1;
    int last_label = 0;
    //printf("test in klotaki plan cost\n");
    plan_id = 0;
    jiter->state->to_tuple(jiter->state, subplan[id], jiter->_tuple_tmp);
    for (uint32_t i = 0; i < jiter->state->tuple_size; ++i) {
      //printf("%d ",jiter->_tuple_tmp[i]);
      struct jupiter_group_t *group = &groups[i];
      float portion = (float)jiter->_tuple_tmp[i] / (float)group->group_size;
      judge[i]+=portion;
      for (uint32_t j = 0; j < group->nclasses; ++j) {
        struct jupiter_class_t *class = &group->classes[j];
        int now_label = class->switches[0]->label;
        unsigned sw_to_up = (unsigned)(ceil(class->nswitches * portion));
        label[now_label]+=sw_to_up;
        if(id == 0)
          label_sum[now_label] += group->group_size;
        if (sw_to_up >= class->nswitches)
          sw_to_up = class->nswitches;
          cost += sw_to_up*2*jn->alpha;
        if(sw_to_up!=0&&now_label!=last_label)
        {
          //printf("cost:%f nowlabel %d last label %d\n",cost,now_label,last_label);
          cost+=2;
          last_label = now_label;
        }
      }
    }
    //printf("%f\n",cost);
  }
  int flag = 0;
  for (uint32_t i = 1; i <= expr->label_num; ++i) {
    judge[i] = (float)label[i]/(float)label_sum[i];
    //printf("judge_:%f ",judge[i]);
    if(judge[i]>1+1e-5)
      return -1;
    if(judge[i]<1-1e-5)
    {
      flag = 1;
    }
  }
  if(flag)
    return 0;
  //if(cost<=8&&cost>0)
  //  while(1);

  /*if(cost<debug_best_cost||debug_best_cost == 0||debug_flag)
  {
    printf("new best cost: %f\n",cost);
    for(int id = 0;id<plan_len;id++)
    {
      printf("%d ",subplan[id]);
    }
    printf("plans\n");
    for(int id = 0;id<plan_len;id++)
    {
      print_subplan(exec,expr,subplan[id]);
    }
    debug_best_cost = cost;
  }*/
  return cost;
}

static risk_cost_t
_term_best_plan_to_finish(struct exec_t *exec, struct expr_t const *expr, 
    struct rvar_t *rvar, unsigned idx, unsigned *ret_plan_idx, unsigned *ret_plan_length,
    unsigned cur_step, unsigned subplan_id, double* risk_cache) {
  TO_PUG(exec);
  struct plan_repo_t *plans = pug->plans;
  unsigned *ptr = plans->plans;

  risk_cost_t best_cost = INFINITY;
  unsigned best_plan_idx = 0;
  unsigned best_plan_len = UINT_MAX;
  struct risk_cost_func_t *viol_cost = expr->risk_violation_cost;
  struct rvar_t *best_risk = 0;

  struct rvar_t *cost_rvar = 0, *cost_rvar_tmp = 0;
  int debug = 0;

  // Create a zeroed rvar for initial cost
  struct rvar_t *zero_rvar = rvar_zero();
  unsigned max_plan_length = MIN(plans->max_plan_size, expr->criteria_time->steps);
  if(expr->criteria_time->steps == 0)
    max_plan_length = plans->max_plan_size;
  //printf("maxp plan len: %d\n",max_plan_length);
  //printf("how many plan: %d",plans->plan_count);
  for (uint32_t i = 0; i < plans->plan_count; ++i) {//todo 在计算cost之前检查合法性并进行排列
    //printf("count plan: %d\n",i);
    //||expr->network_type == NET_KLOTSI
    if(expr->network_type == NET_JUPITER)
    {
      unsigned plan_len = 0;
      cost_rvar = (struct rvar_t *)zero_rvar->to_bucket(zero_rvar, BUCKET_SIZE);
      // Build the cost of the remainder of the plan, aka, long-term
      for (uint32_t j = idx; j < max_plan_length; ++j) {
        for (uint32_t dur = 0; dur < expr->mop_duration; ++dur) {
          cost_rvar_tmp = pug->steady_cost[ptr[j]];
          cost_rvar_tmp = cost_rvar_tmp->convolve(cost_rvar_tmp, cost_rvar, BUCKET_SIZE);
          cost_rvar->free(cost_rvar);
          cost_rvar = cost_rvar_tmp;
        }

        // If there are no subplans left don't count it towards the length of the plan.
        if (ptr[j] == 0)
          continue;

        // We should also consider the "rest" of the empty timeline in the calculations.
        plan_len++;
      }

      /* Calculate the cost of the plan */
      struct rvar_t *sum = cost_rvar->convolve(cost_rvar, rvar, BUCKET_SIZE);
      risk_cost_t cost = viol_cost->rvar_to_cost(viol_cost, sum);
      cost += expr->criteria_time->cost(expr->criteria_time, cur_step + plan_len + 1);
      double time_cost = expr->criteria_time->cost(expr->criteria_time, cur_step + plan_len + 1);
      if(time_cost >=1e-5)
        printf("cost+=: %f\n",time_cost); 
      //printf("rvar cost: %f\n",cost);
      sum->free(sum);

  #if DEBUG_MODE==1
      printf("Cost of: (");
      for (uint32_t i = 0; i < max_plan_length; ++i) {
        printf("% 3d, ", ptr[i]);
      }
      printf(")  -> %6.2lf\n", cost);
  #endif
      
      // Move to the next plan
      ptr += plans->max_plan_size;


      if  (_best_plan_criteria(expr, cost, plan_len, 10, 
                                    best_cost, best_plan_len, 10)) {
        best_plan_idx = i;
        best_cost = cost;
        best_plan_len = plan_len;
        if (best_risk)
          best_risk->free(best_risk);
        best_risk = cost_rvar;
      } else {
        cost_rvar->free(cost_rvar);
      }
    }
    else{
      unsigned plan_len = 0;
      unsigned subplan_now[100];
      unsigned violation = 0;
      memset(subplan_now,0,sizeof(subplan_now));
      //printf("max plan len: %d\n",max_plan_length);
      for (uint32_t j = 0; j < max_plan_length; ++j) {
        
        if (ptr[j] == 0)
          continue;
        
        subplan_now[j] = ptr[j];
        printf("%d ",ptr[j]);
        //printf("test type:%d ",rcache[ptr[j]]->_type);
        //printf("rcache in term best plan:%f\n",risk_cache[ptr[j]]);//todo 在这个地方segmentation fault
        if(risk_cache[ptr[j]] == 0)
          violation = 1;
        plan_len++;
      }
      printf("subplan in term best plan: %d\n",cur_step);
      if(violation)
      {
        debug+=plans->max_plan_size;
        ptr += plans->max_plan_size;
        //printf("ptr add debug:%d plan: %d\n",debug,ptr[16]);
        continue;
      }
      //printf("test in count cost\n");
      float cost = klotski_plan_cost(exec,expr,&subplan_now,plan_len);
      //float cost = 0;
      //printf("get klotski cost: %f\n",cost);
      if(cost<best_cost)
      {
        best_plan_idx = i;
        best_cost = cost;
        best_plan_len = plan_len;
      }
      debug+=plans->max_plan_size;
      ptr += plans->max_plan_size; 
      //printf("ptr add debug:%d plan: %d\n",debug,ptr[16]); 
    }  
  }
  if (expr->verbose >= VERBOSE_SHOW_ME_PLAN_RISK) {
    if (best_risk)
      best_risk->plot(best_risk);
  }

#if DEBUG_MODE==1
  printf("Best plan to finish: (");//, subplan_id);
  ptr = plans->plans + (best_plan_idx * plans->max_plan_size);
  for (uint32_t i = 0; i < best_plan_len + idx; ++i) {
    printf("% 3d, ", ptr[i]);
  }
  printf(") = (");//%6.2lf, ", rvar->expected(rvar));

  struct rvar_t *rv = rvar;
  for (uint32_t i = 0; i < best_plan_len + idx; ++i) {
    rv = rv->convolve(rv, pug->steady_cost[ptr[i]], 1);
    printf("% 6.2lf (% 6.2lf), ", 
        pug->steady_cost[ptr[i]]->expected(pug->steady_cost[ptr[i]]),
        rv->expected(rv));
  }
  printf(")  -> %6.2lf, %6.2lf\n", best_cost, rv->expected(rv));
#endif

  if (best_risk)
    best_risk->free(best_risk);
  *ret_plan_idx = best_plan_idx;
  *ret_plan_length = best_plan_len;
  //printf("best rvar cost: %f\n",best_cost);
  zero_rvar->free(zero_rvar);
  return best_cost;
}

static int
_exec_pug_find_best_next_subplan(struct exec_t *exec,
    struct expr_t const *expr, trace_time_t at, risk_cost_t *ret_cost,
    unsigned *ret_plan_len, unsigned *ret_plan, unsigned cur_step){//,struct rvar_t **rcache_lhw
  TO_PUG(exec);
  struct plan_repo_t *plans = pug->plans;
  risk_cost_t  best_plan_cost = INFINITY;
  unsigned     best_plan_len = UINT_MAX;
  double       best_pref_score = 0;
  unsigned     best_subplan = 0;
  unsigned *best_plan_subplans  = ret_plan;

  unsigned *subplans = _plans_remaining_subplans(exec);
  int finished = 1;
  printf("test in _exec_pug_find_best_next_subplan: %d\n",cur_step);
  struct rvar_t **rcache = malloc(sizeof(struct rvar_t *) * pug->plans->_subplan_count);//_malloc 9
  double *risk_cache = malloc(sizeof(double)*pug->plans->_subplan_count);//_malloc 10
  //printf("subplan count:%d\n",pug->plans->_subplan_count);
  for (uint32_t i = 0; i < pug->plans->_subplan_count; ++i) {
    printf("monte carlo get: %d\n",at);
    rcache[i] = pug->short_term_risk(exec, expr, i, at);
    risk_cache[i] = rcache[i]->expected(rcache[i]);
    if (subplans[i] != 1)
      continue;
    //rcache_lhw = pug->short_term_risk(exec, expr, i, at);
  }
  int remain_subplan_count = 0;
  //printf("how many subplan:%d\n",pug->plans->_subplan_count);
#ifndef ROLLBACK_EXPERIMENT
  for (uint32_t i = 1; i < pug->plans->_subplan_count; ++i) {
#else
  for (uint32_t i = 4; i < MIN(22, pug->plans->_subplan_count); ++i) {
#endif
    if (subplans[i] != 1)
    {
      continue;
    }
    //printf(" %d \n",i);
    remain_subplan_count += 1;

    //finished = 0;
    /* TODO: This is very hacky atm, but unfortunately, there is not enough
     * time to do it properly. So the idea of this is:
     *
     * 1) Choose a subplan.
     * 2) Fix that subplan, i.e., prune the search space to plans that have
     * that subplan.
     * 3) Find the best "long-term" sequence of actions to finish that plan
     * 4) Unfix that subplan and move to the next subplan.
     *
     * By fixing and unfixing I mean playing with the plans->_cur_index and
     * plans->plan_count values.
     *
     * - Omid 1/25/2019
     * */

    /* Fix the plans */
    unsigned plan_count = plans->plan_count;
    unsigned plan_idx = 0;
    unsigned plan_length = 0;
    double pref_score = pug->iter->pref_score(pug->iter, i);
    _plan_invalidate_not_equal(plans, i, plans->_cur_index);
    //int index = (plans->plan_count-1)*plans->max_plan_size;
    //printf("test plan invalidate: maxplanlen:%d, plancount:%d,index: %d,%d\n",plans->max_plan_size,plans->plan_count,index,plans->plans[index]);

#if DEBUG_MODE
    struct mop_t *mop = pug->iter->mop_for(pug->iter, i);
    char *ret = mop->explain(mop, expr->network);
    DEBUG("Short Term Subplan %s", ret);
    mop->free(mop);
    free(ret);
#endif

    // Use the failure model to assess the short-term risk for subplan[i]
    struct rvar_t *st_risk;
    if(expr->network_type == NET_KLOTSKI)
    {
      st_risk = rcache[i];
      //printf("klotski fail : %d, %f\n",i,rcache[i]->expected(rcache[i]));
    }
    else{
      st_risk = expr->failure->apply(
        expr->failure, expr->network, pug->iter, rcache, i);
        
    }
    

    risk_cost_t cost = _term_best_plan_to_finish(exec, expr, 
        st_risk, plans->_cur_index + 1, &plan_idx, &plan_length, cur_step, i,risk_cache);
    if(cost>=0)
      finished = 0;//lhw change 剪枝，只有正的cost可以当做计划
    //printf("term best plan to finish : %f\n",cost);
#if DEBUG_MODE
    DEBUG("Short Term Cost: %lf", expr->risk_violation_cost->rvar_to_cost(expr->risk_violation_cost, st_risk));
    info("Total cost to finish: %lf", cost);
#endif
    st_risk->free(st_risk);

    if (_best_plan_criteria(expr,
          cost, plan_length, pref_score,
          best_plan_cost, best_plan_len, best_pref_score)) {
      best_plan_cost = cost;
      best_plan_len = plan_length;
      best_pref_score = pref_score;
      best_subplan = i;
      memcpy( best_plan_subplans, 
          plans->plans + (plan_idx * plans->max_plan_size), 
          plans->plan_size_in_bytes);
    }
    /*if(i==3)//danger change
    {
      memcpy( best_plan_subplans, 
          plans->plans + (plan_idx * plans->max_plan_size), 
          plans->plan_size_in_bytes);
      printf("change best subplan: %d\n",best_plan_subplans[0]);
      break;
    }*/

    // Recover the plans that we stopped looking into
    plans->plan_count = plan_count;

    // info("Expected risk of running subplan %d is %f", i, st_risk->expected(st_risk));
    // Get the best long-term plan to finish subplans[i]
  }
  //printf("best plan: %d remain_subplan_count: %d\n",best_plan_len,remain_subplan_count);
  /*for(uint32_t i = 0; i < pug->plans->_subplan_count; ++i) {
    rcache[i]->free(rcache[i]);
  }*/
  free(rcache);
  free(subplans);//_free 2

  if (!finished) {
    *ret_cost = best_plan_cost;
    *ret_plan_len = best_plan_len;
  }

  (void)(best_subplan);
#if DEBUG_MODE==1
  info(">>>> Choosing subplan %d with cost %lf", best_subplan, best_plan_cost);
#endif
  //printf("finished best plan: %d\n",best_plan_subplans[0]);
  free(rcache);//_free 9
  free(risk_cache);//_free 10
  return finished;
}

static void __attribute__((unused))
_print_freedom_plan(struct exec_t *exec, struct expr_t const *expr, 
    unsigned best_subplan_len, unsigned *best_plan_subplans) {
  TO_PUG(exec);
  struct plan_repo_t *plans = pug->plans;

  char *fin = malloc(2048 * sizeof(char));//_malloc 11
  memset(fin, 0, 2048);
  for (uint32_t i = plans->_cur_index; i < plans->max_plan_size; ++i)  {
    if (best_plan_subplans[i] == 0)
      break;
    char *ret = pug->iter->explain(pug->iter, best_plan_subplans[i]);
    strcat(fin, ret);
    strcat(fin, ", ");
    free(ret);
  }
  info("PLAN: %s", fin);
  free(fin);//_free 11
}

static risk_cost_t
_exec_pug_best_plan_at(struct exec_t *exec, struct expr_t const *expr,
    trace_time_t at, risk_cost_t *best_plan_cost, unsigned *best_plan_len,
    unsigned *best_plan_subplans) {
  TO_PUG(exec);

  int finished = 0;
  risk_cost_t running_cost = 0;
  struct plan_repo_t *plans = pug->plans;

  plans->_cur_index = 0;
  plans->plan_count = plans->initial_plan_count;

  /* TODO: PUG continously assess the risk of each plan
   * so there is no single "best" plan.  We just return the first cost as the
   * estimated cost of the best plan.
   *
   * - Omid 3/31/2019
   * */
  int first_estimate = 0;
  //struct rvar_t **rcache_lhw = malloc(sizeof(struct rvar_t *) * pug->plans->_subplan_count);
  //printf("test in _exec_pug_best_plan_at\n");
  while (1) {
    printf("find best next subplan! %d\n",plans->_cur_index);
    finished = _exec_pug_find_best_next_subplan(
        exec, expr, at, best_plan_cost, best_plan_len, best_plan_subplans,
        plans->_cur_index);//,rcache_lhw
    /*for(int i = 0;i<*best_plan_len;i++)
    {
      printf("%d ",best_plan_subplans[i]);
    }
    printf("best plan in _exec_pug_best_plan_at\n");*/
#if DEBUG_MODE
    _print_freedom_plan(exec, expr, *best_plan_len, best_plan_subplans);
#endif

    if (finished)
      break;

    DEBUG("Estimated cost of %d(th) subplan is: %f with %d steps",
        plans->_cur_index, *best_plan_cost, *best_plan_len);
    _plan_invalidate_not_equal(
        plans, best_plan_subplans[plans->_cur_index], plans->_cur_index);
    plans->_cur_index += 1;
    //printf("invalidate not equal plan count: %d\n",plans->plan_count);
    if (!first_estimate) {
      running_cost = *best_plan_cost;
      first_estimate = 1;
    }
    //printf("at add: %d!\n",at);
    if(expr->network_type == NET_JUPITER)
      at += expr->mop_duration;
  }
  
  /*for(uint32_t i = 0; i < pug->plans->_subplan_count; ++i) {
    rcache_lhw[i]->free(rcache_lhw[i]);
  }
  printf("finish exec plan\n");
  free(rcache_lhw);*/
  *best_plan_len = plans->_cur_index;
  
  return running_cost;
}

static struct mop_t **
_exec_mops_for_create(struct exec_t *exec, struct expr_t const *expr,
    unsigned *subplans, unsigned nsubplan) {
  TO_PUG(exec);
  struct mop_t **mops = malloc(sizeof(struct mop_t *) * nsubplan);//_malloc 12
  for (uint32_t i = 0; i < nsubplan; ++i) {
    mops[i] = pug->iter->mop_for(pug->iter, subplans[i]);
  }

#if DEBUG_MODE
  for (uint32_t i = 0; i < nsubplan; ++i) {
    char *desc = mops[i]->explain(mops[i], expr->network);
    info("MOp explanation: %s", desc);
    free(desc);
  }
#endif

  return mops;
}

static void
_exec_mops_for_free(struct exec_t *exec, struct expr_t const *expr,
    struct mop_t **mops, unsigned nsubplan) {
  for (uint32_t i = 0; i < nsubplan; ++i) {
    mops[i]->free(mops[i]);
  }
  free(mops);
}

static void
_exec_pug_validate(struct exec_t *exec, struct expr_t const *expr) {
  TO_PUG(exec);

  pug->trace = traffic_matrix_trace_load(400, expr->traffic_test);
  if (pug->trace == 0)
    panic("Couldn't load the traffic matrix file: %s", expr->traffic_test);

  pug->trace_training = traffic_matrix_trace_load(400, expr->traffic_training);
  if (pug->trace == 0)
    panic("Couldn't load the training traffic matrix file: %s", expr->traffic_test);

  pug->pred = exec_predictor_create(exec, expr, expr->predictor_string);
  
  if (expr->criteria_time == 0)
    panic_txt("Time criteria not set.");

  /* TODO: This shouldn't be jupiter specific 
   *
   * Omid - 1/25/2019
   * */
  printf("test: %d\n",expr->upgrade_list.num_switches);
  struct jupiter_switch_plan_enumerator_t *en = jupiter_switch_plan_enumerator_create(
      expr->upgrade_list.num_switches,
      expr->located_switches,
      expr->upgrade_freedom,
      expr->upgrade_nfreedom);
  pug->iter = en->iter((struct plan_t *)en,expr);
  
  pug->planner = (struct plan_t *)en;
  
  pug->plans = _plans_get(exec, expr);
  //printf("subplan_count: %d plan count: %d\n",pug->plans->_subplan_count,pug->plans->plan_count);
  TO_JITER(pug->iter);
    /*for(int id = 0;id<pug->plans->_subplan_count;id++)
    {
      print_subplan(exec,expr,id);
    }*/
  
  if (pug->plans == 0)
    panic_txt("Couldn't build the plan repository.");

  info("Found %d valid plans.", pug->plans->plan_count);
  info("subplan sum: %d,equivalent subplan: %d",pug->plans->_subplan_count,label_tabel_size);
}

static struct exec_output_t *
_exec_pug_runner(struct exec_t *exec, struct expr_t const *expr) {
  TO_PUG(exec);

  struct exec_output_t *res = malloc(sizeof(struct exec_output_t));//_malloc 13 maybe not free
  struct exec_result_t result = {0};
  res->result = array_create(sizeof(struct exec_result_t), 10);

  struct plan_repo_t *plans = pug->plans;
  risk_cost_t  best_plan_cost = INFINITY;
  unsigned     best_plan_len = UINT_MAX;
  unsigned     *best_plan_subplans  = malloc(sizeof(int) * plans->max_plan_size);//_malloc 14

  /* TODO: Refactorthe PUG_LONG out of this loop
   *
   * -Omid 04/03/2019 */
   struct jupiter_network_t* jn = (struct jupiter_network_t*)expr->network;
  if(jn->flag >= 1)
  {
    info("[%4d] Actual cost of the best plan (%02d) is: %4.3f",
        0, pug->nmops, pug->plans->best_cost);
    printf("[%4d] Actual cost of the best plan (%02d) is: %4.3f\n",
        0, pug->nmops, pug->plans->best_cost);

    result.at = 0;
    result.num_steps = pug->nmops;
    result.description = 0;
    result.cost = pug->plans->best_cost;
    
    array_append(res->result, &result);
    if (pug->type == PUG_LONG) 
    _exec_mops_for_free(exec, expr, pug->mops, best_plan_len);
    pug->mops = 0;
    free(best_plan_subplans);//_free 14

    return res;
  }
  for (uint32_t i = expr->scenario.time_begin; i < expr->scenario.time_end; i += expr->scenario.time_step) {
    trace_time_t at = i;

    pug->prepare_steady_cost(exec, expr, at);
    risk_cost_t estimated_cost = 0;

    if (!(pug->type == PUG_LONG && pug->mops)) {
      estimated_cost = _exec_pug_best_plan_at(
          exec, expr, at, &best_plan_cost, &best_plan_len, best_plan_subplans);
      pug->mops = _exec_mops_for_create(
          exec, expr, best_plan_subplans, best_plan_len);
      /*for(int j = 0;j<best_plan_len;j++)
      {
        printf("%d ",best_plan_subplans[j]);
      }
      printf("best sub plan len: %d estimated cost: %f\n",best_plan_len,estimated_cost);*/
      pug->nmops = best_plan_len;
    }
    risk_cost_t actual_cost = exec_plan_cost(
        exec, expr, pug->mops, pug->nmops, at);

    

    if (pug->type != PUG_LONG) 
      _exec_mops_for_free(exec, expr, pug->mops, pug->nmops);

    info("[%4d] Actual cost of the best plan (%02d) is: %4.3f : %4.3f",
        at, pug->nmops, actual_cost, estimated_cost);
    printf("[%4d] Actual cost of the best plan (%02d) is: %4.3f : %4.3f",
        at, pug->nmops, actual_cost, estimated_cost);

    result.at = i;
    result.num_steps = pug->nmops;
    result.description = 0;
    result.cost = actual_cost;
    
    array_append(res->result, &result);
    printf("exec end\n");
    pug->release_steady_cost(exec, expr, at);
  }

  if (pug->type == PUG_LONG) 
    _exec_mops_for_free(exec, expr, pug->mops, best_plan_len);//_free 12
  pug->mops = 0;
  free(best_plan_subplans);

  return res;
}

static void
_exec_pug_long_explain(struct exec_t const *exec) {
  text_block_txt(
       "Pug long only uses long-term traffic estimates to find plans.  It is\n"
       "agnostic to current traffic matrices.  This is also called Janus Offline\n"
       "in the paper.\n");
}


static void
_exec_pug_lookback_explain(struct exec_t const *exec) {
  text_block_txt(
"Pug lookback uses the last pug:backtrack-traffic-counter number of traffic\n"
"matrices as prediction of possible traffic matrices in the future.  It also\n"
"uses the traffic matrices produced by the predictor:type to estimate the\n"
"future SLO violation.  It then combines these results to find the optimal plan."); }

static void
_exec_pug_short_and_long_explain(struct exec_t const *exec) {
  text_block_txt(
      "Pug uses short-term + long-term traffic estimates to find plans.\n"
      "You can set the predictor that pug uses through the .ini file.");
}

static void
prepare_steady_cost_static(struct exec_t *exec, struct expr_t const *expr, trace_time_t time) {
  TO_PUG(exec);
  // We have already loaded the steady_packet_loss
  if (pug->steady_packet_loss != 0)
    return;

  // Load the steady_packet_loss data
  unsigned subplan_count = 0;
  pug->steady_packet_loss = exec_rvar_cache_load(expr, &subplan_count);
  printf("test in prepare_steady_cost_static\n");
  if (pug->steady_packet_loss == 0)
    panic_txt("Couldn't load the long-term RVAR cache.");

  /* Create the cost variables */
  if (expr->risk_violation_cost == 0)
    panic_txt("Risk of violation cost not set.");

  struct rvar_t **rcache =  malloc(sizeof(struct rvar_t *) * subplan_count);//_malloc 15
  info_txt("Preparing the steady_cost cache.");
  for (uint32_t i = 0; i < subplan_count; ++i) {
    struct rvar_t *rv = expr->risk_violation_cost->rvar_to_rvar(
        expr->risk_violation_cost, pug->steady_packet_loss[i], 0);
#if DEBUG_MODE==1
    info("Cost of subplan %d is %lf", i, rv->expected(rv));
    char *explanation = pug->iter->explain(pug->iter, i);
    info_txt(explanation);
    free(explanation);
#endif
    
    rcache[i] = (struct rvar_t *)rv->to_bucket(rv, BUCKET_SIZE);
    rv->free(rv);
  }

  // Create the actual steady cost values
  pug->steady_cost = malloc(sizeof(struct rvar_t *) * subplan_count);//_malloc 16
  info_txt("Adjusting subplan costs to consider the failure model.");

  struct plan_iterator_t *iter = pug->planner->iter(pug->planner,expr);
  for (uint32_t i = 0; i < subplan_count; ++i) {
    if(expr->network_type == NET_KLOTSKI)
    {
      pug->steady_cost[i] = rcache[i];
      //printf("klotski fail : %d, %f\n",i,rcache[i]->expected(rcache[i]));
    }
    else{
      pug->steady_cost[i] = expr->failure->apply(
        expr->failure, expr->network,
        pug->iter, rcache, i);
        
    }
    if (expr->verbose >= VERBOSE_MORE_INFO) {
      info("Expected cost before failure considerations %d: %f, after: %f", 
          i, rcache[i]->expected(rcache[i]), 
          pug->steady_cost[i]->expected(pug->steady_cost[i]));
    }
  }
  iter->free(iter);
  info_txt("Finished applying the failure model.");

  // Free the cost resources
  for (uint32_t i = 0; i < subplan_count; ++i) {
    rcache[i]->free(rcache[i]);
  }
  free(rcache);//_free 16

  info_txt("Done preparing the steady_cost cache.");
}

static void
release_steady_cost_static(struct exec_t *exec, struct expr_t const *expr, trace_time_t time) {
  // Does nothing
  return;
}

//todo 给每种状态计算cost
static void
prepare_steady_cost_dynamic(struct exec_t *exec, struct expr_t const *expr, trace_time_t time) {
  TO_PUG(exec);

  unsigned subplan_count = 0;
  struct array_t **arr = exec_rvar_cache_load_into_array(expr, &subplan_count);
  if (arr == 0) {
    panic_txt("Couldn't load the RVAR array.");
    return;
  }

  /* Create the cost variables */
  if (expr->risk_violation_cost == 0)
    panic_txt("Risk of violation cost not set.");

  assert(pug->steady_packet_loss == 0);
  assert(pug->steady_cost == 0);

  pug->steady_packet_loss = malloc(sizeof(struct rvar_t *) * subplan_count);//_malloc 17
  pug->steady_cost = malloc(sizeof(struct rvar_t *) * subplan_count);//_malloc 18

  trace_time_t start, end;

  trace_time_t backtrack_time = expr->pug_backtrack_traffic_count;
  int pug_backtrack = expr->pug_is_backtrack;

  if (pug_backtrack) {
    end = time;
    if (time < backtrack_time) {
      start = 0;
    } else {
      start = time - backtrack_time;
    }
    if (start == end) {
      end = start + 1;
    }
  } else {
    start = time;
    end = start + backtrack_time;
    if (end >= exec->trace->num_indices) {
      end = (trace_time_t)exec->trace->num_indices - 1;
    }
    if (start >= end) {
      start = end - 1;
    }
  }

  struct rvar_t **rcache = malloc(sizeof(struct rvar_t *) * subplan_count);//_malloc 19
  for (uint32_t i = 0; i < subplan_count; ++i) {
    void *data = 0; unsigned data_size = 0;
    data = array_splice(arr[i], start, end, &data_size);
    pug->steady_packet_loss[i] = rvar_sample_create_with_vals(data, data_size);

    struct rvar_t *rv = expr->risk_violation_cost->rvar_to_rvar(
        expr->risk_violation_cost, pug->steady_packet_loss[i], 0);
    rcache[i] = (struct rvar_t *)rv->to_bucket(rv, BUCKET_SIZE);
    rv->free(rv);
  }

  /* Apply the failure model to long term plans */
  for (uint32_t i = 0; i < subplan_count; ++i) {
    if(expr->network_type == NET_KLOTSKI)
    {
      pug->steady_cost[i] = rcache[i];
      //printf("klotski fail : %d, %f\n",i,rcache[i]->expected(rcache[i]));
    }
    else{
      pug->steady_cost[i] = expr->failure->apply(
        expr->failure, expr->network,
        pug->iter, rcache, i);
        
    }
    
  }

  for (uint32_t i = 0; i < subplan_count; ++i) {
    rcache[i]->free(rcache[i]);
  }
  free(rcache);//_free 19
}

static void
release_steady_cost_dynamic(struct exec_t *exec, struct expr_t const *expr, trace_time_t time) {
  TO_PUG(exec);
  // Things are already released no need to do anything about them.
  if (!pug->steady_packet_loss)
    return;
  //printf("subplan count:%d\n",pug->plans->_subplan_count);
  for (int i = 0; i < pug->plans->_subplan_count; ++i) {
    
    struct rvar_t *rv = pug->steady_packet_loss[i];
    
    rv->free(rv);
    if(expr->network_type == NET_KLOTSKI)
      continue;
    rv = pug->steady_cost[i];//在klotski中steady cost直接等于rcache所以可能直接free掉了
    rv->free(rv);
    printf("test in release_steady_cost_dynamic\n");
  }
  
  free(pug->steady_packet_loss);//_free 17
  free(pug->steady_cost);//_free 18

  pug->steady_packet_loss = 0;
  pug->steady_cost = 0;
}

struct exec_t *exec_pug_create_short_and_long_term(void) {
  struct exec_t *exec = malloc(sizeof(struct exec_pug_t));//_malloc 20 maybe not free
  exec->net_dp = 0;

  exec->validate = _exec_pug_validate;
  exec->run = _exec_pug_runner;
  exec->explain = _exec_pug_short_and_long_explain;

  TO_PUG(exec);
  pug->steady_packet_loss = 0;
  pug->steady_cost = 0;
  pug->short_term_risk = _short_term_risk_using_predictor;
  pug->prepare_steady_cost = prepare_steady_cost_static;
  pug->release_steady_cost = release_steady_cost_static;
  pug->type = PUG_PUG;
  pug->mops = 0;

  return exec;
}

struct exec_t *exec_pug_create_long_term_only(void) {
  struct exec_t *exec = malloc(sizeof(struct exec_pug_t));//_malloc 21 maybe not free
  exec->net_dp = 0;

  exec->validate = _exec_pug_validate;
  exec->run = _exec_pug_runner;
  exec->explain = _exec_pug_long_explain;

  TO_PUG(exec);
  pug->steady_packet_loss = 0;
  pug->steady_cost = 0;
  pug->short_term_risk = _short_term_risk_using_long_term_cache;
  pug->prepare_steady_cost = prepare_steady_cost_static;
  pug->release_steady_cost = release_steady_cost_static;
  pug->type = PUG_LONG;
  pug->mops = 0;

  return exec;
}

struct exec_t *exec_pug_create_lookback(void) {
  struct exec_t *exec = malloc(sizeof(struct exec_pug_t));//_malloc 22 maybe not free
  exec->net_dp = 0;

  exec->validate = _exec_pug_validate;
  exec->run = _exec_pug_runner;
  exec->explain = _exec_pug_lookback_explain;

  TO_PUG(exec);
  pug->steady_packet_loss = 0;
  pug->steady_cost = 0;
  pug->short_term_risk = _short_term_risk_using_predictor;
  pug->prepare_steady_cost = prepare_steady_cost_dynamic;
  pug->release_steady_cost = release_steady_cost_dynamic;
  pug->type = PUG_LOOKBACK;
  pug->mops = 0;

  return exec;
}
