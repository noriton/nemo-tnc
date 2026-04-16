/*
  Reed Solomon encode/decode test program
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "rs_codec.h"

#define RS_CODE 255
#define RS_MESG 239
#define RS_PARI (RS_CODE - RS_MESG)

#define ERR_RATE 1000 /* error rate is 1/ERR_RATE */
//#define LOOP_COUNT 10000
#define LOOP_COUNT 1

int main(void)
{
	int i, j, k;
	uint8_t code1[RS_CODE], code2[RS_CODE], code3[RS_CODE];
//	uint8_t code_old[RS_CODE];
	uint8_t parityn[RS_CODE];

	int error_count;

	correct_t corlist[CORRECT_LIST_LEN];

	int count;

	int flag = 1;

	for (count = 0; count < LOOP_COUNT; count++) {

		if (rs_check(RS_CODE, RS_MESG, RS_PARI)) {
			fprintf(stderr, "rs_check() return error\n");
			exit(1);
		}


		/* make original message */
		for (i = 0; i < RS_MESG; i++) {
			code1[i] = rand() & 0xff;
		}

		/* generate Reed-Solomon parity */
		if (rs_block_encode(parityn, code1, RS_PARI, RS_MESG)) {
			fprintf(stderr, "rs_encode() return error\n");
			exit(1);
		}
		for (i = 0; i < RS_PARI; i++) {
			code1[i+RS_MESG] = parityn[i];
		}
		
		/* copy RS code */
		for (i = 0; i < RS_CODE; i ++) {
			code2[i] = code1[i];
		}

		/* add errors */
		for (i = 0; i < RS_PARI / 2; i++) {
			j = rand() % RS_CODE; /* index */
			k = rand() & 0xff; /* error value */

			code2[j] = k; /* error */
		}

		/* copy error code */
		for (i = 0; i < RS_CODE; i++) {
			code3[i] = code2[i];
		}

		error_count = rs_block_decode(corlist, code3, RS_PARI, RS_CODE);

		if (error_count < 0) {
			fprintf(stderr, "rs_decode() can not correct error\n");
			return RS_ERR;
		}

		/* error correction */
		error_corect(code3, corlist, RS_CODE, error_count);

		/* error correction */
//		error_count = rs_decode(code4, RS_CODE, RS_MESG);
//		if (error_count < 0) {
//			fprintf(stderr, "rs_decode() can not correct error\n");
//			return RS_ERR;
//		}

#define CODEDEBUG YES

#ifdef CODEDEBUG

		for (i = 0; i < RS_CODE; i++) {
			if (code1[i] != code3[i]) {	
				flag = 1;
				break;
			}
		}
		if (flag == 1) {
			/* print codes */
			printf("count %d , error_count %d\n", count, error_count);
			printf("[index], original, with error, correct\n");
			for (i = 0; i < RS_CODE; i++) {
				printf("[%d], %02x, %02x, %02x  %c%c\n", 
				i, code1[i], code2[i], code3[i],
				(code2[i] != code3[i]) ? '*' : ' ',
				(code1[i] != code3[i]) ? '!' : ' ');
			}
			flag = 0;
		}
#endif
	}



	return RS_OK;
}
