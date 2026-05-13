<div align="center">

# cc-isolated

**Claude Code в изолированной Docker-песочнице на macOS**

[![macOS](https://img.shields.io/badge/macOS-13%2B-000000?logo=apple&logoColor=white)](https://www.apple.com/macos/)
[![Docker](https://img.shields.io/badge/Docker-2496ED?logo=docker&logoColor=white)](https://www.docker.com/)
[![Colima](https://img.shields.io/badge/Colima-claude%20profile-FF6F00)](https://github.com/abiosoft/colima)
[![Claude Code](https://img.shields.io/badge/Claude%20Code-sandbox-7C3AED)](https://claude.com/claude-code)
[![Shell](https://img.shields.io/badge/shell-bash-4EAA25?logo=gnubash&logoColor=white)](#)

</div>

---

> Claude свободно правит код и крутит контейнеры, но физически не видит ничего за пределами явно разрешённых директорий. Каждая сессия — отдельный контейнер.

## Содержание

- [Зачем](#зачем)
- [Архитектура](#архитектура)
- [Особенности](#особенности)
- [Установка](#установка)
- [Использование](#использование)
- [SSH-доступ](#ssh-доступ)
- [Конфигурация](#конфигурация)
- [Установка в `PATH`](#установка-в-path)

## Зачем

Когда даёшь Claude Code режим `--dangerously-skip-permissions`, он получает полный доступ к твоему Mac: всё в `$HOME`, твои ssh-ключи, документы, кэши браузеров. `cc-isolated` отрезает эту поверхность: Claude бежит в Docker-контейнере **внутри отдельной Colima VM**, у которой смонтированы только нужные папки. Даже если контейнер скомпрометирован — забор остаётся.

## Архитектура

```
                  ┌──────────────────────────────────────────────┐
                  │  Mac                                         │
                  │                                              │
                  │    ~/Documents/projects   ~/.claude          │
                  │            │                  │              │
                  │            ▼                  ▼              │
                  │  ┌─────────────────────────────────────┐     │
                  │  │  Colima VM "claude"  (видит только) │     │
                  │  │                                     │     │
                  │  │  ┌───────────────────────────────┐  │     │
                  │  │  │  Docker container             │  │     │
                  │  │  │   ├─ Claude Code              │  │     │
                  │  │  │   ├─ Docker CLI ── sock ──┐   │  │     │
                  │  │  │   └─ ssh (allow-list)     │   │  │     │
                  │  │  └───────────────────────────┼───┘  │     │
                  │  │                              ▼      │     │
                  │  │              dockerd (in VM)        │     │
                  │  └─────────────────────────────────────┘     │
                  └──────────────────────────────────────────────┘
```

## Особенности

<table>
<tr><td width="220"><b>Узкий маунт VM</b></td>
<td>Профиль Colima <code>claude</code> монтирует только <code>~/Documents/projects</code> и <code>~/.claude</code>. Всё остальное на Mac (ssh-ключи в <code>~/.ssh</code>, документы, кэши) для VM не существует.</td></tr>

<tr><td><b>Docker-out-of-Docker</b></td>
<td>В контейнер пробрасывается <code>/var/run/docker.sock</code> из VM. Claude крутит <code>docker compose up/build/down</code> внутри той же изолированной VM, не на Mac.</td></tr>

<tr><td><b>GID сокета на лету</b></td>
<td>Скрипт читает GID <code>/var/run/docker.sock</code> из VM и пробрасывает в контейнер через <code>--group-add</code>. Непривилегированный пользователь получает доступ к сокету без хардкода в Dockerfile.</td></tr>

<tr><td><b>Защита от <code>DOCKER_HOST</code></b></td>
<td>Эта env переменная молча перебивает <code>docker context use</code> и сливает запуск в default-профиль. Скрипт <code>unset</code>-ит её локально (без побочного эффекта для шелла пользователя).</td></tr>

<tr><td><b>Sync credentials</b></td>
<td><code>~/.claude</code> маунтится целиком (история, hooks, file-history). <code>~/.claude.json</code> лежит вне маунта, поэтому копируется в <code>~/.claude/.cc-host-claude.json</code> до запуска и обратно после выхода через <code>trap</code>.</td></tr>

<tr><td><b>Изолированный SSH</b></td>
<td>В контейнере <code>~/.ssh</code> — симлинк на <code>~/.claude/ssh</code>. Только разрешённые ключи и явный список хостов. <code>IdentitiesOnly yes</code> + <code>StrictHostKeyChecking accept-new</code>.</td></tr>

<tr><td><b>Параллельные сессии</b></td>
<td>Каждый <code>cc</code> запускает свой контейнер с уникальным именем (PID в суффиксе). Можно работать в N терминалах одновременно.</td></tr>
</table>

## Установка

**Требования:** [Colima](https://github.com/abiosoft/colima), Docker CLI.

```bash
# 1. Создать профиль claude
colima start claude \
  --mount "$HOME/Documents/projects:w" \
  --mount "$HOME/.claude:w" \
  --vm-type vz \
  --mount-type virtiofs

# 2. Собрать образ
./cc-build
```

## Использование

```bash
cc my-org/api           # путь относительно ~/Documents/projects
cc my-org/api my-sess   # с именем сессии
cc ~/Documents/projects/foo         # абсолютный путь
cc .                                # текущая директория
```

Внутри запускается `claude --dangerously-skip-permissions`. Открывай сколько угодно терминалов — каждый получит свой контейнер.

## SSH-доступ

`~/.claude/ssh/config` на хосте — общий список серверов, доступных из любых сессий.

**Добавить сервер:**

```bash
# 1. Положить ключ
cp ~/path/to/my-key ~/.claude/ssh/my-key
chmod 600 ~/.claude/ssh/my-key

# 2. Дописать блок в ~/.claude/ssh/config
cat >> ~/.claude/ssh/config <<'EOF'

Host my-server
  HostName 1.2.3.4
  User root
  IdentityFile ~/.ssh/my-key
EOF
```

Пересборка не нужна — маунт `~/.claude` живой. В контейнере сразу станет доступно `ssh my-server`.

## Конфигурация

| Переменная | По умолчанию | Описание |
|-----------|-------------|----------|
| `CC_MOUNT` | `~/Documents/projects` | Корневая директория для монтирования в контейнер |
| `CC_PROXY_PORT` | — | Порт HTTP/HTTPS прокси на хосте (напр. `11809`) |

**Скрипты:**

| Файл | Назначение |
|------|-----------|
| `cc`       | Запуск сессии |
| `cc-build` | Пересборка образа (после обновления Claude Code и т.д.) |

## Установка в `PATH`

```bash
echo 'export PATH="$HOME/Documents/projects/cc-isolated:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

---

<div align="center">
<sub>Built for use with <a href="https://claude.com/claude-code">Claude Code</a>.</sub>
</div>
