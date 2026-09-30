/*
 * Challenge 16 — 스택 버퍼 오버플로 (심화: 용량 인자를 무시하는 append)
 *
 * [시나리오]
 *   여러 필드를 구분자로 이어 붙여 한 줄의 레코드를 스택 버퍼에 만든다.
 *   append_field() 는 대상 버퍼와 그 용량(cap)을 받아 필드를 덧붙이는 헬퍼처럼 보인다.
 *
 * [기대 동작]
 *   cap 을 지켜 필드를 이어 붙이고 정상 종료. 전체 레코드는 NUL 포함 61바이트라
 *   rec[24] 에 들어가지 않는다. 넘치면 잘라 담거나(truncate) 오류로 처리하고,
 *   전체 문자열을 출력하려면 버퍼를 키운다.
 *
 * [증상]
 *   append_field() 는 cap 을 받지만 쓰지 않는다((void)cap). 세 번째 필드
 *   ("department=Engineering") 에서 이미 rec[24] 를 크게 넘긴다.
 *   1바이트만 넘는 off-by-one 이 아니라, 경계 검사를 생략해서 나는 큰 오버플로.
 *   main 반환 시 스택 카나리 검사 실패로 "stack smashing detected" → SIGABRT
 *   (경우에 따라 SIGSEGV). "cap 을 받으니 안전하겠지"라는 착각이 함정.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=16_unused_cap_overflow
 *   (gdb) run                        → abort
 *   (gdb) bt                         → __stack_chk_fail / main 반환 근처
 *   (gdb) break append_field ; run    → *len 이 cap 을 넘어서도 계속 쓰는지 관찰
 *   (gdb) print *len ; print cap      → *len 이 cap(=24)을 초과하는 순간이 원인
 *
 * [printf(로그)로 잡기]
 *   덧붙이기 전에 현재 길이/용량/추가 길이를 출력:
 *     fprintf(stderr, "append: len=%zu cap=%zu +%zu\n", *len, cap, strlen(field));
 *   → 구분자(첫 필드가 아니면 1) + 필드 + NUL 이 cap 을 넘는데도 쓰기가 진행되면 오버플로.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: append_field 에서 cap 을 실제로 사용하세요. 첫 필드가 아니면 구분자 1바이트가
 *       더 필요합니다.
 *         extra = (*len > 0) ? 1 : 0;          // 구분자
 *         if (*len + extra + flen + 1 > cap)   // +1 은 NUL
 *       넘치면 잘라 담거나(truncate) 오류로 처리하세요.
 */
#include <stdio.h>
#include <string.h>

// 반환값으로 성공/실패 상태를 전달
static int append_field(char *buf, size_t cap, size_t *len, const char *field, char sep)
{
    // 첫번째 필드가 아니면 구분자 1바이트가 추가된다.
    int extra = (*len > 0) ? 1 : 0;
    // 추가할 필드의 문자열 길이 계산
    size_t flen = strlen(field);
    // (예외처리) 현재 길이 + 구분자 + 필드 + '\0'이 버퍼 용량을 초과하면 오류 반환
    if ((*len + extra + flen + 1) > cap)
        return -1;
    // 첫번째 필드가 아니면 필드 앞에 구분자를 추가한다.
    if (extra)
    {
        buf[(*len)++] = sep;
    }
    // 필드의 모든 문자를 버퍼에 복사
    for (size_t i = 0; i < flen; i++)
    {
        buf[(*len)++] = field[i];
    }
    // 문자열 마지막에 NUL 문자 추가
    buf[*len] = '\0';
    // 정상적으로 필드를 추가했음을 반환
    return 0;
}

static int build_record(char *rec, size_t cap)
{
    // 이어 붙일 필드 목록
    const char *fields[] = {
        "id=1042",
        "name=Jonathan",
        "department=Engineering",
        "role=maintainer",
    };
    // fileds 배열의 원소 개수 계산
    int n = (int)(sizeof(fields) / sizeof(fields[0]));
    // 현재까지 만들어진 문자열의 길이
    size_t len = 0;
    // 빈 문자열로 초기화
    rec[0] = '\0';
    // 모든 필드를 순서대로 추가
    for (int i = 0; i < n; i++)
    {
        // append_filed의 반환값으로 성공/실패 확인
        int result = append_field(rec, cap, &len, fields[i], '|');
        // 필드 전체를 넣을 공간이 부족하면 오류 반환
        if (result == -1)
            return -1;
    }
    // 모든 필드를 정상적으로 추가
    return 0;
}

int main(void)
{
    // 전체 레코드는 '\0' 포함 61바이트이므로 여유 있게 64바이트 버퍼를 사용한다.
    // cap 검사(append_field)는 그대로 두어, 필드가 더 늘어나도 넘치지 않고 -1로 실패한다.
    char rec[64];
    // 레코드 생성 결과 확인
    int result = build_record(rec, sizeof rec);
    // 레코드 생성 실패
    if (result == -1)
    {
        printf("record 생성 실패\n");
        return 1;
    }
    // 정상적으로 생성된 레코드 출력
    printf("record = %s\n", rec);
    // 정상 종료.
    return 0;
}
