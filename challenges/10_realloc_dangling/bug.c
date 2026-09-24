/*
 * Challenge 10 — realloc 후 옛 포인터 사용 (심화: undo 스냅샷 댕글링)
 *
 * [시나리오]
 *   정수 편집 버퍼 EditBuffer. 내용이 커지면 eb_grow() 가 realloc 으로 버퍼를 키운다.
 *   "실행 취소(undo)"를 위해 eb_snapshot() 이 현재 상태를 undo[] 에 저장한다.
 *
 * [기대 동작]
 *   스냅샷을 찍고 값을 많이 추가한 뒤, 정리(eb_free)에서 누수 없이 해제하고 정상 종료.
 *
 * [증상]
 *   eb_snapshot() 이 저장하는 것은 "그 시점의 data 포인터(원시 주소)"다. 이후 eb_grow()
 *   가 realloc 으로 버퍼를 옮기면(주소 변경), 저장해 둔 스냅샷 포인터는 '이미 해제된
 *   옛 블록'을 가리키게 된다(댕글링). 정리 시 eb_free() 는 현재 data 를 해제한 뒤
 *   undo[] 의 옛 포인터들도 free 하는데, 그 블록들은 realloc 이 이미 해제한 것이라
 *   → double free / invalid pointer 로 glibc abort(SIGABRT).
 *
 * [gdb 로 잡기]
 *   make gdb NAME=10_realloc_dangling
 *   (gdb) run                       → abort
 *   (gdb) bt                        → eb_free 의 free(e->undo[i]) 지점
 *   (gdb) print e->undo[i]          → 이 주소가 현재 data 와 다른 '옛' 주소임을 확인
 *   (gdb) break eb_grow             → realloc 전후 e->data 주소가 바뀌는지 관찰
 *
 * [printf(로그)로 잡기]
 *   grow 에서 realloc 전후 주소를, 스냅샷/해제 시 저장/해제 주소를 찍어 대조:
 *     (grow)     fprintf(stderr, "grow old=%p new=%p\n", (void*)old, (void*)e->data); // old 추가 후 확인
 *     (snapshot) fprintf(stderr, "snap  save=%p\n", (void*)e->data);
 *     (free)     fprintf(stderr, "free  undo[%d]=%p\n", i, (void*)e->undo[i]);
 *   → snapshot 이 저장한 주소가 grow 에서 이동해 이미 해제된 뒤, free 에서 다시
 *     그 주소를 해제하면 이중 해제.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 스냅샷은 "원시 버퍼 포인터"가 아니라 내용의 '복사본'을 따로 소유해야 한다.
 *       (예: 스냅샷 시 malloc+memcpy 로 별도 버퍼를 만들고, 그 복사본만 해제)
 *       realloc 이후에는 옛 포인터를 절대 사용/해제하지 말 것.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_UNDO 8
typedef struct
{
    int *data;       // Int 배열의 첫번째 원소를 가리키는 포인터.
    size_t len, cap; // 크기나 개수를 표현하는 정수형 타입
                     // len은 현재 원소 개수, cap은 확보된 원소 공간의 개수
    int *clipboard;  // Int 배열을 가리키는 포인터
    // undo[0]-> int * , undo[1] -> int * , undo[2] -> int * 이런 식으로 들어감.
    // 이전 상태로 되돌리기 위해 스냅샷들을 보관하는 배열
    int *undo[MAX_UNDO]; // int를 가리키는 포인터 8개를 담는 배열
    int undo_n;          // 현재 스냅샷 개수이자, 다음 스냅샷을 저장할 배열 인덱스
    // undo_n = 0 , 0개 들어있음, 다음은 undo[0], undo_n = 1, 1개 들어있음. 다음은 unod[1], undo_n = 2, 2개 들어있음. 다음은 undo[2]임.
} EditBuffer;

// 초기 상태를 설정한다.
// e->cap개의 int를 저장할 수 있는 공간을 malloc으로 할당한다.

static void eb_init(EditBuffer *e)
{
    e->cap = 4;    // 처음 확보할 int 원소 공간의 개수 - 현재 확보해 놓은 공간에 몇개의 Int를 넣을 수 있는가
    e->len = 0;    // 현재 사용중인 int 원소 개수 - 현재 들어있는 Int 개수
    e->undo_n = 0; // 현재 0개 들어있고, 시작할 인덱스는 Undo[0]이 됨.
    // e->data에는 말록으로 int 4개를 저장할 별도 공간
    e->data = malloc(e->cap * sizeof(int)); // int 4개를 저장할 별도 공간을 할당한다.
    if (!e->data)
    {
        perror("malloc");
        exit(1);
    }
    /* data 바로 뒤에 놓이는 별도 할당을 두어, data 가 힙 맨 끝(top)이 아니게 되어
        데이터가 제자리에서 확장되기 어려운 상황을 만든다.
        이후 realloc 이 제자리 확장 대신 '이동'을 택하게 만든다(→ 옛 블록 해제)
        realloc이 새로운 위치로 Data를 이동하면, 기존 data 블록은 Realloc에 의해 해제된다.
        . */
    e->clipboard = malloc(e->cap * sizeof(int));
    if (!e->clipboard)
    {
        perror("malloc");
        exit(1);
    }
}

static void eb_snapshot(EditBuffer *e)
{
    if (e->undo_n < MAX_UNDO)
    // 현재 사용중인 데이터의 복사본을 위한 공간을 할당한다.
    // e->data의 현재 데이터 len개를 snapshot으로 복사한다.
    // 복사본의 주소를 Undo에 저장한다.
    // [수정 전 코드] e->undo[e->undo_n++] = e->data;
    // 이후 undo_n을 1 증가시킨다.
    // undo[0]부터 udno[7]까지 사용할 수 있다.
    // [에러를 수정한 코드] 스냅샷마다 별도의 힙 공간을 할당하고 배열 내용을 복사한다.
    {
        int *snapshot = malloc(e->len * sizeof(int));
        memcpy(snapshot, e->data, e->len * sizeof(int));
        e->undo[e->undo_n++] = snapshot;
    }
}

static void eb_grow(EditBuffer *e, size_t need)
{
    // 현재 cap 값을 nc에 복사한다.
    // 이후 새로운 용량을 계산할 때 사용한다.
    size_t nc = e->cap;
    // 필요한 용량보다 작으면 Nc를 2배한다.
    // 필요할 용량 이상이 될때까지 반복한다.
    while (nc < need)
        nc *= 2;
    // 기존 data가 가리키는 메모리 크기를 nc개의 int를 저장할 수 있는 크기로 변경한다.
    // realloc이 반환한 주소를 P에 받는다.
    int *p = realloc(e->data, nc * sizeof(int));
    // realloc이 실패해서 NULL을 반환하면
    if (!p)
    {
        perror("realloc");
        // realloc 실패 시 기존 e->data가 가리키는 메모리는 아직 살아있음.
        // 프로그램을 종료하기 전에 기존 data를 직접 해제한다.
        free(e->data);
        // 오류 상태를 나타내며 프로그램을 종료한다.
        exit(1);
    }
    // realloc이 반환한 주소를 Data에 반영한다.
    e->data = p;
    // 새로 확보한 용량을 cap에 반영한다.
    e->cap = nc;
}

static void eb_push(EditBuffer *e, int v)
{
    // 현재 원소 개수와 확보된 공간의 개수가 같으면
    // 새로운 원소를 넣을 공간이 없으므로 용량을 늘린다.
    if (e->len == e->cap)
        // 새로운 원소 하나를 추가하기 위해 필요한 용량을 Len+1로 계산해서 전달한다.
        eb_grow(e, e->len + 1);
    // v는 Data 배열에 추가할 정수값
    // 현재 Len 위치에 v를 넣고, Len을 1 증가시킨다.
    e->data[e->len++] = v;
}

static void eb_free(EditBuffer *e)
{
    // 현재 data가 가리키는 메모리를 해제한다.
    free(e->data);
    // clipboard가 가리키는 메모리를 해제한다.
    free(e->clipboard);
    // 현재까지 저장된 스냅샷 개수만큼 반복한다.
    for (int i = 0; i < e->undo_n; i++)
    {
        // undo[i] 가 가리키는 메모리를 해제한다.
        // 현재 코드에서는 이미 해제된 옛 data를 가리킬 수 있다.
        free(e->undo[i]);
    }
    // 스냅샷 개수를 0으로 초기화한다.
    // 다음 스냅샷은 undo[0]에 저장하게 된다.
    e->undo_n = 0;
    // data 포인터에 NULL을 대입힌다.
    e->data = NULL;
}

int main(void)
{
    // EditBuffer 타입의 변수를 e라는 이름으로 만든다.
    EditBuffer e;
    // &e를 넘겨준 것이고, 그 &e가 EditBuffer * 타입의 값이다.
    eb_init(&e);
    // I가 0, 1, 2일때 각각 i를 eb_push의 v로 전달한다.
    // eb_push는 전달받은 V를 data에 추가하고, len을 1 증가시킨다.
    for (int i = 0; i < 3; i++)
        eb_push(&e, i);
    // 현재 data의 주소를 Undo[0]에 저장한다.
    // 배열 내용은 복사하지 않는다.
    eb_snapshot(&e);
    // i가 0부터 3999까지 증가하면서 총 4000개의 정수를 추가한다.
    for (int i = 0; i < 4000; i++)
        eb_push(&e, i);
    // 현재 원소 개수, 확보된 용량, 첫번째 원소, 마지막 원소를 출력한다.
    printf("len=%zu cap=%zu head=%d tail=%d\n",
           e.len, e.cap, e.data[0], e.data[e.len - 1]);
    // 구조체가 가리키는 동적 메모리를 해제한다.
    eb_free(&e);
    // 정리가 정상적으로 완료되었음을 출력한다.
    printf("done\n");
    // main을 정상 종료한다.
    return 0;
}
