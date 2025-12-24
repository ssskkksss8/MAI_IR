import json
from pathlib import Path
import time
import pymorphy2
from tokenize_corpus import TOKEN_RE, tokenize

morph = pymorphy2.MorphAnalyzer()

def lemmatize_token(tok):
    if any("а" <= ch <= "я" or "ё" == ch for ch in tok):
        p = morph.parse(tok)[0]
        return p.normal_form
    else:
        return tok

def build_lemma_freqs(batch_paths, max_docs=None):
    lemma_freqs = {}
    base_freqs = {}
    doc_count = 0

    t0 = time.perf_counter()

    for path in batch_paths:
        print("Обрабатываю батч:", path)
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                if max_docs is not None and doc_count >= max_docs:
                    break
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

                for tok in tokenize(text):
                    base_freqs[tok] = base_freqs.get(tok, 0) + 1
                    lemma = lemmatize_token(tok)
                    lemma_freqs[lemma] = lemma_freqs.get(lemma, 0) + 1

                doc_count += 1
            if max_docs is not None and doc_count >= max_docs:
                break

    t1 = time.perf_counter()
    print("Обработано документов:", doc_count)
    print("Время с лемматизацией: {:.3f} с".format(t1 - t0))
    return base_freqs, lemma_freqs

def main():
    batch_files = sorted(Path("../corpus_aviation").glob("batch_*.jsonl"))
    if not batch_files:
        print("Нет batch_*.jsonl")
        return

    base_freqs, lemma_freqs = build_lemma_freqs(batch_files, max_docs=500)

    with open("term_freqs_raw.tsv", "w", encoding="utf-8") as f:
        for term, cnt in sorted(base_freqs.items(), key=lambda x: -x[1]):
            f.write("{}\t{}\n".format(term, cnt))

    with open("term_freqs_lemma.tsv", "w", encoding="utf-8") as f:
        for term, cnt in sorted(lemma_freqs.items(), key=lambda x: -x[1]):
            f.write("{}\t{}\n".format(term, cnt))

    print("Сырые частоты сохранены в term_freqs_raw.tsv")
    print("Частоты по леммам сохранены в term_freqs_lemma.tsv")

if __name__ == "__main__":
    main()