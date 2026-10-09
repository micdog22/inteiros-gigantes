/*
 * bench_karatsuba.c: mede onde a multiplicação de Karatsuba passa a ganhar
 * da escolar, para escolher BI_KARATSUBA_THRESHOLD.
 *
 * Para cada tamanho n (em limbs de 32 bits) compara:
 *   escolar:     n x n direto
 *   1 nível:     um passo de Karatsuba (3 produtos n/2 x n/2) e depois escolar
 * O limiar bom é o primeiro n em que "1 nível" fica mais rápido.
 */
#include "bigint.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t rng = 2026;

static uint32_t next32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)rng;
}

static void random_bigint(bigint *a, size_t limbs)
{
    char hex[8 * 1024 + 1];
    size_t k = 0;
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < limbs * 8 && k + 1 < sizeof hex; i++)
        hex[k++] = digits[next32() & 15u];
    hex[0] = '8';   /* garante o tamanho exato */
    hex[k] = '\0';
    bi_from_hex(a, hex);
}

/* Tempo médio (em microssegundos) de uma multiplicação. */
static double time_mul(const bigint *a, const bigint *b, size_t threshold, int schoolbook)
{
    bigint r;
    bi_init(&r);
    long reps = 1;
    double elapsed;
    for (;;) {
        clock_t t0 = clock();
        for (long i = 0; i < reps; i++) {
            if (schoolbook)
                bi_mul_schoolbook(&r, a, b);
            else
                bi_mul_karatsuba(&r, a, b, threshold);
        }
        elapsed = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (elapsed > 0.05)
            break;
        reps *= 2;
    }
    bi_free(&r);
    return elapsed * 1e6 / (double)reps;
}

int main(void)
{
    static const size_t sizes[] = {8, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 128, 192, 256, 512};
    bigint a, b;
    size_t crossover = 0;
    bi_init(&a);
    bi_init(&b);

    printf("%6s %14s %14s %14s\n", "limbs", "escolar (us)", "1 nível (us)", "Karatsuba (us)");
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        size_t n = sizes[i];
        random_bigint(&a, n);
        random_bigint(&b, n);
        double school = time_mul(&a, &b, 0, 1);
        double one = time_mul(&a, &b, n, 0);
        double full = time_mul(&a, &b, BI_KARATSUBA_THRESHOLD, 0);
        printf("%6zu %14.2f %14.2f %14.2f%s\n", n, school, one, full,
               one < school ? "  <- Karatsuba ganha" : "");
        if (crossover == 0 && one < school * 0.97)
            crossover = n;
    }
    if (crossover)
        printf("\nUm nível de Karatsuba passa a compensar por volta de %zu limbs (%zu bits).\n",
               crossover, crossover * 32);
    printf("Limiar em uso (BI_KARATSUBA_THRESHOLD): %d limbs.\n", BI_KARATSUBA_THRESHOLD);
    bi_free(&a);
    bi_free(&b);
    return 0;
}
