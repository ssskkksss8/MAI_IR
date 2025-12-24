import json
import re
import time
import argparse
from pathlib import Path
from collections import Counter

LETTER = "A-Za-zА-Яа-яЁё0-9"
LEMMA_RE = re.compile(rf"[{LETTER}]+(?:-[{LETTER}]+)*")

def tokenize_for_lemma(text, split_hyphen=True, normalize_yo=True, min_len=1):
    for m in LEMMA_RE.finditer(text):
        tok = m.group(0).lower()
        if normalize_yo:
            tok = tok.replace("ё", "е")
        if split_hyphen and "-" in tok:
            for part in tok.split("-"):
                if len(part) >= min_len:
                    yield part
        else:
            if len(tok) >= min_len:
                yield tok

def process_batches(batch_paths, freqs_out_path="term_freqs.tsv", min_len=1, split_hyphen=True, normalize_yo=True):
    total_bytes = 0
    total_tokens = 0
    total_len = 0
    freqs = Counter()

    t0 = time.perf_counter()

    for path in batch_paths:
        print("Обрабатываю батч:", path)
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except ValueError:
                    continue
                text = obj.get("text", "")
                if not text:
                    continue

                total_bytes += len(text.encode("utf-8"))

                for tok in tokenize_for_lemma(text, split_hyphen=split_hyphen, normalize_yo=normalize_yo, min_len=min_len):
                    total_tokens += 1
                    total_len += len(tok)
                    freqs[tok] += 1

    dt = time.perf_counter() - t0

    avg_len = (float(total_len) / total_tokens) if total_tokens else 0.0
    kb = total_bytes / 1024.0
    speed = kb / dt if dt > 0 else 0.0

    print("----- СТАТИСТИКА -----")
    print("Батчей:", len(batch_paths))
    print("Объём входного текста: {:.1f} КБ".format(kb))
    print("Всего токенов:", total_tokens)
    print("Средняя длина токена: {:.3f}".format(avg_len))
    print("Время токенизации: {:.3f} с".format(dt))
    print("Скорость токенизации: {:.1f} КБ/с".format(speed))

    with open(freqs_out_path, "w", encoding="utf-8") as out:
        for term, cnt in freqs.most_common():
            out.write(f"{term}\t{cnt}\n")

    print("Частоты терминов сохранены в", freqs_out_path)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glob", default="batch_*.jsonl")
    ap.add_argument("--out", default="term_freqs.tsv")
    ap.add_argument("--min_len", type=int, default=1)
    ap.add_argument("--no_split_hyphen", action="store_true")
    ap.add_argument("--no_normalize_yo", action="store_true")
    args = ap.parse_args()

    batch_files = sorted(Path("../corpus_aviation/").glob(args.glob))
    if not batch_files:
        print(f"Файлов по шаблону {args.glob} не найдено.")
        return

    process_batches(
        batch_files,
        freqs_out_path=args.out,
        min_len=args.min_len,
        split_hyphen=not args.no_split_hyphen,
        normalize_yo=not args.no_normalize_yo,
    )

if __name__ == "__main__":
    main()
