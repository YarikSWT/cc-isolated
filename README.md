<div align="center">

# cc-isolated

**A secure Docker sandbox for [Claude Code](https://claude.com/claude-code) on macOS**

Run `claude --dangerously-skip-permissions` safely. Every session lives in its own ephemeral container inside a dedicated [Colima](https://github.com/abiosoft/colima) VM that mounts only the directories you explicitly whitelist — your SSH keys, browser caches, and `$HOME` stay invisible to the AI agent.

[![macOS](https://img.shields.io/badge/macOS-13%2B-000000?logo=apple&logoColor=white)](https://www.apple.com/macos/)
[![Docker](https://img.shields.io/badge/Docker-2496ED?logo=docker&logoColor=white)](https://www.docker.com/)
[![Colima](https://img.shields.io/badge/Colima-claude%20profile-FF6F00)](https://github.com/abiosoft/colima)
[![Claude Code](https://img.shields.io/badge/Claude%20Code-sandbox-7C3AED)](https://claude.com/claude-code)
[![Shell](https://img.shields.io/badge/shell-bash-4EAA25?logo=gnubash&logoColor=white)](#)

</div>

---

> Claude edits code and spins up containers freely — but it physically cannot see anything outside the directories you explicitly allow. Each session is a fresh container.

## Contents

- [Why run Claude Code in a sandbox?](#why-run-claude-code-in-a-sandbox)
- [Architecture](#architecture)
- [Features](#features)
- [Installation](#installation)
- [Usage](#usage)
- [SSH access](#ssh-access)
- [MCP servers (Playwright and others)](#mcp-servers-playwright-and-others)
- [Configuration](#configuration)
- [Install in `PATH`](#install-in-path)
- [FAQ](#faq)

## Why run Claude Code in a sandbox?

When you pass `--dangerously-skip-permissions` to Claude Code, the AI agent gets full read/write access to your Mac: everything under `$HOME`, your SSH keys, browser caches, documents — the lot. `cc-isolated` removes that attack surface.

Claude runs inside a Docker container, which itself runs inside a dedicated Colima VM with **narrow filesystem mounts**. Even if the container is compromised, the VM boundary holds. You get the speed of skip-permissions mode without giving an LLM unrestricted access to your laptop.

Common use cases:

- Letting Claude Code refactor or experiment in a repo without worrying about it touching unrelated files.
- Running untrusted prompts or third-party agent workflows that you'd normally hesitate to allow.
- Reproducible, ephemeral dev environments — one container per session, gone on exit.

## Architecture

```
                  ┌──────────────────────────────────────────────┐
                  │  Mac                                         │
                  │                                              │
                  │    ~/Documents/projects   ~/.claude          │
                  │            │                  │              │
                  │            ▼                  ▼              │
                  │  ┌─────────────────────────────────────┐     │
                  │  │  Colima VM "claude"  (sees only)    │     │
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

A second layer of containment beyond a plain Docker container: the daemon Claude can reach is also isolated inside the VM, not your host's Docker.

## Features

<table>
<tr><td width="220"><b>Narrow VM mounts</b></td>
<td>The Colima <code>claude</code> profile mounts only <code>~/Documents/projects</code> and <code>~/.claude</code>. Everything else on the Mac (SSH keys in <code>~/.ssh</code>, documents, caches, browser data) does not exist as far as the VM is concerned.</td></tr>

<tr><td><b>Docker-out-of-Docker</b></td>
<td>The container has <code>/var/run/docker.sock</code> from the VM mounted in. Claude runs <code>docker compose up/build/down</code> against the same isolated VM — never against your Mac's Docker.</td></tr>

<tr><td><b>Dynamic socket GID</b></td>
<td>The launcher reads the GID of <code>/var/run/docker.sock</code> inside the VM and passes it to the container via <code>--group-add</code>. The unprivileged user gets socket access without any hard-coded UIDs in the Dockerfile.</td></tr>

<tr><td><b><code>DOCKER_HOST</code> guard</b></td>
<td>That env var silently overrides <code>docker context use</code> and would route launches into the default profile. The script <code>unset</code>s it locally (without side effects in your shell).</td></tr>

<tr><td><b>Atomic credential sync</b></td>
<td><code>~/.claude</code> is mounted live (history, hooks, file-history). <code>~/.claude.json</code> lives outside the mount, so it is copied to <code>~/.claude/.cc-host-claude.json</code> before launch and back after exit via a <code>trap</code> — both copies are atomic (cp + mv via tmp) so a concurrent reader never sees a half-written file.</td></tr>

<tr><td><b>Isolated SSH</b></td>
<td>Inside the container <code>~/.ssh</code> is a symlink to <code>~/.claude/ssh</code>. Only whitelisted keys and an explicit host list. <code>IdentitiesOnly yes</code> + <code>StrictHostKeyChecking accept-new</code>.</td></tr>

<tr><td><b>Parallel sessions</b></td>
<td>Each <code>cc</code> call spawns its own container with a unique name (PID suffix). Work in N terminals in parallel — every session is independent.</td></tr>

<tr><td><b>Batteries included</b></td>
<td>The image ships Node.js 22 LTS, Python 3, bun, gcc/g++/make, git, vim, Docker CLI, and Claude Code itself — enough to run most JavaScript/TypeScript and Python projects, including ones with native dependencies (e.g. <code>bcrypt</code>).</td></tr>
</table>

## Installation

**Requirements:** [Colima](https://github.com/abiosoft/colima), Docker CLI, macOS 13+.

```bash
# 1. Create the isolated VM profile
colima start claude \
  --mount "$HOME/Documents/projects:w" \
  --mount "$HOME/.claude:w" \
  --vm-type vz \
  --mount-type virtiofs

# 2. Build the sandbox image
./cc-build
```

## Usage

```bash
cc my-org/api           # path relative to ~/Documents/projects
cc my-org/api my-sess   # with a custom session name
cc ~/Documents/projects/foo         # absolute path
cc .                                # current directory
```

Inside, `claude --dangerously-skip-permissions` is launched. Open as many terminals as you like — each gets its own container.

## SSH access

`~/.claude/ssh/config` on the host is a shared list of servers reachable from any session.

**Add a server:**

```bash
# 1. Drop the key in
cp ~/path/to/my-key ~/.claude/ssh/my-key
chmod 600 ~/.claude/ssh/my-key

# 2. Append a block to ~/.claude/ssh/config
cat >> ~/.claude/ssh/config <<'EOF'

Host my-server
  HostName 1.2.3.4
  User root
  IdentityFile ~/.ssh/my-key
EOF
```

No rebuild required — the `~/.claude` mount is live. `ssh my-server` works immediately in the next container.

## MCP servers (Playwright and others)

`cc-isolated` supports two patterns for [Model Context Protocol](https://modelcontextprotocol.io) servers:

- **stdio MCP** (most servers — `github`, `filesystem`, `fetch`, `sequential-thinking`, …). Register once with `claude mcp add --scope user <name> npx -y <package>` from any session. The registration is persisted in `~/.claude.json` and synced back to the host on exit, so it applies to every future session.
- **HTTP / SSE MCP** (heavy or stateful — Playwright, browser automation, databases). Long-lived sidecar containers declared in `mcp-stack/compose.yaml`, started with `./cc-mcp up -d`. They live on the shared `cc-net` Docker network so every cc session resolves them by service name.

Example: bring up the Playwright MCP server and register it with Claude Code:

```bash
./cc-mcp up -d            # starts playwright-mcp on cc-net
cc some/project
# inside the session:
claude mcp add --scope user --transport http playwright http://playwright-mcp:8931/mcp
```

See `CLAUDE.md` for the gotchas (DNS rebinding protection, `--allowed-hosts`, etc.).

## Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `CC_MOUNT` | `~/Documents/projects` | Root directory mounted into the container |
| `CC_PROXY_PORT` | — | Host port for HTTP/HTTPS proxy (e.g. `11809`) |

**Scripts:**

| File | Purpose |
|------|---------|
| `cc`       | Start a session |
| `cc-build` | Rebuild the image (after Claude Code updates, etc.) |
| `cc-mcp`   | Manage MCP sidecar containers (`up`, `down`, `logs`, …) |

## Install in `PATH`

```bash
echo 'export PATH="$HOME/Documents/projects/cc-isolated:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

## FAQ

**Does it work on Linux or Windows?**
Not out of the box. The launcher assumes Colima and macOS paths. The container image is plain Debian and works anywhere — you'd just need to swap the VM/launcher layer (e.g. Lima, Multipass) and adjust mounts.

**Why Colima instead of Docker Desktop?**
Colima is free, scriptable, and lets you run multiple isolated VM profiles. The `claude` profile is a separate VM with explicit narrow mounts. Docker Desktop, by contrast, exposes your entire `$HOME` to its VM by default — defeating the point of the sandbox.

**Can I run multiple Claude Code sessions at once?**
Yes. Each `cc` invocation spins up an independent container with a unique name. Open as many terminals as you want.

**How is this different from running Claude Code inside a plain Docker container?**
A plain container shares the host's kernel, Docker socket, and whatever you've mounted. `cc-isolated` puts the container inside a dedicated VM, so the Docker daemon the agent can talk to is itself isolated. A hypothetical container breakout still hits a VM boundary, not your Mac.

**Does it slow Claude Code down?**
Startup is a few seconds for the container to come up. Once running, the only overhead is virtiofs file I/O on the mounted projects directory — negligible for normal coding work.

**What if I want native macOS tooling (Xcode, simctl, Homebrew)?**
Those are out of scope — this sandbox is for Linux-style development. If you need macOS-only tools, you'll want to run those outside the sandbox.

**How do I update Claude Code inside the image?**
Run `./cc-build` to rebuild from scratch with the latest `@anthropic-ai/claude-code` from npm.

---

<div align="center">
<sub>Built for use with <a href="https://claude.com/claude-code">Claude Code</a>. Keywords: Claude Code Docker sandbox, AI agent isolation, secure Claude Code on macOS, Colima sandbox, containerized AI development.</sub>
</div>
