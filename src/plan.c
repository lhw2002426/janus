#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "networks/jupiter.h"

#include "util/common.h"
#include "util/group_gen.h"
#include "util/log.h"

#include "plan.h"

static struct jupiter_group_t *
_jupiter_get_group_for(struct jupiter_multigroup_t *mg,
    struct jupiter_located_switch_t *sw) {
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
  for (int i = 0; i < planner->num_switches; ++i) {
    struct jupiter_located_switch_t *sw = &planner->switches[i];
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
      class->color = sw->color;
      class->pod = sw->pod;
      class->type = sw->type;
      _jupiter_add_switch_to_class(class, sw);
    }
  }
}

static
int _max_class_size(struct jupiter_group_t *group) {
  int ret = 0;
  for (uint32_t i = 0; i < group->nclasses; ++i) {
    ret = MAX(ret, group->classes[i].nswitches);
  }

  return ret;
}

struct jupiter_switch_plan_enumerator_t *jupiter_switch_plan_enumerator_create(
    uint32_t num_switches, struct jupiter_located_switch_t const *switches,
    uint32_t *freedom_degree, uint32_t ndegree) {
  if (num_switches == 0)
    panic("Creating a planner with no switches ...");

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
  }

  _jupiter_build_groups(planner);

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

int _sup_subplan_count(struct plan_iterator_t *iter) {
  TO_JITER(iter);

  return jiter->state->num_subsets(jiter->state);
}

void _sup_begin (struct plan_iterator_t *iter) {
  TO_JITER(iter);
  jiter->state->begin(jiter->state);
}

int  _sup_next (struct plan_iterator_t *iter) {
  TO_JITER(iter);
  return jiter->state->next(jiter->state);
}

int  _sup_end (struct plan_iterator_t *iter) {
  TO_JITER(iter);
  return jiter->state->end(jiter->state);
}

void _sup_free(struct plan_iterator_t *iter) {
  TO_JITER(iter);

  jiter->state->free(jiter->state);
  free(jiter->_tuple_tmp);
  free(jiter);
}

void _jupiter_mop_free(struct mop_t *mop) {
  struct jupiter_switch_mop_t *jop = (struct jupiter_switch_mop_t *)mop;
  free(jop->switches);
  free(jop);
}

int _jupiter_mop_pre(struct mop_t *mop, struct network_t *net) {
  struct jupiter_network_t *jup = (struct jupiter_network_t *)net;
  struct jupiter_switch_mop_t *jop = (struct jupiter_switch_mop_t *)mop;
  (void)(jup); (void)(jop);

  for (uint32_t i = 0; i < jop->nswitches; ++i) {
    struct jupiter_located_switch_t *sw = jop->switches[i];
    jup->drain_switch((struct network_t *)jup, sw->sid);
  }

  //info("Draining %d switches.", jop->nswitches);

  return 0;
}

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

void _sup_plan(struct plan_iterator_t *iter, int **ret, int *size) {
  TO_JITER(iter);
  struct group_iter_t *state = jiter->state;
  int *arr = malloc(sizeof(int) * state->state_length);
  *ret = arr;

  for (uint32_t i = 0; i < state->state_length; ++i) {
    *arr = state->state[i];
    arr++;
  }

  *size = state->state_length;
}

#define DEFAULT_CAP_SIZE 10

struct mop_t *_sup_mop_for(struct plan_iterator_t *iter, int id) {
  TO_JITER(iter);
  struct jupiter_switch_mop_t  *mop = malloc(sizeof(struct jupiter_switch_mop_t));
  mop->ncap = DEFAULT_CAP_SIZE;
  mop->switches = malloc(sizeof(struct jupiter_located_switch_t *) * mop->ncap);
  mop->nswitches = 0;
  jiter->state->to_tuple(jiter->state, id, jiter->_tuple_tmp);

  struct jupiter_group_t *groups = jiter->planner->multigroup.groups;
  for (uint32_t i = 0; i < jiter->state->tuple_size; ++i) {
    struct jupiter_group_t *group = &groups[i];
    float portion = (float)jiter->_tuple_tmp[i] / (float)group->group_size;

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
      int sw_to_up = (int)(ceil(class->nswitches * portion));
      if (mop->nswitches + sw_to_up >= mop->ncap) {
        mop->ncap = mop->ncap * 2 + sw_to_up;
        mop->switches = realloc(mop->switches, sizeof(struct jupiter_located_switch_t *) * mop->ncap);
      }

      for (uint32_t k = 0; k < sw_to_up; ++k) {
        mop->switches[mop->nswitches++] = class->switches[k];
      }
    }
  }
  mop->pre  = _jupiter_mop_pre;
  mop->post = _jupiter_mop_post;
  mop->free = _jupiter_mop_free;

  return (struct mop_t *)mop;
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
  iter->subplan_count = _sup_subplan_count;
  iter->free  = _sup_free;
  iter->planner = planner;

  struct jupiter_group_t *groups = planner->multigroup.groups;
  iter->state = 0;

  for (uint32_t i = 0; i < planner->multigroup.ngroups; ++i) {
    struct group_iter_t *s = npart_create(groups[i].group_size);
    info("Creating an npart with %d size", groups[i].group_size);
    if (iter->state == 0) {
      iter->state = s;
    } else {
      iter->state = dual_npart_create(iter->state, s);
    }
  }

  info("Iter state is: %p", iter->state);
  iter->_tuple_tmp = malloc(sizeof(uint32_t) * iter->state->tuple_size);

  return iter;
}

struct plan_iterator_t*
jupiter_switch_plan_enumerator_iterator(
    struct plan_t *planner) {
  return (struct plan_iterator_t *)_sup_init(
      (struct jupiter_switch_plan_enumerator_t*)planner);
}