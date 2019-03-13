#ifndef _ALGO_RANDVAR_H_          
#define _ALGO_RANDVAR_H_         

#include <stdint.h>

typedef double rvar_type_t;

enum RVAR_TYPE {
  SAMPLED, BUCKETED,
};

struct rvar_t {
  rvar_type_t     (*expected)  (struct rvar_t const*);
  rvar_type_t     (*percentile)(struct rvar_t const*, float);
  struct rvar_t * (*convolve)  (struct rvar_t const*, struct rvar_t const*, rvar_type_t bucket_size);
  struct rvar_bucket_t * (*to_bucket) (struct rvar_t const*, rvar_type_t);

  void (*free)(struct rvar_t *);
  char * (*serialize)(struct rvar_t *, int *size);
  void (*plot)(struct rvar_t const *);
  struct rvar_t * (*copy)(struct rvar_t const *);

  enum RVAR_TYPE _type;
};

// Create a bucketized RVar value
struct rvar_t *rvar_bucket_create(rvar_type_t, rvar_type_t, uint32_t);

// Create a sampled RVar value
struct rvar_t *rvar_sample_create(int);

// Rvar sampled type
struct rvar_sample_t {
  struct rvar_t;
  rvar_type_t low, high;        
  uint32_t num_samples;      
  rvar_type_t *vals;            
};

// Rvar Bucketized type
struct rvar_bucket_t {
  struct rvar_t;
  rvar_type_t bucket_size;
  rvar_type_t low;

  uint32_t    nbuckets;
  rvar_type_t *buckets;
};


struct rvar_t *rvar_deserialize(char const *data);
void rvar_sample_finalize(struct rvar_sample_t *rvar, uint32_t steps);
struct rvar_t *rvar_sample_create_with_vals(rvar_type_t *vals, uint32_t nvals);
struct rvar_t *rvar_zero(void);

//rvar_bucket_t *rvar_to_bucket(struct

#endif // _ALGO_RANDVAR_H_   
