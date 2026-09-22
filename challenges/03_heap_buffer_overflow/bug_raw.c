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
 *   정작 realloc 은 "옛 용량(l->cap)" 으로 호출한다. 즉 논리 용량(cap)은 커지는데
 *   실제 버퍼는 한 세대 뒤처져, push 가 실제 버퍼 밖으로 계속 쓴다.
 *   힙 경계를 넘어 쓰면서 힙 메타데이터가 깨지거나(→ 이후 realloc/free 에서 SIGABRT)
 *   매핑되지 않은 페이지까지 밀고 나가 SIGSEGV. 크래시는 push 의 대입 지점 또는
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
 *
 * TODO: realloc 은 반드시 "새 용량(newcap)" 으로 호출하고, l->cap 갱신과 순서를 맞춰야 한다.
 *       (성장 로직은 '용량 필드'와 '실제 확보량'이 항상 같도록 유지해야 한다)
 */
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
  int *data;
  /* [Thinking Point]
   * 개수/크기를 담는 len, cap 을 왜 int 가 아니라 size_t 로 선언할까?
   *   tip 1. size_t 는 "이 플랫폼에서 표현 가능한 가장 큰 객체 크기"를 담도록 만든
   *          부호 없는(unsigned) 정수 타입이다. malloc/sizeof/strlen 의 타입도 size_t 다.
   *   tip 2. int 는 보통 32비트라 약 21억(2^31-1)에서 넘치고, 음수도 가능하다.
   *          원소가 그보다 많아지거나 cap*sizeof(int) 계산이 커지면 int 는 오버플로된다.
   *   생각해보기: 크기를 int 로 두면 어떤 버그가 생길 수 있을까?
   */
  size_t len;
  size_t cap;
} IntList;

static void list_init(IntList *l)
{
  l->cap = 8; // 할당된 용량 정보 초기화
  l->len = 0; // 원소 개수 초기화

  // 할당할 바이트수를 인자로 줌.
  // l->cap을 주는 이유는 배열에 몇 개의 int를 담을 공간을 만들지 나타내는 값이 cap임
  l->data = malloc(l->cap * sizeof(int));
  if (!l->data)
  {
    // 직전에 발생한 시스템 라이브러리 오류를 사람이 읽을 수 잇는 메시지로 출력하는 함수임.
    // 이걸 테스트하고 싶으면 size_t newcap = SIZE_MAX; 처럼 비정상적으로 큰 값을 넣는 방법이 있음
    // 정상적인 환경에서는 조건문 밖에 잠깐 두면 됨.
    perror("malloc");
    // 프로그램 전체를 즉시 종료시킴.
    exit(1);
  }
}

static void list_ensure(IntList *l, size_t need)
{
  if (need <= l->cap)
    return;
  // 삼항 연산자임. 조건 ? 참일 때 값 : 거짓일 때 값
  // 기존 용량이 있으면 2배로 늘리고, 기존 용량이 0이면 처음 용량을 8로 설정한다.
  size_t newcap = l->cap ? l->cap * 2 : 8;
  while (newcap < need)
    newcap *= 2;
  // 리얼록은 기존 포인터와 새로 필요한 전체 바이트 크기를 전달함.
  int *p = realloc(l->data, l->cap * sizeof(int));
  // p == NULL일때만 perror()가 실행됨.

  if (!p)
  {
    // 직전에 발생한 시스템 라이브러리 오류를 사람이 읽을 수 잇는 메시지로 출력하는 함수임.
    perror("realloc");
    free(l->data);
    exit(1);
  }
  l->cap = newcap;
  l->data = p;
}

// 값 하나를 리스트에 추가하는 함수임.
static void list_push(IntList *l, int x)
{
  // l->cap은 배열에 들어있는 원소 수와 l->cap은 현재 할당된 공간이 수용할 수 있는 원소 개수로
  // 현재 할당된 공간을 다 사용한 상태라서 용량을 늘리는 것임.
  // 만약에 공간이 비어있지 않다면
  if (l->len == l->cap)
    // 전체 공간을 늘리는 역할
    // 여기 내부에서 realloc을 함.
    list_ensure(l, l->cap + 1);
  // 배열의 현재 빈칸에 x를 넣고 len을 1 증가시킨다.
  // 새로운 x 하나를 배열에 넣음.
  // 확보된 공간에 실제로 x를 넣음.
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