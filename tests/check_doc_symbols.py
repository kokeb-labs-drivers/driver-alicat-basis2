#!/usr/bin/env python3
"""Every API name the documentation cites in backticks must exist in `inc/` or `src/`.

A name is "API" when it contains `::`, ends in `()` or is one CamelCase word. A cited name that no
header or source file contains is a dead reference: the page describes something that is not there
(for example a renamed table). Run by CTest as `alicat_basis2_doc_symbols`.

The pages the template manages (`TEMPLATE_PAGES`) are not checked: they are the same in every driver made from the
template and cite other drivers' functions on purpose (the defect catalogue's `Service()`, `Transfers()`), so their
names are not this driver's API.

Usage: check_doc_symbols.py REPO_ROOT
"""
import glob
import os
import re
import sys

# Words that are not driver symbols: standard names and prose that looks like a name.
# The repository knowledge pages the template manages (its drift check keeps them identical to the template's).
TEMPLATE_PAGES = frozenset({"knowledge-index.md", "mechanization-policy.md", "recurring-defects.md"})
IGNORED = frozenset({"std", "uint8_t", "uint16_t", "uint32_t", "int64_t", "size_t", "Host", "Repository"})


def code_words(root):
    """All identifiers of inc/ and src/, and for each namespace the identifiers of the files that open it."""
    words = set()
    by_namespace = {}
    for path in glob.glob(os.path.join(root, "inc", "*")) + glob.glob(os.path.join(root, "src", "*")):
        with open(path, encoding="utf-8", errors="ignore") as handle:
            text = handle.read()
        file_words = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", text))
        words |= file_words
        for declared in re.findall(r"^namespace\s+([A-Za-z_][A-Za-z0-9_:]*)", text, re.MULTILINE):
            for part in declared.split("::"):
                by_namespace.setdefault(part, set()).update(file_words)
    return words, by_namespace


def is_api(part):
    return "::" in part or part.endswith("()") or re.fullmatch(r"[A-Z][a-z]+[A-Za-z0-9]*", part) is not None


def main(root):
    words, by_namespace = code_words(root)
    pages = sorted(p for p in glob.glob(os.path.join(root, "docs", "*.md"))
                   if os.path.basename(p) not in TEMPLATE_PAGES) + [os.path.join(root, "README.md")]
    missing = {}
    for page in pages:
        with open(page, encoding="utf-8") as handle:
            text = handle.read()
        for cited in re.finditer(r"`([^`\n]+)`", text):
            for part in re.split(r"[ ,;]+", cited.group(1)):
                part = part.strip("(),;.")
                if not part or not is_api(part):
                    continue
                idents = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", part)
                for ident in idents:
                    if ident not in IGNORED and ident not in words:
                        missing.setdefault(ident, set()).add(os.path.basename(page))
                # `ns::name`: the name must be declared in a file that opens that namespace.
                for scope, name in zip(idents, idents[1:]):
                    if scope in by_namespace and name in words and name not in by_namespace[scope]:
                        missing.setdefault(scope + "::" + name, set()).add(os.path.basename(page))
    for ident, pages_seen in sorted(missing.items()):
        print("dead reference: %s in %s" % (ident, ", ".join(sorted(pages_seen))))
    return 1 if missing else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
