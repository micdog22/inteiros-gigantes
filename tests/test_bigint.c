/*
 * test_bigint.c: testes de unidade da biblioteca e do avaliador.
 *
 * Os valores esperados foram conferidos com o python3, que tem inteiros
 * de precisão arbitrária nativos.
 */
#include "teste.h"

#include "bigint.h"
#include "calc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_DEC(a, expected)                                                         \
    do {                                                                               \
        char *t_ = bi_to_dec(a);                                                       \
        tests_run++;                                                                   \
        if (t_ == NULL || strcmp(t_, (expected)) != 0) {                              \
            tests_failed++;                                                            \
            fprintf(stderr, "  FALHOU %s:%d: %s = %s, esperado %s\n", __FILE__, __LINE__, \
                    #a, t_ ? t_ : "(nulo)", (expected));                              \
        }                                                                              \
        free(t_);                                                                      \
    } while (0)

/* ---------------------------------------------------------- utilidades */

static uint64_t rng_state = 7;

static uint64_t rng_next(void)
{
    uint64_t z = (rng_state += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

static void set_dec(bigint *a, const char *s)
{
    if (bi_from_dec(a, s) != BI_OK) {
        fprintf(stderr, "número de teste inválido: %s\n", s);
        exit(2);
    }
}

static void set_hex(bigint *a, const char *s)
{
    if (bi_from_hex(a, s) != BI_OK) {
        fprintf(stderr, "número de teste inválido: %s\n", s);
        exit(2);
    }
}

/* Número aleatório com exatamente limbs limbs; kind 1 = todos os bits 1. */
static void random_limbs(bigint *a, size_t limbs, int kind)
{
    char *hex = malloc(limbs * 8 + 1);
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < limbs * 8; i++)
        hex[i] = kind == 1 ? 'f' : digits[rng_next() & 15u];
    if (hex[0] == '0')
        hex[0] = '1';
    hex[limbs * 8] = '\0';
    set_hex(a, hex);
    free(hex);
}

/* 2^k + delta */
static void pow2_plus(bigint *a, size_t k, int64_t delta)
{
    bi_set_u64(a, 1);
    bi_shl(a, a, k);
    bi_add_i64(a, a, delta);
}

/* --------------------------------------------------------- conversões */

static void test_text(void)
{
    SECTION("decimal e hexadecimal");
    bigint a;
    bi_init(&a);
    CHECK_DEC(&a, "0");
    CHECK_EQ(bi_sign(&a), 0);

    set_dec(&a, "-0");
    CHECK_DEC(&a, "0");
    CHECK_EQ(a.neg, 0);   /* não existe -0 */
    set_dec(&a, "+42");
    CHECK_DEC(&a, "42");
    set_dec(&a, "000123");
    CHECK_DEC(&a, "123");
    set_dec(&a, "-000");
    CHECK_DEC(&a, "0");
    CHECK_EQ(a.neg, 0);

    const char *bad[] = {"", "-", "+", "12a", " 12", "1 2", "0x10", "--1", "1.5"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK_EQ(bi_from_dec(&a, bad[i]), BI_ERR_PARSE);

    /* Bordas dos pedaços de 9 dígitos. */
    const char *edges[] = {"999999999", "1000000000", "999999999999999999", "1000000000000000000",
                           "4294967295", "4294967296", "18446744073709551615",
                           "18446744073709551616", "-123456789012345678901234567890"};
    for (size_t i = 0; i < sizeof edges / sizeof edges[0]; i++) {
        set_dec(&a, edges[i]);
        CHECK_DEC(&a, edges[i]);
    }

    /* 10^k para k até 120. */
    bigint ten;
    bi_init(&ten);
    bi_set_u64(&ten, 10);
    char expected[130];
    int ok = 1;
    bi_set_u64(&a, 1);
    for (int k = 0; k <= 120; k++) {
        expected[0] = '1';
        memset(expected + 1, '0', (size_t)k);
        expected[k + 1] = '\0';
        char *s = bi_to_dec(&a);
        if (s == NULL || strcmp(s, expected) != 0)
            ok = 0;
        free(s);
        bi_mul(&a, &a, &ten);
    }
    CHECK(ok);

    set_hex(&a, "ff");
    CHECK_DEC(&a, "255");
    set_hex(&a, "-0xFF");
    CHECK_DEC(&a, "-255");
    set_hex(&a, "0X1");
    CHECK_DEC(&a, "1");
    CHECK_EQ(bi_from_hex(&a, "0x"), BI_ERR_PARSE);
    CHECK_EQ(bi_from_hex(&a, "g"), BI_ERR_PARSE);
    CHECK_EQ(bi_from_hex(&a, ""), BI_ERR_PARSE);
    pow2_plus(&a, 64, 0);
    char *h = bi_to_hex(&a);
    CHECK(h != NULL && strcmp(h, "10000000000000000") == 0);
    free(h);
    bi_set_i64(&a, -255);
    h = bi_to_hex(&a);
    CHECK(h != NULL && strcmp(h, "-ff") == 0);
    free(h);
    set_hex(&a, "123456789abcdef0123456789abcdef");
    h = bi_to_hex(&a);
    CHECK(h != NULL && strcmp(h, "123456789abcdef0123456789abcdef") == 0);
    free(h);

    /* Ida e volta aleatória. */
    ok = 1;
    for (int round = 0; round < 200; round++) {
        bigint b;
        bi_init(&b);
        random_limbs(&a, 1 + rng_next() % 40, 0);
        if (rng_next() & 1)
            bi_neg(&a, &a);
        char *d = bi_to_dec(&a), *x = bi_to_hex(&a);
        if (bi_from_dec(&b, d) != BI_OK || bi_cmp(&a, &b) != 0)
            ok = 0;
        if (bi_from_hex(&b, x) != BI_OK || bi_cmp(&a, &b) != 0)
            ok = 0;
        free(d);
        free(x);
        bi_free(&b);
    }
    CHECK(ok);
    bi_free(&a);
    bi_free(&ten);
}

static void test_small_values(void)
{
    SECTION("conversão de e para inteiros de 64 bits");
    bigint a;
    int64_t i;
    uint64_t u;
    bi_init(&a);

    bi_set_i64(&a, INT64_MIN);
    CHECK_DEC(&a, "-9223372036854775808");
    CHECK(bi_get_i64(&a, &i) && i == INT64_MIN);
    CHECK(!bi_get_u64(&a, &u));
    bi_set_i64(&a, INT64_MAX);
    CHECK(bi_get_i64(&a, &i) && i == INT64_MAX);
    bi_set_u64(&a, UINT64_MAX);
    CHECK_DEC(&a, "18446744073709551615");
    CHECK(bi_get_u64(&a, &u) && u == UINT64_MAX);
    CHECK(!bi_get_i64(&a, &i));
    set_dec(&a, "-9223372036854775809");
    CHECK(!bi_get_i64(&a, &i));
    set_dec(&a, "18446744073709551616");
    CHECK(!bi_get_u64(&a, &u));
    bi_set_i64(&a, 0);
    CHECK(bi_get_i64(&a, &i) && i == 0);
    CHECK_EQ(a.len, 0);
    bi_free(&a);
}

static void test_compare(void)
{
    SECTION("comparações");
    bigint a, b;
    bi_init(&a);
    bi_init(&b);
    set_dec(&a, "-5");
    set_dec(&b, "3");
    CHECK_EQ(bi_cmp(&a, &b), -1);
    CHECK_EQ(bi_cmp(&b, &a), 1);
    CHECK_EQ(bi_cmp_abs(&a, &b), 1);
    set_dec(&b, "-5");
    CHECK_EQ(bi_cmp(&a, &b), 0);
    set_dec(&b, "-50000000000000000000000");
    CHECK_EQ(bi_cmp(&a, &b), 1);   /* -5 > -5 * 10^22 */
    CHECK_EQ(bi_cmp_abs(&a, &b), -1);
    set_dec(&a, "0");
    CHECK_EQ(bi_cmp(&a, &b), 1);
    CHECK_EQ(bi_sign(&b), -1);
    pow2_plus(&a, 100, 0);
    CHECK_EQ(bi_bit_length(&a), 101);
    CHECK_EQ(bi_test_bit(&a, 100), 1);
    CHECK_EQ(bi_test_bit(&a, 99), 0);
    CHECK_EQ(bi_test_bit(&a, 5000), 0);
    bi_free(&a);
    bi_free(&b);
}

/* ------------------------------------------------- soma e subtração */

static void test_add_sub(void)
{
    SECTION("soma e subtração (vai-um em todos os limbs)");
    bigint a, b, r, e;
    bi_init(&a);
    bi_init(&b);
    bi_init(&r);
    bi_init(&e);

    /* (2^(32k) - 1) + 1 = 2^(32k): o vai-um atravessa os k limbs. */
    int ok = 1;
    for (size_t k = 1; k <= 12; k++) {
        pow2_plus(&a, 32 * k, -1);
        for (size_t i = 0; i < a.len; i++)
            if (a.limb[i] != 0xFFFFFFFFu)
                ok = 0;
        bi_set_u64(&b, 1);
        bi_add(&r, &a, &b);
        pow2_plus(&e, 32 * k, 0);
        if (bi_cmp(&r, &e) != 0 || r.len != k + 1)
            ok = 0;
        /* E o caminho de volta: 2^(32k) - 1 empresta de todos os limbs. */
        bi_sub(&r, &e, &b);
        if (bi_cmp(&r, &a) != 0 || r.len != k)
            ok = 0;
    }
    CHECK(ok);

    set_dec(&a, "5");
    set_dec(&b, "-7");
    bi_add(&r, &a, &b);
    CHECK_DEC(&r, "-2");
    set_dec(&a, "-5");
    bi_sub(&r, &a, &b);
    CHECK_DEC(&r, "2");
    bi_sub(&r, &b, &b);   /* resultado sobre o próprio operando */
    CHECK_DEC(&r, "0");
    CHECK_EQ(r.neg, 0);
    set_dec(&a, "-123456789012345678901234567890");
    bi_neg(&b, &a);
    bi_add(&r, &a, &b);
    CHECK_EQ(r.len, 0);
    CHECK_EQ(r.neg, 0);
    bi_neg(&r, &r);
    CHECK_EQ(r.neg, 0);   /* -0 continua sendo 0 */
    set_dec(&a, "99999999999999999999");
    bi_add(&a, &a, &a);
    CHECK_DEC(&a, "199999999999999999998");
    bi_add_i64(&a, &a, INT64_MIN);
    CHECK_DEC(&a, "190776627963145224190");

    bi_free(&a);
    bi_free(&b);
    bi_free(&r);
    bi_free(&e);
}

/* ------------------------------------------------------ multiplicação */

static void test_mul(void)
{
    SECTION("multiplicação escolar e de Karatsuba");
    bigint a, b, r, s;
    bi_init(&a);
    bi_init(&b);
    bi_init(&r);
    bi_init(&s);

    set_dec(&a, "12345678901234567890");
    set_dec(&b, "98765432109876543210");
    bi_mul(&r, &a, &b);
    CHECK_DEC(&r, "1219326311370217952237463801111263526900");
    bi_set_u64(&a, UINT64_MAX);
    bi_mul(&r, &a, &a);
    CHECK_DEC(&r, "340282366920938463426481119284349108225");
    bi_set_i64(&b, -3);
    bi_mul(&r, &a, &b);
    CHECK_DEC(&r, "-55340232221128654845");
    bi_set_u64(&b, 0);
    bi_neg(&a, &a);
    bi_mul(&r, &a, &b);
    CHECK_EQ(r.len, 0);
    CHECK_EQ(r.neg, 0);   /* (-x) * 0 = 0, sem sinal */
    bi_mul_i64(&r, &a, -2);
    CHECK_DEC(&r, "36893488147419103230");

    /* Karatsuba == escolar, perto do limiar, desequilibrado e com limbs
       cheios (todos os bits 1, o pior caso para o vai-um). */
    int ok = 1, runs = 0;
    const size_t thresholds[] = {2, 3, 4, 8, BI_KARATSUBA_THRESHOLD};
    for (size_t ti = 0; ti < sizeof thresholds / sizeof thresholds[0]; ti++) {
        size_t t = thresholds[ti];
        for (size_t n = 1; n <= 140; n += (n < 60 ? 1 : 7)) {
            for (int kind = 0; kind < 3; kind++) {
                size_t m = kind == 2 ? n * 3 + 1 : n + rng_next() % 3;
                random_limbs(&a, n, kind == 1);
                random_limbs(&b, m, kind == 1);
                if (rng_next() & 1)
                    bi_neg(&b, &b);
                bi_mul_schoolbook(&r, &a, &b);
                bi_mul_karatsuba(&s, &a, &b, t);
                runs++;
                if (bi_cmp(&r, &s) != 0)
                    ok = 0;
            }
        }
    }
    CHECK(ok);
    CHECK(runs > 1000);

    /* Exatamente em volta do limiar padrão. */
    ok = 1;
    for (size_t n = BI_KARATSUBA_THRESHOLD - 2; n <= BI_KARATSUBA_THRESHOLD + 2; n++) {
        random_limbs(&a, n, 0);
        random_limbs(&b, n, 0);
        bi_mul_schoolbook(&r, &a, &b);
        bi_mul(&s, &a, &b);
        if (bi_cmp(&r, &s) != 0)
            ok = 0;
    }
    CHECK(ok);

    /* Resultado sobre os operandos: a = a * a. */
    set_dec(&a, "-111111111111111111111");
    bi_mul(&a, &a, &a);
    CHECK_DEC(&a, "12345679012345679012320987654320987654321");
    bi_free(&a);
    bi_free(&b);
    bi_free(&r);
    bi_free(&s);
}

/* ------------------------------------------------------------ divisão */

static void check_divmod(const char *a, const char *b, const char *tq, const char *tr,
                         const char *fq, const char *fr)
{
    bigint x, y, q, r;
    bi_init(&x);
    bi_init(&y);
    bi_init(&q);
    bi_init(&r);
    set_dec(&x, a);
    set_dec(&y, b);
    CHECK_EQ(bi_divmod(&q, &r, &x, &y), BI_OK);
    CHECK_DEC(&q, tq);
    CHECK_DEC(&r, tr);
    CHECK_EQ(bi_fdivmod(&q, &r, &x, &y), BI_OK);
    CHECK_DEC(&q, fq);
    CHECK_DEC(&r, fr);
    bi_free(&x);
    bi_free(&y);
    bi_free(&q);
    bi_free(&r);
}

static void test_divmod(void)
{
    SECTION("divisão (Algoritmo D de Knuth, truncada e com piso)");
    /*            a       b      trunc q  r     piso q  r */
    check_divmod("7", "2", "3", "1", "3", "1");
    check_divmod("-7", "2", "-3", "-1", "-4", "1");
    check_divmod("7", "-2", "-3", "1", "-4", "-1");
    check_divmod("-7", "-2", "3", "-1", "3", "-1");
    check_divmod("-6", "3", "-2", "0", "-2", "0");
    check_divmod("5", "123456789012345678901234567890", "0", "5", "0", "5");
    check_divmod("-5", "123456789012345678901234567890", "0", "-5", "-1",
                 "123456789012345678901234567885");
    check_divmod("123456789012345678901234567890", "123456789012345678901234567890", "1", "0",
                 "1", "0");
    check_divmod("1219326311370217952237463801111263526900", "98765432109876543210",
                 "12345678901234567890", "0", "12345678901234567890", "0");

    bigint a, b, q, r, back;
    bi_init(&a);
    bi_init(&b);
    bi_init(&q);
    bi_init(&r);
    bi_init(&back);

    /* Divisão por zero. */
    set_dec(&a, "10");
    CHECK_EQ(bi_divmod(&q, &r, &a, &b), BI_ERR_DIV_ZERO);
    CHECK_EQ(bi_fdivmod(&q, NULL, &a, &b), BI_ERR_DIV_ZERO);
    CHECK_EQ(bi_divmod(&q, &q, &a, &a), BI_ERR_DOMAIN);   /* q e r iguais */

    /*
     * Casos da literatura que exercitam a correção de qhat e o passo
     * "soma de volta" (D6) do algoritmo, que acontece raramente.
     */
    static const char *knuth[][4] = {
        {"800000000000000000000003", "200000000000000000000001", "3", "200000000000000000000000"},
        {"80000000000000000003", "20000000000000000001", "3", "20000000000000000000"},
        {"7fff000080000000000000000000", "80000000000000000001", "fffe0000", "7fffffffffff00020000"},
        {"8000000000000000fffe00000000", "8000000000000000ffff", "ffffffff", "7fffffffffff0000ffff"},
        {"8000000000000000fffffffe00000000", "80000000000000000000ffff", "100000000",
         "fffeffff00000000"},
        {"8000000000000000fffffffe00000000", "8000000000000000ffffffff", "ffffffff",
         "7fffffffffffffffffffffff"},
    };
    int ok = 1;
    for (size_t i = 0; i < sizeof knuth / sizeof knuth[0]; i++) {
        bigint eq, er;
        bi_init(&eq);
        bi_init(&er);
        set_hex(&a, knuth[i][0]);
        set_hex(&b, knuth[i][1]);
        set_hex(&eq, knuth[i][2]);
        set_hex(&er, knuth[i][3]);
        if (bi_divmod(&q, &r, &a, &b) != BI_OK || bi_cmp(&q, &eq) != 0 || bi_cmp(&r, &er) != 0)
            ok = 0;
        bi_free(&eq);
        bi_free(&er);
    }
    CHECK(ok);

    /* Invariante a = q * b + r com 0 <= |r| < |b|, em vários tamanhos. */
    ok = 1;
    for (int round = 0; round < 400; round++) {
        size_t an = 1 + rng_next() % 60, bn = 1 + rng_next() % 40;
        random_limbs(&a, an, round % 7 == 0);
        random_limbs(&b, bn, round % 5 == 0);
        if (rng_next() & 1)
            bi_neg(&a, &a);
        if (rng_next() & 1)
            bi_neg(&b, &b);
        for (int floor_mode = 0; floor_mode < 2; floor_mode++) {
            bi_status st = floor_mode ? bi_fdivmod(&q, &r, &a, &b) : bi_divmod(&q, &r, &a, &b);
            bi_mul(&back, &q, &b);
            bi_add(&back, &back, &r);
            if (st != BI_OK || bi_cmp(&back, &a) != 0 || bi_cmp_abs(&r, &b) >= 0)
                ok = 0;
            if (r.len > 0 && floor_mode && r.neg != b.neg)
                ok = 0;
            if (r.len > 0 && !floor_mode && r.neg != a.neg)
                ok = 0;
        }
    }
    CHECK(ok);

    /* Resultado sobre o operando: a = a / b. */
    set_dec(&a, "1000000000000000000000");
    set_dec(&b, "7");
    bi_divmod(&a, NULL, &a, &b);
    CHECK_DEC(&a, "142857142857142857142");

    bi_free(&a);
    bi_free(&b);
    bi_free(&q);
    bi_free(&r);
    bi_free(&back);
}

/* ------------------------------------------------- potências e afins */

static void test_pow(void)
{
    SECTION("potência, potência modular, mdc, raiz e fatorial");
    bigint a, b, m, r;
    bi_init(&a);
    bi_init(&b);
    bi_init(&m);
    bi_init(&r);

    bi_set_u64(&a, 3);
    bi_pow(&r, &a, 100);
    CHECK_DEC(&r, "515377520732011331036461129765621272702107522001");
    bi_set_u64(&a, 0);
    bi_pow(&r, &a, 0);
    CHECK_DEC(&r, "1");
    bi_pow(&r, &a, 5);
    CHECK_DEC(&r, "0");
    bi_set_i64(&a, -2);
    bi_pow(&r, &a, 3);
    CHECK_DEC(&r, "-8");
    bi_pow(&r, &a, 4);
    CHECK_DEC(&r, "16");
    bi_set_i64(&a, -1);
    bi_pow(&r, &a, 12345);
    CHECK_DEC(&r, "-1");
    bi_set_u64(&a, 2);
    bi_pow(&r, &a, 1000);
    char *s = bi_to_dec(&r);
    CHECK(s != NULL && strlen(s) == 302);
    free(s);
    CHECK_EQ(bi_pow(&r, &a, UINT64_C(1) << 40), BI_ERR_RANGE);

    /* Potência modular. */
    bi_set_u64(&a, 2);
    set_dec(&b, "1000000000000000000");
    set_dec(&m, "1000000007");
    bi_powmod(&r, &a, &b, &m);
    CHECK_DEC(&r, "719476260");
    bi_set_i64(&a, -2);
    bi_set_u64(&b, 3);
    bi_set_u64(&m, 5);
    bi_powmod(&r, &a, &b, &m);
    CHECK_DEC(&r, "2");
    bi_set_u64(&m, 1);
    bi_powmod(&r, &a, &b, &m);
    CHECK_DEC(&r, "0");
    bi_set_u64(&b, 0);
    bi_set_u64(&m, 7);
    bi_powmod(&r, &a, &b, &m);
    CHECK_DEC(&r, "1");
    bi_set_u64(&m, 0);
    CHECK_EQ(bi_powmod(&r, &a, &b, &m), BI_ERR_DOMAIN);
    bi_set_i64(&m, -7);
    CHECK_EQ(bi_powmod(&r, &a, &b, &m), BI_ERR_DOMAIN);
    bi_set_u64(&m, 7);
    bi_set_i64(&b, -1);
    CHECK_EQ(bi_powmod(&r, &a, &b, &m), BI_ERR_DOMAIN);
    /* Pequeno teorema de Fermat: a^(p-1) = 1 (mod p). */
    int ok = 1;
    pow2_plus(&m, 127, -1);   /* primo de Mersenne */
    bi_add_i64(&b, &m, -1);
    for (int round = 0; round < 20; round++) {
        random_limbs(&a, 1 + rng_next() % 4, 0);
        bi_powmod(&r, &a, &b, &m);
        if (!(r.len == 1 && r.limb[0] == 1))
            ok = 0;
    }
    CHECK(ok);

    /* mdc. */
    bi_set_u64(&a, 0);
    bi_set_u64(&b, 0);
    bi_gcd(&r, &a, &b);
    CHECK_DEC(&r, "0");
    bi_set_i64(&b, -5);
    bi_gcd(&r, &a, &b);
    CHECK_DEC(&r, "5");
    bi_set_i64(&a, -12);
    bi_set_u64(&b, 18);
    bi_gcd(&r, &a, &b);
    CHECK_DEC(&r, "6");
    bi_set_u64(&a, 2);
    bi_pow(&a, &a, 100);
    bi_set_u64(&b, 6);
    bi_pow(&b, &b, 50);
    bi_gcd(&r, &a, &b);
    bi_set_u64(&m, 2);
    bi_pow(&m, &m, 50);
    CHECK_EQ(bi_cmp(&r, &m), 0);
    /* Fibonacci consecutivos são primos entre si (o pior caso de Euclides). */
    bi_set_u64(&a, 1);
    bi_set_u64(&b, 1);
    for (int i = 0; i < 500; i++) {
        bi_add(&r, &a, &b);
        bi_swap(&a, &b);
        bi_swap(&b, &r);
    }
    bi_gcd(&r, &a, &b);
    CHECK_DEC(&r, "1");

    /* Raiz quadrada inteira. */
    const char *roots[][2] = {{"0", "0"}, {"1", "1"}, {"2", "1"}, {"3", "1"}, {"4", "2"},
                              {"15", "3"}, {"16", "4"}, {"17", "4"},
                              {"99999999999999999999999999999999", "9999999999999999"},
                              {"100000000000000000000000000000000", "10000000000000000"}};
    for (size_t i = 0; i < sizeof roots / sizeof roots[0]; i++) {
        set_dec(&a, roots[i][0]);
        bi_isqrt(&r, &a);
        CHECK_DEC(&r, roots[i][1]);
    }
    bi_set_i64(&a, -4);
    CHECK_EQ(bi_isqrt(&r, &a), BI_ERR_DOMAIN);
    ok = 1;
    for (int round = 0; round < 100; round++) {
        random_limbs(&a, 1 + rng_next() % 30, round % 9 == 0);
        bi_isqrt(&r, &a);
        bi_mul(&b, &r, &r);              /* r^2 <= a */
        bi_add_i64(&m, &r, 1);
        bi_mul(&m, &m, &m);              /* (r+1)^2 > a */
        if (bi_cmp(&b, &a) > 0 || bi_cmp(&m, &a) <= 0)
            ok = 0;
    }
    CHECK(ok);

    /* Fatorial. */
    bi_factorial(&r, 0);
    CHECK_DEC(&r, "1");
    bi_factorial(&r, 1);
    CHECK_DEC(&r, "1");
    bi_factorial(&r, 20);
    CHECK_DEC(&r, "2432902008176640000");
    bi_factorial(&r, 25);
    CHECK_DEC(&r, "15511210043330985984000000");
    bi_factorial(&r, 100);
    CHECK_DEC(&r, "933262154439441526816992388562667004907159682643816214685929638952175999932299156"
                  "08941463976156518286253697920827223758251185210916864000000000000000000000000");
    bi_factorial(&r, 1000);
    s = bi_to_dec(&r);
    size_t zeros = 0;
    for (size_t i = s ? strlen(s) : 0; i > 0 && s[i - 1] == '0'; i--)
        zeros++;
    CHECK(s != NULL && strlen(s) == 2568);
    CHECK_EQ(zeros, 249);   /* 1000/5 + 1000/25 + 1000/125 + 1000/625 */
    free(s);
    CHECK_EQ(bi_factorial(&r, UINT64_C(1) << 33), BI_ERR_RANGE);

    bi_free(&a);
    bi_free(&b);
    bi_free(&m);
    bi_free(&r);
}

/* ---------------------------------------------------------------- bits */

static void test_bits(void)
{
    SECTION("deslocamentos e operações bit a bit");
    bigint a, b, r;
    bi_init(&a);
    bi_init(&b);
    bi_init(&r);

    bi_set_u64(&a, 1);
    bi_shl(&r, &a, 100);
    CHECK_DEC(&r, "1267650600228229401496703205376");
    bi_shr(&r, &r, 100);
    CHECK_DEC(&r, "1");
    const size_t shifts[] = {0, 1, 31, 32, 33, 63, 64, 65, 100};
    int ok = 1;
    for (size_t i = 0; i < sizeof shifts / sizeof shifts[0]; i++) {
        random_limbs(&a, 3, 0);
        bi_shl(&r, &a, shifts[i]);
        bi_shr(&r, &r, shifts[i]);
        if (bi_cmp(&r, &a) != 0)
            ok = 0;
    }
    CHECK(ok);
    bi_set_i64(&a, -5);
    bi_shr(&r, &a, 1);
    CHECK_DEC(&r, "-3");   /* piso, como no Python */
    bi_set_i64(&a, -4);
    bi_shr(&r, &a, 1);
    CHECK_DEC(&r, "-2");
    bi_set_i64(&a, -1);
    bi_shr(&r, &a, 1000);
    CHECK_DEC(&r, "-1");
    bi_set_u64(&a, 5);
    bi_shr(&r, &a, 1000);
    CHECK_DEC(&r, "0");
    bi_set_i64(&a, -3);
    bi_shl(&r, &a, 33);
    CHECK_DEC(&r, "-25769803776");
    bi_set_u64(&a, 0);
    bi_shl(&r, &a, 77);
    CHECK_DEC(&r, "0");

    set_dec(&a, "81985529216486895");     /* 0x123456789abcdef */
    set_dec(&b, "18446744069414584320");  /* 0xffffffff00000000 */
    bi_and(&r, &a, &b);
    CHECK_DEC(&r, "81985526906748928");
    bi_or(&r, &a, &b);
    CHECK_DEC(&r, "18446744071724322287");
    bi_xor(&r, &a, &b);
    CHECK_DEC(&r, "18364758544817573359");
    bi_xor(&r, &a, &a);
    CHECK_DEC(&r, "0");
    bi_set_i64(&b, -1);
    CHECK_EQ(bi_and(&r, &a, &b), BI_ERR_DOMAIN);
    CHECK_EQ(bi_or(&r, &b, &a), BI_ERR_DOMAIN);
    CHECK_EQ(bi_xor(&r, &b, &b), BI_ERR_DOMAIN);
    bi_free(&a);
    bi_free(&b);
    bi_free(&r);
}

/* -------------------------------------------------------- primalidade */

static int is_prime_dec(const char *s)
{
    bigint n;
    int p = -1;
    bi_init(&n);
    set_dec(&n, s);
    if (bi_is_prime(&n, &p) != BI_OK)
        p = -1;
    bi_free(&n);
    return p;
}

/* Uma rodada de Miller-Rabin feita só com a API pública. */
static int strong_probable_prime(const bigint *n, uint64_t base)
{
    bigint n1, d, a, x, two;
    bi_init(&n1);
    bi_init(&d);
    bi_init(&a);
    bi_init(&x);
    bi_init(&two);
    bi_add_i64(&n1, n, -1);
    size_t s = 0;
    while (!bi_test_bit(&n1, s))
        s++;
    bi_shr(&d, &n1, s);
    bi_set_u64(&a, base);
    bi_set_u64(&two, 2);
    bi_powmod(&x, &a, &d, n);
    int ok = (x.len == 1 && x.limb[0] == 1) || bi_cmp(&x, &n1) == 0;
    for (size_t k = 1; k < s && !ok; k++) {
        bi_powmod(&x, &x, &two, n);
        ok = bi_cmp(&x, &n1) == 0;
    }
    bi_free(&n1);
    bi_free(&d);
    bi_free(&a);
    bi_free(&x);
    bi_free(&two);
    return ok;
}

static void test_primes(void)
{
    SECTION("Miller-Rabin");
    const char *primes[] = {"2", "3", "5", "97", "997", "1009", "1000003", "2147483647",
                            "2305843009213693951",                        /* 2^61 - 1 */
                            "618970019642690137449562111",                /* 2^89 - 1 */
                            "162259276829213363391578010288127",          /* 2^107 - 1 */
                            "170141183460469231731687303715884105727"};  /* 2^127 - 1 */
    const char *composites[] = {"0", "1", "4", "9", "1000001", "561", "1105", "1729", "2465",
                                "2821", "6601", "8911", "41041", "825265",   /* Carmichael */
                                "2047", "3277", "4033", "4681", "8321",      /* pseudoprimos fortes base 2 */
                                "3215031751",                  /* forte para as bases 2, 3, 5 e 7 */
                                "3825123056546413051",         /* forte para as bases 2 a 31 */
                                "318665857834031151167461",    /* forte para as bases 2 a 37 */
                                "147573952589676412927",       /* 2^67 - 1 = 193707721 x 761838257287 */
                                "-7"};
    int ok = 1;
    for (size_t i = 0; i < sizeof primes / sizeof primes[0]; i++)
        if (is_prime_dec(primes[i]) != 1) {
            ok = 0;
            fprintf(stderr, "  devia ser primo: %s\n", primes[i]);
        }
    for (size_t i = 0; i < sizeof composites / sizeof composites[0]; i++)
        if (is_prime_dec(composites[i]) != 0) {
            ok = 0;
            fprintf(stderr, "  devia ser composto: %s\n", composites[i]);
        }
    CHECK(ok);

    /*
     * 3317044064679887385961981 engana as 13 bases fixas (2 a 41): é o menor
     * número com essa propriedade. Só as bases extras pegam.
     */
    bigint n, p, q;
    bi_init(&n);
    bi_init(&p);
    bi_init(&q);
    set_dec(&n, "3317044064679887385961981");
    static const uint64_t bases[13] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41};
    int fools_all = 1;
    for (int i = 0; i < 13; i++)
        fools_all &= strong_probable_prime(&n, bases[i]);
    CHECK(fools_all);
    CHECK_EQ(is_prime_dec("3317044064679887385961981"), 0);

    /* Primos de Mersenne maiores e produtos de dois primos grandes. */
    pow2_plus(&n, 521, -1);
    int r = -1;
    CHECK_EQ(bi_is_prime(&n, &r), BI_OK);
    CHECK_EQ(r, 1);
    pow2_plus(&n, 607, -1);
    bi_is_prime(&n, &r);
    CHECK_EQ(r, 1);
    pow2_plus(&n, 523, -1);   /* 523 é primo, mas 2^523 - 1 não */
    bi_is_prime(&n, &r);
    CHECK_EQ(r, 0);
    pow2_plus(&p, 89, -1);
    pow2_plus(&q, 107, -1);
    bi_mul(&n, &p, &q);
    bi_is_prime(&n, &r);
    CHECK_EQ(r, 0);
    bi_mul(&n, &p, &p);       /* quadrado de primo */
    bi_is_prime(&n, &r);
    CHECK_EQ(r, 0);
    bi_free(&n);
    bi_free(&p);
    bi_free(&q);
}

/* -------------------------------------------------------- calculadora */

static void check_calc(const char *expr, const char *expected)
{
    bigint v;
    calc_error err;
    bi_init(&v);
    int rc = calc_eval(expr, &v, 0, &err);
    tests_run++;
    char *s = rc == 0 ? bi_to_dec(&v) : NULL;
    if (rc != 0 || s == NULL || strcmp(s, expected) != 0) {
        tests_failed++;
        fprintf(stderr, "  FALHOU: \"%s\" deu %s, esperado %s\n", expr,
                rc ? err.message : (s ? s : "(nulo)"), expected);
    }
    free(s);
    bi_free(&v);
}

static void check_calc_error(const char *expr, size_t pos, const char *fragment)
{
    bigint v;
    calc_error err;
    bi_init(&v);
    int rc = calc_eval(expr, &v, 0, &err);
    tests_run++;
    if (rc == 0 || err.pos != pos || strstr(err.message, fragment) == NULL) {
        tests_failed++;
        fprintf(stderr, "  FALHOU: \"%s\": rc %d, posição %zu (esperada %zu), mensagem \"%s\"\n",
                expr, rc, err.pos, pos, rc ? err.message : "");
    }
    bi_free(&v);
}

static void test_calc(void)
{
    SECTION("avaliador de expressões");
    check_calc("1 + 2 * 3", "7");
    check_calc("(1 + 2) * 3", "9");
    check_calc("2^3^2", "512");
    check_calc("-2^2", "-4");
    check_calc("(-2)^2", "4");
    check_calc("(-2)^3", "-8");
    check_calc("--5", "5");
    check_calc("5--3", "8");
    check_calc("-7 / 2", "-4");
    check_calc("-7 % 2", "1");
    check_calc("7 % -2", "-1");
    check_calc("7 / -2", "-4");
    check_calc("5!", "120");
    check_calc("3!!", "720");
    check_calc("-3!", "-6");
    check_calc("0!", "1");
    check_calc("mdc(12; 18)", "6");
    check_calc("MDC(-12; 18)", "6");
    check_calc("raiz(17)", "4");
    check_calc("raiz(10^100)", "100000000000000000000000000000000000000000000000000");
    check_calc("primo(97)", "1");
    check_calc("primo(2^127 - 1)", "1");
    check_calc("primo(91)", "0");
    check_calc("potmod(2; 10; 1000)", "24");
    check_calc("potmod(3; 10^100; 1000000007)", "9102203");
    check_calc("0x10 + 0XfF", "271");
    check_calc("  42  ", "42");
    check_calc("2^521 - 1",
               "68647976601306097149819007990813932172694353001433054093944634591855431833976560521"
               "22559640661454554977296311391480858037121987999716643812574028291115057151");
    check_calc("(2^64 - 1) * (2^64 + 1) - 2^128", "-1");
    check_calc("100! / 98!", "9900");
    check_calc("0^0 + 1^99999999999999 + (-1)^99999999999999", "1");
    check_calc("(((((((((((7)))))))))))", "7");

    check_calc_error("", 0, "vazia");
    check_calc_error("1 +", 3, "terminou");
    check_calc_error("2 * (3 + 4", 10, "faltou fechar");
    check_calc_error("1)", 1, "sem o '('");
    check_calc_error("10 / 0", 3, "divisão por zero");
    check_calc_error("10 % (5 - 5)", 3, "divisão por zero");
    check_calc_error("2^-1", 1, "expoente negativo");
    check_calc_error("(-3)!", 4, "negativo");
    check_calc_error("raiz(-4)", 0, "negativo");
    check_calc_error("mdc(1, 2)", 5, "';'");
    check_calc_error("mdc(1)", 0, "recebe 2 argumentos");
    check_calc_error("potmod(1; 2; 3; 4)", 0, "recebe 3 argumentos");
    check_calc_error("potmod(2; 3; 0)", 0, "módulo");
    check_calc_error("foo(3)", 0, "desconhecida");
    check_calc_error("2(3)", 1, "operador");
    check_calc_error("2 $ 3", 2, "caractere inesperado '$'");
    check_calc_error("0x", 0, "hexadecimal");
    check_calc_error("10^10^10", 2, "grande demais");
    check_calc_error("99999999!", 8, "grande demais");
    check_calc_error("(2^1048575) * 2", 12, "grande demais");

    /* Aninhamento profundo vira erro, não estouro de pilha. */
    char deep[2100];
    memset(deep, '(', 1000);
    deep[1000] = '1';
    memset(deep + 1001, ')', 1000);
    deep[2001] = '\0';
    check_calc_error(deep, 400, "aninhada");
    memset(deep, '-', 2000);
    deep[2000] = '1';
    deep[2001] = '\0';
    check_calc_error(deep, 400, "aninhada");
}

static void test_strerror(void)
{
    SECTION("mensagens de erro");
    int ok = 1;
    for (int i = BI_OK; i <= BI_ERR_RANGE; i++) {
        const char *m = bi_strerror((bi_status)i);
        if (m == NULL || m[0] == '\0')
            ok = 0;
        for (int j = BI_OK; j < i; j++)
            if (strcmp(m, bi_strerror((bi_status)j)) == 0)
                ok = 0;
    }
    CHECK(ok);
}

int main(void)
{
    test_text();
    test_small_values();
    test_compare();
    test_add_sub();
    test_mul();
    test_divmod();
    test_pow();
    test_bits();
    test_primes();
    test_calc();
    test_strerror();
    return test_summary();
}
