# Makefile do Inteiros Gigantes.
# Variáveis que podem ser trocadas: CC, CFLAGS, LDFLAGS, BUILD, PYTHON.
#   make             compila build/calc-gigante e build/libbigint.a
#   make test        testes de unidade, diferencial contra o python3 e da linha de comando
#   make sanitize    os mesmos testes com AddressSanitizer e UBSan
#   make bench       mede o limiar da multiplicação de Karatsuba
CFLAGS ?= -O2
LDFLAGS ?=
BUILD ?= build
PYTHON ?= python3
ALL_CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Iinclude -Isrc $(CFLAGS)
HEADERS = include/bigint.h src/calc.h

all: $(BUILD)/calc-gigante $(BUILD)/libbigint.a

$(BUILD)/%.o: src/%.c $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BUILD)/libbigint.a: $(BUILD)/bigint.o
	$(AR) rcs $@ $(BUILD)/bigint.o

$(BUILD)/calc-gigante: $(BUILD)/main.o $(BUILD)/calc.o $(BUILD)/libbigint.a
	$(CC) $(CFLAGS) $(BUILD)/main.o $(BUILD)/calc.o $(BUILD)/libbigint.a $(LDFLAGS) -o $@

$(BUILD)/test_bigint: tests/test_bigint.c tests/teste.h $(BUILD)/calc.o $(BUILD)/libbigint.a
	$(CC) $(ALL_CFLAGS) tests/test_bigint.c $(BUILD)/calc.o $(BUILD)/libbigint.a $(LDFLAGS) -o $@

$(BUILD)/oraculo: tests/oraculo.c $(BUILD)/calc.o $(BUILD)/libbigint.a
	$(CC) $(ALL_CFLAGS) tests/oraculo.c $(BUILD)/calc.o $(BUILD)/libbigint.a $(LDFLAGS) -o $@

$(BUILD)/bench_karatsuba: tests/bench_karatsuba.c $(BUILD)/libbigint.a
	$(CC) $(ALL_CFLAGS) tests/bench_karatsuba.c $(BUILD)/libbigint.a $(LDFLAGS) -o $@

test: $(BUILD)/test_bigint $(BUILD)/oraculo $(BUILD)/calc-gigante
	./$(BUILD)/test_bigint
	@if command -v $(PYTHON) >/dev/null 2>&1; then \
		$(PYTHON) tests/diferencial.py ./$(BUILD)/oraculo; \
	else \
		echo "python3 não encontrado: teste diferencial ignorado"; \
	fi
	sh tests/cli.sh ./$(BUILD)/calc-gigante

sanitize:
	$(MAKE) BUILD=$(BUILD)/sanitize CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" test

bench: $(BUILD)/bench_karatsuba
	./$(BUILD)/bench_karatsuba

clean:
	rm -rf $(BUILD)

.PHONY: all test sanitize bench clean
