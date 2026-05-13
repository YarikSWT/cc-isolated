# cc-isolated — agent context

Sandbox для запуска Claude Code на macOS. Каждая сессия `cc <project>` — отдельный эфемерный контейнер внутри Colima VM `claude`. Подробное описание в `README.md`.

## Главный gotcha: `DOCKER_HOST` бьёт `DOCKER_CONTEXT`

В шелле пользователя экспортирован `DOCKER_HOST`, указывающий на **default** колиму. Любая docker-команда без `unset DOCKER_HOST` уезжает в default-профиль вместо `colima-claude`, даже если ты явно указал `DOCKER_CONTEXT` или `--context`. Docker печатает warning и игнорирует контекст — warning легко пропустить и сделать неправильные выводы ("контейнера нет!" → на самом деле смотришь не туда).

**Правило:** всегда префиксуй `unset DOCKER_HOST;` перед docker-командами при диагностике этого проекта:

```bash
unset DOCKER_HOST
docker --context colima-claude ps           # или
DOCKER_CONTEXT=colima-claude docker ps
```

Скрипты `cc` и `cc-mcp` это делают сами — не вмешивайся в их логику без причины.

## Архитектура (в одном экране)

```
Host Mac
└── Colima VM "claude" (mount: ~/Documents/projects, ~/.claude)
    └── dockerd
        ├── playwright-mcp  ─┐
        ├── другие MCP      ─┤── cc-net (user-defined bridge)
        ├── cc-<sess>-$$    ─┤   DNS-resolution по имени сервиса
        └── cc-<sess>-$$    ─┘
```

- Один dockerd на всё. Маунт сокета внутрь cc-контейнера = Docker-out-of-Docker (но в той же изолированной VM).
- Каждая `cc` сессия — `docker run --rm` с уникальным именем `cc-<session>-<PID>`. Параллельные терминалы = параллельные контейнеры.
- `~/.claude` смонтирован живьём; `~/.claude.json` копируется in/out через `trap` (синк регистраций MCP, истории, hooks).

## Файлы репо

| Файл | Что внутри |
|---|---|
| `cc` | Запуск сессии. UID/GID-проброс сокета, копия `.claude.json`, network attach. |
| `cc-build` | Пересборка `claude-sandbox:latest`. |
| `cc-mcp` | Обёртка над `docker compose` для MCP-сайдкаров. |
| `Dockerfile` | Образ сессии. |
| `mcp-stack/compose.yaml` | HTTP/SSE MCP-сервисы (long-lived). |

## MCP — два класса серверов

**stdio MCP** (большинство: github, filesystem, fetch, sqlite, memory, …):
- Spawn'ятся самим Claude Code по stdio. Не нужен sidecar.
- Регистрация: `claude mcp add --scope user <name> npx -y <package>` из любой сессии — попадает в `~/.claude.json` и синкается во все будущие сессии через trap в `cc`.

**HTTP/SSE MCP** (тяжёлые, stateful, например playwright):
- Long-lived sidecar в `mcp-stack/compose.yaml`, поднимается через `./cc-mcp up -d`. Переживает рестарты cc-сессий.
- Адресуется по имени сервиса внутри `cc-net` (например `http://playwright-mcp:8931/mcp`).
- Регистрация: `claude mcp add --scope user --transport http <name> http://<service>:<port>/<path>` из любой сессии.

### Playwright MCP — известная ловушка

Сервер по умолчанию отдаёт `403 "Access is only allowed at localhost"` на запросы где `Host:` хедер не `localhost:<port>` — защита от DNS rebinding. Из cc-контейнера ходим по имени `playwright-mcp` → 403. Лечится флагом `--allowed-hosts=*` (см. `mcp-stack/compose.yaml`). В нашей сети угроза неприменима, выключать безопасно.

Дополнительно: в Docker поддерживается **только headless chromium**. Headed-режим и firefox/webkit потребуют локальной установки вместо sidecar.

## Полезные команды для агента

```bash
unset DOCKER_HOST                                                   # обязательно
docker --context colima-claude ps                                   # что крутится в claude-VM
docker --context colima-claude network inspect cc-net               # кто в сети sidecars+sessions
docker --context colima-claude logs --tail 40 <name>                # дебаг падений
docker --context colima-claude exec <cc-session> curl http://<svc>  # пробник связности изнутри сессии

./cc-mcp up -d            # поднять MCP-стек
./cc-mcp logs <service>   # дебаг конкретного MCP
./cc-mcp down             # остановить
```

## При изменениях

- Правка флагов MCP в compose → `./cc-mcp up -d` пересоздаёт контейнер с новыми аргументами.
- Правка `Dockerfile` → `./cc-build`. Live-сессии не пересобираются автоматически, нужен новый `cc`.
- Правка `cc` → применяется к **следующим** `cc`-вызовам; уже работающие сессии не затрагиваются.
