#ifndef _JUPITER_H_
#define _JUPITER_H_

enum SWITCH_STAT {
  DOWN,
  UP,
};
#include "dataplane.h"
#include "network.h"

/* Jupiter network switch stats
 *
 * Mainly used for creation of dataplane.  If a switch is DOWN it does not
 * participate in the capacity estimation of the dataplane.  Whereas UP does
 * otherwise.
 * */


/* Status of a switch.  Each switch ID uniquely identifies the switch
 * type/location/ and purpose in the jupiter topology.  For that, we have to
 * rely on core/agg/tor and pod numbers in the jupiter_network_t structure */
struct switch_stats_t {
  enum SWITCH_STAT stat; /* Status of the switch */
  switch_id_t      id;   /* Switch identifier */
};

struct jupiter_network_t {
  struct network_t;  /* A jupiter network is a ... network */

  uint32_t core, agg, pod, tor;    /* Topology specific parameters for Jupiter network */
  bw_t     link_bw;                /* Only one link-bandwidth exists across the whole topology */
  struct switch_stats_t *switches; /* List of switches in the topology */

  /* TODO: Can possible replace this with accessor functions? OR macros
   *
   * - Omid 3/26/2019 */
  struct switch_stats_t *core_ptr; /* Pointers to core switches */
  struct switch_stats_t *agg_ptr;  /* Pointers to aggregate switches */
  struct switch_stats_t *tor_ptr;  /* Pointers to tor switches */

  /* TODO: I used to like putting data at the end of the structure.  Probably
   * not the best idea here.
   *
   * - Omid 3/26/2019 */
      //klotski varible
  uint32_t num_switches;
  float alpha,theta;
  uint8_t used[500];
  double edge_traffic[501][501], cap[501][501];
  struct jupiter_located_switch_t* klotski_switches;
  uint32_t pair_num;
  struct pair_t* pairs[500];
    //char end[];
  void (*klotski_net_init)(struct jupiter_network_t*, uint32_t);
  void (*clear)(struct jupiter_network_t*);
  void (*clear_dis)(struct jupiter_network_t*);

  struct jupiter_located_switch_t* (*get_sw)(struct jupiter_network_t*, uint32_t id);
  void (*build_edge)(struct jupiter_network_t*, uint32_t src, uint32_t dst, double capacity);
  char end[];
};
void _klotski_net_init(struct jupiter_network_t*, uint32_t);
void _clear(struct jupiter_network_t*);
void _clear_dis(struct jupiter_network_t*);

struct jupiter_located_switch_t* _get_sw(struct jupiter_network_t*, uint32_t id);
void _build_edge(struct jupiter_network_t*, uint32_t src, uint32_t dst, double capacity);
void _copy_switches(struct jupiter_network_t*, struct jupiter_located_switch_t* sws,int n);


/* Creates a new jupiter network with the passed parameters. */
struct network_t *
jupiter_network_create(
    uint32_t core,
    uint32_t pod,
    uint32_t agg_per_pod,
    uint32_t tor_per_pod,
    bw_t link_bw);

struct klotski_network_t*
    klotski_network_create(const char* path1, const char* path2, uint32_t nswitch, uint32_t nedge,float alpha,float theta) ;


/* Setup a traffic matrix on the jupiter topology.  Mainly used for dataplane creation */
int  jupiter_set_traffic (struct network_t *, struct traffic_matrix_t const*);

/* Returns the active traffic matrix on the topology */
int  jupiter_get_traffic (struct network_t *, struct traffic_matrix_t const**);

/* Builds and returns the current dataplane */
int  jupiter_get_dataplane (struct network_t *, struct dataplane_t*); 

/* Drains a switch, i.e., it sets the status of the switch to "DOWN".  Uses the
 * switch_id, which can be obtained by leverating the jupiter_get_core/agg
 * helper functions. ToRs are irrelevant at this stage so there is no helper
 * function for them.*/
void jupiter_drain_switch(struct network_t *, switch_id_t);

/* Undrains a switch, i.e., it sets the status of the switch to "UP".  Uses the
 * switch_id, which can be obtained by leverating the jupiter_get_core/agg
 * helper functions. ToRs are irrelevant at this stage so there is no helper
 * function for them.*/
void jupiter_undrain_switch(struct network_t *, switch_id_t);
void jupiter_network_free(struct network_t *);

/* Returns the switch id of the ith core switch */
switch_id_t jupiter_get_core(struct network_t *, uint32_t);

/* Returns the switch id of the ith agg switch in the jth pod */
switch_id_t jupiter_get_agg(struct network_t *, uint32_t, uint32_t);

void klotski_network_free(struct jupiter_network_t*);
#endif // _JUPITER_H_
