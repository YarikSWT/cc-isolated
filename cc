#!/usr/bin/env bash
set -euo pipefail

# DOCKER_HOST из шелла перебивает docker context, поэтому без unset все
# docker-команды уходили бы в default-профиль вместо изолированного claude.
unset DOCKER_HOST
export DOCKER_CONTEXT=colima-claude

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE_NAME="claude-sandbox:latest"

# ── Настройки (можно переопределить через env) ──
MOUNT_ROOT="${CC_MOUNT:-$HOME/Documents/projects}"
PROXY_PORT="${CC_PROXY_PORT:-}"

usage() {
  cat <<'EOF'
Usage: cc <project-path> [session-name]

  project-path   Путь к проекту (абсолютный или относительный к ~/Documents/projects)
  session-name   Имя сессии (по умолчанию — имя папки проекта)

Env:
  CC_MOUNT       Корневая директория для монтирования (default: ~/Documents/projects)
  CC_PROXY_PORT  Порт прокси на хосте (опционально, напр. 11809)

Examples:
  cc my-org/api
  cc ~/Documents/projects/my-org/api my-session
  cc .                                    # текущая директория
EOF
  exit 1
}

[ "${1:-}" = "" ] && usage
[ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ] && usage

# ── Резолв пути проекта ──
INPUT_PATH="$1"
if [[ "$INPUT_PATH" = /* ]]; then
  PROJECT_PATH="$INPUT_PATH"
elif [[ "$INPUT_PATH" = "." ]]; then
  PROJECT_PATH="$(pwd)"
else
  PROJECT_PATH="$MOUNT_ROOT/$INPUT_PATH"
fi

# Нормализовать путь
PROJECT_PATH="$(cd "$PROJECT_PATH" 2>/dev/null && pwd)" || {
  echo "Error: directory not found: $PROJECT_PATH"
  exit 1
}

# Убедиться, что проект внутри MOUNT_ROOT
MOUNT_ROOT_REAL="$(cd "$MOUNT_ROOT" && pwd)"
if [[ "$PROJECT_PATH" != "$MOUNT_ROOT_REAL/"* && "$PROJECT_PATH" != "$MOUNT_ROOT_REAL" ]]; then
  echo "Error: $PROJECT_PATH is outside mount root $MOUNT_ROOT_REAL"
  echo ""
  echo "Either run from within $MOUNT_ROOT_REAL or override:"
  echo "  CC_MOUNT=$(dirname "$PROJECT_PATH") cc ."
  exit 1
fi

SESSION_NAME="${2:-$(basename "$PROJECT_PATH")}"
CONTAINER_NAME="cc-${SESSION_NAME}-$$"

# GID группы docker внутри claude VM — нужен для --group-add, иначе пользователь
# claude в контейнере получает permission denied на /var/run/docker.sock.
SOCK_GID="$(colima ssh --profile claude -- stat -c %g /var/run/docker.sock 2>/dev/null | tr -d '\r' || true)"
SOCK_GID="${SOCK_GID:-991}"

# ── Docker context ──
ctx=$(docker context show 2>/dev/null || true)
if [ "$ctx" != "colima-claude" ]; then
  echo "Switching docker context: $ctx -> colima-claude"
  docker context use colima-claude >/dev/null
fi

# ── Собрать образ если нет ──
if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
  echo "Building image $IMAGE_NAME ..."
  docker build --build-arg HOST_UID="$(id -u)" -t "$IMAGE_NAME" "$SCRIPT_DIR"
fi

# ── Запуск ──
echo "Session:   $CONTAINER_NAME"
echo "Mount:     $MOUNT_ROOT_REAL -> $MOUNT_ROOT_REAL"
echo "Workdir:   $PROJECT_PATH"
echo ""

PROXY_ARGS=()
if [ -n "$PROXY_PORT" ]; then
  PROXY_ARGS=(
    -e HTTPS_PROXY="http://host.docker.internal:${PROXY_PORT}"
    -e HTTP_PROXY="http://host.docker.internal:${PROXY_PORT}"
    -e NO_PROXY="localhost,127.0.0.1,host.docker.internal"
  )
fi

# ── Проброс ~/.claude.json через копию внутри ~/.claude ──
# Файл ~/.claude.json лежит вне маунта colima-claude (там только ~/.claude),
# поэтому копируем его в ~/.claude/.cc-host-claude.json. В контейнере на этот
# путь смотрит симлинк /home/claude/.claude.json. После выхода — копируем
# обратно, чтобы обновления из контейнера попали на хост.
HOST_CLAUDE_JSON="$HOME/.claude.json"
SHARED_CLAUDE_JSON="$HOME/.claude/.cc-host-claude.json"
if [ -f "$HOST_CLAUDE_JSON" ]; then
  cp -p "$HOST_CLAUDE_JSON" "$SHARED_CLAUDE_JSON"
fi
trap '[ -f "$SHARED_CLAUDE_JSON" ] && cp -p "$SHARED_CLAUDE_JSON" "$HOST_CLAUDE_JSON" 2>/dev/null || true' EXIT INT TERM

docker run -it --rm \
  --name "$CONTAINER_NAME" \
  --group-add "$SOCK_GID" \
  -v "$MOUNT_ROOT_REAL:$MOUNT_ROOT_REAL" \
  -v "$HOME/.claude:/home/claude/.claude" \
  -v /var/run/docker.sock:/var/run/docker.sock \
  ${ANTHROPIC_API_KEY:+-e ANTHROPIC_API_KEY="$ANTHROPIC_API_KEY"} \
  ${PROXY_ARGS[@]+"${PROXY_ARGS[@]}"} \
  -w "$PROJECT_PATH" \
  "$IMAGE_NAME"
