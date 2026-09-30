/*
 * Challenge 18 — 에러 처리(goto cleanup) 경로의 Double Free (심화: 다자원 래더)
 *
 * [시나리오]
 *   연결(Conn)을 열며 여러 자원을 순서대로 확보한다: 수신 버퍼(rx) → 송신 버퍼(tx)
 *   → 세션 상태(state). 확보 도중 실패하면 goto 라벨 사다리로 "역순 정리"한다.
 *   마지막에 핸드셰이크 검증을 수행하고, 실패하면 역시 정리 경로로 빠진다.
 *
 * [예시 상황]
 *  TCP 소켓 + TLS (HTTPS)
 *  소켓을 열고, 읽기/쓰기 버퍼를 잡고, SSL 세션을 만든 뒤 SSL_do_handshake()로 인증서를 확인.
 *  핸드셰이크가 실패하면 소켓·SSL 객체·버퍼를 역순으로 닫음. handshake_ok가 바로 이 단계.
 *
 * [기대 동작]
 *   각 자원을 확보한 만큼만, 정확히 한 번씩 해제하고 실패 코드를 반환.
 *
 * [증상]
 *   핸드셰이크 검증 실패 분기에서 tx 버퍼를 "특별 처리"한다며 먼저 free 한 뒤
 *   `goto fail_tx` 로 점프한다. 그런데 fail_tx 라벨도 tx 를 free 한다 → 같은 포인터
 *   이중 해제 → glibc "double free detected" abort. 자원이 많고 라벨 사다리가 길어
 *   "어느 경로가 무엇을 이미 해제했는지" 추적하기 어렵다.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=18_cleanup_double_free
 *   (gdb) run                        → abort
 *   (gdb) bt                         → conn_open 의 fail_tx: free(c->tx) 지점
 *   (gdb) break conn_open            → 각 free 호출 순서를 따라가며 tx 가 두 번 해제되는지 확인
 *   (gdb) print c->tx                → 검증 실패 분기의 free 후에도 같은 주소가 fail_tx 에서 또 해제됨
 *
 * [printf(로그)로 잡기]
 *   각 free 에 라벨을 붙여 경로별 해제를 추적:
 *     fprintf(stderr, "free tx @validate tx=%p\n", (void*)c->tx); free(c->tx);
 *     fail_tx: fprintf(stderr, "free tx @fail_tx tx=%p\n", (void*)c->tx); free(c->tx);
 *   → 같은 tx 주소가 두 번 출력되면 이중 해제.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: "해제한 자원은 즉시 포인터를 NULL 로" 만들어 라벨에서 다시 해제해도 무해하게
 *       하거나(free(NULL) 안전), 특정 자원을 조기 해제하지 말고 정리 경로 한 곳에만
 *       해제 책임을 두세요.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *rx;
    char *tx;
    int *state;
} Conn;

// c는 Conn 구조체의 주소를 저장하는 포인터 변수임.
static int handshake_ok(const Conn *c)
{
    // c를 사용하지 않는다.
    (void)c;
    // 항상 실패를 반환한다. int반환형 함수라.
    return 0; /* 실패 */
}

// 32를 버프 사이즈로 받는다.
static int conn_open(Conn *c, size_t bufsz)
{
    // Conn의 포인터 멤버를 NULL로 초기화함.
    c->rx = c->tx = NULL;
    // 구조체 멤버인 rx, tx, state를 초기화함.
    c->state = NULL;

    // bufsz 크기만큼 rx 버퍼를 힙에 할당함.
    c->rx = malloc(bufsz);
    // malloc 실패로 rx가 NULL이면
    if (!c->rx)
        // fail_rx 라벨로 이동함.
        goto fail_rx;
    // bufsz 크기만큼 tx 버퍼를 힙에 할당함.
    c->tx = malloc(bufsz);
    if (!c->tx)
        // malloc 실패로 state가 NULL이면 fail_state 라벨로 이동함.
        goto fail_tx;
    // int 4개를 저장할 공간을 할당함.
    c->state = malloc(sizeof(int) * 4);
    if (!c->state)
        // malloc 실패로 tx가 NULL이면 fail_tx 라벨로 이동함.
        goto fail_state;
    // strcpy(목적지, 원본)
    strcpy(c->rx, "rx-ready");
    strcpy(c->tx, "tx-ready");
    // i가 0~3일 때 반복함.
    // state[0]~state[3]에 0~3을 기록함.
    for (int i = 0; i < 4; i++)
        c->state[i] = i;
    // handshake_ok()는 항상 0을 반환함.
    // handshake_ok()가 0을 반환하므로 !0은 참이 됨.
    if (!handshake_ok(c))
    {
        // 여기서는 아무것도 직접 해제하지 않고 정리 사다리에 맡긴다.
        // rx, tx, state 세 자원을 모두 확보한 상태이므로 가장 마지막에 확보한
        // state부터 역순으로 해제하도록 fail_state로 이동한다.
        // (이전: goto fail_tx → state 해제를 건너뛰어 메모리 누수 발생)
        fprintf(stderr, "handshake failed: cleanup from fail_state\n");
        goto fail_state;
    }
    return 0;
// 실패한 지점에 따라 필요한 정리 라벨로 이동함.
fail_state:
    free(c->state);
// fail_state에서 내려오면 fail_tx로 이어져 tx를 해제함.
fail_tx:
    fprintf(stderr, "free tx @fail_tx tx=%p\n", (void *)c->tx);
    free(c->tx);
fail_rx:
    free(c->rx);
    // 해제한 주소가 구조체에 남아 dangling 포인터가 되지 않도록 NULL로 비운다.
    c->rx = c->tx = NULL;
    c->state = NULL;
    // 여기까지 해제하면 -1을 반환한다.
    return -1;
}

int main(void)
{
    // Conn 타입의 구조체 변수 c를 생성한다.
    Conn c;
    // c의 주소를 conn_open에 전달한다.
    // 반환된 값을 rc에 기록한다.
    int rc = conn_open(&c, 32);
    // conn open -1 출력문
    printf("conn_open rc=%d\n", rc);
    return 0;
}
