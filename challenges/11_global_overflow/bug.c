/*
 * Challenge 11 — Global Buffer Overflow (심화: 전역 아레나 bump 할당기)
 *
 * [시나리오]
 *   고정 크기 전역 버퍼(arena, .bss)를 "bump 포인터" 방식으로 나눠 쓰는 초간단
 *   할당기. 문자열 인터너(intern)가 들어온 문자열을 아레나에 복사해 보관한다.
 *
 * [기대 동작]
 *   문자열을 차례로 아레나에 인터닝하고, 마지막 문자열과 전체 길이 합을 출력한 뒤 정상 종료.
 *
 * [증상]
 *   arena_alloc() 이 남은 공간을 검사하지 않고 offset 만 증가시킨다(bump). 문자열을
 *   계속 인터닝하면 offset 이 ARENA_SIZE 를 넘어, 반환 포인터가 전역 배열 경계 밖을
 *   가리키게 되고 그 위치에 memcpy 로 쓰면서 .bss 를 벗어나 매핑되지 않은 영역까지
 *   침범 → SIGSEGV. 크래시는 intern 의 memcpy 에서 나지만, 원인은 "경계 미검사 bump".
 *
 * [gdb 로 잡기]
 *   make gdb NAME=11_global_overflow
 *   (gdb) run                          → 크래시(SIGSEGV)
 *   (gdb) bt                           → intern 의 memcpy 지점
 *   (gdb) print arena_off              → ARENA_SIZE 를 한참 초과했는지 확인
 *   (gdb) print (long)arena_off - (long)sizeof(arena)   → 경계에서 얼마나 넘었는지
 *   (gdb) print &arena[0]              → 반환 포인터가 arena 범위를 벗어났는지 대조
 *
 * [printf(로그)로 잡기]
 *   할당 때마다 offset 과 용량을 비교 출력:
 *     fprintf(stderr, "alloc n=%zu off=%zu cap=%zu\n", n, arena_off, sizeof(arena));
 *   → off 가 cap 을 넘어서도 계속 커지면 경계를 벗어난 것.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: arena_alloc() 에서 (arena_off + n <= sizeof(arena)) 를 반드시 검사하고,
 *       공간이 부족하면 NULL 반환 또는 오류 처리하세요.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_SIZE 4096
/* [Thinking Point]
 * 이 arena[] 는 함수 밖에 선언된 '전역 변수'다. 만약 이걸 arena_alloc() 함수 '안'의
 * 지역 변수로 옮기면 무슨 차이가 생길까?
 *   tip 1. 저장 위치가 다르다. 전역/static 은 프로그램 내내 사는 .bss/.data 영역에,
 *          지역 변수는 함수가 실행되는 동안만 사는 '스택'에 놓인다.
 *   tip 2. 수명이 다르다. 전역은 프로그램 시작~끝까지 유지되지만, 지역은 함수가 return
 *          하면 사라진다. → 그 주소를 함수 밖으로 돌려주면 7번(stack use-after-return)!
 *   tip 3. 초기화가 다르다. 전역/static 은 자동으로 0 으로 초기화되지만(그래서 .bss),
 *          지역 변수는 초기화하지 않으면 쓰레기 값이다(8번 챌린지).
 *   생각해보기: 여러 번 호출돼도 같은 저장소를 계속 나눠 쓰려면(커서 arena_off 유지)
 *               이 버퍼는 왜 전역(또는 static)이어야 할까? -> 그니까 그게 궁금하네. */

// 전역/static 변수는 초기값을 지정하지 않아도 c언어 규칙에 의해 0으로 초기화된다. 그런 변수의 저장 영역이 일반적으로 .bss에 배치된다.
static unsigned char arena[ARENA_SIZE]; /* 전역(.bss) 아레나 */
// Size_t는 메모리 크기나 배열의 크기, 인덱스 등을 표현하기 위한 부호 없는 정수형임.
// 메모리 크기는 음수가 될 수 없기때문에 주로 사용함.
static size_t arena_off = 0;

static void *arena_alloc(size_t n)
{
    // 만약에 arena_off에 n을 더한 것이 sizeof(arena)를 초과하면 null을 반환하기로 한다.
    // 끝까지 다 사용하는 경우는 정상이기 때문.
    if (arena_off + n > sizeof(arena))
        return NULL;
    // p는 매개변수가 없는? 전역 아레나의 주소에 arena_off를 담는다.
    // 일단 처음에는 arena_off 는 0이니까 거기를 시작주소로 해서 , arena_off의 수를 더한다.
    // 그리고 시작주소를 반환한다.
    void *p = &arena[arena_off];
    // arena_off는 0에서 시작해서, n(그러니까 intern에서 전달받은 의미로는 배열 buf의 전체 길이만큼) 더한다.
    arena_off += n;
    return p;
}

static char *intern(const char *s)
{
    // main에서 전달된 배열 buf의 길이를 구하는데, 널 문자까지 포함한 길이를 n으로 한다.
    size_t n = strlen(s) + 1;
    // [gdb 디버깅] 에러가 뜨는데. 0x7f7f7f7f7f7f7f7f <error: Cannot access memory at address 0x7f7f7f7f7f7f7f7f>
    // [디버거] 근데 희한한건 n =9에서 13까지 갔다가 다시 되돌아오고 그럼.
    // 배열 buf의 길이를 매개변수로 전달하여, dst 포인터변수를 만든다.
    char *dst = arena_alloc(n);
    // dst이름 때문에 헷갈리기는 헷는데 어쨌든, 아레나의 시작주소에서 시작을 해서, main에서 전달된 배열 buf를 넣고, n만큼
    // memcpy(복사할 데이터를 넣을 아레나의 시작 주소, 복사할 데이터가 있는 스택 메모리(지역변수) 시작 주소, 복사할 바이트 수 (널 포함이라))
    // 만약에 dst가 올바르지 않은 값이면? 아레나의 시작 주소가 아레나의 경계를 넘어선 값이면 복사를 하지 않고
    if (dst != NULL)
        memcpy(dst, s, n); /* 경계를 넘은 위치면 여기서 크래시 */
    // dst가 가지고 있는 주소값을 호출한 곳으로 돌려줌.
    // 공간 부족하면 dst가 NULL이므로 NULL이 반환됨.
    return dst;
}

int main(void)
{
    // 문자열 리터럴을 수정할 수 없도록 가리키는 포인터들의 배열
    const char *words[] = {
        "insert",
        "delete",
        "search",
        "traverse",
        "balance",
        "rotate",
        "rehash",
        "compact",
        "serialize",
        "checkpoint",
    };
    // words 배열 전체 크기를 원소 하나의 크기로 나누어 전체 원소 개수를 구한다.
    int nwords = (int)(sizeof(words) / sizeof(words[0]));
    // 마지막으로 intern()이 반환한 문자열 주소를 저장할 포인터를 NULL로 초기화한다.
    char *last = NULL;
    // total은 long타입의 변수이고, strlen(last)로 구한 문자열 길이를 누적한다.
    long total = 0;
    // i를 0부터 99999까지 증가시키면서 총 100000번 반복한다.
    for (int i = 0; i < 100000; i++)
    {
        // 최대 char 32개를 저장할 수 있는 배열 buf를 만든다. 인덱스는 0~31이고 전체 크기는 32바이트이다.
        // buf는 지역배열로. 일반적으로 스택 영역에 만들어짐.

        char buf[32];
        // snprintf()는 문자열을 버퍼에 만들되, 버퍼 크기를 지정해서, 최대 길이를 제한하는 함수임.
        // snprintf()는 가변인자 함수임. sizeof(buf)는 32바이트임.
        // %s에 words[i % nwords]가 들어가고 , %d에 i가 들어감
        // 이 코드는 반복문마다 intern()에 넘길 문자열을 하나 만드는 것임.
        // words[i % nwords] 하면 문자열 순환 선택임.
        // Ex. search-12와 같은 문자열이 생성됨. (i와 문자열을 "-" 형식으로 결합)
        // intern()에는 snprintf()가 buf에 만들어놓은 문자열이 들어감.
        snprintf(buf, sizeof buf, "%s-%d", words[i % nwords], i);
        // Ex. i =0, buf -> "insert-0", i = 1, buf -> "delete-1",
        // buf를 intern()에 전달하고, intern()이 반환한 문자열 주소를 last에 저장한다. 기록한다.
        last = intern(buf);
        // intern이 null을 반환하는 경우도 고려해야 함.
        if (last = NULL)
            fprintf(stderr, "arena full\n");
        return 1;
        // strlen(last)로 last 문자열의 길이를 구하고, (long) 으로 Long타입으로 변환한 뒤 total에 누적한다.
        // strlen()의 문자열 길이에는 마지막 '\0'이 포함되지 않는다.
        total += (long)strlen(last);
    }
    // last가 가리키는 문자열과 Total에 누적된 전체 문자열 길이를 출력한다.
    // %s는 문자열, %ld는 long 타입 정수를 출력한다.
    printf("interned, last=%s total_len=%ld\n", last, total);
    return 0;
}
