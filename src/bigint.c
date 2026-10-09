/*
 * bigint.c: aritmética de precisão arbitrária.
 *
 * As funções mag_* trabalham só com magnitudes (vetores de limbs sem
 * sinal); as funções bi_* cuidam do sinal, da memória e dos casos
 * especiais. Toda conta entre limbs de 32 bits usa um intermediário de
 * 64 bits, então o "vai um" nunca se perde.
 */
#include "bigint.h"

#include <stdlib.h>
#include <string.h>

typedef uint32_t limb_t;
typedef uint64_t dlimb_t;

#define LIMB_BITS 32u
#define CHUNK10 1000000000u                     /* 10^9: 9 dígitos decimais por pedaço */
#define MAX_POW_BITS (UINT64_C(1) << 32)        /* proteção contra potências absurdas */

const char *bi_strerror(bi_status st)
{
    switch (st) {
    case BI_OK: return "sucesso";
    case BI_ERR_MEMORY: return "memória insuficiente";
    case BI_ERR_DIV_ZERO: return "divisão por zero";
    case BI_ERR_PARSE: return "número inválido";
    case BI_ERR_DOMAIN: return "operação não definida para esses valores";
    case BI_ERR_RANGE: return "resultado grande demais";
    }
    return "erro desconhecido";
}

/* ------------------------------------------------------------- memória */

void bi_init(bigint *a)
{
    a->limb = NULL;
    a->len = 0;
    a->cap = 0;
    a->neg = 0;
}

void bi_free(bigint *a)
{
    free(a->limb);
    bi_init(a);
}

void bi_swap(bigint *a, bigint *b)
{
    bigint t = *a;
    *a = *b;
    *b = t;
}

static bi_status reserve(bigint *a, size_t n)
{
    if (n <= a->cap)
        return BI_OK;
    if (n > SIZE_MAX / sizeof(limb_t))
        return BI_ERR_MEMORY;
    size_t cap = a->cap ? a->cap : 4;
    while (cap < n)
        cap = cap > SIZE_MAX / 2 ? n : cap * 2;
    if (cap > SIZE_MAX / sizeof(limb_t))
        cap = n;
    limb_t *p = realloc(a->limb, cap * sizeof *p);
    if (p == NULL)
        return BI_ERR_MEMORY;
    a->limb = p;
    a->cap = cap;
    return BI_OK;
}

/* Remove zeros à esquerda e normaliza o sinal do zero (não existe -0). */
static void trim(bigint *a)
{
    while (a->len > 0 && a->limb[a->len - 1] == 0)
        a->len--;
    if (a->len == 0)
        a->neg = 0;
}

/* Coloca o resultado t em r e libera o que r tinha antes. */
static bi_status finish(bigint *r, bigint *t)
{
    trim(t);
    bi_swap(r, t);
    bi_free(t);
    return BI_OK;
}

bi_status bi_copy(bigint *dst, const bigint *src)
{
    if (dst == src)
        return BI_OK;
    bi_status st = reserve(dst, src->len);
    if (st != BI_OK)
        return st;
    if (src->len > 0)
        memcpy(dst->limb, src->limb, src->len * sizeof(limb_t));
    dst->len = src->len;
    dst->neg = src->neg;
    return BI_OK;
}

bi_status bi_set_u64(bigint *a, uint64_t v)
{
    bi_status st = reserve(a, 2);
    if (st != BI_OK)
        return st;
    a->limb[0] = (limb_t)v;
    a->limb[1] = (limb_t)(v >> 32);
    a->len = 2;
    a->neg = 0;
    trim(a);
    return BI_OK;
}

bi_status bi_set_i64(bigint *a, int64_t v)
{
    /* 0 - (uint64_t)v evita o estouro de -INT64_MIN. */
    uint64_t mag = v < 0 ? UINT64_C(0) - (uint64_t)v : (uint64_t)v;
    bi_status st = bi_set_u64(a, mag);
    if (st == BI_OK && v < 0)
        a->neg = 1;
    return st;
}

int bi_get_u64(const bigint *a, uint64_t *out)
{
    if (a->neg || a->len > 2)
        return 0;
    uint64_t v = 0;
    if (a->len > 0)
        v = a->limb[0];
    if (a->len > 1)
        v |= (uint64_t)a->limb[1] << 32;
    *out = v;
    return 1;
}

int bi_get_i64(const bigint *a, int64_t *out)
{
    if (a->len > 2)
        return 0;
    uint64_t mag = 0;
    if (a->len > 0)
        mag = a->limb[0];
    if (a->len > 1)
        mag |= (uint64_t)a->limb[1] << 32;
    if (!a->neg) {
        if (mag > (uint64_t)INT64_MAX)
            return 0;
        *out = (int64_t)mag;
    } else {
        if (mag > (uint64_t)INT64_MAX + 1u)
            return 0;
        *out = mag == (uint64_t)INT64_MAX + 1u ? INT64_MIN : -(int64_t)mag;
    }
    return 1;
}

/* ------------------------------------------------------- magnitudes */

static size_t mag_norm(const limb_t *a, size_t n)
{
    while (n > 0 && a[n - 1] == 0)
        n--;
    return n;
}

static int mag_cmp(const limb_t *a, size_t an, const limb_t *b, size_t bn)
{
    if (an != bn)
        return an < bn ? -1 : 1;
    for (size_t i = an; i-- > 0;)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

/* r = a + b, com an >= bn. r precisa de an + 1 limbs. Devolve o tamanho. */
static size_t mag_add(limb_t *r, const limb_t *a, size_t an, const limb_t *b, size_t bn)
{
    dlimb_t carry = 0;
    size_t i;
    for (i = 0; i < bn; i++) {
        dlimb_t t = (dlimb_t)a[i] + b[i] + carry;
        r[i] = (limb_t)t;
        carry = t >> 32;
    }
    for (; i < an; i++) {
        dlimb_t t = (dlimb_t)a[i] + carry;
        r[i] = (limb_t)t;
        carry = t >> 32;
    }
    r[an] = (limb_t)carry;
    return an + (carry != 0);
}

/*
 * r = a - b, com a >= b e an >= bn. Devolve o tamanho normalizado.
 * O "empresta um" sai do bit 63: se a subtração em 64 bits ficou
 * negativa, ela dá a volta e o bit mais alto liga.
 */
static size_t mag_sub(limb_t *r, const limb_t *a, size_t an, const limb_t *b, size_t bn)
{
    dlimb_t borrow = 0;
    size_t i;
    for (i = 0; i < bn; i++) {
        dlimb_t t = (dlimb_t)a[i] - b[i] - borrow;
        r[i] = (limb_t)t;
        borrow = t >> 63;
    }
    for (; i < an; i++) {
        dlimb_t t = (dlimb_t)a[i] - borrow;
        r[i] = (limb_t)t;
        borrow = t >> 63;
    }
    return mag_norm(r, an);
}

/* x[0..xn) += y[0..yn), com yn <= xn. Devolve o vai-um final. */
static limb_t add_into(limb_t *x, size_t xn, const limb_t *y, size_t yn)
{
    dlimb_t carry = 0;
    size_t i;
    for (i = 0; i < yn; i++) {
        dlimb_t t = (dlimb_t)x[i] + y[i] + carry;
        x[i] = (limb_t)t;
        carry = t >> 32;
    }
    for (; carry != 0 && i < xn; i++) {
        dlimb_t t = (dlimb_t)x[i] + carry;
        x[i] = (limb_t)t;
        carry = t >> 32;
    }
    return (limb_t)carry;
}

/* x[0..xn) -= y[0..yn), com yn <= xn e x >= y. */
static void sub_into(limb_t *x, size_t xn, const limb_t *y, size_t yn)
{
    dlimb_t borrow = 0;
    size_t i;
    for (i = 0; i < yn; i++) {
        dlimb_t t = (dlimb_t)x[i] - y[i] - borrow;
        x[i] = (limb_t)t;
        borrow = t >> 63;
    }
    for (; borrow != 0 && i < xn; i++) {
        dlimb_t t = (dlimb_t)x[i] - borrow;
        x[i] = (limb_t)t;
        borrow = t >> 63;
    }
}

/* r = a * m + add. r precisa de an + 1 limbs (pode ser o próprio a). */
static size_t mag_mul_small(limb_t *r, const limb_t *a, size_t an, limb_t m, limb_t add)
{
    dlimb_t carry = add;
    for (size_t i = 0; i < an; i++) {
        dlimb_t t = (dlimb_t)a[i] * m + carry;
        r[i] = (limb_t)t;
        carry = t >> 32;
    }
    r[an] = (limb_t)carry;
    return mag_norm(r, an + 1);
}

/* a = a / d no próprio vetor (d != 0). Devolve o resto. */
static limb_t mag_div_small(limb_t *a, size_t an, limb_t d)
{
    dlimb_t rem = 0;
    for (size_t i = an; i-- > 0;) {
        dlimb_t cur = (rem << 32) | a[i];
        a[i] = (limb_t)(cur / d);
        rem = cur % d;
    }
    return (limb_t)rem;
}

static limb_t mag_mod_small(const limb_t *a, size_t an, limb_t d)
{
    dlimb_t rem = 0;
    for (size_t i = an; i-- > 0;)
        rem = ((rem << 32) | a[i]) % d;
    return (limb_t)rem;
}

/* ------------------------------------------------------- comparações */

int bi_cmp_abs(const bigint *a, const bigint *b)
{
    return mag_cmp(a->limb, a->len, b->limb, b->len);
}

int bi_cmp(const bigint *a, const bigint *b)
{
    if (a->neg != b->neg)
        return a->neg ? -1 : 1;
    int c = bi_cmp_abs(a, b);
    return a->neg ? -c : c;
}

int bi_sign(const bigint *a)
{
    return a->len == 0 ? 0 : a->neg ? -1 : 1;
}

int bi_is_zero(const bigint *a)
{
    return a->len == 0;
}

int bi_is_odd(const bigint *a)
{
    return a->len > 0 && (a->limb[0] & 1u);
}

static unsigned clz32(limb_t x)
{
    unsigned n = 0;
    if (x == 0)
        return 32;
    while (!(x & 0x80000000u)) {
        x <<= 1;
        n++;
    }
    return n;
}

size_t bi_bit_length(const bigint *a)
{
    if (a->len == 0)
        return 0;
    return a->len * LIMB_BITS - clz32(a->limb[a->len - 1]);
}

int bi_test_bit(const bigint *a, size_t bit)
{
    size_t i = bit / LIMB_BITS;
    if (i >= a->len)
        return 0;
    return (int)((a->limb[i] >> (bit % LIMB_BITS)) & 1u);
}

/* --------------------------------------------------- soma e subtração */

/* Soma com sinais: se os sinais batem, soma as magnitudes; senão subtrai
   a menor da maior e fica com o sinal da maior. */
static bi_status add_signed(bigint *r, const bigint *a, int a_neg, const bigint *b, int b_neg)
{
    bigint t;
    bi_init(&t);
    if (a_neg == b_neg) {
        const bigint *x = a, *y = b;
        if (x->len < y->len) {
            x = b;
            y = a;
        }
        bi_status st = reserve(&t, x->len + 1);
        if (st != BI_OK)
            return st;
        t.len = mag_add(t.limb, x->limb, x->len, y->limb, y->len);
        t.neg = a_neg;
    } else {
        int c = bi_cmp_abs(a, b);
        if (c != 0) {
            const bigint *big = c > 0 ? a : b, *small = c > 0 ? b : a;
            bi_status st = reserve(&t, big->len);
            if (st != BI_OK)
                return st;
            t.len = mag_sub(t.limb, big->limb, big->len, small->limb, small->len);
            t.neg = c > 0 ? a_neg : b_neg;
        }
    }
    return finish(r, &t);
}

bi_status bi_add(bigint *r, const bigint *a, const bigint *b)
{
    return add_signed(r, a, a->neg, b, b->neg);
}

bi_status bi_sub(bigint *r, const bigint *a, const bigint *b)
{
    return add_signed(r, a, a->neg, b, b->len ? !b->neg : 0);
}

bi_status bi_neg(bigint *r, const bigint *a)
{
    bi_status st = bi_copy(r, a);
    if (st == BI_OK && r->len > 0)
        r->neg = !r->neg;
    return st;
}

bi_status bi_abs(bigint *r, const bigint *a)
{
    bi_status st = bi_copy(r, a);
    if (st == BI_OK)
        r->neg = 0;
    return st;
}

bi_status bi_add_i64(bigint *r, const bigint *a, int64_t b)
{
    bigint t;
    bi_init(&t);
    bi_status st = bi_set_i64(&t, b);
    if (st == BI_OK)
        st = bi_add(r, a, &t);
    bi_free(&t);
    return st;
}

/* ---------------------------------------------------- multiplicação */

/* Multiplicação escolar: cada limb de a vezes todo o b, somando deslocado. */
static void mul_school(limb_t *r, const limb_t *a, size_t an, const limb_t *b, size_t bn)
{
    memset(r, 0, (an + bn) * sizeof *r);
    for (size_t i = 0; i < an; i++) {
        dlimb_t ai = a[i], carry = 0;
        if (ai == 0)
            continue;
        for (size_t j = 0; j < bn; j++) {
            /* (2^32 - 1)^2 + 2 * (2^32 - 1) = 2^64 - 1: nunca estoura. */
            dlimb_t t = ai * b[j] + r[i + j] + carry;
            r[i + j] = (limb_t)t;
            carry = t >> 32;
        }
        r[i + bn] = (limb_t)carry;
    }
}

/*
 * Karatsuba: com a = a1*B^m + a0 e b = b1*B^m + b0,
 *   a*b = z2*B^(2m) + z1*B^m + z0, onde
 *   z0 = a0*b0, z2 = a1*b1 e z1 = (a0 + a1)(b0 + b1) - z0 - z2.
 * Três multiplicações de metade do tamanho em vez de quatro.
 * r recebe an + bn limbs e não pode se sobrepor a a nem a b.
 */
static bi_status mul_kara(limb_t *r, const limb_t *a, size_t an, const limb_t *b, size_t bn,
                          size_t threshold)
{
    if (an < bn) {
        const limb_t *tp = a;
        a = b;
        b = tp;
        size_t tn = an;
        an = bn;
        bn = tn;
    }
    if (bn < threshold || bn < 2) {
        mul_school(r, a, an, b, bn);
        return BI_OK;
    }

    if (an >= 2 * bn) {
        /* Muito desequilibrado: fatia a em pedaços do tamanho de b. */
        limb_t *tmp = malloc(2 * bn * sizeof *tmp);
        if (tmp == NULL)
            return BI_ERR_MEMORY;
        memset(r, 0, (an + bn) * sizeof *r);
        for (size_t off = 0; off < an; off += bn) {
            size_t len = an - off < bn ? an - off : bn;
            bi_status st = mul_kara(tmp, a + off, len, b, bn, threshold);
            if (st != BI_OK) {
                free(tmp);
                return st;
            }
            add_into(r + off, an + bn - off, tmp, len + bn);
        }
        free(tmp);
        return BI_OK;
    }

    /* Aqui an < 2 * bn, então b também tem pelo menos m limbs. */
    size_t m = (an + 1) / 2;
    const limb_t *a0 = a, *a1 = a + m, *b0 = b, *b1 = b + m;
    size_t a1n = an - m, b1n = bn - m;
    bi_status st;

    /* z0 vai para r[0..2m) e z2 para r[2m..an+bn). */
    if ((st = mul_kara(r, a0, m, b0, m, threshold)) != BI_OK)
        return st;
    if (b1n > 0) {
        if ((st = mul_kara(r + 2 * m, a1, a1n, b1, b1n, threshold)) != BI_OK)
            return st;
    } else {
        memset(r + 2 * m, 0, (an + bn - 2 * m) * sizeof *r);
    }

    limb_t *buf = malloc((4 * m + 4) * sizeof *buf);
    if (buf == NULL)
        return BI_ERR_MEMORY;
    limb_t *sa = buf, *sb = buf + m + 1, *z1 = buf + 2 * m + 2;
    size_t san = mag_norm(sa, mag_add(sa, a0, m, a1, a1n));
    size_t sbn = mag_norm(sb, mag_add(sb, b0, m, b1, b1n));
    memset(z1, 0, (2 * m + 2) * sizeof *z1);
    if (san > 0 && sbn > 0) {
        if ((st = mul_kara(z1, sa, san, sb, sbn, threshold)) != BI_OK) {
            free(buf);
            return st;
        }
    }
    /* z1 = (a0 + a1)(b0 + b1) - z0 - z2 = a0*b1 + a1*b0 (nunca negativo). */
    sub_into(z1, 2 * m + 2, r, 2 * m);
    sub_into(z1, 2 * m + 2, r + 2 * m, an + bn - 2 * m);
    size_t z1n = mag_norm(z1, 2 * m + 2);
    add_into(r + m, an + bn - m, z1, z1n);
    free(buf);
    return BI_OK;
}

static bi_status mul_common(bigint *r, const bigint *a, const bigint *b, size_t threshold,
                            int schoolbook)
{
    bigint t;
    bi_init(&t);
    if (a->len == 0 || b->len == 0)
        return finish(r, &t);
    if (a->len > SIZE_MAX / 2 - b->len)
        return BI_ERR_MEMORY;
    bi_status st = reserve(&t, a->len + b->len);
    if (st != BI_OK)
        return st;
    if (schoolbook) {
        mul_school(t.limb, a->limb, a->len, b->limb, b->len);
    } else if ((st = mul_kara(t.limb, a->limb, a->len, b->limb, b->len, threshold)) != BI_OK) {
        bi_free(&t);
        return st;
    }
    t.len = a->len + b->len;
    t.neg = a->neg ^ b->neg;
    return finish(r, &t);
}

bi_status bi_mul(bigint *r, const bigint *a, const bigint *b)
{
    return mul_common(r, a, b, BI_KARATSUBA_THRESHOLD, 0);
}

bi_status bi_mul_schoolbook(bigint *r, const bigint *a, const bigint *b)
{
    return mul_common(r, a, b, 0, 1);
}

bi_status bi_mul_karatsuba(bigint *r, const bigint *a, const bigint *b, size_t threshold)
{
    return mul_common(r, a, b, threshold, 0);
}

bi_status bi_mul_i64(bigint *r, const bigint *a, int64_t b)
{
    bigint t;
    bi_init(&t);
    bi_status st = bi_set_i64(&t, b);
    if (st == BI_OK)
        st = bi_mul(r, a, &t);
    bi_free(&t);
    return st;
}

/* ------------------------------------------------------------ divisão */

/*
 * Algoritmo D de Knuth (TAOCP vol. 2, 4.3.1): divisão longa na base 2^32.
 * a tem an limbs, b tem bn >= 2 limbs com b[bn-1] != 0 e an >= bn.
 * q recebe an - bn + 1 limbs e r recebe bn limbs.
 */
static bi_status knuth_d(limb_t *q, limb_t *r, const limb_t *a, size_t an, const limb_t *b,
                         size_t bn)
{
    limb_t *vn = malloc(bn * sizeof *vn);
    limb_t *un = malloc((an + 1) * sizeof *un);
    if (vn == NULL || un == NULL) {
        free(vn);
        free(un);
        return BI_ERR_MEMORY;
    }

    /* D1, normalização: desloca os dois para que o limb mais alto do
       divisor tenha o bit 31 ligado. Assim a estimativa de cada dígito do
       quociente erra por no máximo 2. */
    unsigned s = clz32(b[bn - 1]);
    for (size_t i = bn - 1; i > 0; i--)
        vn[i] = (b[i] << s) | (s ? b[i - 1] >> (32 - s) : 0);
    vn[0] = b[0] << s;
    un[an] = s ? a[an - 1] >> (32 - s) : 0;
    for (size_t i = an - 1; i > 0; i--)
        un[i] = (a[i] << s) | (s ? a[i - 1] >> (32 - s) : 0);
    un[0] = a[0] << s;

    for (size_t j = an - bn + 1; j-- > 0;) {
        /* D3: estima o dígito com os dois limbs mais altos do resto parcial. */
        dlimb_t num = ((dlimb_t)un[j + bn] << 32) | un[j + bn - 1];
        dlimb_t qhat = num / vn[bn - 1];
        dlimb_t rhat = num % vn[bn - 1];
        while ((qhat >> 32) != 0 || qhat * vn[bn - 2] > ((rhat << 32) | un[j + bn - 2])) {
            qhat--;
            rhat += vn[bn - 1];
            if ((rhat >> 32) != 0)
                break;
        }

        /* D4: subtrai qhat * divisor do resto parcial. */
        dlimb_t carry = 0, borrow = 0;
        for (size_t i = 0; i < bn; i++) {
            dlimb_t p = qhat * vn[i] + carry;
            carry = p >> 32;
            dlimb_t t = (dlimb_t)un[i + j] - (limb_t)p - borrow;
            un[i + j] = (limb_t)t;
            borrow = t >> 63;
        }
        dlimb_t top = (dlimb_t)un[j + bn] - carry - borrow;
        un[j + bn] = (limb_t)top;

        /* D5 e D6: se ficou negativo (raro, chance ~2/2^32), qhat era 1 a mais. */
        if ((top >> 63) != 0) {
            qhat--;
            dlimb_t c = 0;
            for (size_t i = 0; i < bn; i++) {
                dlimb_t t = (dlimb_t)un[i + j] + vn[i] + c;
                un[i + j] = (limb_t)t;
                c = t >> 32;
            }
            un[j + bn] += (limb_t)c;
        }
        q[j] = (limb_t)qhat;
    }

    /* D8: desfaz a normalização no resto. */
    for (size_t i = 0; i + 1 < bn; i++)
        r[i] = (un[i] >> s) | (s ? un[i + 1] << (32 - s) : 0);
    r[bn - 1] = un[bn - 1] >> s;
    free(vn);
    free(un);
    return BI_OK;
}

/* q = |a| / |b| e r = |a| % |b| (b != 0), em variáveis novas. */
static bi_status mag_divmod(bigint *q, bigint *r, const limb_t *a, size_t an, const limb_t *b,
                            size_t bn)
{
    bi_status st;
    if (mag_cmp(a, an, b, bn) < 0) {
        /* Divisor maior que o dividendo: quociente 0, resto a. */
        q->len = 0;
        if ((st = reserve(r, an)) != BI_OK)
            return st;
        if (an > 0)
            memcpy(r->limb, a, an * sizeof *a);
        r->len = an;
        return BI_OK;
    }
    if (bn == 1) {
        if ((st = reserve(q, an)) != BI_OK || (st = reserve(r, 1)) != BI_OK)
            return st;
        memcpy(q->limb, a, an * sizeof *a);
        r->limb[0] = mag_div_small(q->limb, an, b[0]);
        q->len = mag_norm(q->limb, an);
        r->len = mag_norm(r->limb, 1);
        return BI_OK;
    }
    if ((st = reserve(q, an - bn + 1)) != BI_OK || (st = reserve(r, bn)) != BI_OK)
        return st;
    if ((st = knuth_d(q->limb, r->limb, a, an, b, bn)) != BI_OK)
        return st;
    q->len = mag_norm(q->limb, an - bn + 1);
    r->len = mag_norm(r->limb, bn);
    return BI_OK;
}

/* Divisão truncada em temporários (tq, tr), sem tocar nas saídas. */
static bi_status divmod_trunc(bigint *tq, bigint *tr, const bigint *a, const bigint *b)
{
    if (b->len == 0)
        return BI_ERR_DIV_ZERO;
    bi_status st = mag_divmod(tq, tr, a->limb, a->len, b->limb, b->len);
    if (st != BI_OK)
        return st;
    tq->neg = a->neg ^ b->neg;
    tr->neg = a->neg;
    trim(tq);
    trim(tr);
    return BI_OK;
}

bi_status bi_divmod(bigint *q, bigint *r, const bigint *a, const bigint *b)
{
    bigint tq, tr;
    bi_init(&tq);
    bi_init(&tr);
    if (q != NULL && q == r)
        return BI_ERR_DOMAIN;
    bi_status st = divmod_trunc(&tq, &tr, a, b);
    if (st == BI_OK) {
        if (q != NULL)
            bi_swap(q, &tq);
        if (r != NULL)
            bi_swap(r, &tr);
    }
    bi_free(&tq);
    bi_free(&tr);
    return st;
}

bi_status bi_fdivmod(bigint *q, bigint *r, const bigint *a, const bigint *b)
{
    bigint tq, tr;
    bi_init(&tq);
    bi_init(&tr);
    if (q != NULL && q == r)
        return BI_ERR_DOMAIN;
    bi_status st = divmod_trunc(&tq, &tr, a, b);
    /* O truncado e o piso só diferem quando o resto tem sinal oposto ao
       do divisor: aí o quociente desce 1 e o resto ganha o divisor. */
    if (st == BI_OK && tr.len > 0 && tr.neg != b->neg) {
        st = bi_add_i64(&tq, &tq, -1);
        if (st == BI_OK)
            st = bi_add(&tr, &tr, b);
    }
    if (st == BI_OK) {
        if (q != NULL)
            bi_swap(q, &tq);
        if (r != NULL)
            bi_swap(r, &tr);
    }
    bi_free(&tq);
    bi_free(&tr);
    return st;
}

/* ------------------------------------------------- potências e afins */

bi_status bi_pow(bigint *r, const bigint *a, uint64_t e)
{
    bigint result, base;
    bi_status st;

    /* |a| <= 1 não cresce: trata à parte (0^0 = 1, como no Python). */
    if (a->len == 0)
        return bi_set_u64(r, e == 0 ? 1 : 0);
    if (a->len == 1 && a->limb[0] == 1) {
        int neg = a->neg && (e & 1u);
        st = bi_set_u64(r, 1);
        if (st == BI_OK && neg)
            r->neg = 1;
        return st;
    }
    uint64_t bits = bi_bit_length(a);
    if (e > MAX_POW_BITS / bits)
        return BI_ERR_RANGE;

    int negative = a->neg && (e & 1u);   /* base negativa e expoente ímpar */
    bi_init(&result);
    bi_init(&base);
    st = bi_set_u64(&result, 1);
    if (st == BI_OK)
        st = bi_abs(&base, a);
    /* Quadrado e multiplicação: a^13 = a^8 * a^4 * a^1 (13 = 1101 em binário). */
    while (st == BI_OK && e > 0) {
        if (e & 1u)
            st = bi_mul(&result, &result, &base);
        e >>= 1;
        if (st == BI_OK && e > 0)
            st = bi_mul(&base, &base, &base);
    }
    if (st == BI_OK) {
        result.neg = negative;
        bi_swap(r, &result);
    }
    bi_free(&result);
    bi_free(&base);
    return st;
}

/* r = (x * y) mod m, com x, y em [0, m). */
static bi_status mul_mod(bigint *r, const bigint *x, const bigint *y, const bigint *m)
{
    bigint p, q, rem;
    bi_init(&p);
    bi_init(&q);
    bi_init(&rem);
    bi_status st = bi_mul(&p, x, y);
    if (st == BI_OK)
        st = mag_divmod(&q, &rem, p.limb, p.len, m->limb, m->len);
    if (st == BI_OK)
        bi_swap(r, &rem);
    bi_free(&p);
    bi_free(&q);
    bi_free(&rem);
    return st;
}

bi_status bi_powmod(bigint *r, const bigint *a, const bigint *e, const bigint *m)
{
    bigint result, base;
    bi_status st;

    if (m->len == 0 || m->neg || e->neg)
        return BI_ERR_DOMAIN;
    bi_init(&result);
    bi_init(&base);
    /* Base reduzida para [0, m), mesmo se a for negativo. */
    st = bi_fdivmod(NULL, &base, a, m);
    if (st == BI_OK)
        st = bi_set_u64(&result, 1);
    if (st == BI_OK && m->len == 1 && m->limb[0] == 1)
        result.len = 0;   /* tudo mod 1 é 0 */
    /* Da esquerda para a direita: para cada bit do expoente, eleva ao
       quadrado; se o bit é 1, multiplica pela base. */
    for (size_t i = bi_bit_length(e); st == BI_OK && i-- > 0;) {
        st = mul_mod(&result, &result, &result, m);
        if (st == BI_OK && bi_test_bit(e, i))
            st = mul_mod(&result, &result, &base, m);
    }
    if (st == BI_OK) {
        trim(&result);
        bi_swap(r, &result);
    }
    bi_free(&result);
    bi_free(&base);
    return st;
}

bi_status bi_gcd(bigint *r, const bigint *a, const bigint *b)
{
    bigint x, y, q, rem;
    bi_init(&x);
    bi_init(&y);
    bi_init(&q);
    bi_init(&rem);
    bi_status st = bi_abs(&x, a);
    if (st == BI_OK)
        st = bi_abs(&y, b);
    /* Euclides: mdc(x, y) = mdc(y, x mod y), até y zerar. */
    while (st == BI_OK && y.len > 0) {
        st = mag_divmod(&q, &rem, x.limb, x.len, y.limb, y.len);
        bi_swap(&x, &y);
        bi_swap(&y, &rem);
    }
    if (st == BI_OK)
        bi_swap(r, &x);
    bi_free(&x);
    bi_free(&y);
    bi_free(&q);
    bi_free(&rem);
    return st;
}

bi_status bi_isqrt(bigint *r, const bigint *a)
{
    bigint x, y, q;
    bi_status st;

    if (a->neg)
        return BI_ERR_DOMAIN;
    if (a->len == 0)
        return bi_set_u64(r, 0);
    bi_init(&x);
    bi_init(&y);
    bi_init(&q);
    /*
     * Newton para f(x) = x^2 - a: x' = (x + a / x) / 2. Começando acima
     * da raiz (2^ceil(bits/2) >= sqrt(a)), a sequência desce sem parar
     * até a resposta; quando para de descer, achamos o piso da raiz.
     */
    size_t bits = bi_bit_length(a);
    st = bi_set_u64(&x, 1);
    if (st == BI_OK)
        st = bi_shl(&x, &x, (bits + 1) / 2);
    while (st == BI_OK) {
        st = bi_divmod(&q, NULL, a, &x);
        if (st == BI_OK)
            st = bi_add(&y, &x, &q);
        if (st == BI_OK)
            st = bi_shr(&y, &y, 1);
        if (st != BI_OK || bi_cmp(&y, &x) >= 0)
            break;
        bi_swap(&x, &y);
    }
    if (st == BI_OK)
        bi_swap(r, &x);
    bi_free(&x);
    bi_free(&y);
    bi_free(&q);
    return st;
}

/* Produto de lo..hi, dividindo ao meio: os fatores ficam com tamanhos
   parecidos e a multiplicação de Karatsuba entra em ação. */
static bi_status product_range(bigint *r, uint64_t lo, uint64_t hi)
{
    if (hi - lo < 16) {
        bi_status st = bi_set_u64(r, lo);
        for (uint64_t k = lo + 1; st == BI_OK && k <= hi; k++) {
            if ((st = reserve(r, r->len + 1)) == BI_OK)
                r->len = mag_mul_small(r->limb, r->limb, r->len, (limb_t)k, 0);
        }
        return st;
    }
    bigint left, right;
    bi_init(&left);
    bi_init(&right);
    uint64_t mid = lo + (hi - lo) / 2;
    bi_status st = product_range(&left, lo, mid);
    if (st == BI_OK)
        st = product_range(&right, mid + 1, hi);
    if (st == BI_OK)
        st = bi_mul(r, &left, &right);
    bi_free(&left);
    bi_free(&right);
    return st;
}

bi_status bi_factorial(bigint *r, uint64_t n)
{
    if (n > UINT32_MAX)
        return BI_ERR_RANGE;
    if (n < 2)
        return bi_set_u64(r, 1);
    bigint t;
    bi_init(&t);
    bi_status st = product_range(&t, 2, n);
    if (st == BI_OK)
        bi_swap(r, &t);
    bi_free(&t);
    return st;
}

/* --------------------------------------------------------------- bits */

bi_status bi_shl(bigint *r, const bigint *a, size_t bits)
{
    bigint t;
    bi_init(&t);
    if (a->len == 0)
        return finish(r, &t);
    size_t limbs = bits / LIMB_BITS;
    unsigned sh = (unsigned)(bits % LIMB_BITS);
    if (limbs > SIZE_MAX / 2 - a->len)
        return BI_ERR_MEMORY;
    bi_status st = reserve(&t, a->len + limbs + 1);
    if (st != BI_OK)
        return st;
    memset(t.limb, 0, limbs * sizeof(limb_t));
    if (sh == 0) {
        memcpy(t.limb + limbs, a->limb, a->len * sizeof(limb_t));
        t.limb[a->len + limbs] = 0;
    } else {
        limb_t carry = 0;
        for (size_t i = 0; i < a->len; i++) {
            t.limb[i + limbs] = (a->limb[i] << sh) | carry;
            carry = a->limb[i] >> (32 - sh);
        }
        t.limb[a->len + limbs] = carry;
    }
    t.len = a->len + limbs + 1;
    t.neg = a->neg;
    return finish(r, &t);
}

bi_status bi_shr(bigint *r, const bigint *a, size_t bits)
{
    bigint t;
    bi_init(&t);
    size_t limbs = bits / LIMB_BITS;
    unsigned sh = (unsigned)(bits % LIMB_BITS);
    int lost = 0;   /* algum bit 1 foi descartado? */

    if (limbs >= a->len) {
        lost = a->len > 0;
    } else {
        bi_status st = reserve(&t, a->len - limbs);
        if (st != BI_OK)
            return st;
        for (size_t i = 0; i < limbs; i++)
            lost |= a->limb[i] != 0;
        if (sh != 0)
            lost |= (a->limb[limbs] & ((1u << sh) - 1u)) != 0;
        for (size_t i = limbs; i < a->len; i++) {
            limb_t lo = a->limb[i] >> sh;
            limb_t hi = (sh != 0 && i + 1 < a->len) ? a->limb[i + 1] << (32 - sh) : 0;
            t.limb[i - limbs] = lo | hi;
        }
        t.len = a->len - limbs;
    }
    trim(&t);
    /* Piso para negativos (como o >> do Python): -5 >> 1 = -3. Se algum
       bit 1 foi descartado, a magnitude sobe 1 antes de pôr o sinal. */
    if (a->neg) {
        if (lost) {
            bi_status st = bi_add_i64(&t, &t, 1);
            if (st != BI_OK) {
                bi_free(&t);
                return st;
            }
        }
        t.neg = t.len > 0;
    }
    return finish(r, &t);
}

typedef enum { OP_AND, OP_OR, OP_XOR } bit_op;

static bi_status bitwise(bigint *r, const bigint *a, const bigint *b, bit_op op)
{
    if (a->neg || b->neg)
        return BI_ERR_DOMAIN;
    bigint t;
    bi_init(&t);
    size_t n = a->len > b->len ? a->len : b->len;
    bi_status st = reserve(&t, n);
    if (st != BI_OK)
        return st;
    for (size_t i = 0; i < n; i++) {
        limb_t x = i < a->len ? a->limb[i] : 0, y = i < b->len ? b->limb[i] : 0;
        t.limb[i] = op == OP_AND ? (x & y) : op == OP_OR ? (x | y) : (x ^ y);
    }
    t.len = n;
    return finish(r, &t);
}

bi_status bi_and(bigint *r, const bigint *a, const bigint *b)
{
    return bitwise(r, a, b, OP_AND);
}

bi_status bi_or(bigint *r, const bigint *a, const bigint *b)
{
    return bitwise(r, a, b, OP_OR);
}

bi_status bi_xor(bigint *r, const bigint *a, const bigint *b)
{
    return bitwise(r, a, b, OP_XOR);
}

/* ------------------------------------------------------------ texto */

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bi_status bi_from_dec(bigint *a, const char *s)
{
    int neg = 0;
    if (*s == '+' || *s == '-') {
        neg = *s == '-';
        s++;
    }
    size_t n = strlen(s);
    if (n == 0)
        return BI_ERR_PARSE;
    for (size_t i = 0; i < n; i++)
        if (!is_digit(s[i]))
            return BI_ERR_PARSE;

    bigint t;
    bi_init(&t);
    bi_status st = reserve(&t, n / 9 + 3);
    if (st != BI_OK)
        return st;
    /* Pedaços de 9 dígitos: t = t * 10^9 + pedaço. O primeiro pedaço pega
       o que sobra (n mod 9) para os outros ficarem completos. */
    size_t pos = 0, take = n % 9 ? n % 9 : 9;
    while (pos < n) {
        limb_t chunk = 0, mult = 1;
        for (size_t k = 0; k < take; k++) {
            chunk = chunk * 10u + (limb_t)(s[pos + k] - '0');
            mult *= 10u;
        }
        t.len = mag_mul_small(t.limb, t.limb, t.len, mult, chunk);
        pos += take;
        take = 9;
    }
    t.neg = neg;
    return finish(a, &t);
}

bi_status bi_from_hex(bigint *a, const char *s)
{
    int neg = 0;
    if (*s == '+' || *s == '-') {
        neg = *s == '-';
        s++;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    size_t n = strlen(s);
    if (n == 0)
        return BI_ERR_PARSE;
    for (size_t i = 0; i < n; i++)
        if (hex_value(s[i]) < 0)
            return BI_ERR_PARSE;

    bigint t;
    bi_init(&t);
    bi_status st = reserve(&t, n / 8 + 1);
    if (st != BI_OK)
        return st;
    /* Cada limb são 8 dígitos hexadecimais, lidos a partir do fim. */
    size_t limbs = 0;
    for (size_t end = n; end > 0; end = end > 8 ? end - 8 : 0) {
        size_t start = end > 8 ? end - 8 : 0;
        limb_t v = 0;
        for (size_t i = start; i < end; i++)
            v = (v << 4) | (limb_t)hex_value(s[i]);
        t.limb[limbs++] = v;
    }
    t.len = limbs;
    t.neg = neg;
    return finish(a, &t);
}

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p != NULL)
        memcpy(p, s, n);
    return p;
}

char *bi_to_dec(const bigint *a)
{
    if (a->len == 0)
        return dup_str("0");
    /* Divide repetidamente por 10^9: cada resto são 9 dígitos, do fim
       para o começo. 2^32 tem 9,63 dígitos, então len * 2 pedaços sobram. */
    size_t n = a->len;
    if (n > SIZE_MAX / 20)
        return NULL;
    limb_t *tmp = malloc(n * sizeof *tmp);
    limb_t *chunks = malloc((2 * n + 1) * sizeof *chunks);
    if (tmp == NULL || chunks == NULL) {
        free(tmp);
        free(chunks);
        return NULL;
    }
    memcpy(tmp, a->limb, n * sizeof *tmp);
    size_t nc = 0;
    while (n > 0) {
        chunks[nc++] = mag_div_small(tmp, n, CHUNK10);
        n = mag_norm(tmp, n);
    }
    char *out = malloc(nc * 9 + 2);
    if (out != NULL) {
        char *p = out;
        if (a->neg)
            *p++ = '-';
        /* O pedaço mais alto sem zeros à esquerda, os outros com 9 dígitos. */
        char first[16];
        int k = 0;
        limb_t v = chunks[nc - 1];
        do {
            first[k++] = (char)('0' + v % 10u);
            v /= 10u;
        } while (v > 0);
        while (k > 0)
            *p++ = first[--k];
        for (size_t c = nc - 1; c-- > 0;) {
            limb_t w = chunks[c];
            for (int d = 8; d >= 0; d--) {
                p[d] = (char)('0' + w % 10u);
                w /= 10u;
            }
            p += 9;
        }
        *p = '\0';
    }
    free(tmp);
    free(chunks);
    return out;
}

char *bi_to_hex(const bigint *a)
{
    static const char digits[] = "0123456789abcdef";
    if (a->len == 0)
        return dup_str("0");
    if (a->len > SIZE_MAX / 9)
        return NULL;
    char *out = malloc(a->len * 8 + 2);
    if (out == NULL)
        return NULL;
    char *p = out;
    if (a->neg)
        *p++ = '-';
    int started = 0;
    for (size_t i = a->len; i-- > 0;) {
        for (int sh = 28; sh >= 0; sh -= 4) {
            unsigned d = (a->limb[i] >> sh) & 0xFu;
            if (d != 0 || started) {
                *p++ = digits[d];
                started = 1;
            }
        }
    }
    *p = '\0';
    return out;
}

/* ------------------------------------------------------- primalidade */

static const uint16_t SMALL_PRIMES[] = {
    2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
    73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163,
    167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251, 257,
    263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317, 331, 337, 347, 349, 353, 359,
    367, 373, 379, 383, 389, 397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457, 461,
    463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541, 547, 557, 563, 569, 571, 577,
    587, 593, 599, 601, 607, 613, 617, 619, 631, 641, 643, 647, 653, 659, 661, 673, 677,
    683, 691, 701, 709, 719, 727, 733, 739, 743, 751, 757, 761, 769, 773, 787, 797, 809,
    811, 821, 823, 827, 829, 839, 853, 857, 859, 863, 877, 881, 883, 887, 907, 911, 919,
    929, 937, 941, 947, 953, 967, 971, 977, 983, 991, 997};

#define DETERMINISTIC_BASES 13          /* 2, 3, 5, ..., 41 */
#define EXTRA_ROUNDS 24

/* Limite de Sorenson e Webster (2015): abaixo dele, as 13 primeiras bases bastam. */
static const char *DETERMINISTIC_LIMIT = "3317044064679887385961981";

/*
 * Uma rodada de Miller-Rabin com a base a. Com n - 1 = d * 2^s (d ímpar),
 * n passa se a^d = 1 ou se a^(d*2^k) = n - 1 para algum k < s.
 * Um primo sempre passa; um composto passa para no máximo 1/4 das bases.
 */
static bi_status mr_round(const bigint *n, const bigint *n1, const bigint *d, size_t s,
                          const bigint *a, int *passed)
{
    bigint x;
    bi_init(&x);
    bi_status st = bi_powmod(&x, a, d, n);
    *passed = 0;
    if (st == BI_OK) {
        if ((x.len == 1 && x.limb[0] == 1) || bi_cmp(&x, n1) == 0) {
            *passed = 1;
        } else {
            for (size_t k = 1; k < s && st == BI_OK; k++) {
                st = mul_mod(&x, &x, &x, n);
                if (st == BI_OK && bi_cmp(&x, n1) == 0) {
                    *passed = 1;
                    break;
                }
                if (st == BI_OK && x.len == 1 && x.limb[0] == 1)
                    break;   /* chegou a 1 sem passar por n - 1: composto */
            }
        }
    }
    bi_free(&x);
    return st;
}

static uint64_t splitmix64(uint64_t *state)
{
    uint64_t z = (*state += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

bi_status bi_is_prime(const bigint *n, int *is_prime)
{
    *is_prime = 0;
    if (n->neg || n->len == 0 || (n->len == 1 && n->limb[0] < 2))
        return BI_OK;

    /* Divisão por primos pequenos resolve a maioria dos casos. */
    for (size_t i = 0; i < sizeof SMALL_PRIMES / sizeof SMALL_PRIMES[0]; i++) {
        limb_t p = SMALL_PRIMES[i];
        if (n->len == 1 && n->limb[0] == p) {
            *is_prime = 1;
            return BI_OK;
        }
        if (mag_mod_small(n->limb, n->len, p) == 0)
            return BI_OK;
    }
    if (n->len == 1 && n->limb[0] < 997u * 997u) {
        *is_prime = 1;   /* sem divisor até a raiz */
        return BI_OK;
    }

    bigint n1, d, a, limit;
    bi_init(&n1);
    bi_init(&d);
    bi_init(&a);
    bi_init(&limit);
    bi_status st = bi_add_i64(&n1, n, -1);
    size_t s = 0;
    while (st == BI_OK && !bi_test_bit(&n1, s))
        s++;
    if (st == BI_OK)
        st = bi_shr(&d, &n1, s);
    if (st == BI_OK)
        st = bi_from_dec(&limit, DETERMINISTIC_LIMIT);
    int deterministic = st == BI_OK && bi_cmp(n, &limit) < 0;
    int rounds = DETERMINISTIC_BASES + (deterministic ? 0 : EXTRA_ROUNDS);

    /* Semente derivada de n: o mesmo n dá sempre a mesma resposta. */
    uint64_t seed = UINT64_C(0x243F6A8885A308D3) ^ n->len;
    for (size_t i = 0; i < n->len; i++)
        seed = (seed ^ n->limb[i]) * UINT64_C(0x100000001B3);

    int passed = 1;
    for (int round = 0; st == BI_OK && passed && round < rounds; round++) {
        if (round < DETERMINISTIC_BASES) {
            st = bi_set_u64(&a, SMALL_PRIMES[round]);
        } else {
            /* Base pseudoaleatória em [2, n - 2]. */
            st = reserve(&a, n->len);
            if (st == BI_OK) {
                for (size_t i = 0; i < n->len; i++)
                    a.limb[i] = (limb_t)splitmix64(&seed);
                a.len = n->len;
                a.neg = 0;
                trim(&a);
                bigint span;
                bi_init(&span);
                st = bi_add_i64(&span, n, -3);
                if (st == BI_OK)
                    st = bi_fdivmod(NULL, &a, &a, &span);
                if (st == BI_OK)
                    st = bi_add_i64(&a, &a, 2);
                bi_free(&span);
            }
        }
        if (st == BI_OK)
            st = mr_round(n, &n1, &d, s, &a, &passed);
    }
    if (st == BI_OK)
        *is_prime = passed;
    bi_free(&n1);
    bi_free(&d);
    bi_free(&a);
    bi_free(&limit);
    return st;
}
