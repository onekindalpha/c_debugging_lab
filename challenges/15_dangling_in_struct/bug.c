/*
 * Challenge 15 — 구조체에 저장된 Dangling Pointer (심화: 세션이 해제된 User 참조)
 *
 * [시나리오]
 *   로그인하면 User 객체를 힙에 만들고, Session 이 그 User 를 가리킨다. User 는 권한
 *   검사 콜백(permission)을 첫 멤버로 가진다. 요청을 처리할 때 세션의 user 를 통해
 *   권한 콜백을 호출한다.
 *
 * [기대 동작]
 *   로그인 → 요청 처리(권한 확인) → 로그아웃 순으로 정상 종료.
 *
 * [증상]
 *   logout() 이 session->user 를 free 하지만 session->user 를 NULL 로 만들지 않는다
 *   (댕글링 멤버). 그 뒤 감사 로그 등에서 같은 크기의 객체를 새로 할당하면 방금 해제된
 *   User 청크가 재사용되어 permission 포인터 자리가 다른 값으로 덮인다.
 *   이후 handle_request() 가 session->user->permission() 을 호출 → 망가진 함수
 *   포인터로 점프 → SIGSEGV/SIGBUS. 크래시는 호출 지점에서 나지만, 원인은
 *   "해제된 객체를 가리키는 구조체 멤버(session->user)를 계속 사용"한 것.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=15_dangling_in_struct
 *   (gdb) run                        → 크래시(SIGSEGV/SIGBUS)
 *   (gdb) bt                         → handle_request 의 s->user->permission(...) 지점
 *   (gdb) print s->user              → 이미 free 된 User 주소(로그아웃에서 해제됨)
 *   (gdb) print s->user->permission  → 재사용으로 오염된(원래 함수와 다른) 포인터
 *   (gdb) break logout               → 언제 user 가 해제되는지 역추적
 *
 * [printf(로그)로 잡기]
 *   free 전/후로 콜백 포인터를 찍어 값이 바뀌는지 확인:
 *     fprintf(stderr, "before logout: perm=%p\n", (void*)s->user->permission);
 *     logout(s);
 *     fprintf(stderr, "after  logout: perm=%p\n", (void*)s->user->permission); // 오염
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 객체를 해제하면 그 객체를 가리키던 구조체 멤버도 즉시 무효화(NULL)하고,
 *       사용 전에 NULL 을 검사하세요. "해제 순서 + 소유 포인터 무효화".
 */
#include <stdio.h>  // 입출력 함수
#include <stdlib.h> // 동적 메모리와 프로그램 종료 관련 함수
#include <string.h> // 문자열과 메모리 관련 함수

typedef int (*PermFn)(const char *action);

typedef struct
{
    PermFn permission; // permission은 PermFn 타입의 함수 포인터임. 함수의 주소를 담고 있으며, 저장된 함수 주소를 통해 권한 검사 함수를 호출할 수 있음.
    int uid;
    char name[24];
} User;

typedef struct
{
    User *user;
    int session_id;
} Session;

// allow_all은 action의 내용과 관계없이 항상 1을 반환함.
static int allow_all(const char *action)
{
    (void)action;
    return 1;
}
// 만약에 특정 action만 허용하는 함수라면 action을 검사.
// int allow_read_only(const char *action)
// {
//  return strcmp(action, "read") == 0;
// }
static User *login(int uid, const char *name)
{
    User *u = malloc(sizeof *u);
    if (!u)
    {
        perror("malloc");
        exit(1);
    }
    // allow_all의 함수 주소를 permission에 대입함.
    // User마다 다른 권한 검사 함수를 Permission에 연결할 수 있도록 설계함.
    u->permission = allow_all;
    u->uid = uid;
    strncpy(u->name, name, sizeof(u->name) - 1);
    u->name[sizeof(u->name) - 1] = '\0';
    return u;
}

static void logout(Session *s)
{
    free(s->user);
    // User 객체를 해제한 뒤, User를 가리키던 s->user도 NULL로 설정함.
    // 해제된 User 주소가 dangling pointer로 남지 않도록 포인터를 무효화함.
    s->user = NULL;
}

/* 감사 로그 항목. User 와 같은 크기라 해제된 청크를 재사용하기 쉽다. */
static char *audit_record(const char *event)
{
    // sizeof(User)는 User 구조체가 메모리에서 차지하는 바이트 수
    // rec이 User와 같은 크기의 메모리 공간을 요청하면, 방금 해제한 User의 메모리를 다시 사용할 가능성이 높아짐.
    // 수정 전에는 session->user가 해제된 User의 주소를 계속 가지고 있었음.
    // 따라서 같은 청크가 재사용되면 session->user가 가리키는 주소에 새로운 데이터가 들어갈 수 있었음.
    char *rec = malloc(sizeof(User));
    if (!rec)
        exit(1);
    // User 크기의 메모리 전체를 0xAB로 채워 해제된 청크의 재사용을 확인하게 쉽게 만듦.
    // 해제된 청크가 재사용되었는지 메모리 상태를 확인하기 위한 디버깅 코드
    // 이후 snprintf()가 메모리 앞부분을 "audit:logout"으로 덮어씀.
    memset(rec, 0xAB, sizeof(User)); /* permission 자리를 0xAB.. 로 오염 */
    // rec가 가리키는 메모리에 "audit:" + event문자열을 기록함
    // sizeof(User)는 최대 기록 크기이며, 마지막 '\0'도 포함함.
    snprintf(rec, sizeof(User), "audit:%s", event);
    return rec;
}

static int handle_request(Session *s, const char *action)
{
    // permission에는 함수의 주소가 들어있어야 함.
    // 수정 전에는 해제된 메모리가 재사용되면서 permission 위치가 문자열 데이터로 덮였음
    // 문자열 바이트가 함수 주소로 해석되어 잘못된 주소로 실행이 이동했고, SIGBUS가 발생함.
    // User가 해제된 상태에서는 s->user가 NULL이므로 접근하지 않고 요청을 거부함.
    if (s->user == NULL)
        return 0;
    return s->user->permission(action);
}

int main(void)
{
    // 초기 상태를 설정함.
    Session s;
    s.session_id = 1;
    s.user = login(42, "alice");

    printf("first request allowed=%d\n", handle_request(&s, "read"));

    logout(&s);
    // audit_record()에서 새로 할당한 메모리 앞부분에 "audit:logout"을 기록함.
    // logout()에서 s.user를 NULL로 설정했으므로 해제된 User주소가 남아있지 않음.
    char *rec = audit_record("logout");
    printf("%s\n", rec);
    // 수정전 디버깅 결과:
    // handle_request에서 s->user->permission 위치의 첫 8바이트를 읽음
    // 해제된 User 청크가 재사용되면서 문자열 바이트가 들어있었음.
    // 문자열 바이트를 함수 주소로 사용함.
    // 0x006c3a7469647561 주소로 점프함.
    // 잘못된 함수 주소로 실행이 이동하면서 SIGBUS가 발생함.
    printf("second request allowed=%d\n", handle_request(&s, "write"));
    // 감사 로그 메모리를 해제함.
    free(rec);
    return 0;
}
