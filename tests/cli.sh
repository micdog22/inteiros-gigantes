#!/bin/sh
# Testes da linha de comando do calc-gigante (saídas e códigos de saída).
# Uso: sh tests/cli.sh <caminho do calc-gigante>
set -u
CALC=$1
pass=0
fail=0
ok() { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); echo "  FALHOU: $*"; }

expect() {
    # expect "<expressão>" "<saída esperada>"
    out=$("$CALC" "$1" 2>&1)
    if [ $? -eq 0 ] && [ "$out" = "$2" ]; then ok; else bad "$1 -> $out (esperado $2)"; fi
}

expect "2^521 - 1" "6864797660130609714981900799081393217269435300143305409394463459185543183397656052122559640661454554977296311391480858037121987999716643812574028291115057151"
expect "100! / 98!" "9900"
expect "-7 / 2" "-4"
expect "primo(2^127 - 1)" "1"
expect "potmod(3; 10^100; 1000000007)" "9102203"
expect "mdc(2^100; 6^50)" "1125899906842624"

out=$("$CALC" "100!" | wc -c | tr -d ' ')
if [ "$out" = "159" ]; then ok; else bad "100! devia ter 158 dígitos (+ quebra de linha), veio $out"; fi

out=$("$CALC" --hex "255" "-255")
if [ "$out" = "0xff
-0xff" ]; then ok; else bad "--hex: $out"; fi

out=$("$CALC" --info "2^521 - 1" | tail -1)
if [ "$out" = "(157 dígitos, 521 bits)" ]; then ok; else bad "--info: $out"; fi

# Expressão que começa com sinal não é confundida com opção.
expect "-5 + 3" "-2"

# Erros: mensagem, posição marcada e código de saída 1.
out=$("$CALC" "2 * (3 + 4" 2>&1)
rc=$?
if [ $rc -eq 1 ] && echo "$out" | grep -q "faltou fechar o parêntese"; then ok; else bad "parêntese aberto: $out"; fi
if echo "$out" | tail -1 | grep -q "^            ^$"; then ok; else bad "posição do ^: $out"; fi
out=$("$CALC" "1 / 0" 2>&1)
if [ $? -eq 1 ] && echo "$out" | grep -q "divisão por zero"; then ok; else bad "divisão por zero: $out"; fi
out=$("$CALC" "2^1000" --limite 512 2>&1)
if [ $? -eq 1 ] && echo "$out" | grep -q "grande demais"; then ok; else bad "--limite: $out"; fi
"$CALC" --nada >/dev/null 2>&1
if [ $? -eq 2 ]; then ok; else bad "opção inválida devia dar código 2"; fi

# Entrada padrão: uma expressão por linha, comentários ignorados.
out=$(printf '# comentário\n1 + 1\n\n2^10\n' | "$CALC")
if [ "$out" = "2
1024" ]; then ok; else bad "entrada padrão: $out"; fi
printf '1 + 1\n1 / 0\n3\n' | "$CALC" >/dev/null 2>&1
if [ $? -eq 1 ]; then ok; else bad "erro numa linha devia dar código 1"; fi

if "$CALC" --help | grep -q "uso: calc-gigante"; then ok; else bad "--help"; fi

echo "$pass verificações da linha de comando, $fail falhas"
[ "$fail" -eq 0 ]
