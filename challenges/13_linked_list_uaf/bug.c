/*
 * Challenge 13 — Linked List Use-After-Free (심화: 잡 큐 필터링)
 *
 * [시나리오]
 *   우선순위가 있는 잡(Job)들을 단일 연결 리스트 큐로 관리한다. 스케줄러가
 *   "임계값 미만 우선순위"의 잡을 큐에서 제거(취소)하며, 제거된 잡의 id 를 감사
 *   로그(동적 배열)에 기록한다.
 *
 * [기대 동작]
 *   저우선순위 잡을 모두 제거하고, 취소된 개수와 남은 개수를 출력한 뒤 정상 종료.
 *
 * [증상]
 *   필터 루프가 제거 대상 노드를 job_release()로 free한 뒤, 해제된 노드의 next를
 *   읽어 다음 노드로 이동한다(UAF). free() 이후 해당 메모리의 내용은 더 이상
 *   유효하지 않으며, 메모리 할당기가 관리 정보로 덮어쓸 수도 있다. 따라서 cur->next가
 *   엉뚱한 주소가 되고, 다음 순회에서 그 주소의 필드를 역참조하다 SIGSEGV가 발생한다.
 *   크래시는 다음 순회에서 발생하지만, 근본 원인은 "free 후 next 읽기"다.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=13_linked_list_uaf
 *   (gdb) run                       → 크래시(SIGSEGV)
 *   (gdb) bt                        → filter_jobs 의 cur = cur->next / cur->priority 지점
 *   (gdb) print cur                 → 방금 free 한(또는 그로부터 파생된) 노드 주소
 *   (gdb) print *cur                → 값이 깨져 있거나 next 가 엉뚱한 주소임을 확인
 *
 * [printf(로그)로 잡기]
 *   free 전에 next 를 미리 찍고, free 후 이동한 cur 을 비교:
 *     Job *nx = cur->next;
 *     fprintf(stderr, "free id=%d cur=%p saved_next=%p\n", cur->id,(void*)cur,(void*)nx);
 *     job_release(cur);
 *     fprintf(stderr, "after free, cur->next would read freed memory\n");
 *   → free 뒤 읽은 next 가 saved_next 와 달라지거나 그 직후 크래시.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 노드를 free 하기 "전에" next 를 지역 변수에 저장하고, 그 저장값으로 이동하세요.
 *       (해제된 메모리의 어떤 필드도 읽지 않는다)
 */
#include <stdio.h>
#include <stdlib.h>

typedef struct Job
{
    int id;
    int priority;
    struct Job *next;
} Job;
// 구조체 이름을 안 만들고 별칭만 만듦.
typedef struct
{
    int *ids;        // int 배열 시작 주소 저장하는 포인터.
    size_t len, cap; // 현재 사용중인 원소의 개수와 전체 공간.
} Audit;

static void audit_add(Audit *a, int id)
{
    // 현재 사용중인 원소 개수와 확보된 원소 개수가 같으면
    // 새로운 id를 추가할 공간이 없으므로 메모리를 늘림.
    if (a->len == a->cap)
    {
        // cap이 0이면 16개를 확보하고
        // 이미 공간이 있으면 기존 용량의 2배로 늘림.
        a->cap = a->cap ? a->cap * 2 : 16;
        // 기존 ids 배열의 크기를 새로운 cap에 맞게 변경함.
        // realloc()이 기존 메모리를 그대로 사용할 수도 잇고,
        // 더 큰 공간으로 이동시킬 수도 있음.
        int *p = realloc(a->ids, a->cap * sizeof(int));
        // 메모리 크기 변경에 실패했으면 오류를 출력하고 프로그램을 종료함.
        if (!p)
        {
            perror("realloc");
            exit(1);
        }
        // realloc()이 반환한 메모리 시작 주소를 ids에 기록함.
        a->ids = p;
    }
    // ids의 현재 위치에 id를 추가하고 len을 1 증가시킴.
    a->ids[a->len++] = id;
}

static Job *push_job(Job *head, int id, int priority)
{
    // n이라는 이름의 Job * 포인터 변수를 선언함.
    // Job 구조체 하나를 저장할 수 있는 힙 메모리를 할당하고,
    // malloc()이 반환한 시작주소를 n에 기록함.
    Job *n = malloc(sizeof *n);
    // 메모리 할당에 실패했으면 오류를 출력하고 프로그램을 종료함.
    if (!n)
    {
        perror("malloc");
        exit(1);
    }
    // n이 가리키는 Job 구조체의 Id와 priority에 값을 대입함.
    n->id = id;
    n->priority = priority;
    // 새로 만든 Job의 next가 기존 head가 가리키던 Job을 가리키게 함.
    n->next = head;
    // Push_job()은 새로 만든 Job의 주소를 반환함.
    return n;
}

/* 취소된 잡을 반납한다(해제 책임은 이 함수가 진다). */
// 해제 책임은 이 함수가 진다는게 무슨 말이지
static void job_release(Job *j)
{
    free(j);
}

static Job *filter_jobs(Job *head, int threshold, Audit *audit)
{
    // 남길 Job 리스트의 첫번째와 마지막 Job을 가리킴.
    Job *keep = NULL, *keep_tail = NULL;
    // 기존 리스트의 첫번째 Job부터 시작함.
    Job *cur = head;
    // 모든 Job을 순서대로 확인함.
    while (cur != NULL)
    {
        // 현재 Job의 우선순위가 임계치보다 낮으면
        if (cur->priority < threshold)
        {
            // 감사로그에 해제할 현재 Job의 ID를 기록함.
            audit_add(audit, cur->id);
            // 현재 Job의 다음 Job 주소를 미리 확보함.
            Job *nx = cur->next;
            // 현재 Job을 해제함.
            job_release(cur);
            // 미리 확보한 다음 Job으로 이동함.
            cur = nx;
        }
        // 현재 Job 우선순위가 임계치 이상이면 남김.
        else
        {
            // 현재 Job의 다음 Job 주소를 미리 확보함.
            Job *nx = cur->next;
            // 프린트 로그 추가
            fprintf(stderr, "free id=%d cur=%p saved_next=%p\n", cur->id, (void *)cur, (void *)nx);
            // 현재 Job을 기존 리스트에서 분리함.
            cur->next = NULL;
            // 여긴 왜 또 해제를 하는거지.
            // job_release(cur);
            // 프린트 로그 추가
            fprintf(stderr, "after free, cur->next would read freed memory\n");
            // 현재 Job을 남길 리스트의 마지막에 연결함.
            if (keep_tail)
                keep_tail->next = cur;
            else
                keep = cur;
            // 현재 Job을 남길 리스트의 마지막 Job으로 설정함.
            keep_tail = cur;
            // 미리 확보한 다음 Job으로 이동함.
            cur = nx;
        }
    }
    return keep;
}

int main(void)
{
    // Job 연결리스트가 아직 비어 있으므로, 첫번째 Job을 가리키는 head를 NULL로 초기화한다.
    Job *head = NULL;
    for (int i = 1; i <= 4000; i++)
        // push_job()이 새로만든 Job의 주소를 반환하고
        // 반환된 주소를 head에 대입하여 새로운 Job이 첫번째 노드가 됨.
        // (1*7) % 10은 priority 값을 0~9 범위로 반복해서 만들기 위한 계산임.
        head = push_job(head, i, (i * 7) % 10);
    // Audit 구조체의 모든 멤버를 0으로 초기화한다.
    // ids는 포인터이므로 초기화하면 NULL이 된다. 즉, 포인터가 아무 객체도 가리키지 않는 상태다.
    Audit audit = {0};
    // 여기서 임계값을 5로 넘기는 것인가. 다 지금 빈공간인데 audit의 주소를 넘기고 있음.
    head = filter_jobs(head, 5, &audit);
    // 리스트를 처음부터 끝까지 이동하면서 남아있는 Job 개수를 세는 것.
    int remaining = 0;
    // c가 head에서 시작해서 NULL이 될 때까지 반복함.
    // c는 현재 Job을 가리키고, c = c->next로 다음 Job으로 이동함.
    // (c를 head로 시작; c가 NULL이 아니면 반복; 다음 Job으로 이동)
    for (Job *c = head; c; c = c->next)
        remaining++;
    // 취소된 Job의 개수와 남아있는 Job의 개수를 출력함.
    printf("cancelled=%zu remaining=%d\n", audit.len, remaining);
    // 동적으로 할당한 검사 ID 배열을 해제함.
    free(audit.ids);
    // 리스트에 남아 있는 Job을 처음부터 하나씩 해제함.
    for (Job *c = head; c;)
    {
        // 현재 Job의 다음 Job의 주소를 미리 확보함.
        Job *nx = c->next;
        // 현재 Job을 해제함.
        free(c);
        // 미리 확보해둔 다음 Job으로 이동함.
        c = nx;
    }
    return 0;
}
