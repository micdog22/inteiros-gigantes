# Inteiros Gigantes

Aritmética de precisão arbitrária em C: uma biblioteca de inteiros sem limite de tamanho (soma, multiplicação de Karatsuba, divisão pelo Algoritmo D de Knuth, Miller-Rabin e mais) e a calculadora `calc-gigante`, que faz `2^521 - 1` e `100!` sem piscar. Escrita do zero, em C11 puro, para quem quer entender o que acontece por baixo do `int` do Python ou do `BigInteger` do Java.

## Recursos

- **Representação em sinal e magnitude** com limbs de 32 bits e contas intermediárias de 64 bits; o zero nunca é negativo.
- **Conversão** de e para decimal (em pedaços de 9 dígitos, base 10^9) e hexadecimal.
- **Soma, subtração e comparação** com vai-um e empresta-um em 64 bits.
- **Multiplicação escolar e de Karatsuba**, com o limiar medido nesta máquina (`make bench`), inclusive para fatores de tamanhos muito diferentes.
- **Divisão pelo Algoritmo D de Knuth**, truncada (como em C) e com piso (como no Python).
- **Potência, potência modular** (quadrado e multiplicação), **mdc** (Euclides), **raiz quadrada inteira** (Newton) e **fatorial** (árvore de produtos).
- **Deslocamentos e operações bit a bit** (AND, OR, XOR para não negativos).
- **Miller-Rabin** determinístico abaixo de 3,3 × 10^24 e com 24 bases extras acima disso.
- **Calculadora `calc-gigante`** com `+ - * / % ^ !`, parênteses, `mdc`, `raiz`, `primo` e `potmod`, feita com um analisador de descida recursiva e erros em português que apontam a posição do problema.
- **Testada contra o python3:** mais de 7600 operações sorteadas, com números de até 5000 dígitos.

## Como usar

```
$ calc-gigante "2^521 - 1"
6864797660130609714981900799081393217269435300143305409394463459185543183397656052122559640661454554977296311391480858037121987999716643812574028291115057151

$ calc-gigante "100!"
93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000

$ calc-gigante --info "2^521 - 1" | tail -1
(157 dígitos, 521 bits)

$ calc-gigante "primo(2^127 - 1)" "primo(2^127 + 1)"
1
0

$ calc-gigante "potmod(3; 10^100; 1000000007)" "mdc(2^100; 6^50)" "raiz(2 * 10^40)"
9102203
1125899906842624
141421356237309504880

$ calc-gigante --hex "2^64 - 1"
0xffffffffffffffff
```

Sem argumentos, a calculadora lê uma expressão por linha (linhas vazias e começadas por `#` são ignoradas):

```
$ printf '2^10\n10! / 8!\n' | calc-gigante
1024
90
```

Os erros dizem o que houve e onde:

```
$ calc-gigante "2 * (3 + 4"
calc-gigante: erro na posição 11: faltou fechar o parêntese aberto na posição 5
  2 * (3 + 4
            ^

$ calc-gigante "mdc(12, 18)"
calc-gigante: erro na posição 7: use ';' para separar os argumentos (a vírgula é o separador decimal)
  mdc(12, 18)
        ^
```

**Regras da calculadora:**

- `/` e `%` arredondam para baixo, como no Python: `-7 / 2` dá `-4` e `-7 % 2` dá `1`.
- `^` associa à direita (`2^3^2` é `2^9`) e vale mais que o sinal (`-2^2` é `-4`).
- `primo(n)` devolve 1 ou 0 e aceita até 4096 bits; números em hexadecimal começam com `0x`.
- Nenhum resultado pode passar de 2^20 bits (cerca de 315 mil dígitos), porque a conversão para decimal é quadrática. Para mudar: `--limite <bits>`.

```
opções: -x/--hex  -i/--info  -l/--limite BITS  -h/--help  -V/--version
```

### Como biblioteca

Copie `include/bigint.h` e `src/bigint.c` para o seu projeto (não há outras dependências):

```c
#include "bigint.h"

bigint a, b;
bi_init(&a);
bi_init(&b);
bi_from_dec(&a, "123456789012345678901234567890");
bi_set_u64(&b, 2);
bi_pow(&b, &b, 100);          /* b = 2^100 */
bi_mul(&a, &a, &b);           /* o resultado pode ser um dos operandos */
char *texto = bi_to_dec(&a);
puts(texto);
free(texto);
bi_free(&a);
bi_free(&b);
```

Toda função devolve um `bi_status` (`BI_OK`, `BI_ERR_DIV_ZERO`, `BI_ERR_MEMORY`...) e `bi_strerror` traduz o código para uma mensagem em português.

## Como compilar

Precisa de um compilador C11 (clang ou gcc) e `make` ou `cmake`. Testado no macOS com clang; o código usa só a biblioteca padrão do C.

```sh
make                 # gera build/calc-gigante e build/libbigint.a
```

Ou com CMake:

```sh
cmake -S . -B build
cmake --build build
```

## Testes

```sh
make test            # unidade + diferencial contra o python3 + linha de comando
make sanitize        # os mesmos testes com AddressSanitizer e UndefinedBehaviorSanitizer
make bench           # mede o limiar de Karatsuba nesta máquina
ctest --test-dir build --output-on-failure   # depois do build com CMake
```

- **Unidade** (`tests/test_bigint.c`, 261 verificações): vetores para cada operação; zero e a normalização do "-0"; vai-um atravessando todos os limbs (`2^(32k) - 1 + 1`); divisão por zero; divisor maior que o dividendo; casos da literatura que forçam a correção da estimativa e o raro passo "soma de volta" do Algoritmo D; Karatsuba comparada com a escolar para mais de mil pares de tamanhos (perto do limiar, desequilibrados, com todos os bits 1); números de Carmichael e pseudoprimos fortes; o avaliador de expressões e as posições dos erros.
- **Diferencial** (`tests/diferencial.py`): 7695 operações sorteadas com semente fixa, com números de até 5000 dígitos, conferidas com o python3: soma, subtração, multiplicação (as três versões), divisão truncada e com piso, potência, potência modular, mdc, raiz, fatorial, deslocamentos, AND/OR/XOR, hexadecimal, comparação, primalidade e 700 expressões aleatórias na calculadora.
- **Linha de comando** (`tests/cli.sh`): saídas, opções, erros e códigos de saída.

## Como funciona

### Limbs: números na base 2^32

Um inteiro gigante é guardado como um vetor de "dígitos" na base 2^32, chamados de limbs, do menos para o mais significativo, mais um sinal à parte:

```
      123456789012345678901234567890
    = 1 * (2^32)^3 + 2397638646 * (2^32)^2 + 3279151342 * 2^32 + 1312754386

limb:    [0]          [1]          [2]          [3]
       1312754386   3279151342   2397638646   1          neg = 0, len = 4
```

Por que 32 bits e não 64? Porque o produto de dois limbs de 32 bits cabe exatamente em 64 bits, e o C11 tem `uint64_t` em todo lugar. Com limbs de 64 bits precisaríamos de inteiros de 128 bits, que não fazem parte do padrão.

O zero tem `len == 0` e o sinal é sempre limpo depois de cada operação, então "-0" não existe (`5 - 5`, `(-3) * 0` e `bi_from_dec("-0")` dão o mesmo zero).

### Soma e o vai-um

É a soma do papel, coluna por coluna. Em 64 bits, `a[i] + b[i] + vai_um` nunca estoura: o resultado fica nos 32 bits de baixo e o vai-um são os de cima. Na subtração, se `a[i] - b[i] - empresta` ficar negativo, a conta em 64 bits sem sinal "dá a volta" e o bit 63 liga, que é exatamente o empresta-um da próxima coluna.

```
   FFFFFFFF FFFFFFFF FFFFFFFF      (2^96 - 1)
 +                          1
 ---------------------------
 1 00000000 00000000 00000000      o vai-um atravessa todos os limbs
```

### Multiplicação: escolar e Karatsuba

A escolar multiplica cada limb de um número por todos do outro: n^2 produtos. Em 1960, Karatsuba percebeu que dá para trocar uma multiplicação por algumas somas. Partindo cada número ao meio (B = 2^(32m)):

```
a = a1*B + a0          b = b1*B + b0

a*b = z2*B^2 + z1*B + z0, onde
  z0 = a0*b0
  z2 = a1*b1
  z1 = (a0 + a1)*(b0 + b1) - z0 - z2     <- só uma multiplicação nova
```

São 3 multiplicações de metade do tamanho em vez de 4, aplicadas recursivamente: o custo cai de O(n^2) para O(n^1,585). Mas as somas extras têm custo, então para números pequenos a escolar ainda ganha. O `make bench` mede onde está o ponto de virada:

```
 limbs   escolar (us)  1 nível (us) Karatsuba (us)
    32           0.45           0.49           0.45
    40           0.76           0.77           0.77
    48           1.17           1.05           1.05  <- Karatsuba ganha
    64           2.21           1.58           1.91  <- Karatsuba ganha
   128          10.75           6.91           5.51  <- Karatsuba ganha
   256          46.22          39.57          20.43  <- Karatsuba ganha
   512         190.97         140.62          48.69  <- Karatsuba ganha
```

Neste Mac (Apple Silicon, clang -O2) a virada fica em 48 limbs (1536 bits), que é o valor de `BI_KARATSUBA_THRESHOLD`. Com 512 limbs (cerca de 4900 dígitos), a Karatsuba completa já é quase 4 vezes mais rápida. Quando um fator é muito maior que o outro, o maior é fatiado em pedaços do tamanho do menor.

### Divisão: o Algoritmo D de Knuth

É a divisão longa da escola, mas cada "dígito" é um limb. A parte difícil é adivinhar cada dígito do quociente sem tentar os 2^32 valores. O algoritmo (do volume 2 de *The Art of Computer Programming*) faz assim:

1. **Normaliza (D1):** desloca dividendo e divisor para que o limb mais alto do divisor tenha o bit 31 ligado. O quociente não muda, e o resto é desfeito no fim.
2. **Estima (D3):** chuta o próximo dígito dividindo os dois limbs mais altos do resto parcial pelo limb mais alto do divisor:

   ```
   q^ = (u[j+n] * 2^32 + u[j+n-1]) / v[n-1]
   ```

   Com o divisor normalizado, Knuth prova que q^ nunca é menor que o dígito certo e passa dele por no máximo 2. Um teste com o segundo limb do divisor (`q^ * v[n-2] > r^ * 2^32 + u[j+n-2]`) corrige quase todos os chutes errados.
3. **Multiplica e subtrai (D4):** tira `q^ * divisor` do resto parcial.
4. **Soma de volta (D5 e D6):** se a subtração ficou negativa, q^ era 1 a mais: decrementa e soma o divisor de volta. Isso acontece com probabilidade de uns 2 em 2^32, então os testes usam casos construídos de propósito para passar por aqui.

Duas convenções de divisão estão disponíveis: `bi_divmod` trunca em direção a zero (como C e Java: `-7 / 2 = -3`, resto `-1`) e `bi_fdivmod` arredonda para baixo (como Python: `-7 // 2 = -4`, resto `1`). As duas satisfazem `a = q * b + r`; muda só o sinal do resto.

### Conversão para decimal

Para imprimir, o número é dividido repetidamente por 10^9 (o maior 10^k que cabe num limb). Cada resto são 9 dígitos decimais, do fim para o começo. Cada divisão por um limb é uma passada O(n), e são necessárias cerca de n delas: a conversão é O(n^2). É por isso que a calculadora limita os resultados a 2^20 bits por padrão.

### Potências, mdc, raiz e fatorial

- **Quadrado e multiplicação:** para a^13, como 13 = 1101 em binário, a^13 = a^8 * a^4 * a^1. Basta elevar ao quadrado repetidamente e multiplicar quando o bit é 1: cerca de log2(e) passos em vez de e.
- **Potência modular:** a mesma ideia, reduzindo módulo m a cada passo, então os números nunca passam do dobro do tamanho de m. É assim que `potmod(3; 10^100; 1000000007)` sai na hora, com um expoente de 100 dígitos.
- **mdc:** algoritmo de Euclides, `mdc(a, b) = mdc(b, a mod b)`. Números de Fibonacci consecutivos são o pior caso (e estão nos testes).
- **Raiz quadrada inteira:** método de Newton, `x' = (x + n / x) / 2`, começando de uma potência de 2 acima da raiz. A sequência desce até parar, e o ponto de parada é o piso da raiz.
- **Fatorial:** em vez de multiplicar 1 * 2 * 3 * ... (um número enorme vezes um pequeno, sempre), o produto é dividido ao meio recursivamente, `prod(1..n) = prod(1..n/2) * prod(n/2+1..n)`. Os fatores ficam com tamanhos parecidos e a Karatsuba trabalha: `10000!` (35660 dígitos) sai em 20 milissegundos.

### Miller-Rabin

Pelo pequeno teorema de Fermat, se n é primo então a^(n-1) = 1 (mod n). Mas alguns compostos, os números de Carmichael como 561 = 3 * 11 * 17, passam nesse teste para quase toda base. O teste forte de Miller-Rabin olha mais fundo: escreva n - 1 = d * 2^s com d ímpar. Se n é primo, a sequência

```
a^d,  a^(2d),  a^(4d),  ...,  a^(2^s * d)   (mod n)
```

termina em 1, e como as únicas raízes quadradas de 1 módulo um primo são 1 e -1, ou ela já começa em 1, ou passa por -1 (n - 1) antes de chegar em 1. Um composto passa nessa checagem para no máximo 1/4 das bases (teorema de Rabin), e quase sempre bem menos.

O detalhe que torna o teste exato para números "pequenos": Sorenson e Webster (2015) mostraram que, para n < 3.317.044.064.679.887.385.961.981 (cerca de 3,3 × 10^24), testar as 13 bases 2, 3, 5, ..., 41 basta. Esse limite não é folga: o próprio 3317044064679887385961981 é composto e engana as 13 bases (o teste `test_primes` confere isso). Acima do limite, a biblioteca soma 24 bases pseudoaleatórias derivadas do próprio n (então a resposta é sempre a mesma), e a chance de um composto passar fica abaixo de 4^-24, cerca de 3,6 × 10^-15. Antes de tudo, a divisão pelos primos até 997 descarta a maioria dos compostos na hora.

### O avaliador de expressões

A calculadora usa descida recursiva: cada nível de precedência da gramática vira uma função, e cada função chama a do nível mais forte.

```
expressão := termo (('+' | '-') termo)*
termo     := unário (('*' | '/' | '%') unário)*
unário    := ('-' | '+') unário | potência
potência  := pósfixo ('^' unário)?
pósfixo   := primário '!'*
primário  := número | '(' expressão ')' | função '(' argumentos ')'
```

Em `1 + 2 * 3`, `expressão` pede um termo (1), vê o `+` e pede outro termo, que já volta pronto como `2 * 3 = 6`: a precedência sai da própria estrutura das chamadas. A potência chama `unário` à direita, o que a deixa associativa à direita e faz `-2^2` virar `-(2^2)`. Antes de multiplicar ou elevar, a calculadora estima o tamanho do resultado e recusa na hora o que passaria do limite, em vez de travar calculando.

### Mapa do código

```
include/bigint.h          API pública da biblioteca
src/bigint.c              limbs, soma, Karatsuba, Algoritmo D, potências, Miller-Rabin
src/calc.c, src/calc.h    o avaliador de expressões (descida recursiva)
src/main.c                o programa calc-gigante
tests/test_bigint.c       testes de unidade
tests/oraculo.c           executa operações lidas da entrada (para o teste diferencial)
tests/diferencial.py      sorteia operações e confere com o python3
tests/cli.sh              testes da linha de comando
tests/bench_karatsuba.c   medição do limiar de Karatsuba
```

## Limitações

- A conversão para decimal e a divisão são quadráticas. Bibliotecas de produção como o GMP usam algoritmos subquadráticos (divisão de Newton, Toom-Cook, FFT) e assembly; aqui o objetivo é que o código caiba na cabeça de quem lê.
- `primo()` aceita até 4096 bits na calculadora (um primo de 3217 bits leva uns 2 segundos). A biblioteca não tem esse limite.
- As funções não são seguras para criptografia: o tempo de execução depende dos valores (ataques de temporização) e a memória não é apagada ao liberar.

## Contribuindo

Issues e pull requests são bem-vindos.

## Licença

MIT. Veja [LICENSE](LICENSE).
