/*
  Galoris Feild processing routines on GF(2^8)
*/

#ifndef GF_FUNC_H
#define GF_FUNC_H

#include "galois_field.h"

/*
gf_function use index and power tables
*/

extern const gf_t GF_POWER[];
extern const gf_t GF_INDEX[];

/*
  index
  return y where a^y = x
 */
static inline gf_t gf_ind(gf_t x)
{
  return GF_INDEX[x];
}

/*
  power
  return a^x
 */
static inline gf_t gf_pow(gf_t x)
{
  return GF_POWER[x];
}

/*
  multiplication
  return x * y
*/
static inline gf_t gf_mul(gf_t x, gf_t y)
{
  int ind;

  if (x == 0 || y == 0) return 0;

  ind = gf_ind(x) + gf_ind(y);
  if (ind >= GF_ORDER) ind -= GF_ORDER;

  return gf_pow(ind);
}

/*
  take the reciproc number
  return 1/x  (y where x * y = 1)
 */
static inline gf_t gf_recip(gf_t x)
{
  if (x == 0) return 0; 

  return gf_pow(GF_ORDER - gf_ind(x));
}

/*
  dividing
  return x / y
*/
static inline gf_t gf_div(gf_t x, gf_t y)
{
  return gf_mul(x, gf_recip(y));
}

#endif

