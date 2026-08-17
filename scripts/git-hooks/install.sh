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

commit_msg_hook="$repo_root/.git/hooks/commit-msg"
cat > "$commit_msg_hook" <<'EOF'
#!/bin/sh
exec python3 "$(git rev-parse --show-toplevel)/scripts/git-hooks/check-co-author.py" "$1"
EOF
chmod +x "$commit_msg_hook"
echo "installed $commit_msg_hook"
