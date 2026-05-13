# cc-isolated — Claude Code в изолированной песочнице

Запуск Claude Code в Docker-контейнере через Colima VM.
Claude может свободно редактировать код и управлять Docker, но физически не имеет доступа к файлам за пределами указанной директории.

## Как это работает

```
Mac (~/Documents/projects)
  └─ Colima VM "claude" (видит только эту папку)
       └─ Docker-контейнер (Claude Code + Docker CLI)
            └─ docker.sock → демон в VM
```

- **Colima VM** — отдельная виртуалка с единственным маунтом, изоляция от остального Mac
- **Docker-out-of-Docker** — в контейнер пробрасывается сокет демона из VM, Claude может запускать `docker compose up/down/build`
- **Proxy** — трафик идёт через прокси на хосте (порт настраивается)

## Требования

- [Colima](https://github.com/abiosoft/colima) с профилем `claude`
- Docker CLI на Mac

### Настройка Colima (один раз)

```bash
colima start claude \
  --mount "$HOME/Documents/projects:w" \
  --mount "$HOME/.claude:w" \
  --vm-type vz \
  --mount-type virtiofs
```

`~/.claude` пробрасывается, чтобы Claude в контейнере унаследовал твою аутентификацию и историю. Файл `~/.claude.json` (лежит вне `~/.claude`) скрипт `cc` перед запуском копирует в `~/.claude/.cc-host-claude.json` и копирует обратно при выходе.

Переключение контекста происходит автоматически при запуске `cc`.

## Использование

```bash
# Запуск в проекте (путь относительно ~/Documents/projects)
cc my-org/api

# С именем сессии
cc my-org/api my-session

# Абсолютный путь
cc ~/Documents/projects/some-project

# Текущая директория
cc .
```

Внутри контейнера запускай `claude` как обычно.

Можно открыть несколько терминалов и запустить несколько `cc` — каждый получит свой контейнер.

## Скрипты

| Файл | Назначение |
|------|-----------|
| `cc` | Запуск сессии |
| `cc-build` | Пересборка образа (после обновления Claude Code и т.д.) |

## Переменные окружения

| Переменная | По умолчанию | Описание |
|-----------|-------------|----------|
| `CC_MOUNT` | `~/Documents/projects` | Корневая директория, монтируемая в контейнер |
| `CC_PROXY_PORT` | `11809` | Порт HTTP/HTTPS прокси на хосте |

## Добавить в PATH

```bash
echo 'export PATH="$HOME/Documents/projects/cc-isolated:$PATH"' >> ~/.zshrc
source ~/.zshrc
```
# isolated-ai-code-cli
