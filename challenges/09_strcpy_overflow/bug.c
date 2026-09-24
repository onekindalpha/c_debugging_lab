/*
 * Challenge 09 — strcpy 힙 오버플로 (심화: join 크기계산 off-by-one)
 *
 * [시나리오]
 *   여러 조각(parts)을 구분자 없이 이어 붙여 하나의 문자열을 만드는 join().
 *   필요한 크기를 먼저 계산(joined_size)해 malloc 한 뒤, 각 조각을 순서대로 복사한다.
 *
 * [기대 동작]
 *   모든 조각을 이어 붙인 결과 길이를 출력하고 정상 종료.
 *
 * [증상]
 *   크기 계산 함수 joined_size() 의 루프가 `i < n - 1` 이라, "마지막 조각"의 길이를
 *   더하지 않는다. 그런데 실제 복사 루프는 `i < n` 으로 마지막 조각까지 복사한다.
 *   마지막 조각이 크면(여기서는 큰 본문), 할당량보다 훨씬 많이 써서 힙을 크게 넘어간다.
 *   → 힙 메타데이터 손상(이후 free 에서 abort) 또는 매핑 밖 접근으로 SIGSEGV.
 *   크래시는 strcpy/free 에서 나지만, 원인은 "크기 계산의 off-by-one"이다.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=09_strcpy_overflow
 *   (gdb) run                       → 크래시(SIGSEGV 또는 abort)
 *   (gdb) bt                        → join 의 strcpy 또는 free 근처
 *   (gdb) break joined_size ; run    → 반환값(need)과 실제 필요한 총합을 비교
 *   (gdb) print need                → 마지막 조각 길이가 빠져 need 가 부족함을 확인
 *
 * [printf(로그)로 잡기]
 *   계산한 크기와 실제로 복사한 바이트를 비교 출력:
 *     fprintf(stderr, "alloc=%zu copied=%zu\n", need, off);
 *   → copied 가 alloc 을 넘어서면 그 초과분이 힙을 침범한 것.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 크기 계산 루프를 `i < n` 으로 고쳐 모든 조각 길이와 종료 문자('\0') 자리를
 *       빠짐없이 더한다. "계산 루프와 복사 루프의 범위를 반드시 일치"시킨다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 필요한 총 바이트 수 = 모든 조각 길이 합 + 종료 문자 1 */
// size_t 는 joined_size()함수를 통해 메모리 크기를 표현하는 타입으로 반환한다는 의미임.
// char *const는 parts를 통해 포인터 자체를 변경하지 않음.
// int n은 parts에 문자열 포인터가 몇개인지 전달하는 값임.
static size_t joined_size(const char *const *parts, int n)
{
    // 널문자 '\0' 을 저장할 1바이트를 미리 확보함.
    size_t total = 1; /* '\0' 자리 */
    // parts[0]부터 Parts[n-1]까지 구해야 하니까 i <n이라고 해야 하는데 왜 이렇게 되어있지.
    // 마지막 문자열을 제외하고 문자열 길이를 더함.
    // 기존에 이 부분이 i < n -1 이었음. 따라서 마지막 문자열 길이만큼 덜복사된 것임.
    for (int i = 0; i < n; i++)
    {
        // strlen은 '\0'이 나오기 전까지 문자열 길이를 반환함.
        total += strlen(parts[i]);
    }
    // 계산된 총 메모리 크기를 반환함.
    return total;
}
// Static은 이 함수가 정의된 .c 파일 내부에서만 사용 가능하다는 의미임.
// char *join 은 join()함수가 char을 가리키는 포인터를 반환한다는 의미임.  - 문자열이 있는 메모리의 주소를 반환함.
// const는 문자열의 문자 내용을 변경하지 않음.
// *const는 parts를 통해 포인터 자체를 변경하지 않음.
// int n은 parts에 문자열 포인터가 몇 개 있는지 전달하는 값임.
static char *join(const char *const *parts, int n)
{
    // need 라는 변수를 만들고 메모리 크기를 표현하는 size_t 타입으로 선언함.
    // joned_size에 parts라는 문자열 포인터들이 담겨있는 배열을 넣고,
    // parts의 문자열들을 합쳤을 때 필요한 메모리 크기를 계산한다.
    size_t need = joined_size(parts, n);
    // need 바이트의 힙 버퍼(메모리)를 할당하고 시작 주소를 out에 기록한다.
    char *out = malloc(need); /* 마지막 조각 길이만큼 부족하게 할당됨 */
    if (!out)
    {
        // malloc()이 성공하면 out에 주소가 저장되지만,
        // 실패하면  NULL을 반환하고, Out = NULL이 된다.
        perror("malloc");
        // 현재 실행 중인 프로그램을 즉시 종료한다.
        exit(1);
    }
    // off라는 지역 변수를 만들고 메모리 크기를 표현하는 size_t 타입으로 선언함.
    // offset의 줄임말로, 현재까지 out에 복사한 문자열의 길이를 기록하는 지역변수이며, 처음에는 0임.
    size_t off = 0;
    // parts[0]부터 parts[n-1]까지 순서대로 처리함.
    for (int i = 0; i < n; i++)
    { /* 복사는 마지막 조각까지 전부 → 오버플로 */
        // out(malloc의 시작주소) 의 off번째 위치부터 parts[i]가 가리키는 문자열을 복사함.
        // out이 가리키는 힙 영역 안에서 off 바이트 떨어진 위치부터 넣는 것임.
        strcpy(out + off, parts[i]);
        // strlen(parts[i])는 포인터의 크기 8바이트를 계산하는게 아니라, 포인터가 가리키는 문자열의 길이를 계산함.
        // strlen은 문자열을 따라가면서 '\0'이 나오기 전까지 몇 바이트인지 계산함.
        // 방금 복사한 문자열의 길이만큼 off를 증가시켜 다음 복사 위치를 이동함.
        // off는 현재까지 복사한 문자열의 총 길이임.
        // out에서 다음 문자열을 복사할 위치를 계산하는데 사용함.
        off += strlen(parts[i]);
        // fprintf(stderr, "alloc=%zu copied=%zu\n", need, off);
    }
    // out은 포인터이지만 out[off]는 out이 가리키는 메모리의 off번째 바이트를 의미함.
    // out에 이어붙인 최종 문자열의 끝 위치에 문자열 종료 문자 '\0'을 기록함.
    fprintf(stderr, "alloc=%zu copied=%zu\n", need, off);
    out[off] = '\0';
    return out;
}

int main(void)
{
    // static body라는 200,000바이트 크기의 정적 배열을 선언한다.
    // static의 의미는 배열의 수명이 프로그램 종료까지 유지된다.
    static char body[200000];
    // body가 차지하는 전체 바이트 수에서 1바이트를 제외한 영역을 'x'로 채움.
    // -1을 하는 이유는 문자열을 담는 배열이라면 마지막 공간을 '\0'에 남겨두기 위함.
    memset(body, 'x', sizeof body - 1);
    // 그리고 나서 body의 마지막 1바이트에 널 문자를 넣는 것임.
    body[sizeof body - 1] = '\0';
    // const char * 타입의 포인터 4개를 저장하는 배열 parts를 만듦.
    // 배열의 각 원소가 문자열을 가리키는 포인터인거고, Parts가 문자열 포인터들의 배열인 것이고.
    // const는 포인터가 가리키는 문자열을 수정하지 못한다는 의미임.
    // parts[0] = "GET ", parts[1] = "/index.html", parts[2] = " HTTP"
    // body는 HTTP 응답에서 실제 내용이 들어가는 부분을 흉내낸 문자열 데이터임.
    const char *parts[] = {"GET ", "/index.html", " HTTP/1.1\r\n\r\n", body};
    // (int)부분은 형변환임. parts 배열 전체 크기(바이트). parts 배열의 원소 하나 크기(바이트)
    // n은 Parts 배열의 원소 개수를 저장한 변수임.
    int n = (int)(sizeof(parts) / sizeof(parts[0]));
    // join함수를 반환한 주소를 저장하는 포인터 변수 Msg를 선언하고, 반환된 주소를 msg에 대입.
    // join을 호출하면 join()이 parts[0]부터 parts[3]까지 따라가면서 각 문자열의 내용을 하나의 힙 버퍼에 이어붙이는 구조임.
    char *msg = join(parts, n); /* 복사 중 힙 오버플로 → 크래시 */
    // 힙 버퍼를 넘어쓴다는 것은 할당된 힙 메모리의 경계를 넘어 데이터를 기록.
    // msg가 가리키는 문자열의 길이를 구해서 출력하는 코드.
    // %zu는 size_t값을 출력하는 형식 지정자.
    // 단, strlen()은 '\0'을 길이에 포함하지 않음.
    printf("joined length = %zu\n", strlen(msg));
    // msg가 가리키는 힙 메모리를 해제함.
    // 메모리는 해제되고 msg라는 포인터 변수 자체는 그대로 존재함.
    // 이후 msg를 통해 해제된 메모리에 접근하면 Use-after-free가 발생함.
    free(msg);
    return 0;
}
