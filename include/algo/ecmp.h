#ifndef ALGO_ECMP_H
#define ALGO_ECMP_H
#include "dataplane.h"
#include "network.h"
#include "networks/jupiter.h"

/* Returns the flows under max-min fairness
 *
 * TODO: I can probably switch this with a simulator class which accepts an
 * input and returns packet-loss/impact?  Still not sure if that's the best
 * model.
 */
int ecmp(struct jupiter_network_t *,double*);

#endif