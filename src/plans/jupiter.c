#include <math.h>

#include "algo/group_gen.h"
#include "util/common.h"
#include "util/log.h"
#include "networks/jupiter.h"

#include "plan.h"
#include "plans/jupiter.h"

 
static struct plan_iterator_t* jupiter_switch_plan_enumerator_iterator(
    struct plan_t *planner);

void _klotski_switch_init(struct jupiter_located_switch_t* sw, uint32_t n)
{
    sw->neighbor_size = 0;
    sw->neighbor_nodes = (uint32_t*)malloc(n * sizeof(uint32_t));
    sw->dis = 0;
    sw->traffic = 0;
    sw->stat = UP;
}
static struct jupiter_group_t *
_jupiter_get_group_for(struct jupiter_multigroup_t *mg,
    struct jupiter_located_switch_t *sw) {
  //printf("get group for\n");
  //printf("swcoler: %d, mg ngroups: %d\n",sw->color,mg->ngroups);
  return &mg->groups[sw->color % mg->ngroups];
}

static void
_jupiter_add_switch_to_class(
    struct jupiter_class_t *class,
    struct jupiter_located_switch_t *sw) {
  class->nswitches += 1;
  class->switches = realloc(class->switches, 
      sizeof(struct jupiter_located_switch_t *) * class->nswitches);
  class->switches[class->nswitches - 1] = sw;
}

static void
_jupiter_build_groups(struct jupiter_switch_plan_enumerator_t *planner) {
  //printf("build group\n");
  for (int i = 0; i < planner->num_switches; ++i) {
    struct jupiter_located_switch_t *sw = &planner->switches[i];
    //printf("build group: %d\n",i);
    struct jupiter_group_t *group = _jupiter_get_group_for(&planner->multigroup, sw);

    uint32_t done = 0;
    for (uint32_t j = 0; j < group->nclasses; ++j) {
      struct jupiter_class_t *class = &group->classes[j];
      if (class->pod == sw->pod &&
          class->type == sw->type) {
        _jupiter_add_switch_to_class(class, sw);
        done = 1;
        break;
      }
    }
    
    if (!done) {
      group->nclasses += 1;
      group->classes = realloc(group->classes, sizeof(struct jupiter_class_t) * group->nclasses);
      struct jupiter_class_t *class = &group->classes[group->nclasses-1];
      class->nswitches = 0;
      class->switches = 0;
      class->pod = sw->pod;
      class->type = sw->type;
      _jupiter_add_switch_to_class(class, sw);
    }
  }
}

static
unsigned _max_class_size(struct jupiter_group_t *group) {
  unsigned ret = 0;
  for (uint32_t i = 0; i < group->nclasses; ++i) {
    ret = MAX(ret, group->classes[i].nswitches);
  }

  return ret;
}

struct jupiter_switch_plan_enumerator_t *jupiter_switch_plan_enumerator_create(
    uint32_t num_switches, struct jupiter_located_switch_t const *switches,
    uint32_t *freedom_degree, uint32_t ndegree) {
  if (num_switches == 0)
    panic_txt("Creating a planner with no switches ...");

  size_t size = sizeof(struct jupiter_switch_plan_enumerator_t);
  struct jupiter_switch_plan_enumerator_t *planner = malloc(size);

  size = sizeof(struct jupiter_located_switch_t) * num_switches;
  planner->switches = malloc(size);
  memcpy(planner->switches, switches, size);
  planner->num_switches = num_switches;

  planner->iter = jupiter_switch_plan_enumerator_iterator;

  planner->multigroup.ngroups = ndegree;
  planner->multigroup.groups = malloc(sizeof(struct jupiter_group_t) * ndegree);
  memset(planner->multigroup.groups, 0, sizeof(struct jupiter_group_t) * ndegree);

  for (uint32_t i = 0; i < ndegree; ++i) {
    planner->multigroup.groups[i].group_size = freedom_degree[i];
    //printf("group size: %d\n",freedom_degree[i]);
  }
  
  _jupiter_build_groups(planner);
  //printf("create enmuerator\n");
  for (uint32_t i = 0; i < ndegree; ++i) {
    struct jupiter_group_t *group = &planner->multigroup.groups[i];
    group->group_size = MIN(freedom_degree[i], _max_class_size(group));
  }

  
  return planner;
}

void jupiter_switch_plan_enumerator_free(
    struct jupiter_switch_plan_enumerator_t *jup) {
  if (jup->num_switches > 0) {
    free(jup->switches);
    jup->num_switches = 0;
    jup->switches = 0;
  }
  free(jup);
}

#define TO_JITER(p) struct jupiter_switch_plan_enumerator_iterator_t *jiter =\
     ((struct jupiter_switch_plan_enumerator_iterator_t *)(p))

#define JUPITER_DEFAULT_GROUP_SIZE 10

static
unsigned _sup_subplan_count(struct plan_iterator_t *iter) {
  TO_JITER(iter);

  return jiter->state->num_subsets(jiter->state);
}

static
void _sup_begin (struct plan_iterator_t *iter) {
  TO_JITER(iter);
  jiter->state->begin(jiter->state);
}

static
int  _sup_next (struct plan_iterator_t *iter) {
  TO_JITER(iter);
  return jiter->state->next(jiter->state);
}

static
int  _sup_end (struct plan_iterator_t *iter) {
  TO_JITER(iter);
  return jiter->state->end(jiter->state);
}

static
void _sup_free(struct plan_iterator_t *iter) {
  TO_JITER(iter);

  jiter->state->free(jiter->state);
  free(jiter->_tuple_tmp);
  free(jiter);
}

static
void _jupiter_mop_free(struct mop_t *mop) {
  struct jupiter_switch_mop_t *jop = (struct jupiter_switch_mop_t *)mop;
  for(int i = 0;i<jop->nswitches;i++)
  {
    printf("%d ",jop->drain[i]);
  }
  printf("mop free\n");
  free(jop->switches);
  /*if(jop->nswitches>0)
  {
    printf("free drain\n");
    free(jop->drain);
  }*/   
  free(jop);
}

static
float _jupiter_mop_pre(struct mop_t *mop, struct network_t *net) {//改成了float,为了和klotski的mop pre统一
  if(net->network_type == NET_JUPITER)
  {
    printf("jupiter mop pre\n");
    int debugout = 1;
    struct jupiter_network_t *jup = (struct jupiter_network_t *)net;
    struct jupiter_switch_mop_t *jop = (struct jupiter_switch_mop_t *)mop;
    (void)(jup); (void)(jop);
    //printf("drain nswitches: %d\n",jop->nswitches);
    for (uint32_t i = 0; i < jop->nswitches; ++i) {
      struct jupiter_located_switch_t *sw = jop->switches[i];
      (void)(sw);
      //printf("drain test\n");
      jup->drain_switch((struct network_t *)jup, sw->sid);
      //printf("drain sid: %d pod: %d\n",sw->sid,sw->pod);
      debugout = 0;
      
    }
    //jup->drain_switch((struct network_t *)jup, 4);//danger change
    //printf("new drain: 4\n");
    return 0;
  }
  else{
    printf("kloski mop pre\n");
    struct jupiter_network_t* kup = (struct jupiter_network_t*)net;
    struct jupiter_switch_mop_t *kop = (struct jupiter_switch_mop_t *)mop;
    uint32_t switches_update_type[6];
    memset(switches_update_type, 0, sizeof(switches_update_type));
    float cost = kop->nswitches;
    (void)(kup);
    //printf("test\n");
    //printf("kop nswitches: %d\n",kop->nswitches);
    for (uint32_t i = 0; i < kop->nswitches; ++i) {
        //printf("drain: %d\n",kop->drain[i]);
        struct jupiter_located_switch_t sw = kup->klotski_switches[kop->drain[i]];
        //printf("test\n");
        (void)(sw);
        
        kup->drain_switch(kup, sw.sid);
        switches_update_type[sw.type] = 1;
    }
    /*if (switches_update_type[last_type] == 1)//lhw问题，是否要考虑last type
    {
        switches_update_type[last_type] = 0;
    }*/
    //对janus最有利的cost计算，依次drain/undrain相同类型的switch
    for (int i = 0; i < 6; i++)
    {
        cost += switches_update_type[i] * kup->alpha;
    }
    cost *= 2;//drain undrain分别做一次
    return cost;
  }
  
}
static
unsigned _jupiter_mop_size(struct mop_t *mop) {
  struct jupiter_switch_mop_t *jop = (struct jupiter_switch_mop_t *)mop;
  return jop->nswitches;
}

static
unsigned
_jupiter_block_stats(struct mop_t *mop, struct network_t *net, 
    struct mop_block_stats_t **ret) {
  struct jupiter_network_t *jup = (struct jupiter_network_t *)net;
  struct jupiter_switch_mop_t *jop = (struct jupiter_switch_mop_t *)mop;

  unsigned num_blocks = (jup->pod + 1 /* core switch group */);
  size_t size = sizeof(struct mop_block_stats_t)  * (num_blocks);
  struct mop_block_stats_t *blocks = 
    malloc(sizeof(struct mop_block_stats_t) * num_blocks);
  memset(blocks, 0, size);

  unsigned core_id = jup->pod; /* The last entry is the core switch count */
  struct jupiter_located_switch_t *sw = 0;

  for (int i = 0; i < jup->pod; ++i) {
    blocks[i].id.id = i;
    blocks[i].id.type = BT_POD_AGG;
    blocks[i].all_switches = jup->agg;
  }

  blocks[core_id].id.id = (int)core_id;
  blocks[core_id].id.type = BT_CORE;
  blocks[core_id].all_switches = jup->core;

  for (int i = 0; i < jop->nswitches; ++i) {
    sw = jop->switches[i];
    if (sw->type == JST_CORE) {
      blocks[core_id].down_switches += 1;
    } else {
      blocks[sw->pod].down_switches += 1;
    }
  }

  *ret = blocks;
  return num_blocks;
}

static
char *_jupiter_mop_explain(struct mop_t *mop, struct network_t *net) {
  struct mop_block_stats_t *blocks = 0;
  unsigned nblocks = mop->block_stats(mop, net, &blocks);

  struct jupiter_network_t *jup = (struct jupiter_network_t *)net;

  size_t size = (5 /* name */ + 5 /*: [] */ + 5 /* sw-count */) * nblocks;
  char *ret = malloc(sizeof(char) * size);
  memset(ret, 0, size);

  //TODO: This should iterate over nblocks
  for (int i = 0; i < jup->pod; ++i) {
    char desc[80] = {0};
    snprintf(desc, 80, "[P% 2d: % 3d], ", i, blocks[i].down_switches);
    strcat(ret, desc);
  }

  char desc[80] = {0};
  snprintf(desc, 80, "[C: % 3d]", blocks[jup->pod].down_switches);
  strcat(ret, desc);

  free(blocks);
  return ret;
}

static
int _jupiter_mop_post(struct mop_t *mop, struct network_t *net) {
  struct jupiter_network_t *jup = (struct jupiter_network_t *)net;
  struct jupiter_switch_mop_t *jop = (struct jupiter_switch_mop_t *)mop;
  (void)(jup); (void)(jop);

  for (uint32_t i = 0; i < jop->nswitches; ++i) {
    struct jupiter_located_switch_t *sw = jop->switches[i];
    jup->undrain_switch((struct network_t *)jup, sw->sid);
  }

  //info("Undraining %d switches.", jop->nswitches);
  return 0;
}

void _sup_plan(struct plan_iterator_t *iter, unsigned **ret, unsigned *size) {
  TO_JITER(iter);
  struct group_iter_t *state = jiter->state;
  unsigned *arr = malloc(sizeof(int) * state->state_length);
  *ret = arr;

  for (uint32_t i = 0; i < state->state_length; ++i) {
    //printf("%d ",state->state[i]);
    *arr = state->state[i];
    arr++;
  }
  //printf("subplan\n");
  *size = state->state_length;
}

#define DEFAULT_CAP_SIZE 10

void _info_switch(struct jupiter_located_switch_t *sw) {
  char s = 0;
  if (sw->type == JST_CORE) {
    s = 'C';
    printf("%c%d", s, sw->sid);
  } else if (sw->type == JST_AGG) {
    s = 'A';
    printf("%c%d [%d]", s, sw->sid, sw->pod);
  }

}

double _sup_pref_score(struct plan_iterator_t *iter, unsigned id) {
  TO_JITER(iter);
  jiter->state->to_tuple(jiter->state, id, jiter->_tuple_tmp);
  double pref_score = 0; //jiter->state->num_subsets(jiter->state);
  struct jupiter_group_t *groups = jiter->planner->multigroup.groups;
  for (uint32_t i = 0; i < jiter->state->tuple_size; ++i) {
    struct jupiter_group_t *group = &groups[i];
    float portion = (float)jiter->_tuple_tmp[i] / (float)group->group_size;
    //pref_score += (portion * (i + 1) * (i + 1));
    pref_score += (portion);
    //pref_score += (portion != 0);
    //pref_score += 1/(portion + 1);
    //pref_score += (jiter->_tuple_tmp[i] != 0);
  }
  return pref_score;
}

char *_sup_explain(struct plan_iterator_t *iter, unsigned id) {
  TO_JITER(iter);
  jiter->state->to_tuple(jiter->state, id, jiter->_tuple_tmp);
  size_t size = sizeof(char) * (10 * jiter->state->tuple_size + 5);
  char *ret = malloc(size);
  memset(ret, 0, size);
  struct jupiter_group_t *groups = jiter->planner->multigroup.groups;
  strcat(ret, "(");
  for (uint32_t i = 0; i < jiter->state->tuple_size; ++i) {
    struct jupiter_group_t *group = &groups[i];
    //float portion = (float)jiter->_tuple_tmp[i] / (float)group->group_size;

    char desc[80] = {0};
    snprintf(desc, 80, "%2d/%2d, ", jiter->_tuple_tmp[i], group->group_size);//portion);
    strcat(ret, desc);

    /*
    for (uint32_t j = 0; j < group->nclasses; ++j) {
      struct jupiter_class_t *class = &group->classes[j];
      int sw_to_up = (int)(ceil(class->nswitches * portion));
      for (uint32_t k = 0; k < sw_to_up; ++k) {
        _info_switch(class->switches[k]);
        printf(", ");
      }
    }
    */
  }
  strcat(ret, ")");
  // printf("\n");
  return ret;
}

struct mop_t *_sup_mop_for(struct plan_iterator_t *iter, unsigned id) {
  TO_JITER(iter);
  struct jupiter_switch_mop_t  *mop = malloc(sizeof(struct jupiter_switch_mop_t));
  mop->ncap = DEFAULT_CAP_SIZE;
  mop->switches = malloc(sizeof(struct jupiter_located_switch_t *) * mop->ncap);
  //mop->drain = malloc(sizeof(uint32_t)* (mop->ncap));
  printf("drain num:%d ncap: %d\n",strlen(mop->drain),mop->ncap);
  mop->nswitches = 0;
  jiter->state->to_tuple(jiter->state, id, jiter->_tuple_tmp);
  for (uint32_t i = 0; i < jiter->state->tuple_size; ++i)
    printf("%d ",jiter->_tuple_tmp[i]);
  printf("tuple tmp\n");
  struct jupiter_group_t *groups = jiter->planner->multigroup.groups;
  for (uint32_t i = 0; i < jiter->state->tuple_size; ++i) {
    struct jupiter_group_t *group = &groups[i];
    float portion = (float)jiter->_tuple_tmp[i] / (float)group->group_size;
    //printf("portion: %f\n",portion);

    /* TODO: Right now we don't consider remainder in mop_for. This could
     * be a big deal when only a few switches are left in each group.
     *
     * So for example, when we are upgrading (12, 12, 12) with a (1/5, 1/5, 1/5)
     * we have the option of choosing between (3 out of 12 OR 2 out of 12).
     *
     * For now we go with the ceil so that we are more "conservative" when
     * planning.  Being conservative is always safe.
     *
     * - Omid - 1/19/2019
     * */
    for (uint32_t j = 0; j < group->nclasses; ++j) {
      struct jupiter_class_t *class = &group->classes[j];
      unsigned sw_to_up = (unsigned)(ceil(class->nswitches * portion));
      if (sw_to_up >= class->nswitches)
        sw_to_up = class->nswitches;

      if (mop->nswitches + sw_to_up >= mop->ncap) {
        mop->ncap = mop->ncap * 2 + sw_to_up;
        mop->switches = realloc(mop->switches, sizeof(struct jupiter_located_switch_t *) * mop->ncap);
      }

      for (uint32_t k = 0; k < sw_to_up; ++k) {
        mop->drain[mop->nswitches] = class->switches[k]->sid;
        mop->switches[mop->nswitches] = class->switches[k];
        //printf("mopfor switches id: %d pod: %d\n",mop->drain[mop->nswitches],class->switches[k]->pod);
        mop->nswitches++;
      }
    }
  }
  mop->pre  = _jupiter_mop_pre;
  mop->post = _jupiter_mop_post;
  mop->free = _jupiter_mop_free;
  mop->size = _jupiter_mop_size;
  mop->explain = _jupiter_mop_explain;
  mop->block_stats = _jupiter_block_stats;
  //printf("end sup mop for\n");
  return (struct mop_t *)mop;
}

unsigned _sup_lds(
    struct plan_iterator_t *iter,
    struct mop_block_stats_t *blocks,
    unsigned nblocks) {
  TO_JITER(iter);

  struct jupiter_group_t *groups = jiter->planner->multigroup.groups;
  uint32_t *group_numbers = malloc(sizeof(int) * jiter->state->tuple_size);

  for (uint32_t i = 0; i < jiter->state->tuple_size; ++i) {
    struct jupiter_group_t *group = &groups[i];
    double max_portion = 0;

    // Find the max portion of switches that are filled up in this group
    // TODO: This is more complicated for FatTree like topologies
    for (uint32_t j = 0; j < group->nclasses; ++j) {
      struct jupiter_class_t *class = &group->classes[j];
      // Find a block that matches the class and remember the number of switches in it
      for (unsigned k = 0; k < nblocks; ++k) {
        struct mop_block_stats_t *block = &blocks[k];
        enum BLOCK_TYPE bt = block->id.type;
        int id = block->id.id;
        if ((bt == BT_CORE && class->type == JST_CORE) ||
            ((bt == BT_POD_AGG && class->type == JST_AGG && id == class->pod))) {
          double portion = ((double)block->down_switches / (double)class->nswitches);
          max_portion = MAX(portion, max_portion);
          break;
        } else {
          continue;
        }
      }
    }
    if (max_portion >= 1) {
      // warn("Inaccurate estimation ... Jupiter's independent failure model "
      //      "cannot find a least dominative subplan.");
      max_portion = 1;
    }
    group_numbers[i] = (uint32_t)(floor(max_portion * group->group_size));
  }

  unsigned subplan_id = jiter->state->from_tuple(jiter->state, jiter->state->tuple_size, group_numbers);
  free(group_numbers);

  return subplan_id;
}


struct jupiter_switch_plan_enumerator_iterator_t *_sup_init(
    struct jupiter_switch_plan_enumerator_t *planner) {
  struct jupiter_switch_plan_enumerator_iterator_t *iter = malloc(
      sizeof(struct jupiter_switch_plan_enumerator_iterator_t));

  iter->begin = _sup_begin;
  iter->end   = _sup_end;
  iter->next  = _sup_next;
  iter->plan  = _sup_plan;
  iter->mop_for = _sup_mop_for;
  iter->explain = _sup_explain;
  iter->pref_score = _sup_pref_score;
  iter->subplan_count = _sup_subplan_count;
  iter->free  = _sup_free;
  iter->planner = planner;
  iter->least_dominative_subplan = _sup_lds;

  struct jupiter_group_t *groups = planner->multigroup.groups;
  iter->state = 0;
  //printf("npart groups: %d\n",planner->multigroup.ngroups);
  int debug_cnt = 0;
  for (uint32_t i = 0; i < planner->multigroup.ngroups; ++i) {
    //printf("group_size: %d\n",groups[i].group_size);
    struct group_iter_t *s = npart_create(groups[i].group_size);
    
    info("Creating an npart with %d size", groups[i].group_size);
    if (iter->state == 0) {
      iter->state = s;
      /*for(int j = 0;j<groups[i].group_size;j++)
      {
        printf("%d ",iter->state->state[j]);
      }
      printf("test subplan: %d\n",debug_cnt);*/
      debug_cnt++;
    } else {
      iter->state = dual_npart_create(iter->state, s);
    }
  }

  info("Iter state is: %p", iter->state);
  iter->_tuple_tmp = malloc(sizeof(uint32_t) * iter->state->tuple_size);
  /*for(iter->begin(iter);!iter->end(iter);iter->next(iter))
  {
    for(int j = 0;j<iter->state->state_length;j++)
      {
        printf("%d ",iter->state->state[j]);
      }
      printf("test subplan %d\n",iter->state->state_length);
  }*/
  return iter;
}

static struct plan_iterator_t*
jupiter_switch_plan_enumerator_iterator(
    struct plan_t *planner) {
  return (struct plan_iterator_t *)_sup_init(
      (struct jupiter_switch_plan_enumerator_t*)planner);
}

struct mop_t *
jupiter_mop_for(struct jupiter_located_switch_t **sws, uint32_t nsws) {
  struct jupiter_switch_mop_t  *mop = malloc(sizeof(struct jupiter_switch_mop_t));
  mop->pre  = _jupiter_mop_pre;
  mop->post = _jupiter_mop_post;
  mop->free = _jupiter_mop_free;
  mop->size = _jupiter_mop_size;
  mop->explain = _jupiter_mop_explain;
  mop->block_stats = _jupiter_block_stats;

  mop->ncap = nsws;
  mop->switches = malloc(sizeof(struct jupiter_located_switch_t *) * nsws);
  //mop->drain = malloc(sizeof(uint32_t)*nsws);
  mop->nswitches = nsws;

  for (uint32_t i = 0; i < nsws; ++i) {
    mop->switches[i] = sws[i];
    printf("%d ",sws[i]->sid);
    mop->drain[i] = sws[i]->sid;//todo 对sws代表的是一个switch还是一群有误解
    //printf("id: %d pod: %d",sws[i]->sid,sws[i]->pod);
    
  }
  //printf("jupiter mopfor\n");
  
  return (struct mop_t *)mop;
}
