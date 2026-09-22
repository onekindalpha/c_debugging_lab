/*
 * Challenge 03 — Heap Buffer Overflow (심화: 동적 배열 성장 버그)
 *
 * [시나리오]
 *   자동 성장하는 정수 동적 배열 IntList (init/ensure/push/sum). 용량이 부족하면
 *   list_ensure() 가 용량을 2배로 늘리고 realloc 한다. 이 리스트로 큰 수열을
 *   만들어 합을 구한다.
 *
 * [기대 동작]
 *   0..N-1 을 100 으로 나눈 나머지를 리스트에 넣고, 길이·용량·합을 출력한 뒤 정상 종료.
 *
 * [증상]
 *   list_ensure() 가 새 용량(newcap)을 계산해 l->cap 에는 반영하지만,
 *   정작 realloc 은 "옛 용량(l->cap)" 으로 호출한다.
 *   즉 논리 용량(cap)은 커지는데
 *   (cap값을 먼저 키워 놓고, realloc()에는 키우기 전 값을 넣었기 때문에 발생함)
 *   실제 버퍼는 한 세대 뒤처져, push 가 실제 버퍼 밖으로 계속 쓴다.
 *   (capacity가 나타내는 크기와 실제 malloc/realloc로 확보된 버퍼 크기가 서로 다르다)
 *   힙 경계를 넘어 쓰면서 힙 메타데이터가 깨지거나(→ 이후 realloc/free 에서 SIGABRT)
 *   (오버플로가 메모리에서 어디까지 도달하느냐에 따라 달라짐. 경계를 넘어간 바로 다음 공간이 무엇이냐가 중요함. 버퍼 뒤에 다른 데이터가 있을 수도 있고, 다음 할당 블록이 있을 수도 있고, 힙 관리자가 사용하는 메타데이터가 있을수 도 있음) (예를 들어서, 다음 블록의 관리 정보가 있는 영역까지 덮어쓰면 힙 메타데이터가 깨질 수 있음. )
 *     (예를 들어 요청한 것은 4개인데 그 5번째 원소를 쓰는 순간 버퍼 오버플로우가 발생한다. )
 *  (보통 프로그램이 크기를 잘못 관리해서 그렇다. )
 *  (보통 push(50)를 하려면 ensure()가 공간을 늘려야 하는데 버그가 있으면 실제 버퍼는 여전히 4칸일 수 있음. 그래서 실제 확보된 공간 이상으로 버퍼 공간을 넘어 쓸 수 있음.그러한 불일치가 핵심 원인임) 내가 Malloc()으로 할당받은 메모리 블록의 끝을 넘어 쓴다.
 * (그래서 내가 할당받은 블록의 끝을 넘는다. 인접 메모리를 덮어쓸 수 있다. )
 * (힙 관리 정보는 malloc을 호출할때 메모리 할당자가 내부적으로 해당 메모리 블록을 관리하기 위한 정보도 함께 고나리를 함. 그래서 또 다른 블록이 있으면 공간 관리 정보 다음 블록의 공간 같은 모습이 될 수 있음)
 *   (첨언: 동적 배열에서 배열 범위를 넘겨쓰는 상황을 말함)
 *  (예를 들어서 주소가 여전히 프로세스에 매핑되어있는 정상적인 메모리라면 그냥 써질 수 있음)
 *   (예를 들어서 잘못 덮어쓴 순간에는 그 정보를 검사하지 않다가, free(arr)를 하면 malloc의 관리 코드가 이 블록을 반환해야 하니까 이 블록의 상태와 주변 정보를 확인하고 정리해야겠다라고 동작을 하고 그 과정에서 앞에서 망가뜨린 내부 정보가 비정상적이라는 것을 발견할 수 있음)
 *  (그리고 abort()를 호출해서 힙 내부 상태 이상을 발견하면 abort()를 통해 SIGABRT가 될 수 있음)
 *  (realloc()도 메모리 블록 크기를 조정해야 하기 때문에 내부 정보를 확인하고 벼녁ㅇ하는 과정에서 같은 문제가 발견될 ㅜㅅ 있음. 발견된 손상이 free())
 *   매핑되지 않은 페이지까지 밀고 나가 SIGSEGV. 크래시는 push 의 대입 지점 또는
 *    (SIGSEGV는 뭐냐면 버퍼 밖으로 쓰기는 했는데 계속 잘못된 주소를 따라가다가 아예 프로세스에 매핑되지 않은 페이지에 접근하는 경우를 말함. 계속 잘못된 주소 접근 매핑되지 않은 페이지. 운영체제가 접근을 차단함. 프로세스가 접근할 수 있도록 가상 주소와 실제 메모리/페이지가 연결되어 있지 않은 영역)
 *   (메모리 범위를 너무 많이 벗어나서, 아예 존재하지 않는 메모리 주소까지 접근하는 상황을 말함)
 *   다음 realloc 에서 나지만, 원인은 ensure 의 realloc 인자다.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=03_heap_buffer_overflow
 *   (gdb) run                         → 크래시(SIGSEGV) 또는 abort
 *   (gdb) bt                          → list_push 의 l->data[l->len]=x 또는 realloc 내부
 *   (gdb) frame N ; print *l           → cap 은 큰데 실제 버퍼는 그보다 작음(불일치)
 *   (gdb) print l->len  / print l->cap → len 이 실제 확보량을 넘어섰는지 확인
 *   (gdb) break list_ensure           → newcap 과 realloc 에 넘기는 크기를 대조
 *
 * [printf(로그)로 잡기]
 *   ensure 에서 (old cap, newcap, realloc 에 넘기는 크기) 를 함께 찍어 불일치를 본다:
 *     fprintf(stderr, "ensure old=%zu new=%zu realloc_bytes=%zu\n",
 *             l->cap, newcap, l->cap * sizeof(int));
 *   → newcap 과 realloc 크기가 다르면 그게 원인.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *  printf로 출력한 내용은 바로 터미널에 나타난다는 보장이 없어서, 프로그램이 크래시하면 마지막 로그가 화면에 안 보일 수 있다. stdrr는 보통 즉시 출력되기 때문에 디버깅 로그에 사용한다.
 * stdout에는 버퍼가 있음.
 * stdout은 버퍼링 될 수 있으므로 크래시 직전까지 실행되 노륵를 확실하게 확인하려면 stderr에 디버깅 로그를 출력하는 것이 유리하다.
 * TODO: realloc 은 반드시 "새 용량(newcap)" 으로 호출하고, l->cap 갱신과 순서를 맞춰야 한다.]
 * 동적 배열ㄹ의 크기를 넣을때, 기존 cap이 10이면, 실제 메모리도 20개짜리로 늘려야 함.
 *  실제 메모리 크기와 l->cap의 값이 일치해야 한다.
 *       (성장 로직은 '용량 필드'와 '실제 확보량'이 항상 같도록 유지해야 한다)
 * (그니까 순서는 newcap을 먼저 계산하고 realloc()으로 실제 메모리를 newcap만큼 확보하고, 성공하면 l->cap을 newcap으로 변경한다.
 */
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    int *data;
    /* [Thinking Point]
     * 개수/크기를 담는 len, cap 을 왜 int 가 아니라 size_t 로 선언할까?
     *   tip 1. size_t 는 "이 플랫폼에서 표현 가능한 가장 큰 객체 크기"를 담도록 만든
     *    부호 없는(unsigned) 정수 타입이다. malloc/sizeof/strlen 의 타입도       size_t 다.
     * size_t가 메모리 크기와 배열의 크기를 표현하기 위해 사용하는 unsigened정수형이기 때문임.
     *  현재 원소 개수
     * 할당된 원소 개수
     * size_t는 음수를 표현할 수 이ㅓㅂㅅ다.
     *  int에는 음수가 잇으니까 정상적으로 -1이 됨.
     *
     *   tip 2. int 는 보통 32비트라 약 21억(2^31-1)에서 넘치고, 음수도 가능하다.
     *          원소가 그보다 많아지거나 cap*sizeof(int) 계산이 커지면 int 는 오버플로된다.
     *   생각해보기: 크기를 int 로 두면 어떤 버그가 생길 수 있을까?
     *  큰 len이나 cap을 표현하거나 cap * sizeof(int)처럼 메모리 크기를 계산할 때 int의 범위를 초과하여 오버플로가 발생하고, 잘못된 메모리 크기가 계산될 수 있다.
     */
    size_t len;
    size_t cap;
} IntList;

static void list_init(IntList *l)
{
    l->cap = 8;
    l->len = 0;
    // 여기서 말록
    l->data = malloc(l->cap * sizeof(int));
    if (!l->data)
    {
        perror("malloc");
        exit(1);
    }
}

static void list_ensure(IntList *l, size_t need)
{
    if (need <= l->cap)
        return;
    // size_t를 계싼함 원래 있는 변수인가?
    size_t newcap = l->cap ? l->cap * 2 : 8;
    while (newcap < need)
        newcap *= 2;
    // p라는 포인터 변수로 리얼록을 줌. 근데 l->data, ;->cap은
    l->cap = newcap;
    int *p = realloc(l->data, l->cap * sizeof(int));
    if (!p)
    {
        perror("realloc");
        free(l->data);
        exit(1);
    }
    // 포인터 변수는 왜 밑에서 바꾸지
    l->data = p;
}

static void list_push(IntList *l, int x)
{
    if (l->len == l->cap)
        list_ensure(l, l->cap + 1);
    l->data[l->len++] = x;
}

static long long list_sum(const IntList *l)
{
    long long s = 0;
    for (size_t i = 0; i < l->len; i++)
        s += l->data[i];
    return s;
}

static void list_free(IntList *l)
{
    free(l->data);
    l->data = NULL;
    l->len = l->cap = 0;
}

int main(void)
{
    IntList l;
    list_init(&l);

    const int N = 2000000;
    for (int i = 0; i < N; i++)
    {
        list_push(&l, i % 100);
    }

    printf("len=%zu cap=%zu sum=%lld\n", l.len, l.cap, list_sum(&l));
    list_free(&l);
    return 0;
}
