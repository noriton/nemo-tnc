/*
  Reed Solomon encode/decode test program
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "poly.h"

int main(void)
{
	int i;

	poly_t gp;

	for (i = 2; i <= 32; i+=2) {

		make_gp(&gp, i);

		poly_print(&gp);

	}
}
