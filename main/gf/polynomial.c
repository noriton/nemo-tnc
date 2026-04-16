/*
  Galois field Polynomial Functions
*/

#include <stdio.h>

#include "galois_field.h"
#include "gf_func.h"
#include "polynomial.h"


/*
  assignment polynomial
*/
void poly_assign(poly_t *dist, poly_t *src)
{
	for (int deg = 0; deg <= src->degree; deg++) {
		 dist->coefficient[deg] = src->coefficient[deg];
	}
	dist->degree = src->degree;
}

/*
  normalizing polynomial
*/
void poly_normalize(poly_t *poly)
{
	while ((poly->degree > 0) && (poly->coefficient[poly->degree] == 0)) {
		poly->degree--;
	}
}

/*
  clear to polynomial
*/
void poly_clear(poly_t *poly)
{
	for (int i = 0; i < POLY_SIZE; i++) {
		poly->coefficient[i] = 0;
	}
	poly->degree = 0;
}

/*
  set zero to polynomial
*/
void poly_setzero(poly_t *poly)
{
	poly->coefficient[0] = 0;
	poly->degree = 0;
}

/*
  set one to polynomial
*/
void poly_setone(poly_t *poly)
{
	poly->coefficient[0] = 1;
	poly->degree = 0;
}

/*
  return true if polynomial is zero
*/
int poly_iszero(poly_t *poly)
{
	return (poly->degree == 0) && (poly->coefficient[0] == 0);
}


/* calculate polynomial sum of products value */
gf_t poly_sum_products(poly_t *poly, gf_t x)
{
	int i;
	gf_t s = 0;

	for (i = poly->degree; i >= 0; --i) {
		s = gf_mul(s, x) ^ poly->coefficient[i];
	}

	return s;
}

/* result += result + poly */
void poly_add_assign(poly_t *result, poly_t *poly)
{
	int i;

	if (result->degree >= poly->degree) {
		for (i = 0; i <= poly->degree; i++) {
			result->coefficient[i] ^= poly->coefficient[i];
		}
	} else {
		for (i = 0; i <= result->degree; i++) {
			result->coefficient[i] ^= poly->coefficient[i];
		}
		for (i = result->degree+1; i <= poly->degree; i++) {
			result->coefficient[i] = poly->coefficient[i];
		}
		result->degree = poly->degree;
	}
	poly_normalize(result);
}

void poly_add(poly_t *result, poly_t *poly_f, poly_t *poly_g)
{
	poly_t *high;
	poly_t *low;
	int i;

	if (poly_f->degree >= poly_g->degree) {
		high = poly_f;
		low  = poly_g;
	} else {
		high = poly_g;
		low  = poly_f;
	}
	for (i = 0; i <= low->degree; i++) {
		result->coefficient[i] = high->coefficient[i] ^ low->coefficient[i];
	}
	for (i = low->degree+1; i <= high->degree; i++) {
		result->coefficient[i] = high->coefficient[i];
	}
	result->degree = high->degree;
	poly_normalize(result);
}

	
/* polynomial multiplication */
void poly_mul(poly_t *result, poly_t *poly_f, poly_t *poly_g)
{
	poly_clear(result);

	for (int i = 0; i <= poly_f->degree; i++) {
		gf_t m = poly_f->coefficient[i];
		for (int j = 0; j <= poly_g->degree; j++) {
			result->coefficient[i + j] ^= gf_mul(m, poly_g->coefficient[j]);
		}
	}
	result->degree = poly_f->degree + poly_g->degree;
	poly_normalize(result);
}

/*
  polynomial division

  return
    quotient = dividend / divisor
    remainder = dividend % dvisor
*/
int poly_div(poly_t *quotient, poly_t *remainder, poly_t *dividend, poly_t *divisor)
{

#define RS_SAVE_MEMORY

#ifdef RS_SAVE_MEMORY
	poly_t *work_dividend = remainder;
#else
	poly_t work_dividend_area;
	poly_t *work_dividend = &work_dividend_area;
#endif

	if (poly_iszero(divisor)) {
		return DIVID_ZERO;
	}

	poly_assign(work_dividend, dividend);
	poly_clear(quotient);

	/* degree of quotient */
	int q_deg = dividend->degree - divisor->degree;

	if (q_deg >= 0) {
		quotient->degree = q_deg;

		/* reciplocal number of most significana coefficienticient of divisor */
		gf_t part_divisor = divisor->coefficient[divisor->degree];

		for (int i = work_dividend->degree; i >= divisor->degree; --i) {
			/* quotient = work->coefficient / divisor->coefficient */
			gf_t part_quotient = gf_div(work_dividend->coefficient[i], part_divisor); 
			quotient->coefficient[q_deg] = part_quotient;

			/* subtract same addition, it is xor  : divisor * part_quotient */
			for (int j = 0; j <= divisor->degree; j++) {
				work_dividend->coefficient[i - divisor->degree + j] ^= gf_mul(divisor->coefficient[j], part_quotient);
			}
			--q_deg;
  		}
	}

#ifndef RS_SAVE_MEMORY
	poly_assign(remainder, work_dividend); // work_dividend is remainder
#endif

	poly_normalize(remainder); // normalize remainder
	poly_normalize(quotient);  // normalize quotinent

	return POLY_OK;
}

/*
  extended Euclidean Algorithm for GF(2^8)

  given x, y, t; (t is remainder degree)
  expect a, b, c where ax + by = c (deg c < t)
*/
void poly_euclid(poly_t *a, poly_t *b, poly_t *x, poly_t *y, int t)
{
	poly_t q, r, g, f;
	poly_t *pp_r = &r, *pp_g = &g, *pp_f = &f;

	poly_t a0, a1;
	poly_t *pp_a0 = &a0, *pp_a1 = &a1;

	poly_t b0, b1;
	poly_t *pp_b0 = &b0, *pp_b1 = &b1;

	poly_t tmp;
	poly_t *pp_tmp;


	poly_assign(pp_g, x);
	poly_assign(pp_f, y);

	poly_setzero(pp_a0);
	poly_setone(pp_a1);
	poly_setone(pp_b0);
	poly_setzero(pp_b1);


	while (pp_f->degree >= t) { /* exit if deg f < t */

		poly_div(&q, pp_r, pp_g, pp_f); 

		pp_tmp = pp_g;
		pp_g = pp_f;
		pp_f = pp_r;
		pp_r = pp_tmp;

		poly_mul(&tmp, &q, pp_a1); // tmp = q * a1
		poly_sub_assign(pp_a0, &tmp); // a0 -= tmp

		pp_tmp = pp_a0;
		pp_a0 = pp_a1;
		pp_a1 = pp_tmp;

		poly_mul(&tmp, &q, pp_b1); // tmp = q * b1
		poly_sub_assign(pp_b0, &tmp); // b0 -= tmp

		pp_tmp = pp_b0;
		pp_b0 = pp_b1;
		pp_b1 = pp_tmp;
	}

	poly_assign(a, pp_a1);
	poly_assign(b, pp_b1);
}

/*
  Formal differentiation polynomial
 */
void poly_differ(poly_t *df, poly_t *f)
{
	if (f->degree <= 0) return;

	for (int i = 1; i <= f->degree; i++) {
		df->coefficient[i - 1] = (i % 2) ? f->coefficient[i] : 0 ;
	}
	df->degree = f->degree - 1;
	poly_normalize(df);
}

/*
  make generation polynamial G_2t(x) 
  (t is maximum collection data number)
*/
int make_gen_poly(poly_t *g_poly, int gen_poly_len)
{
        int i;
        poly_t x, tmp;

	if (gen_poly_len >= GF_ELEMENT) return OVER_RANGE;

        poly_setone(g_poly);

        /* initialize polynomial (x - a^n) */
        poly_clear(&x);
        x.degree = 1;
        x.coefficient[1] = 1;

        for (i = 0; i  < gen_poly_len; i++) {
                x.coefficient[0] = gf_pow(i); /* make (x - a^i) */

                poly_mul(&tmp, g_poly, &x); /* tmp = G_i(x) * (x - a^i) */
                poly_assign(g_poly, &tmp);  /* G_i+i(x) = tmp */
        }

        return POLY_OK;
}

/*
  output polynomial coefficienticients
*/
void poly_print(poly_t *poly)
{
	int i;
	gf_t c;

	printf("degree: %d\n", poly->degree);
	for (i = 0; i <= poly->degree; i++) {
		c = poly->coefficient[i];
		if (i == 0) {
			printf("x[%d]: a^%d(%d) ", i, gf_ind(c), c);
		} else if (c != 0) {
			printf("+ x[%d]: a^%d(%d) ", i, gf_ind(c), c);
		}
	}
	printf("\n");
}

