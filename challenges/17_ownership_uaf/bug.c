/*
 * Challenge 17 — 소유권 혼동 UAF (심화: 메시지 브로커)
 *
 * [시나리오]
 *   간단한 발행/구독 브로커. 발행된 메시지(Msg: 힙에 복사된 body)를 인박스 큐에 넣고,
 *   deliver() 가 하나씩 꺼내 구독자 콜백에 넘긴다. 구독자는 메시지를 처리하고 나서
 *   "소비했으니" free 한다. 브로커는 감사(audit)를 위해 발행 시점에 같은 Msg 포인터를
 *   log[] 에도 담아둔다.
 *
 * [기대 동작]
 *   모든 메시지를 배달·소비하고, 브로커를 종료하며 누수 없이 정리한 뒤 정상 종료.
 *
 * [예시 상황]
 *   채팅 서버
 *   hello를 발행하면, 수신함(inbox)에 넣어서 상대 앱에 푸시하고, 동시에 대화 이력(log)에도 같은 메시지를 남깁니다.
 *
 * [증상]
 *   구독자가 배달받은 Msg 를 free 하는데, 브로커의 log[] 는 "같은 포인터"를 여전히
 *   들고 있다(소유권이 두 곳에 걸침). broker_shutdown() 이 log[] 를 순회하며 이미
 *   소비자가 해제한 Msg 를 다시 정리한다: msg_free() 가 해제된 구조체를 재차 읽어
 *   (m->body) 그 값을 free → use-after-free/이중 해제. 해제된 청크는 할당자가 덮어써
 *   m->body 가 엉뚱한 주소가 되므로 대개 SIGSEGV(glibc 가 감지하면 double free abort).
 *   발행/배달/감사가 서로 다른 함수에 흩어져 있어 "누가 소유자인지" 헷갈리는 것이 함정.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=17_ownership_uaf
 *   (gdb) run                        → 크래시(SIGSEGV, 또는 abort)
 *   (gdb) bt                         → broker_shutdown → msg_free(b->log[i]) 지점
 *   (gdb) print b->log[i]            → 이 주소가 앞서 구독자가 free 한 것과 같은지 확인
 *   (gdb) break on_message           → 구독자가 free 하는 주소를 기록해 두고 대조
 *
 * [printf(로그)로 잡기]
 *   "누가 어떤 주소를 free 하는지"를 추적한다:
 *     (구독자)   fprintf(stderr, "consume free msg=%p\n", (void*)m);
 *     (shutdown) fprintf(stderr, "audit   free log[%d]=%p\n", i, (void*)b->log[i]);
 *   → 같은 주소가 두 곳에서 free 되면 이중 해제.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 소유권은 한 곳만 갖는다. log[] 는 "감사용 참조"일 뿐이므로 free 하지 않거나,
 *       배달 시 로그 슬롯을 무효화(넘긴 소유권을 추적)하세요. 소비자가 소유하면
 *       브로커는 절대 그 Msg 를 해제하지 않는다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int id;
    char *body; /* 힙 문자열 */
} Msg;

#define QCAP 16 // QCAP이라는 이름을 16으로치환함.
//
typedef struct
{
    Msg *inbox[QCAP];                               // Msg를 가리키는 포인터 16개를 배열로 만듦. 각 칸에는 Msg의 주소가 들어감.
    int head, tail; /* 원형 큐 */                   // head는 큐에서 읽을 위치. tail은 큐에 넣을 위치.
    Msg *log[QCAP];                                 // 감사용 배열. publish()에서 생성한 Msg 주소를 기록함.
    int log_n; /* 감사용: 같은 Msg 포인터를 보관 */ // log[]에 기록된 Msg 포인터의 개수.
} Broker;
// Msg의 수명은 누가 책임지는가?
// 동적 메모리를 여러 포인터가 가리킬 때 소유권을 정해야 함.
// 소유자는 Msg의 수명을 책임지고 마지막에 free()를 해야 함.
// 여러 포인터가 같은 Msg를 가리킬 수 있지만,
// free()책임은 한 곳만 가져야 함.
//

// 반환형이 void고 MSG *를 매개변수로 받는 함수의 주소를 저장하는 포인터 타입.
typedef void (*Subscriber)(Msg *m);

static Msg *msg_new(int id, const char *body)
{
    // m은 Msg를 가리키는 포이터 변수
    // sizeof *m은 Msg 하나의 크기
    // Msg 하나가 들어갈 크기의 힙 메모리를 할당하고
    // malloc()이 반환한 주소를 m에 기록함.
    Msg *m = malloc(sizeof *m);
    if (!m)
        exit(1);
    m->id = id;
    // Msg.body가 가리킬 문자열 공간을 별도로 힙에 할당함
    // body에는 원본 문자열의 시작 주소가 들어 잇음
    // strlen(body) +1 만큼 문자열을 저장할 공간을 할당함.
    m->body = malloc(strlen(body) + 1);
    if (!m->body)
        exit(1);
    // 새로 할당한 힙 메모리에 body가 가리키는 문자열을 복사함.
    strcpy(m->body, body);
    // m이 가지고 있는 Msg의 주소를 호출한 함수에 반환함.
    return m;
}

static void msg_free(Msg *m)
{
    // 디버깅: 이미 해제된 m을 읽으므로 UAF임
    // Msg 안의 Body 포인터가 가리키는 문자열을 먼저 해제함.
    free(m->body);
    // Msg 구조체 자체를 해제함.
    free(m);
}

// 여기서 Broker 구조체를 b로 사용함.
static void publish(Broker *b, int id, const char *body)
{
    // 메인에서 publish가 호출되면 이게 가장 먼저 실행되는데
    // body에는 문자열이 들어가는게 아니라 문자열이 있는 메모리 주소가 들어감.
    Msg *m = msg_new(id, body);
    // Broker관리.
    // m에 들어 있는 Msg 주소를 현재 Tail 위치의 inbox에 기록함.
    b->inbox[b->tail] = m;
    // 다음 메시지를 넣을 위치로 Tail을 한칸 이동함
    // QCAP에 도달하면 0으로 돌아가는 원형 큐 방식임.
    // 현재 코드는 큐가 가득 찼는지 검사하지 않으므로
    // tail이 Head를 덮어쓸 수 있음.
    b->tail = (b->tail + 1) % QCAP;
    // 같은 Msg 주소를 감사용 log에도 기록함
    // inbox와 Log가 같은 Msg를 가리키므로 포인터가 두 곳에 존재하게 됨.
    // log_n을 증가시켜 다음 log 위치를 사용함.
    b->log[b->log_n++] = m;
}

static void deliver(Broker *b, Subscriber sub)
{
    // 큐가 비어있지 않은 동안 하나씩 꺼낸다.
    while (b->head != b->tail)
    {
        // 현재 Head 위치에 기록된 MSG 주소를 m에 복사함
        // Msg 자체를 복사하는 것이 아니라 Msg 주소만 복사함.
        // inbox[head]에 기록된 Msg 주소를 m에 복사함.
        // Msg 자체를 복사하는 것이 아니라 Msg 주소만 복사함.
        Msg *m = b->inbox[b->head];
        // 다음 메시지를 가리키도록 head를 이동한다.
        b->head = (b->head + 1) % QCAP;
        // sub에 기록된 함수 주소를 호출하고
        // Msg 주소를 인자로 전달함.
        // 사실상 on_message(m);과 같은 호출임.
        sub(m);
    }
}

static void on_message(Msg *m)
{
    printf("recv #%d: %s\n", m->id, m->body);
    fprintf(stderr, "consume free msg=%p\n", (void *)m);
    // deliver()에서 전달받은 Msg의 소유권을 on_message()가 가지고 있다고 가정하여 소비를 끝낸 후 MSG를 해제함.
    msg_free(m);
}

static void broker_shutdown(Broker *b)
{
    for (int i = 0; i < b->log_n; i++)
    {
        fprintf(stderr,
                "audit free log[%d]=%p\n",
                i,
                (void *)b->log[i]);
        // loG[]에 기록된 Msg 주소를 하나씨기 가져와 Msg를 해제함.
        // 하지만 On_message()가 이미 Msg를 해제했으므로
        // 같은 Msg를 다시 해제하게 됨.
        // 현재 코드의 소유권 혼동 UAF/ 이중 해제 발생 지점
        msg_free(b->log[i]);
    }
    // 숫자는 0이 된다.
    b->log_n = 0;
}

int main(void)
{
    // 브로커 구조체를 초기값을 설정한다.
    Broker b = {.head = 0, .tail = 0, .log_n = 0};
    // Publish함수에 b구조체와 id 그리고 body로는 문자열의 주소를 전달한다.
    publish(&b, 1, "hello");
    publish(&b, 2, "world");
    publish(&b, 3, "broker");
    // deliver에서 반환한 함수 주소를 on_message로 전달한다.
    deliver(&b, on_message);

    // log[]에 기록된 Msg를 정리하는 함수
    // 현재 코드는 on_message()가 이미 Msg를 해제했기 때문에
    // broker_shutdown에서 다시 해제하면 이중 해제가 발생함.
    // broker_shutdown(&b);
    // broker_shutdown()을 호출하지 않으므로 현재 실행에서는 이중 해제가 발생하지 않음.
    // 대신 log[]에 남아있는 포인터는 이미 해제된 msg를 가리키는
    // dangling pointer가 됨.
    printf("done\n");

    return 0;
}
