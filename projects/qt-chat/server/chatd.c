// chatd.c：聊天服务器。在网络专栏《IO 多路复用》的 echo_epoll.c 的基础上：
//   - 消息格式是 4 字节大端长度 + JSON（客户端开发专栏《QTcpSocket 与消息分帧》）
//   - 登录、在线状态、转发聊天消息、确认（ack）、按编号去重、离线消息
// 编译：gcc -O2 -Wall -o chatd chatd.c -lcjson
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <cjson/cJSON.h>

#define MAX_EVENTS 256
#define MAX_FRAME (1 << 20)      // 一条消息最大 1 MB
#define MAX_USERS 1024
#define SEEN 256                 // 每个用户记住最近 256 条消息的编号，用于去重

struct buf {                     // 可增长的字节缓冲区
    char *data;
    size_t len, cap;
};

struct user;

struct conn {
    int fd;
    struct buf in;               // 收到、还没切成消息的字节
    struct buf out;              // 要发、还没写出去的字节
    struct user *user;           // 登录后指向用户，NULL 表示还没登录
};

struct user {
    char name[32];
    struct conn *conn;           // 在线时指向它的连接
    long long seen[SEEN];        // 最近收到的消息编号（环形）
    int seen_pos;
    struct buf offline;          // 不在线时收到的消息，已经编码成帧
};

static struct user users[MAX_USERS];
static int nusers;
static int ep;

static void buf_append(struct buf *b, const void *p, size_t n)
{
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void buf_consume(struct buf *b, size_t n)
{
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
}

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static void log_msg(const char *fmt, const char *a, const char *b)
{
    printf("[%lld] ", now_ms() % 100000);
    printf(fmt, a, b);
    printf("\n");
    fflush(stdout);
}

// 把一个 JSON 对象编码成「长度 + 正文」，追加到缓冲区
static void encode(struct buf *b, cJSON *obj)
{
    char *body = cJSON_PrintUnformatted(obj);
    uint32_t len = htonl((uint32_t)strlen(body));
    buf_append(b, &len, 4);
    buf_append(b, body, strlen(body));
    free(body);
}

static void watch(struct conn *c)
{
    struct epoll_event ev = {.events = EPOLLIN | (c->out.len ? EPOLLOUT : 0), .data.ptr = c};
    epoll_ctl(ep, EPOLL_CTL_MOD, c->fd, &ev);
}

// 尽量把 out 中的数据写出去；返回 -1 表示连接出错
static int flush(struct conn *c)
{
    while (c->out.len > 0) {
        ssize_t w = write(c->fd, c->out.data, c->out.len);
        if (w == -1) {
            if (errno == EAGAIN) break;          // 发送缓冲区满了，等 EPOLLOUT
            return -1;
        }
        buf_consume(&c->out, w);
    }
    watch(c);
    return 0;
}

static void send_json(struct conn *c, cJSON *obj)
{
    encode(&c->out, obj);
    flush(c);                                    // 出错的连接会在下一次读的时候被关闭
}

static struct user *find_user(const char *name, int create)
{
    for (int i = 0; i < nusers; i++)
        if (strcmp(users[i].name, name) == 0)
            return &users[i];
    if (!create || nusers == MAX_USERS)
        return NULL;
    struct user *u = &users[nusers++];
    snprintf(u->name, sizeof u->name, "%s", name);
    return u;
}

// 告诉所有在线的用户：某人上线或下线了
static void broadcast_presence(struct user *who, int online)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "presence");
    cJSON_AddStringToObject(p, "user", who->name);
    cJSON_AddBoolToObject(p, "online", online);
    for (int i = 0; i < nusers; i++)
        if (users[i].conn && &users[i] != who)
            send_json(users[i].conn, p);
    cJSON_Delete(p);
}

static void close_conn(struct conn *c)
{
    struct user *u = c->user;
    epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);
    free(c->in.data);
    free(c->out.data);
    free(c);
    if (u && u->conn == c) {
        u->conn = NULL;
        log_msg("%s 下线%s", u->name, "");
        broadcast_presence(u, 0);
    }
}

static void on_login(struct conn *c, cJSON *msg)
{
    cJSON *name = cJSON_GetObjectItem(msg, "user");
    if (!cJSON_IsString(name) || strlen(name->valuestring) == 0 || strlen(name->valuestring) >= 32)
        return;
    struct user *u = find_user(name->valuestring, 1);
    if (!u) return;
    int was_online = u->conn != NULL;
    if (u->conn && u->conn != c) {               // 同一个用户在别处登录：通知旧的连接，然后关闭它
        log_msg("%s 在别处登录，关闭旧的连接%s", u->name, "");
        struct conn *old = u->conn;
        cJSON *k = cJSON_CreateObject();
        cJSON_AddStringToObject(k, "type", "kicked");   // 客户端收到后不要自动重连，否则两边会互相踢
        send_json(old, k);
        cJSON_Delete(k);
        u->conn = NULL;
        old->user = NULL;
        close_conn(old);
    }
    c->user = u;
    u->conn = c;
    log_msg("%s 上线%s", u->name, "");

    cJSON *ok = cJSON_CreateObject();
    cJSON_AddStringToObject(ok, "type", "login_ok");
    cJSON_AddStringToObject(ok, "user", u->name);
    cJSON *list = cJSON_AddArrayToObject(ok, "users");
    for (int i = 0; i < nusers; i++) {
        if (&users[i] == u) continue;
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "name", users[i].name);
        cJSON_AddBoolToObject(e, "online", users[i].conn != NULL);
        cJSON_AddItemToArray(list, e);
    }
    send_json(c, ok);
    cJSON_Delete(ok);

    if (u->offline.len) {                        // 补发离线期间收到的消息
        buf_append(&c->out, u->offline.data, u->offline.len);
        u->offline.len = 0;
        flush(c);
    }
    if (!was_online)
        broadcast_presence(u, 1);
}

static int seen_before(struct user *u, long long id)
{
    for (int i = 0; i < SEEN; i++)
        if (u->seen[i] == id)
            return 1;
    u->seen[u->seen_pos] = id;
    u->seen_pos = (u->seen_pos + 1) % SEEN;
    return 0;
}

static void on_chat(struct conn *c, cJSON *msg)
{
    cJSON *id = cJSON_GetObjectItem(msg, "id");
    cJSON *to = cJSON_GetObjectItem(msg, "to");
    cJSON *text = cJSON_GetObjectItem(msg, "text");
    if (!c->user || !cJSON_IsNumber(id) || !cJSON_IsString(to) || !cJSON_IsString(text))
        return;
    struct user *peer = find_user(to->valuestring, 0);
    long long ts = now_ms();
    cJSON *reply = cJSON_CreateObject();
    cJSON_AddNumberToObject(reply, "id", id->valuedouble);
    if (!peer) {
        cJSON_AddStringToObject(reply, "type", "error");
        cJSON_AddStringToObject(reply, "reason", "没有这个用户");
    } else {
        if (seen_before(c->user, (long long)id->valuedouble)) {
            log_msg("%s 重发的消息，已经处理过，只回复确认%s", c->user->name, "");
        } else {
            cJSON *fwd = cJSON_CreateObject();
            cJSON_AddStringToObject(fwd, "type", "msg");
            cJSON_AddStringToObject(fwd, "from", c->user->name);
            cJSON_AddNumberToObject(fwd, "id", id->valuedouble);
            cJSON_AddStringToObject(fwd, "text", text->valuestring);
            cJSON_AddNumberToObject(fwd, "ts", (double)ts);
            if (peer->conn)
                send_json(peer->conn, fwd);
            else
                encode(&peer->offline, fwd);     // 对方不在线，存起来，上线时补发
            cJSON_Delete(fwd);
            log_msg("%s -> %s", c->user->name, peer->name);
        }
        cJSON_AddStringToObject(reply, "type", "ack");
        cJSON_AddNumberToObject(reply, "ts", (double)ts);
    }
    send_json(c, reply);
    cJSON_Delete(reply);
}

static void on_frame(struct conn *c, const char *body, size_t len)
{
    cJSON *msg = cJSON_ParseWithLength(body, len);
    cJSON *type = cJSON_GetObjectItem(msg, "type");
    if (cJSON_IsString(type)) {
        if (strcmp(type->valuestring, "ping") == 0) {
            cJSON *pong = cJSON_CreateObject();
            cJSON_AddStringToObject(pong, "type", "pong");
            send_json(c, pong);
            cJSON_Delete(pong);
        } else if (strcmp(type->valuestring, "login") == 0) {
            on_login(c, msg);
        } else if (strcmp(type->valuestring, "msg") == 0) {
            on_chat(c, msg);
        }
    }
    cJSON_Delete(msg);
}

// 读出数据，切出所有完整的消息；返回 -1 表示要关闭连接
static int on_readable(struct conn *c)
{
    char tmp[65536];
    for (;;) {
        ssize_t r = read(c->fd, tmp, sizeof tmp);
        if (r == 0) return -1;                   // 对方关闭
        if (r == -1) {
            if (errno == EAGAIN) break;
            return -1;
        }
        buf_append(&c->in, tmp, r);
    }
    while (c->in.len >= 4) {
        uint32_t len;
        memcpy(&len, c->in.data, 4);
        len = ntohl(len);
        if (len > MAX_FRAME) return -1;          // 长度不合理，断开
        if (c->in.len < 4 + len) break;          // 还没收全
        on_frame(c, c->in.data + 4, len);
        buf_consume(&c->in, 4 + len);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int port = argc > 1 ? atoi(argv[1]) : 9000;
    int lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = INADDR_ANY};
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) == -1 || listen(lfd, 128) == -1) {
        perror("bind/listen");
        return 1;
    }
    ep = epoll_create1(0);
    struct epoll_event ev = {.events = EPOLLIN, .data.ptr = NULL};
    epoll_ctl(ep, EPOLL_CTL_ADD, lfd, &ev);
    printf("chatd 监听端口 %d\n", port);
    fflush(stdout);

    struct epoll_event events[MAX_EVENTS];
    for (;;) {
        int n = epoll_wait(ep, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) {
            struct conn *c = events[i].data.ptr;
            if (c == NULL) {
                int cfd;
                while ((cfd = accept4(lfd, NULL, NULL, SOCK_NONBLOCK)) != -1) {
                    struct conn *nc = calloc(1, sizeof *nc);
                    nc->fd = cfd;
                    struct epoll_event cev = {.events = EPOLLIN, .data.ptr = nc};
                    epoll_ctl(ep, EPOLL_CTL_ADD, cfd, &cev);
                }
                continue;
            }
            if ((events[i].events & EPOLLOUT) && flush(c) == -1) {
                close_conn(c);
                continue;
            }
            if ((events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) && on_readable(c) == -1)
                close_conn(c);
        }
    }
}
