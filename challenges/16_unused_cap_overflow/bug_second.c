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

static void append_field(char *buf, size_t cap, size_t *len, const char *field, char sep)
{
    // 첫번째 필드가 아니면 구분자 1바이트가 추가된다.
    int extra = (*len > 0) ? 1 : 0;
    // 추가할 필드의 문자열 길이 계산한다.
    size_t flen = strlen(field);
    // 구분자 + NULl 문자까지 넣을 최소 공간이 부족하면
    // 더이상 필드를 추가하지 않고 종료된다.
    if (*len + extra + 1 > cap)
        return;
    // 현재 버퍼에서 필드에 사용할 수 있는 공간을 계산한다.
    // -Extra: 구분자 1바이트
    // - 1:문자열 끝의 NULL문자.
    size_t available = cap - *len - extra - 1;
    // 기본적으로 필드 전체를 복사한다.
    size_t copy_len = flen;
    // 필드 전체가 들어가지 않으면 버퍼에 들어갈 수 있는 길이만큼 복사한다.
    if (copy_len > available)
        copy_len = available;
    // 첫번째 필드가 아니면 필드 앞에 구분자를 추가한다.
    if (extra)
    {
        buf[(*len)++] = sep;
    }
    // 필드를 copy_len만큼 버퍼에 복사한다.
    // 공간이 부족하면 필드의 뒷부분은 잘린다.
    for (size_t i = 0; i < copy_len; i++)
    {
        // 현재 길이, 버퍼 용량, 추가할 필드 길이 출력
        fprintf(stderr, "append: len=%zu cap=%zu +%zu\n", *len, cap, strlen(field));
        buf[(*len)++] = field[i];
    }
    // 문자열 마지막에 문자열의 끝을 나타내는 NUL 문자 추가
    buf[*len] = '\0';
}

static void build_record(char *rec, size_t cap)
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
    // 현재까지 버퍼에 들어간 문자열의 길이를 기록한다.
    size_t len = 0;
    // 버퍼를 빈 문자열로 초기화
    rec[0] = '\0';
    // 모든 필드를 순서대로 추가
    for (int i = 0; i < n; i++)
    {
        append_field(rec, cap, &len, fields[i], '|');
    }
}

int main(void)
{
    // 전체 크기가 24바이트인 스택 버퍼를 생성한다.
    // 문자열은 최대 23바이트까지 저장하고 마지막 1바이트는 NUL에 사용한다.
    char rec[24];
    // rec의 크기를 전달하면서 레코드를 생성한다.
    build_record(rec, sizeof rec);
    // 완성된 문자열을 출력한다.
    printf("record = %s\n", rec);
    // 정상 종료.
    return 0;
}
