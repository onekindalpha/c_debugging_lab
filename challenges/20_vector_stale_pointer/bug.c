/*
 * Challenge 20 — 동적 배열 성장 후 stale 원소 포인터 (심화: 히스토그램 hot 포인터)
 *
 * [시나리오]
 *   키별 빈도를 세는 히스토그램. 버킷들을 동적 배열(Histogram.data)에 담는다.
 *   자주 갱신되는 버킷 하나의 "주소"를 hot 포인터로 캐시해 두고 빠르게 증가시킨다
 *   (인덱스 재조회 없이 hot->count 만 바로 갱신하는 흔한 최적화).
 *
 * [기대 동작]
 *   스트림의 키들을 모두 반영하고, hot 버킷을 갱신한 값과 전체 합을 출력.
 *
 * [증상]
 *   hot 포인터를 캐시한 뒤에도 새로운 키가 계속 들어와 배열이 성장한다. 성장 중
 *   realloc 이 배열을 "다른 주소로 이동"시키면(큰 배열은 mmap 재배치로 옛 영역이
 *   unmap 됨), 이전에 받아둔 hot 은 무효(stale) 주소를 가리킨다. 그 stale 포인터로
 *   hot->count 를 쓰면 매핑 밖 접근 → SIGSEGV.
 *   함정: 저장한 것이 "인덱스"가 아니라 "원소의 주소"라는 점. 성장 후 주소는 바뀐다.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=20_vector_stale_pointer
 *   (gdb) run                        → 크래시(SIGSEGV)
 *   (gdb) bt                         → hot->count += ... 지점
 *   (gdb) print hot                  → 캐시해 둔 옛 주소
 *   (gdb) print h.data               → 성장 후 새 기준 주소(hot 과 범위가 다름)
 *   (gdb) print (char*)hot - (char*)h.data   → hot 이 현재 버퍼 범위 밖임을 확인
 *
 * [printf(로그)로 잡기]
 *   성장 전/후로 data 기준 주소와 hot 을 비교 출력:
 *     fprintf(stderr, "before grow data=%p hot=%p\n", (void*)h.data, (void*)hot);
 *     ... 성장 ...
 *     fprintf(stderr, "after  grow data=%p hot=%p\n", (void*)h.data, (void*)hot);
 *   → 성장 후 data 주소가 바뀌었는데 hot 이 옛 주소 그대로면 stale.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 원소는 "주소"가 아니라 "인덱스"로 참조하세요(성장 후에도 data[idx] 는 유효).
 *       꼭 포인터가 필요하면 realloc(성장) 직후 반드시 다시 계산하세요.
 */
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    int key;
    long count;
} Bucket;

typedef struct
{
    Bucket *data;
    size_t len, cap;
} Histogram;

//
static void hist_grow(Histogram *h)
{
    // h->cap이 0이면 16으로 설정
    // 이미 용량이 있으면 기존 용량의 2배로 설정
    h->cap = h->cap ? h->cap * 2 : 16;
    // h->cap 개의 Bucket을 저장할 수 있도록 메모리 크기를 변경
    // 첫 호출에서는 h->data = NULL이므로 새 메모리를 할당
    // 이후 호출에서는 기존 배열의 크기를 늘림.
    Bucket *p = realloc(h->data, h->cap * sizeof(Bucket)); /* 큰 배열은 이동(mmap 재배치) */
    // realloc()은 len()을 변경하지 않음.
    // len은 현재 사용 중인 Bucket의 개수이고, hist_add()에서 증가함.
    // realloc()이 실패하면 기존 배열을 해제하고 프로그램 종료.
    if (!p)
    {
        perror("realloc");
        free(h->data);
        exit(1);
    }
    // realloc()이 반환한 메모리 시작 주소를 h->data에 기록.
    h->data = p;
}

/* 키를 추가하고, 그 버킷의 주소를 돌려준다(성장이 일어날 수 있음). */
static Bucket *hist_add(Histogram *h, int key)
{
    // 현재 사용중인 Bucket의 개수와 용량이 같으면 배열을 성장시킴.
    if (h->len == h->cap)
        hist_grow(h);
    // 현재 len 위치의 Bucket 주소를 b에 저장
    // 이후 len을 1 증가시켜 사용중인 Bucket의 개수를 증가시킴.
    Bucket *b = &h->data[h->len++];
    // Bucket이 어떤 키의 빈도를 저장하는지 기록
    b->key = key;
    // 새로 추가된 키이므로 빈도를 0으로 초기화.
    b->count = 0;
    return b;
}

// 모든 Bucket의 count 합계를 반환
static long hist_total(const Histogram *h)
{
    long t = 0;
    // 모든 Bucket을 순회하면서 count를 합산.
    for (size_t i = 0; i < h->len; i++)
        t += h->data[i].count;
    return t;
}

int main(void)
{
    // Histogram 구조체를 생성하고
    // data는 NULL, 사용중인 Bucket 개수와 용량은 0으로 초기화
    Histogram h = {.data = NULL, .len = 0, .cap = 0};

    for (int k = 0; k < 200000; k++)
        // k를 새로운 Key로 추가
        hist_add(&h, k);

    // 100000번 Bucket을 가리킬 인덱스를 저장.
    // 주소가 아닌 인덱스를 저장하므로 realloc()으로 배열이 이동해도 유효함.
    size_t hot_idx = 100000;
    // 테스트를 위해 100000번 Bucket의 count를 1로 설정.
    h.data[hot_idx].count = 1;
    // realloc() 전 현재 동적 배열의 시작 주소와 hot_idx를 확인.
    fprintf(stderr, "before grow data=%p hot_idx=%zu\n",
            (void *)h.data, hot_idx);
    for (int k = 200000; k < 600000; k++)
        // 새로운 key를 추가하면서 배열이 여러 번 성장할 수 있음.
        hist_add(&h, k);
    // 배열 성장 후 현재 동적 배열의 시작 주소와 hot_idx를 호가인
    // data 주소가 바뀌어도 Hot_idx는 그대로 유지됨.
    fprintf(stderr, "after grow data=%p hot_idx=%zu\n",
            (void *)h.data, hot_idx);
    // 배열이 성장한 후에도 인덱스를 사용해 100000번 Bucket의 count를 갱신
    // 기존 hot 포인터 방식과 달리 stale pointer문제가 발생하지 않음.
    h.data[hot_idx].count += 1000;
    // 100000번 Bucket의 count,
    // 모든 Bucket의 count 합계
    // 전체 Bucket 개수를 출력
    printf("hot=%ld total=%ld len=%zu\n", h.data[hot_idx].count, hist_total(&h), h.len);
    // realloc()으로 확보한 동적 배열 메모리를 해제.
    free(h.data);
    // 해제된 메모리의 주소를 다시 사용하지 않도록 NULL로 설정.
    h.data = NULL;
    return 0;
}
