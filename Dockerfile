FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl gnupg git tini procps less vim unzip \
    && install -m 0755 -d /etc/apt/keyrings \
    && curl -fsSL https://download.docker.com/linux/debian/gpg \
       | gpg --dearmor -o /etc/apt/keyrings/docker.gpg \
    && chmod a+r /etc/apt/keyrings/docker.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/debian bookworm stable" \
       > /etc/apt/sources.list.d/docker.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
         docker-ce-cli docker-buildx-plugin docker-compose-plugin \
    && rm -rf /var/lib/apt/lists/*

COPY --from=oven/bun:1.3.13 /usr/local/bin/bun /usr/local/bin/bun

ARG HOST_UID=501
RUN useradd -m -s /bin/bash -u "$HOST_UID" claude
USER claude

ENV PATH="/home/claude/.bun/bin:$PATH"
RUN bun install -g @anthropic-ai/claude-code

# ~/.claude.json на хосте копируется в ~/.claude/.cc-host-claude.json (cc скрипт),
# а внутри контейнера читаем его через симлинк.
RUN ln -s /home/claude/.claude/.cc-host-claude.json /home/claude/.claude.json

# Изолированный набор ssh-ключей и конфиг — список разрешённых серверов лежит
# в ~/.claude/ssh/ на хосте, в контейнере читаем через симлинк.
RUN ln -s /home/claude/.claude/ssh /home/claude/.ssh

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["claude", "--dangerously-skip-permissions"]
