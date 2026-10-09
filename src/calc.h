/*
 * calc.h: avaliador de expressões com inteiros gigantes.
 *
 * Gramática (descida recursiva, da menor para a maior precedência):
 *
 *   expressão := termo (('+' | '-') termo)*
 *   termo     := unário (('*' | '/' | '%') unário)*
 *   unário    := ('-' | '+') unário | potência
 *   potência  := pósfixo ('^' unário)?          (associa à direita)
 *   pósfixo   := primário '!'*
 *   primário  := número | '(' expressão ')' | nome '(' argumentos ')'
 *   argumentos:= expressão (';' expressão)*
 *
 * Assim -2^2 = -(2^2) = -4 e 2^3^2 = 2^(3^2) = 512, como na matemática.
 * A divisão e o resto arredondam para baixo (como no Python).
 */
#ifndef CALC_H
#define CALC_H

#include <stddef.h>

#include "bigint.h"

/* Limite padrão do tamanho de qualquer resultado: 2^20 bits (cerca de 315 mil dígitos). */
#define CALC_DEFAULT_MAX_BITS ((size_t)1 << 20)

typedef struct {
    size_t pos;           /* posição na expressão onde o erro foi notado */
    char message[200];    /* mensagem em português */
} calc_error;

/* Avalia expr e guarda o valor em result. Devolve 0 se deu certo e 1 se
   houve erro (detalhes em err). */
int calc_eval(const char *expr, bigint *result, size_t max_bits, calc_error *err);

#endif /* CALC_H */
