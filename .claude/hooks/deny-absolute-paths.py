#!/usr/bin/env python3
import json
import re
import shlex
import sys

ALLOWED_ABSOLUTE = {
    "/dev/null",
    "/dev/stdin",
    "/dev/stdout",
    "/dev/stderr",
    "/dev/tty",
    "/dev/zero",
}

SEPARATORS = ("=", ">", "<", "|", "&", ";")

VAR_REF = re.compile(r"\$\{?[A-Za-z_]")

CMD_SUBST = re.compile(r"\$\((?!\()")

EXPLAIN = {
    "absolu": "chemin absolu (commencant par /) : utilise un chemin relatif au projet",
    "home": "chemin home (~) : utilise un chemin relatif au projet",
    "variable": "variable d'environnement ($VAR, ${VAR}) : ecris le chemin en clair, sans variable",
    "remontee": "remontee de dossier (..) : reste dans le repertoire du projet",
    "substitution": "substitution de commande (`...` ou $(...)) : ecris la valeur en clair",
}


def _category(part):
    if part.startswith("/"):
        return "absolu"
    if part.startswith("~"):
        return "home"
    if "`" in part or CMD_SUBST.search(part):
        return "substitution"
    if VAR_REF.search(part):
        return "variable"
    if ".." in part.split("/"):
        return "remontee"
    return None


def absolute_paths_in(token):
    parts = [token]
    for sep in SEPARATORS:
        parts = [piece for chunk in parts for piece in chunk.split(sep)]
    return [p for p in parts if _category(p) is not None]


def bad_paths(command):
    try:
        tokens = shlex.split(command)
    except ValueError:
        return []
    bad = []
    for token in tokens:
        for path in absolute_paths_in(token):
            if path not in ALLOWED_ABSOLUTE:
                bad.append(path)
    return list(dict.fromkeys(bad))


def main():
    try:
        data = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        return 0
    command = (data.get("tool_input") or {}).get("command", "") or ""
    bad = bad_paths(command)
    if not bad:
        return 0
    cats = []
    for path in bad:
        cat = _category(path)
        if cat not in cats:
            cats.append(cat)
    regles = "\n".join(f"  - {EXPLAIN[c]}" for c in cats)
    reason = (
        "Commande refusee par un garde-fou local (chemins relatifs uniquement). "
        f"En cause : {', '.join(bad)}.\n{regles}\n"
        "Corrige puis relance la commande."
    )
    print(
        json.dumps(
            {
                "hookSpecificOutput": {
                    "hookEventName": "PreToolUse",
                    "permissionDecision": "deny",
                    "permissionDecisionReason": reason,
                }
            }
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
