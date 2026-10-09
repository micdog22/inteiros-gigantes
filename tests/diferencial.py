#!/usr/bin/env python3
"""Teste diferencial: confere milhares de operações do oráculo em C com o python3.

O Python tem inteiros de precisão arbitrária nativos, então serve de
referência. As operações são sorteadas com semente fixa (o teste é sempre o
mesmo), com números de até 5000 dígitos e padrões difíceis para os limbs
(todos os bits 1, potências de 2, vizinhos de 2^(32k)).

Uso: python3 tests/diferencial.py <caminho do oraculo>
"""
import math
import random
import subprocess
import sys

SEED = 2026
MAX_DIGITS = 5000

SMALL_PRIMES = [p for p in range(2, 1000) if all(p % d for d in range(2, int(p ** 0.5) + 1))]


def reference_is_prime(n, rng):
    """Miller-Rabin com 40 bases aleatórias (independente do código em C)."""
    if n < 2:
        return False
    for p in SMALL_PRIMES:
        if n % p == 0:
            return n == p
    d, s = n - 1, 0
    while d % 2 == 0:
        d //= 2
        s += 1
    for _ in range(40):
        a = rng.randrange(2, n - 1)
        x = pow(a, d, n)
        if x in (1, n - 1):
            continue
        for _ in range(s - 1):
            x = x * x % n
            if x == n - 1:
                break
        else:
            return False
    return True


class Generator:
    def __init__(self, seed):
        self.rng = random.Random(seed)
        self.ref_rng = random.Random(seed + 1)

    def digits(self, n):
        if n <= 1:
            return self.rng.randrange(10)
        return self.rng.randrange(10 ** (n - 1), 10 ** n)

    def size(self, limit=MAX_DIGITS):
        r = self.rng.random()
        if r < 0.45:
            n = self.rng.randint(1, 30)
        elif r < 0.80:
            n = self.rng.randint(30, 400)
        elif r < 0.95:
            n = self.rng.randint(400, 2000)
        else:
            n = self.rng.randint(2000, MAX_DIGITS)
        return min(n, limit)

    def special(self):
        """Valores que estressam o vai-um e as bordas entre limbs."""
        k = self.rng.randint(1, 40)
        choice = self.rng.randrange(6)
        if choice == 0:
            return 2 ** (32 * k) - 1
        if choice == 1:
            return 2 ** (32 * k)
        if choice == 2:
            return 2 ** (32 * k) + 1
        if choice == 3:
            return (2 ** (32 * k) - 1) << (32 * self.rng.randint(0, 5))
        if choice == 4:
            return 2 ** self.rng.randint(1, 32 * k)
        return 10 ** self.rng.randint(1, 300)

    def integer(self, negative=True, limit=MAX_DIGITS):
        if self.rng.random() < 0.15:
            v = self.special()
            if len(str(v)) > limit:
                v = self.digits(limit)
        else:
            v = self.digits(self.size(limit))
        if negative and self.rng.random() < 0.5:
            v = -v
        return v

    def prime(self, digits):
        while True:
            n = self.digits(digits) | 1
            if reference_is_prime(n, self.ref_rng):
                return n

    def expression(self, depth):
        """Devolve o par (texto para o calc-gigante, texto para o Python)."""
        rng = self.rng
        if depth <= 0 or rng.random() < 0.25:
            v = rng.choice([rng.randint(0, 9), rng.randint(0, 10 ** 6), self.digits(rng.randint(1, 60))])
            if rng.random() < 0.1:
                return hex(v), hex(v)
            return str(v), str(v)
        kind = rng.random()
        if kind < 0.50:
            op = rng.choice(["+", "-", "*", "/", "%"])
            a, pa = self.expression(depth - 1)
            b, pb = self.expression(depth - 1)
            pyop = "//" if op == "/" else op
            if rng.random() < 0.5:
                return "(%s) %s (%s)" % (a, op, b), "(%s) %s (%s)" % (pa, pyop, pb)
            return "%s %s %s" % (a, op, b), "%s %s %s" % (pa, pyop, pb)
        if kind < 0.60 and depth <= 2:
            # Potência só perto das folhas: no máximo duas potências aninhadas,
            # para os valores intermediários caberem no limite da calculadora.
            a, pa = self.expression(depth - 1)
            e = rng.randint(0, 8)
            return "(%s)^%d" % (a, e), "(%s)**%d" % (pa, e)
        if kind < 0.68:
            a, pa = self.expression(depth - 1)
            return "-%s" % a, "-%s" % pa
        if kind < 0.74:
            k = rng.randint(0, 25)
            return "%d!" % k, "math.factorial(%d)" % k
        if kind < 0.82:
            a, pa = self.expression(depth - 1)
            b, pb = self.expression(depth - 1)
            return "mdc(%s; %s)" % (a, b), "math.gcd(%s, %s)" % (pa, pb)
        if kind < 0.90:
            a, pa = self.expression(depth - 1)
            return "raiz(%s)" % a, "math.isqrt(%s)" % pa
        if kind < 0.96:
            a, pa = self.expression(depth - 1)
            e, m = rng.randint(0, 10 ** 12), rng.randint(1, 10 ** 15)
            return "potmod(%s; %d; %d)" % (a, e, m), "pow(%s, %d, %d)" % (pa, e, m)
        k = rng.randint(0, 10 ** 6)
        return "primo(%d)" % k, "primo(%d)" % k


def trunc_divmod(a, b):
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    return q, a - q * b


def to_hex(a):
    return ("-" if a < 0 else "") + format(abs(a), "x")


def build_cases(gen):
    rng = gen.rng
    cases = []

    def add(line, expected):
        cases.append((line, expected))

    for _ in range(500):
        a, b = gen.integer(), gen.integer()
        add("add %d %d" % (a, b), str(a + b))
        add("sub %d %d" % (a, b), str(a - b))
    for _ in range(500):
        a, b = gen.integer(), gen.integer()
        add("mul %d %d" % (a, b), str(a * b))
    for _ in range(150):
        a, b = gen.integer(limit=1500), gen.integer(limit=1500)
        add("smul %d %d" % (a, b), str(a * b))
    for _ in range(300):
        a, b = gen.integer(limit=2000), gen.integer(limit=2000)
        t = rng.randint(2, 64)
        add("kmul %d %d %d" % (a, b, t), str(a * b))
    for _ in range(700):
        a = gen.integer()
        b = gen.integer(limit=rng.choice([20, 300, 3000, MAX_DIGITS]))
        if rng.random() < 0.03:
            b = 0
        if b == 0:
            add("divmod %d %d" % (a, b), "ERRO")
            add("fdivmod %d %d" % (a, b), "ERRO")
            continue
        q, r = trunc_divmod(a, b)
        add("divmod %d %d" % (a, b), "%d %d" % (q, r))
        q, r = divmod(a, b)
        add("fdivmod %d %d" % (a, b), "%d %d" % (q, r))
    for _ in range(200):
        a, e = gen.integer(limit=50), rng.randint(0, 200)
        add("pow %d %d" % (a, e), str(a ** e))
    for _ in range(150):
        a = gen.integer(limit=600)
        e = gen.integer(negative=False, limit=600)
        m = gen.integer(negative=False, limit=600)
        if m == 0:
            m = 1
        add("powmod %d %d %d" % (a, e, m), str(pow(a, e, m)))
    add("powmod 5 3 0", "ERRO")
    add("powmod 5 -3 7", "ERRO")
    for _ in range(300):
        g = gen.integer(negative=False, limit=1500)
        a, b = gen.integer(limit=2500) * g, gen.integer(limit=2500) * g
        add("gcd %d %d" % (a, b), str(math.gcd(a, b)))
    for _ in range(300):
        a = gen.integer(negative=False)
        if rng.random() < 0.3:
            r = gen.integer(negative=False, limit=2500)
            a = r * r + rng.choice([-1, 0, 1]) if r > 0 else r * r
        add("isqrt %d" % a, str(math.isqrt(a)))
    add("isqrt -1", "ERRO")
    for _ in range(60):
        n = rng.randint(0, 1000) if rng.random() < 0.8 else rng.randint(1000, 3000)
        add("fact %d" % n, str(math.factorial(n)))
    for _ in range(300):
        a, k = gen.integer(limit=3000), rng.randint(0, 3000)
        add("shl %d %d" % (a, k), str(a << k))
        add("shr %d %d" % (a, k), str(a >> k))
    for _ in range(300):
        a, b = gen.integer(limit=3000), gen.integer(limit=3000)
        for op, f in (("and", lambda x, y: x & y), ("or", lambda x, y: x | y), ("xor", lambda x, y: x ^ y)):
            expected = "ERRO" if a < 0 or b < 0 else str(f(a, b))
            add("%s %d %d" % (op, a, b), expected)
    for _ in range(300):
        a = gen.integer()
        add("hex %d" % a, to_hex(a))
        text = format(abs(a), "x")
        if rng.random() < 0.5:
            text = text.upper()
        if rng.random() < 0.5:
            text = "0x" + text
        if a < 0:
            text = "-" + text
        add("fromhex %s" % text, str(a))
    for _ in range(300):
        a = gen.integer()
        b = a if rng.random() < 0.2 else gen.integer()
        add("cmp %d %d" % (a, b), str((a > b) - (a < b)))

    # Primalidade: primos e compostos construídos (a resposta é conhecida).
    for _ in range(60):
        p = gen.prime(rng.randint(2, 120))
        add("prime %d" % p, "1")
    for _ in range(60):
        p, q = gen.prime(rng.randint(2, 60)), gen.prime(rng.randint(2, 60))
        add("prime %d" % (p * q), "0")
    for _ in range(100):
        n = gen.digits(rng.randint(1, 80))
        add("prime %d" % n, "1" if reference_is_prime(n, gen.ref_rng) else "0")
    for n in (561, 1105, 1729, 2465, 2821, 6601, 8911, 2047, 3215031751,
              3825123056546413051, 318665857834031151167461, 3317044064679887385961981):
        add("prime %d" % n, "0")

    # Expressões no avaliador (a gramática precisa concordar com a do Python).
    names = {"math": math, "pow": pow,
             "primo": lambda n: int(reference_is_prime(n, gen.ref_rng))}
    for _ in range(700):
        ours, py = gen.expression(rng.randint(1, 5))
        try:
            expected = str(eval(py, {"__builtins__": {}}, names))
        except (ZeroDivisionError, ValueError):
            expected = "ERRO"
        add("calc %s" % ours, expected)
    return cases


def main():
    if len(sys.argv) != 2:
        print("uso: python3 tests/diferencial.py <caminho do oraculo>", file=sys.stderr)
        return 2
    if hasattr(sys, "set_int_max_str_digits"):
        sys.set_int_max_str_digits(0)   # o Python 3.11+ limita int <-> str a 4300 dígitos
    cases = build_cases(Generator(SEED))
    data = "".join(line + "\n" for line, _ in cases)
    proc = subprocess.run([sys.argv[1]], input=data, capture_output=True, text=True, timeout=900)
    if proc.returncode != 0:
        print("o oráculo terminou com código %d" % proc.returncode)
        print(proc.stderr[-2000:])
        return 1
    outputs = proc.stdout.split("\n")
    failures = 0
    per_op = {}
    for i, (line, expected) in enumerate(cases):
        op = line.split(" ", 1)[0]
        got = outputs[i] if i < len(outputs) else "(sem saída)"
        total, bad = per_op.get(op, (0, 0))
        if got != expected:
            failures += 1
            bad += 1
            if failures <= 10:
                short = line if len(line) < 160 else line[:157] + "..."
                print("DIVERGÊNCIA: %s" % short)
                print("  esperado: %s" % (expected[:120]))
                print("  obtido:   %s" % (got[:120]))
        per_op[op] = (total + 1, bad)
    summary = ", ".join("%s %d" % (op, n) for op, (n, _) in sorted(per_op.items()))
    print("operações: " + summary)
    print("%d operações conferidas com o python3, %d divergências" % (len(cases), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
