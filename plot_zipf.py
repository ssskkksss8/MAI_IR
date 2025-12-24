import math
import argparse
from pathlib import Path
import matplotlib.pyplot as plt

def load_freqs(path, max_terms=None):
    freqs = []
    with open(path, "r", encoding="utf-8") as f:
        for i, line in enumerate(f):
            if max_terms is not None and i >= max_terms:
                break
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 2:
                continue
            _, cnt = parts
            try:
                cnt = int(cnt)
            except ValueError:
                continue
            if cnt > 0:
                freqs.append(cnt)
    return freqs

def fit_zipf(freqs, top_n=1000):
    n = min(len(freqs), top_n)
    xs, ys = [], []
    for rank in range(1, n + 1):
        f = freqs[rank - 1]
        xs.append(math.log(rank))
        ys.append(math.log(f))
    m = len(xs)
    sx = sum(xs)
    sy = sum(ys)
    sxx = sum(x * x for x in xs)
    sxy = sum(x * y for x, y in zip(xs, ys))
    denom = m * sxx - sx * sx
    b = (m * sxy - sx * sy) / denom if denom else -1.0
    a = (sy - b * sx) / m if m else 0.0
    return a, b, m

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--lemma", action="store_true")
    ap.add_argument("--top_fit", type=int, default=1000)
    ap.add_argument("--max_terms", type=int, default=None)
    args = ap.parse_args()

    freqs = load_freqs(args.input, args.max_terms)
    freqs.sort(reverse=True)

    a, b, used = fit_zipf(freqs, args.top_fit)
    print(f"Оценка Ципфа: log(C)={a:.6f}, s={-b:.6f}, fit_points={used}")

    ranks = range(1, len(freqs) + 1)
    log_r = [math.log(r) for r in ranks]
    log_f = [math.log(f) for f in freqs]
    zipf = [a + b * x for x in log_r]

    title = "Ципф: после лемматизации" if args.lemma else "Ципф: без лемматизации"

    plt.figure(figsize=(8, 6))
    plt.plot(log_r, log_f, ".", markersize=1, label="Корпус")
    plt.plot(log_r, zipf, "-", linewidth=2, label="Ципф (аппроксимация)")
    plt.xlabel("log(rank)")
    plt.ylabel("log(freq)")
    plt.title(title)
    plt.legend()
    plt.grid(True, which="both", linestyle="--", linewidth=0.5)
    plt.tight_layout()
    plt.savefig(args.output, dpi=200)
    print("График сохранён в:", args.output)

if __name__ == "__main__":
    main()
