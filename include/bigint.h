/*
 * bigint.h: inteiros de precisão arbitrária em C11.
 *
 * Representação em sinal e magnitude: a magnitude fica num vetor de
 * "limbs" de 32 bits (dígitos na base 2^32), do menos para o mais
 * significativo, e as contas intermediárias usam 64 bits. O zero tem
 * len == 0 e nunca é negativo.
 *
 * Todas as funções aceitam que o resultado seja um dos operandos
 * (bi_add(&a, &a, &b) funciona). Exceção: em bi_divmod e bi_fdivmod,
 * q e r precisam ser variáveis diferentes.
 */
#ifndef BIGINT_H
#define BIGINT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BIGINT_VERSION "1.0.0"

/*
 * Abaixo deste número de limbs (no fator menor), a multiplicação escolar
 * é mais rápida que a de Karatsuba. Valor medido com `make bench`.
 */
#define BI_KARATSUBA_THRESHOLD 48

typedef struct {
    uint32_t *limb;   /* magnitude, limb[0] é o menos significativo */
    size_t len;       /* limbs em uso, sem zeros à esquerda (0 = zero) */
    size_t cap;       /* limbs alocados */
    int neg;          /* 1 se negativo */
} bigint;

typedef enum {
    BI_OK = 0,
    BI_ERR_MEMORY,    /* falta de memória */
    BI_ERR_DIV_ZERO,  /* divisão por zero */
    BI_ERR_PARSE,     /* texto não é um número válido */
    BI_ERR_DOMAIN,    /* operação não definida para esses valores */
    BI_ERR_RANGE      /* resultado grande demais */
} bi_status;

const char *bi_strerror(bi_status st);

/* ------------------------------------------------- criação e conversão */

void bi_init(bigint *a);                 /* a = 0, sem alocar */
void bi_free(bigint *a);                 /* libera e volta a ser 0 */
void bi_swap(bigint *a, bigint *b);
bi_status bi_copy(bigint *dst, const bigint *src);
bi_status bi_set_i64(bigint *a, int64_t v);
bi_status bi_set_u64(bigint *a, uint64_t v);
int bi_get_i64(const bigint *a, int64_t *out);   /* 1 se coube, 0 se não */
int bi_get_u64(const bigint *a, uint64_t *out);  /* 1 se coube (a >= 0), 0 se não */

/* Decimal com sinal opcional ("-123", "+7"). */
bi_status bi_from_dec(bigint *a, const char *s);
/* Hexadecimal com sinal e prefixo 0x opcionais ("-0xff", "FF"). */
bi_status bi_from_hex(bigint *a, const char *s);
/* Texto alocado com malloc (libere com free); NULL se faltar memória. */
char *bi_to_dec(const bigint *a);
char *bi_to_hex(const bigint *a);        /* minúsculas, sem prefixo: "-ff" */

/* ------------------------------------------------------- comparações */

int bi_cmp(const bigint *a, const bigint *b);      /* -1, 0 ou 1 */
int bi_cmp_abs(const bigint *a, const bigint *b);  /* compara |a| e |b| */
int bi_sign(const bigint *a);                      /* -1, 0 ou 1 */
int bi_is_zero(const bigint *a);
int bi_is_odd(const bigint *a);
size_t bi_bit_length(const bigint *a);             /* bits de |a| (0 para zero) */
int bi_test_bit(const bigint *a, size_t bit);      /* bit de |a| */

/* ---------------------------------------------------------- aritmética */

bi_status bi_add(bigint *r, const bigint *a, const bigint *b);
bi_status bi_sub(bigint *r, const bigint *a, const bigint *b);
bi_status bi_neg(bigint *r, const bigint *a);
bi_status bi_abs(bigint *r, const bigint *a);
bi_status bi_add_i64(bigint *r, const bigint *a, int64_t b);
bi_status bi_mul_i64(bigint *r, const bigint *a, int64_t b);

/* Multiplicação: escolar abaixo do limiar, Karatsuba acima. */
bi_status bi_mul(bigint *r, const bigint *a, const bigint *b);
/* Versões explícitas, para testes e medições. */
bi_status bi_mul_schoolbook(bigint *r, const bigint *a, const bigint *b);
bi_status bi_mul_karatsuba(bigint *r, const bigint *a, const bigint *b, size_t threshold);

/*
 * Divisão truncada (como em C): o quociente arredonda em direção a zero
 * e o resto tem o sinal do dividendo. -7 / 2 = -3, resto -1.
 * q ou r podem ser NULL.
 */
bi_status bi_divmod(bigint *q, bigint *r, const bigint *a, const bigint *b);
/*
 * Divisão com piso (como no Python): o quociente arredonda para baixo e
 * o resto tem o sinal do divisor. -7 / 2 = -4, resto 1.
 */
bi_status bi_fdivmod(bigint *q, bigint *r, const bigint *a, const bigint *b);

bi_status bi_pow(bigint *r, const bigint *a, uint64_t e);
/* a^e mod m, com e >= 0 e m > 0; o resultado fica em [0, m). */
bi_status bi_powmod(bigint *r, const bigint *a, const bigint *e, const bigint *m);
/* Máximo divisor comum (sempre >= 0). */
bi_status bi_gcd(bigint *r, const bigint *a, const bigint *b);
/* Raiz quadrada inteira: o maior x com x * x <= a (a >= 0). */
bi_status bi_isqrt(bigint *r, const bigint *a);
/* n! para n até 2^32 - 1. */
bi_status bi_factorial(bigint *r, uint64_t n);

/* ---------------------------------------------------------------- bits */

bi_status bi_shl(bigint *r, const bigint *a, size_t bits);   /* a * 2^bits */
bi_status bi_shr(bigint *r, const bigint *a, size_t bits);   /* piso de a / 2^bits */
/* Só para valores não negativos (BI_ERR_DOMAIN caso contrário). */
bi_status bi_and(bigint *r, const bigint *a, const bigint *b);
bi_status bi_or(bigint *r, const bigint *a, const bigint *b);
bi_status bi_xor(bigint *r, const bigint *a, const bigint *b);

/* -------------------------------------------------------- primalidade */

/*
 * Miller-Rabin. Para n < 3.317.044.064.679.887.385.961.981 o teste com as
 * bases 2, 3, 5, ..., 41 é determinístico (resposta exata). Acima disso,
 * somam-se 24 bases pseudoaleatórias (derivadas de n, então o resultado
 * é reprodutível) e a chance de um composto passar é menor que 4^-24.
 * *is_prime recebe 1 (primo) ou 0 (composto, ou n < 2).
 */
bi_status bi_is_prime(const bigint *n, int *is_prime);

#ifdef __cplusplus
}
#endif

#endif /* BIGINT_H */
