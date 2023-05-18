#include <stdio.h>
#include "sfmm.h"

int main(int argc, char const *argv[]) {


    // double* ptr = sf_malloc(sizeof(double));
    // *ptr = 320320320e-320;
    // printf("%f\n", *ptr);
    // sf_free(ptr);

    // size_t sz_x = sizeof(int) * 20, sz_y = sizeof(int) * 16;
    // void *x = sf_malloc(sz_x);
    // void *y = sf_realloc(x, sz_y);
    // printf("%p",y);

    sf_memalign(55,256);

    return EXIT_SUCCESS;
}
