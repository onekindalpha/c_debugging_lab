/*
 * Challenge 06 — NULL Pointer Dereference (심화: HTTP 헤더 파서)
 *
 * [시나리오]
 *   "Key: Value" 형식의 헤더 블록을 줄 단위로 파싱한다. 각 줄에서 ':' 를 찾아
 *   그 자리를 '\0' 로 끊어 key/value 로 나눈 뒤 목록에 저장한다.
 *
 * [기대 동작]
 *   모든 헤더를 key/value 로 나눠 저장하고 개수와 내용을 출력.
 *
 * [증상]
 *   대부분의 줄에는 ':' 가 있지만, 한 줄("Connection")에는 ':' 가 없다.
 *   strchr(line, ':') 이 그 줄에서 NULL 을 돌려주는데, 이를 검사하지 않고
 *   `*colon = '\0'` 로 곧장 쓴다 → NULL 주소에 쓰기 → SIGSEGV.
 *   여러 줄을 도는 루프 안에 묻혀 있어, "어느 줄에서" 죽는지 gdb 로 짚어야 한다.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=06_null_deref
 *   (gdb) run                       → 크래시(SIGSEGV)
 *   (gdb) bt                        → parse_headers 의 *colon = '\0' 지점
 *   (gdb) print colon               → colon == 0x0 (strchr 이 NULL 반환)
 *   (gdb) print line                → ':' 가 없는 그 줄("Connection")을 확인
 *
 * [printf(로그)로 잡기]
 *   각 줄에서 strchr 결과를 찍어 NULL 인 줄을 찾는다:
 *     fprintf(stderr, "line=[%s] colon=%p\n", line, (void*)colon);
 *   → colon 이 (nil) 로 찍힌 줄이 원인.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: strchr 의 NULL 반환을 검사하라. ':' 없는 줄은 건너뛰거나 오류로 처리한다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HEADERS 32
typedef struct
{
  // 헤더의 key 문자열 주소를 저장
  char *keys[MAX_HEADERS];
  // 헤더의 value 문자열 주소를 저장
  char *vals[MAX_HEADERS];
  // 현재 저장된 헤더 개수
  int count;
} Headers;

// 문자열 앞의 공백과 탭을 건너뛰고
// 공백과 탭이 끝난 주소를 반환
// : 뒤의 공백을 제거할 수 있음. 
static char *skip_ws(char *s)
{
  while (*s == ' ' || *s == '\t')
    s++;
  return s;
}
// Text를 줄 단위로 나누어 헤더의 key와 value를 저장
static void parse_headers(char *text, Headers *h)
{
  //'\n'을 기준으로 한줄씩 분리함.
  for (char *line = strtok(text, "\n"); line != NULL; line = strtok(NULL, "\n"))
  {
    // 현재 줄에서 ":"의 주소를 찾음.
    char *colon = strchr(line, ':');
    // ':'가 없으면 헤더 형식이 아니므로 다음 줄로 이동
    if (colon == NULL)
    {
      continue;
    }
    // ':'을 '\0'으로 바꿔 key 문자열을 종료
    *colon = '\0';
    // ':' 앞부분을 키로 사용함.
    char *key = line;
    // ':' 다음부터 시작하여 앞의 공백과 탭을 건너뛰고 value로 사용
    char *val = skip_ws(colon + 1);
    // 헤더 배열의 최대 개수를 넘지 않는 경우 저장
    if (h->count < MAX_HEADERS)
    {
      h->keys[h->count] = key;
      h->vals[h->count] = val;
      // 저장한 헤더 개수 증가.
      h->count++;
    }
  }
}

int main(void)
{
  // HTTP 헤더 형식의 테스트 문자열 생성
  char raw[] =
      "Host: example.com\n"
      "Accept: */*\n"
      "Connection\n"
      "User-Agent: memdbg-cli\n";
  // 헤더 개수를 0으로 초기화
  Headers h = {.count = 0};
  // raw를 파싱하여 h에 헤더 정보 저장
  parse_headers(raw, &h);
  // 파싱된 헤더 개수 출력
  printf("parsed %d headers\n", h.count);
  // 저장된 헤더의 key와 value 출력
  for (int i = 0; i < h.count; i++)
    printf("  %s = %s\n", h.keys[i], h.vals[i]);
  return 0;
}