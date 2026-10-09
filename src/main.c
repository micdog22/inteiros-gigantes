/* main.c: calc-gigante, a calculadora de inteiros gigantes. */
#include "bigint.h"
#include "calc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PROGRAM = "calc-gigante";

typedef struct {
    int hex;
    int info;
    size_t max_bits;
} options;

static void usage(FILE *f)
{
    fprintf(f,
            "uso: calc-gigante [opções] \"expressão\" ...\n"
            "\n"
            "Calcula expressões com inteiros de qualquer tamanho.\n"
            "Sem expressões, lê uma por linha da entrada padrão.\n"
            "\n"
            "Operadores: + - * / %% ^ ! e parênteses\n"
            "  /  e  %%   divisão e resto arredondando para baixo (-7 / 2 = -4, -7 %% 2 = 1)\n"
            "  ^         potência, associa à direita (2^3^2 = 2^9)\n"
            "  !         fatorial (100!)\n"
            "Funções (separe os argumentos com ponto e vírgula):\n"
            "  mdc(a; b)        máximo divisor comum\n"
            "  raiz(n)          raiz quadrada inteira (arredondada para baixo)\n"
            "  primo(n)         1 se n é primo, 0 se não é (Miller-Rabin)\n"
            "  potmod(a; b; m)  a^b mod m, rápido mesmo com expoentes enormes\n"
            "Números em hexadecimal começam com 0x (0xff).\n"
            "\n"
            "Opções:\n"
            "  -x, --hex          mostra o resultado em hexadecimal\n"
            "  -i, --info         mostra também quantos dígitos e bits o resultado tem\n"
            "  -l, --limite BITS  tamanho máximo dos resultados (padrão: %zu bits)\n"
            "  -h, --help         mostra esta ajuda\n"
            "  -V, --version      mostra a versão\n"
            "\n"
            "Exemplos:\n"
            "  calc-gigante \"2^521 - 1\"\n"
            "  calc-gigante \"100!\"\n"
            "  calc-gigante \"primo(2^127 - 1)\"\n"
            "  calc-gigante \"potmod(3; 10^100; 1000000007)\"\n",
            CALC_DEFAULT_MAX_BITS);
}

/* Colunas na tela até o byte pos (conta caracteres UTF-8, não bytes). */
static size_t display_columns(const char *s, size_t from, size_t to)
{
    size_t cols = 0;
    for (size_t i = from; i < to && s[i] != '\0'; i++)
        if (((unsigned char)s[i] & 0xC0u) != 0x80u)
            cols++;
    return cols;
}

/* Mostra a mensagem e a expressão com um ^ embaixo do ponto do erro. */
static void show_error(const char *expr, const calc_error *err)
{
    size_t len = strlen(expr), pos = err->pos > len ? len : err->pos;
    size_t from = 0, to = len;
    if (len > 72) {
        from = pos > 36 ? pos - 36 : 0;
        to = from + 72 < len ? from + 72 : len;
        /* Não corta um caractere UTF-8 no meio. */
        while (from > 0 && ((unsigned char)expr[from] & 0xC0u) == 0x80u)
            from--;
        while (to < len && ((unsigned char)expr[to] & 0xC0u) == 0x80u)
            to++;
    }
    fprintf(stderr, "%s: erro na posição %zu: %s\n", PROGRAM, display_columns(expr, 0, pos) + 1,
            err->message);
    fprintf(stderr, "  %s%.*s%s\n", from > 0 ? "..." : "", (int)(to - from), expr + from,
            to < len ? "..." : "");
    fprintf(stderr, "  %*s^\n", (int)(display_columns(expr, from, pos) + (from > 0 ? 3 : 0)), "");
}

static int evaluate(const char *expr, const options *opt)
{
    bigint v;
    calc_error err;
    bi_init(&v);
    if (calc_eval(expr, &v, opt->max_bits, &err) != 0) {
        show_error(expr, &err);
        bi_free(&v);
        return 1;
    }
    char *dec = bi_to_dec(&v);
    char *text = dec;
    char *hex = NULL;
    if (dec != NULL && opt->hex) {
        hex = bi_to_hex(&v);
        text = hex;
    }
    if (text == NULL) {
        fprintf(stderr, "%s: %s\n", PROGRAM, bi_strerror(BI_ERR_MEMORY));
        free(dec);
        bi_free(&v);
        return 1;
    }
    if (opt->hex)
        printf("%s0x%s\n", text[0] == '-' ? "-" : "", text[0] == '-' ? text + 1 : text);
    else
        printf("%s\n", text);
    if (opt->info) {
        size_t digits = strlen(dec) - (dec[0] == '-' ? 1u : 0u);
        size_t bits = bi_bit_length(&v);
        printf("(%zu dígito%s, %zu bit%s)\n", digits, digits == 1 ? "" : "s", bits,
               bits == 1 ? "" : "s");
    }
    free(dec);
    free(hex);
    bi_free(&v);
    return 0;
}

/* Lê uma linha de qualquer tamanho; devolve NULL no fim da entrada. */
static char *read_line(FILE *f)
{
    size_t cap = 256, len = 0;
    char *buf = malloc(cap);
    if (buf == NULL)
        return NULL;
    for (;;) {
        if (fgets(buf + len, (int)(cap - len), f) == NULL) {
            if (len == 0) {
                free(buf);
                return NULL;
            }
            break;
        }
        len += strlen(buf + len);
        if (len > 0 && buf[len - 1] == '\n')
            break;
        if (cap - len < 2) {
            char *p = realloc(buf, cap * 2);
            if (p == NULL) {
                free(buf);
                return NULL;
            }
            buf = p;
            cap *= 2;
        }
    }
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return buf;
}

static int parse_size(const char *s, size_t *out)
{
    char *end;
    if (s == NULL || *s < '0' || *s > '9')
        return 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (*end != '\0' || v < 64 || v > (unsigned long long)1 << 34)
        return 0;
    *out = (size_t)v;
    return 1;
}

int main(int argc, char **argv)
{
    options opt = {0, 0, CALC_DEFAULT_MAX_BITS};
    int nexpr = 0, only_expr = 0, rc = 0;

    /* Primeiro as opções; qualquer outra coisa (até "-5 + 3") é expressão. */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (only_expr)
            continue;
        if (strcmp(a, "--") == 0) {
            only_expr = 1;
        } else if (strcmp(a, "-x") == 0 || strcmp(a, "--hex") == 0) {
            opt.hex = 1;
        } else if (strcmp(a, "-i") == 0 || strcmp(a, "--info") == 0) {
            opt.info = 1;
        } else if (strcmp(a, "-l") == 0 || strcmp(a, "--limite") == 0) {
            if (i + 1 >= argc || !parse_size(argv[i + 1], &opt.max_bits)) {
                fprintf(stderr, "%s: --limite precisa de um número de bits entre 64 e 17179869184\n",
                        PROGRAM);
                return 2;
            }
            i++;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0) {
            printf("calc-gigante %s\n", BIGINT_VERSION);
            return 0;
        } else if (a[0] == '-' && a[1] == '-' && ((a[2] >= 'a' && a[2] <= 'z') || (a[2] >= 'A' && a[2] <= 'Z'))) {
            fprintf(stderr, "%s: opção inválida: %s\n", PROGRAM, a);
            fprintf(stderr, "use \"calc-gigante --help\" para ver as opções\n");
            return 2;
        }
    }
    /* Depois as expressões, na ordem em que vieram. */
    only_expr = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!only_expr) {
            if (strcmp(a, "--") == 0) {
                only_expr = 1;
                continue;
            }
            if (strcmp(a, "-x") == 0 || strcmp(a, "--hex") == 0 || strcmp(a, "-i") == 0 ||
                strcmp(a, "--info") == 0)
                continue;
            if (strcmp(a, "-l") == 0 || strcmp(a, "--limite") == 0) {
                i++;
                continue;
            }
        }
        nexpr++;
        rc |= evaluate(a, &opt);
    }
    if (nexpr == 0) {
        char *line;
        while ((line = read_line(stdin)) != NULL) {
            const char *p = line;
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p != '\0' && *p != '#')
                rc |= evaluate(line, &opt);
            free(line);
        }
    }
    return rc;
}
