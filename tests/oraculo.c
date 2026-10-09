/*
 * oraculo.c: executa operações lidas da entrada padrão, uma por linha,
 * para o teste diferencial (tests/diferencial.py) comparar com o python3.
 *
 *   add A B | sub A B | mul A B | smul A B | kmul A B LIMIAR
 *   divmod A B | fdivmod A B          (imprime "Q R")
 *   pow A E | powmod A E M | gcd A B | isqrt A | fact N
 *   shl A K | shr A K | and A B | or A B | xor A B
 *   hex A | fromhex H | cmp A B | prime N | calc EXPRESSÃO
 *
 * Erros saem como "ERRO".
 */
#include "bigint.h"
#include "calc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 4

static void print_big(const bigint *a)
{
    char *s = bi_to_dec(a);
    fputs(s ? s : "ERRO", stdout);
    free(s);
}

static int run(char *line)
{
    char *tok[MAX_TOKENS + 1] = {0};
    int n = 0;
    char *p = line;

    if (strncmp(line, "calc ", 5) == 0) {
        bigint v;
        calc_error err;
        bi_init(&v);
        if (calc_eval(line + 5, &v, 0, &err) == 0)
            print_big(&v);
        else
            fputs("ERRO", stdout);
        bi_free(&v);
        return 0;
    }
    while (*p && n <= MAX_TOKENS) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        tok[n++] = p;
        while (*p && *p != ' ')
            p++;
        if (*p)
            *p++ = '\0';
    }
    if (n == 0)
        return 1;

    bigint a, b, c, r, q;
    bi_init(&a);
    bi_init(&b);
    bi_init(&c);
    bi_init(&r);
    bi_init(&q);
    bi_status st = BI_OK;
    const char *op = tok[0];
    int printed = 0;

    if (strcmp(op, "fromhex") == 0) {
        st = n == 2 ? bi_from_hex(&r, tok[1]) : BI_ERR_PARSE;
    } else {
        if (n >= 2 && strcmp(op, "fact") != 0)
            st = bi_from_dec(&a, tok[1]);
        if (st == BI_OK && n >= 3 && strcmp(op, "shl") != 0 && strcmp(op, "shr") != 0 &&
            strcmp(op, "pow") != 0)
            st = bi_from_dec(&b, tok[2]);
        if (st == BI_OK && n >= 4 && strcmp(op, "kmul") != 0)
            st = bi_from_dec(&c, tok[3]);
    }
    if (st == BI_OK) {
        if (strcmp(op, "add") == 0) {
            st = bi_add(&r, &a, &b);
        } else if (strcmp(op, "sub") == 0) {
            st = bi_sub(&r, &a, &b);
        } else if (strcmp(op, "mul") == 0) {
            st = bi_mul(&r, &a, &b);
        } else if (strcmp(op, "smul") == 0) {
            st = bi_mul_schoolbook(&r, &a, &b);
        } else if (strcmp(op, "kmul") == 0) {
            st = bi_mul_karatsuba(&r, &a, &b, (size_t)strtoul(tok[3], NULL, 10));
        } else if (strcmp(op, "divmod") == 0 || strcmp(op, "fdivmod") == 0) {
            st = op[0] == 'f' ? bi_fdivmod(&q, &r, &a, &b) : bi_divmod(&q, &r, &a, &b);
            if (st == BI_OK) {
                print_big(&q);
                putchar(' ');
                print_big(&r);
                printed = 1;
            }
        } else if (strcmp(op, "pow") == 0) {
            st = bi_pow(&r, &a, strtoull(tok[2], NULL, 10));
        } else if (strcmp(op, "powmod") == 0) {
            st = bi_powmod(&r, &a, &b, &c);
        } else if (strcmp(op, "gcd") == 0) {
            st = bi_gcd(&r, &a, &b);
        } else if (strcmp(op, "isqrt") == 0) {
            st = bi_isqrt(&r, &a);
        } else if (strcmp(op, "fact") == 0) {
            st = bi_factorial(&r, strtoull(tok[1], NULL, 10));
        } else if (strcmp(op, "shl") == 0) {
            st = bi_shl(&r, &a, (size_t)strtoull(tok[2], NULL, 10));
        } else if (strcmp(op, "shr") == 0) {
            st = bi_shr(&r, &a, (size_t)strtoull(tok[2], NULL, 10));
        } else if (strcmp(op, "and") == 0) {
            st = bi_and(&r, &a, &b);
        } else if (strcmp(op, "or") == 0) {
            st = bi_or(&r, &a, &b);
        } else if (strcmp(op, "xor") == 0) {
            st = bi_xor(&r, &a, &b);
        } else if (strcmp(op, "hex") == 0) {
            char *h = bi_to_hex(&a);
            fputs(h ? h : "ERRO", stdout);
            free(h);
            printed = 1;
        } else if (strcmp(op, "fromhex") == 0) {
            /* já convertido acima */
        } else if (strcmp(op, "cmp") == 0) {
            printf("%d", bi_cmp(&a, &b));
            printed = 1;
        } else if (strcmp(op, "prime") == 0) {
            int is_prime = 0;
            st = bi_is_prime(&a, &is_prime);
            if (st == BI_OK) {
                printf("%d", is_prime);
                printed = 1;
            }
        } else {
            st = BI_ERR_PARSE;
        }
    }
    if (st != BI_OK)
        fputs("ERRO", stdout);
    else if (!printed)
        print_big(&r);
    bi_free(&a);
    bi_free(&b);
    bi_free(&c);
    bi_free(&r);
    bi_free(&q);
    return 0;
}

int main(void)
{
    size_t cap = 1 << 16;
    char *line = malloc(cap);
    if (line == NULL)
        return 1;
    for (;;) {
        size_t len = 0;
        int c;
        while ((c = getchar()) != EOF && c != '\n') {
            if (len + 1 >= cap) {
                char *p = realloc(line, cap * 2);
                if (p == NULL) {
                    free(line);
                    return 1;
                }
                line = p;
                cap *= 2;
            }
            line[len++] = (char)c;
        }
        if (c == EOF && len == 0)
            break;
        line[len] = '\0';
        run(line);
        putchar('\n');
        if (c == EOF)
            break;
    }
    free(line);
    return 0;
}
