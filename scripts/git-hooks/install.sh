#!/bin/sh
set -e
repo_root=$(git rev-parse --show-toplevel)
hook="$repo_root/.git/hooks/pre-commit"
cat > "$hook" <<'EOF'
#!/bin/sh
exec python3 "$(git rev-parse --show-toplevel)/scripts/git-hooks/strip-comments.py"
EOF
chmod +x "$hook"
echo "installed $hook"
