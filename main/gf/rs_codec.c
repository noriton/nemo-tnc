/*
  Reed Solomon code library
*/

#include <stdio.h>

#include "rs_codec.h"
#include "gf_func.h"
#include "polynomial.h"


int rs_check(int block_len, int message_len, int parity_len)
{

	if (block_len <= 0) return RS_ERR;
	if (block_len >= GF_ELEMENT) return RS_ERR;

	if (message_len <= 0) return RS_ERR;
	if (parity_len <= 1) return RS_ERR;
	if (block_len < message_len + parity_len) return RS_ERR;

	return RS_OK;
}

/*
**	rs_block_encode 
**	
**	input: message  message data. acepte shortened code.
**	input: message_len  message data length.
**	input: parity_len  parity length = t * 2.
**	output: parity  parity data nessary buffer parity_len
*/

int rs_block_encode(uint8_t parity[], uint8_t message[], int parity_len, int message_len)
{
        int i;

        poly_t quotinent, remainder;
        poly_t block_poly, gen_poly;

        make_gen_poly(&gen_poly, parity_len);

        /* message upper sift parity_len */
        for (i = 0; i < parity_len; i++) {
                block_poly.coefficient[i] = 0;
        }

        /* invert copy message */
        for (i = 0; i < message_len ; i++) {
                block_poly.coefficient[parity_len + i] = (gf_t) message[message_len - 1 - i];
        }

        block_poly.degree = parity_len + message_len - 1;
        poly_normalize(&block_poly);

        /* calculate parity; divide by generating polynomial */
        poly_div(&quotinent, &remainder, &block_poly, &gen_poly);

        /* parity is  negated remainder but this is only copy becouse at GF(2^N) */

        for (i = 0; i < parity_len; i++) {
                parity[parity_len - 1 - i] = (uint8_t) remainder.coefficient[i];
        }

        return RS_OK;
}


/*
**	rs_block_decode 
**	
**	input: rscode  resive data(include parity). acepte shortened code.
**	input: rscode_len  resive data length.
**	input: parity_len  parity length = t * 2.
**	output: corlist[]  error positions and error values list
**	return: occured error number ,  if < 0 faital error;
*/

int rs_block_decode(correct_t corlist[], uint8_t rscode[], int parity_len, int rscode_len)
{
	int i;
	poly_t block_poly;

	int rs_gp_n ;

	/* make polynomial from received code */
	for (i = 0; i < rscode_len; i++) {
		block_poly.coefficient[i] = (gf_t) rscode[rscode_len - 1 - i];
	}
	block_poly.degree = rscode_len - 1;
	poly_normalize(&block_poly);


	/* calculate syndrome */
	poly_t syndrome_poly;
	calc_syndrome(&syndrome_poly, &block_poly, parity_len);

	/* check, if syndrome all zero then no-error */
	if (poly_iszero(&syndrome_poly)) {
		return 0;
	}

	/* make polinomial X^(2*t) */
	poly_t x_2t;
	make_x_2t(&x_2t, parity_len);

	/* calculate sigma(x), omega(x) */
	poly_t sigma, omega;
	poly_euclid(&sigma, &omega, &x_2t, &syndrome_poly, parity_len / 2);


	/* find error position */
	int error_count;

	error_count = get_posval(corlist, &sigma, &omega, rscode_len, parity_len / 2 );

	/* there is no error at this point or many error found, unexpected error */
	if (error_count == 0 || error_count > parity_len / 2) return -1;

	return error_count;
}


void error_corect(uint8_t rscode[], correct_t corlist[], int rscode_len, int error_count)
{
	int i;

	/* error correction */
	for (i = 0; i < error_count; i++) {
		rscode[rscode_len - 1 - corlist[i].err_pos] ^= corlist[i].err_val;
	}
}

/* calculate syndrome and make polinomial S(x) */
static void calc_syndrome(poly_t *syndrome_poly, poly_t *code_poly, int parity_len)
{
	for (int i = 0; i < parity_len; i++) {
		gf_t synd = poly_sum_products(code_poly, gf_pow(i));
		syndrome_poly->coefficient[(parity_len - 1) - i] = synd;
	}
	syndrome_poly->degree = parity_len - 1;
	poly_normalize(syndrome_poly);
}

static void make_x_2t(poly_t *x_2t, int parity_len)
{
	poly_clear(x_2t);
	x_2t->degree = parity_len;
	x_2t->coefficient[parity_len] = 1;
}

static int get_posval(correct_t corlist[], poly_t *sigma, poly_t *omega, int code_len, int rs_t)
{
	int errs = 0;

	/* nessesary -sigma(x) but it is do nothing at GF(2^N) (-sigma = sigma) */

	poly_t dsigma_r;
	poly_t *dsigma = &dsigma_r;

	/* differentiating sigma */
	poly_differ(dsigma, sigma);

	for (int i = 0; i < code_len; i++) {

		gf_t an = gf_pow(i);

		/* found error position */
		if (poly_sum_products(sigma, an) == 0) {

			corlist[errs].err_pos = i;
			corlist[errs].err_val = gf_div(poly_sum_products(omega, an), poly_sum_products(dsigma, an));
			errs++;

		}
		if (errs >= rs_t) break;
	}
	return errs;
}


