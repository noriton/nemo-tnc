/*
  Polynomial function header
*/

#ifndef POLYNOMIAL_H

#include "galois_field.h"

#define POLY_OK	0 
#define OVER_RANGE	-1
#define DIVID_ZERO	-2

#define POLY_SIZE (GF_ORDER) /* Maximum number of coefficients */

typedef struct {
  int degree;
  gf_t coefficient[POLY_SIZE];
} poly_t;

/*
  assignment polynomial
*/
void poly_assign(poly_t *to, poly_t *from);

/*
  normalize polynomial degree
*/
void poly_normalize(poly_t *poly);


/*
  set the value 1 to the polynomial
*/
void poly_setone(poly_t *poly);

/*
  set the value 0 to the polynomial
*/
void poly_setzero(poly_t *poly);

/*
  clear polynomial with 0
*/
void poly_clear(poly_t *poly);

/*
  return true if polynomial is zero
*/
int poly_iszero(poly_t *poly);

/*
  polynomial division

    quotient = dividend / divisor
    remainder = dividend % dvisor

  return if divisor is zero, error;
*/
int poly_div(poly_t *dividend, poly_t *divisor, poly_t *quotient, poly_t *remainder);


/*
  output polynomial coefficients
*/
void poly_print(poly_t *poly);



/* calculate polynomial sum of products value */
gf_t poly_sum_products(poly_t *poly, gf_t x);


/* polynomial addition */
void poly_add(poly_t *result, poly_t *f, poly_t *g);

/* polynomial addition and assignment */
/* result += poly */
void poly_add_assign(poly_t *result, poly_t *poly);

/* polynomial subtraction */
static inline void poly_sub(poly_t *result, poly_t *f, poly_t *g)
{
	poly_add(result, f, g);
}
// sub is same add at GF(2^N)

/* polynomial subtraction and assignment */
static inline void poly_sub_assign(poly_t *result, poly_t *poly)
{
	poly_add_assign(result, poly);
}


/* polynomial multiplication */
void poly_mul(poly_t *result, poly_t *f, poly_t *g);


/*
  extended Euclidean Algorithm for gf(2^8)

  input x, y, t; t specifies degree of common divider
  output a, b where deg(a*x + b*y) < t
*/
void poly_euclid(poly_t *a, poly_t *b, poly_t *x, poly_t *y, int t);


/*
  formal differentiation polynomial
 */
void poly_differ(poly_t *df, poly_t *f);


/*
  make generation polynamial
*/
int make_gen_poly(poly_t *gen_poly, int parity_len);


#define POLYNOMIAL_H 1
#endif
