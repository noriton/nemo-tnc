/*
  Reed Solomon code library
*/

#ifndef RS_H

#include "galois_field.h"
#include "polynomial.h"

#define RS_ERR (-1)
#define RS_OK 0

#define CORRECT_LIST_LEN 128

typedef struct {
  uint8_t err_pos;
  gf_t    err_val;
} correct_t;

/*
  chack RS parameta
*/
int rs_check(int block_len, int message_len, int parity_len);


/*
  generate RS parity from message
*/
int rs_block_encode(uint8_t parity[], uint8_t message[], int parity_len, int message_len);


/*
  decode RS code
*/

int rs_block_decode(correct_t corlist[], uint8_t rs_code[], int parity_len, int rscode_len);

void error_corect(uint8_t rscode[], correct_t corlist[], int rscode_len, int error_count);

static void make_x_2t(poly_t *x_2t, int parity_len);
static int get_posval(correct_t corlist[], poly_t *sigma, poly_t *omega, int find_len, int rs_t);
static void calc_syndrome(poly_t *syndrome_poly, poly_t *code_poly, int parity_len);

#define RS_H 1
#endif
