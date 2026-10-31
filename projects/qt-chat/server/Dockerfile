# 在 Ubuntu 24.04 中编译、运行聊天服务器：
#   docker build -t chatd .
#   docker run --rm -p 9000:9000 --name chatd chatd
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends gcc libc6-dev libcjson-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY chatd.c .
RUN gcc -O2 -Wall -Wextra -o /usr/local/bin/chatd chatd.c -lcjson
EXPOSE 9000
CMD ["chatd", "9000"]
