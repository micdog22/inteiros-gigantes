/*
 * calc.c: avaliador de expressões por descida recursiva.
 *
 * Cada regra da gramática (veja calc.h) virou uma função. A função de uma
 * regra chama as funções das regras de precedência maior, então a
 * precedência sai da própria estrutura das chamadas: em "1 + 2 * 3",
 * parse_expr pede um termo (1), vê o '+', e pede outro termo, que já
 * volta como 2 * 3 = 6.
 */
#include "calc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 400   /* parênteses e sinais aninhados */
#define MAX_ARGS 3

typedef struct {
    const char *s;
    size_t pos;
    size_t max_bits;
    int depth;
    calc_error *err;
} parser;

static int fail(parser *p, size_t pos, const char *fmt, ...)
{
    va_list ap;
    p->err->pos = pos;
    va_start(ap, fmt);
    vsnprintf(p->err->message, sizeof p->err->message, fmt, ap);
    va_end(ap);
    return 1;
}

static int fail_status(parser *p, bi_status st, size_t pos)
{
    return fail(p, pos, "%s", bi_strerror(st));
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_hex(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* Pula espaços e devolve o próximo caractere (sem consumir). */
static char peek(parser *p)
{
    while (is_space(p->s[p->pos]))
        p->pos++;
    return p->s[p->pos];
}

static int check_size(parser *p, const bigint *v, size_t pos)
{
    if (bi_bit_length(v) > p->max_bits)
        return fail(p, pos, "resultado grande demais: passa de %zu bits (use --limite para aumentar)",
                    p->max_bits);
    return 0;
}

static int parse_expr(parser *p, bigint *out);
static int parse_unary(parser *p, bigint *out);

/* -------------------------------------------------------------- números */

static int parse_number(parser *p, bigint *out)
{
    size_t start = p->pos, end = start;
    int hex = p->s[start] == '0' && (p->s[start + 1] == 'x' || p->s[start + 1] == 'X');
    if (hex) {
        end += 2;
        while (is_hex(p->s[end]))
            end++;
        if (end == start + 2)
            return fail(p, start, "número hexadecimal sem dígitos depois de 0x");
    } else {
        while (is_digit(p->s[end]))
            end++;
    }
    char *text = malloc(end - start + 1);
    if (text == NULL)
        return fail_status(p, BI_ERR_MEMORY, start);
    memcpy(text, p->s + start, end - start);
    text[end - start] = '\0';
    bi_status st = hex ? bi_from_hex(out, text) : bi_from_dec(out, text);
    free(text);
    if (st != BI_OK)
        return fail_status(p, st, start);
    p->pos = end;
    return check_size(p, out, start);
}

/* -------------------------------------------------------------- funções */

typedef enum { FN_MDC, FN_RAIZ, FN_PRIMO, FN_POTMOD } function_id;

static const struct {
    const char *name;
    function_id id;
    int args;
} FUNCTIONS[] = {
    {"mdc", FN_MDC, 2},
    {"raiz", FN_RAIZ, 1},
    {"primo", FN_PRIMO, 1},
    {"potmod", FN_POTMOD, 3},
};

/* Limites para não travar: primo() e potmod() custam proporcional a
   bits(expoente) * limbs(módulo)^2. */
#define PRIME_MAX_BITS 4096u
#define POWMOD_MAX_WORK (UINT64_C(1) << 31)

static int eval_function(parser *p, function_id id, bigint *args, size_t pos, bigint *out)
{
    bi_status st = BI_OK;
    switch (id) {
    case FN_MDC:
        st = bi_gcd(out, &args[0], &args[1]);
        break;
    case FN_RAIZ:
        if (bi_sign(&args[0]) < 0)
            return fail(p, pos, "raiz de número negativo não é um inteiro");
        st = bi_isqrt(out, &args[0]);
        break;
    case FN_PRIMO: {
        if (bi_bit_length(&args[0]) > PRIME_MAX_BITS)
            return fail(p, pos, "primo() aceita números de até %u bits", PRIME_MAX_BITS);
        int prime = 0;
        st = bi_is_prime(&args[0], &prime);
        if (st == BI_OK)
            st = bi_set_u64(out, (uint64_t)prime);
        break;
    }
    case FN_POTMOD: {
        if (bi_sign(&args[2]) <= 0)
            return fail(p, pos, "potmod: o módulo precisa ser positivo");
        if (bi_sign(&args[1]) < 0)
            return fail(p, pos, "potmod: o expoente não pode ser negativo");
        uint64_t limbs = (bi_bit_length(&args[2]) + 31) / 32;
        uint64_t work = (uint64_t)bi_bit_length(&args[1]) * limbs * limbs;
        if (bi_bit_length(&args[1]) > 65536u || limbs > 4096u || work > POWMOD_MAX_WORK)
            return fail(p, pos, "potmod: expoente e módulo grandes demais para calcular aqui");
        st = bi_powmod(out, &args[0], &args[1], &args[2]);
        break;
    }
    }
    if (st != BI_OK)
        return fail_status(p, st, pos);
    return check_size(p, out, pos);
}

static int parse_call(parser *p, bigint *out)
{
    size_t start = p->pos, end = start;
    char name[16];
    size_t n = 0;

    while (is_alpha(p->s[end]) || is_digit(p->s[end])) {
        if (n + 1 < sizeof name) {
            char c = p->s[end];
            name[n++] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
        end++;
    }
    name[n] = '\0';
    size_t fn = sizeof FUNCTIONS / sizeof FUNCTIONS[0];
    for (size_t i = 0; i < sizeof FUNCTIONS / sizeof FUNCTIONS[0]; i++)
        if (end - start == strlen(FUNCTIONS[i].name) && strcmp(name, FUNCTIONS[i].name) == 0)
            fn = i;
    if (fn == sizeof FUNCTIONS / sizeof FUNCTIONS[0])
        return fail(p, start, "função desconhecida: %.*s (as funções são mdc, raiz, primo e potmod)",
                    (int)(end - start > 20 ? 20 : end - start), p->s + start);
    p->pos = end;
    if (peek(p) != '(')
        return fail(p, p->pos, "esperado '(' depois de %s", FUNCTIONS[fn].name);
    p->pos++;

    bigint args[MAX_ARGS + 1];
    int count = 0, rc = 0;
    for (int i = 0; i <= MAX_ARGS; i++)
        bi_init(&args[i]);
    for (;;) {
        if (count > MAX_ARGS) {
            rc = fail(p, p->pos, "%s recebe %d argumento%s", FUNCTIONS[fn].name, FUNCTIONS[fn].args,
                      FUNCTIONS[fn].args == 1 ? "" : "s");
            break;
        }
        if ((rc = parse_expr(p, &args[count])) != 0)
            break;
        count++;
        char c = peek(p);
        if (c == ';') {
            p->pos++;
            continue;
        }
        if (c == ')') {
            p->pos++;
            break;
        }
        if (c == ',')
            rc = fail(p, p->pos, "use ';' para separar os argumentos (a vírgula é o separador decimal)");
        else if (c == '\0')
            rc = fail(p, p->pos, "faltou fechar o parêntese de %s", FUNCTIONS[fn].name);
        else
            rc = fail(p, p->pos, "esperado ';' ou ')' nos argumentos de %s", FUNCTIONS[fn].name);
        break;
    }
    if (rc == 0 && count != FUNCTIONS[fn].args)
        rc = fail(p, start, "%s recebe %d argumento%s, mas recebeu %d", FUNCTIONS[fn].name,
                  FUNCTIONS[fn].args, FUNCTIONS[fn].args == 1 ? "" : "s", count);
    if (rc == 0)
        rc = eval_function(p, FUNCTIONS[fn].id, args, start, out);
    for (int i = 0; i <= MAX_ARGS; i++)
        bi_free(&args[i]);
    return rc;
}

/* ----------------------------------------------------------- gramática */

static int parse_primary(parser *p, bigint *out)
{
    char c = peek(p);
    if (is_digit(c))
        return parse_number(p, out);
    if (is_alpha(c))
        return parse_call(p, out);
    if (c == '(') {
        size_t open = p->pos++;
        if (parse_expr(p, out))
            return 1;
        if (peek(p) != ')')
            return fail(p, p->pos, "faltou fechar o parêntese aberto na posição %zu", open + 1);
        p->pos++;
        return 0;
    }
    if (c == '\0')
        return fail(p, p->pos, "a expressão terminou antes do esperado");
    if (c == ')')
        return fail(p, p->pos, "esperado um número antes de ')'");
    if ((unsigned char)c >= 0x80)
        return fail(p, p->pos, "caractere inesperado");
    return fail(p, p->pos, "caractere inesperado '%c'", c);
}

static int parse_postfix(parser *p, bigint *out)
{
    if (parse_primary(p, out))
        return 1;
    while (peek(p) == '!') {
        size_t pos = p->pos++;
        uint64_t n;
        if (bi_sign(out) < 0)
            return fail(p, pos, "fatorial de número negativo não existe");
        if (!bi_get_u64(out, &n) || n > UINT32_MAX)
            return fail(p, pos, "fatorial grande demais");
        /* log2(n!) >= n * (log2(n) - 1,45): se isso já passa do limite, nem tenta. */
        uint64_t lg = 0;
        for (uint64_t v = n; v > 1; v >>= 1)
            lg++;
        if (lg > 2 && n * (lg - 2) > p->max_bits)
            return fail(p, pos, "fatorial grande demais: o resultado passaria de %zu bits",
                        p->max_bits);
        bi_status st = bi_factorial(out, n);
        if (st != BI_OK)
            return fail_status(p, st, pos);
        if (check_size(p, out, pos))
            return 1;
    }
    return 0;
}

static int parse_power(parser *p, bigint *out)
{
    if (parse_postfix(p, out))
        return 1;
    if (peek(p) != '^')
        return 0;
    size_t pos = p->pos++;
    bigint e;
    bi_init(&e);
    int rc = parse_unary(p, &e);   /* unário: permite 2^-1 (erro claro) e 2^3^2 */
    if (rc == 0) {
        uint64_t ev = 0;
        size_t base_bits = bi_bit_length(out);
        if (bi_sign(&e) < 0) {
            rc = fail(p, pos, "expoente negativo: o resultado não seria inteiro");
        } else if (base_bits <= 1) {
            /* 0, 1 e -1 não crescem: só importa se o expoente é zero ou par. */
            int odd = bi_is_odd(&e), zero = bi_is_zero(&e);
            bi_status st = BI_OK;
            if (zero)
                st = bi_set_u64(out, 1);
            else if (bi_sign(out) < 0 && !odd)
                st = bi_neg(out, out);
            if (st != BI_OK)
                rc = fail_status(p, st, pos);
        } else if (!bi_get_u64(&e, &ev) || (base_bits - 1) > p->max_bits / (ev ? ev : 1)) {
            rc = fail(p, pos, "resultado grande demais: passa de %zu bits (use --limite para aumentar)",
                      p->max_bits);
        } else {
            bi_status st = bi_pow(out, out, ev);
            if (st != BI_OK)
                rc = fail_status(p, st, pos);
            else
                rc = check_size(p, out, pos);
        }
    }
    bi_free(&e);
    return rc;
}

static int parse_unary(parser *p, bigint *out)
{
    if (++p->depth > MAX_DEPTH) {
        p->depth--;
        return fail(p, p->pos, "expressão aninhada demais");
    }
    int rc;
    char c = peek(p);
    if (c == '-' || c == '+') {
        p->pos++;
        rc = parse_unary(p, out);
        if (rc == 0 && c == '-') {
            bi_status st = bi_neg(out, out);
            if (st != BI_OK)
                rc = fail_status(p, st, p->pos);
        }
    } else {
        rc = parse_power(p, out);
    }
    p->depth--;
    return rc;
}

static int parse_term(parser *p, bigint *out)
{
    if (parse_unary(p, out))
        return 1;
    for (;;) {
        char op = peek(p);
        if (op != '*' && op != '/' && op != '%')
            return 0;
        size_t pos = p->pos++;
        bigint rhs;
        bi_init(&rhs);
        int rc = parse_unary(p, &rhs);
        if (rc == 0) {
            bi_status st;
            if (op == '*') {
                /* a * b >= 2^(bits(a) - 1 + bits(b) - 1): se isso já passa do
                   limite, recusa antes de gastar tempo multiplicando. */
                size_t bits = bi_bit_length(out) + bi_bit_length(&rhs);
                if (!bi_is_zero(out) && !bi_is_zero(&rhs) && bits - 2 > p->max_bits)
                    rc = fail(p, pos, "resultado grande demais: passa de %zu bits (use --limite para aumentar)",
                              p->max_bits);
                else if ((st = bi_mul(out, out, &rhs)) != BI_OK)
                    rc = fail_status(p, st, pos);
                else
                    rc = check_size(p, out, pos);
            } else if (bi_is_zero(&rhs)) {
                rc = fail(p, pos, "divisão por zero");
            } else if (op == '/') {
                if ((st = bi_fdivmod(out, NULL, out, &rhs)) != BI_OK)
                    rc = fail_status(p, st, pos);
            } else {
                if ((st = bi_fdivmod(NULL, out, out, &rhs)) != BI_OK)
                    rc = fail_status(p, st, pos);
            }
        }
        bi_free(&rhs);
        if (rc != 0)
            return 1;
    }
}

static int parse_expr(parser *p, bigint *out)
{
    if (parse_term(p, out))
        return 1;
    for (;;) {
        char op = peek(p);
        if (op != '+' && op != '-')
            return 0;
        size_t pos = p->pos++;
        bigint rhs;
        bi_init(&rhs);
        int rc = parse_term(p, &rhs);
        if (rc == 0) {
            bi_status st = op == '+' ? bi_add(out, out, &rhs) : bi_sub(out, out, &rhs);
            rc = st != BI_OK ? fail_status(p, st, pos) : check_size(p, out, pos);
        }
        bi_free(&rhs);
        if (rc != 0)
            return 1;
    }
}

int calc_eval(const char *expr, bigint *result, size_t max_bits, calc_error *err)
{
    parser p;
    p.s = expr;
    p.pos = 0;
    p.max_bits = max_bits ? max_bits : CALC_DEFAULT_MAX_BITS;
    p.depth = 0;
    p.err = err;
    err->pos = 0;
    err->message[0] = '\0';

    if (peek(&p) == '\0')
        return fail(&p, p.pos, "expressão vazia");
    bigint v;
    bi_init(&v);
    if (parse_expr(&p, &v)) {
        bi_free(&v);
        return 1;
    }
    char c = peek(&p);
    if (c != '\0') {
        if (c == ')')
            fail(&p, p.pos, "parêntese ')' sem o '(' correspondente");
        else if (c == '(' || is_digit(c) || is_alpha(c))
            fail(&p, p.pos, "faltou um operador antes daqui");
        else if (c == ',' || c == ';')
            fail(&p, p.pos, "'%c' só separa argumentos de funções, como em mdc(12; 18)", c);
        else if ((unsigned char)c >= 0x80)
            fail(&p, p.pos, "caractere inesperado");
        else
            fail(&p, p.pos, "caractere inesperado '%c'", c);
        bi_free(&v);
        return 1;
    }
    bi_swap(result, &v);
    bi_free(&v);
    return 0;
}
