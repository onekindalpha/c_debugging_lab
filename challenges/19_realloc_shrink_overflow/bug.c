/*
 * Challenge 19 — realloc 로 줄인 뒤 옛 길이로 접근 (심화: 신호 버퍼 트림)
 *
 * [시나리오]
 *   센서 신호를 담는 Signal 버퍼. 앞부분의 유효 구간만 남기고 나머지를 잘라내
 *   메모리를 절약하는 signal_trim() 을 호출한 뒤, 에너지(제곱합)를 계산한다.
 *
 * [예시 상황]
 *   마이크 / 음성
 *   음성 구간 검출(VAD)이 이 패턴을 사용한다.
 *   44,100Hz로 녹음하면 1초에 samples가 44,100개다.
 *   무음인 앞부분만 남기고 뒤를 자르는 게 signal_trim이고, 소리가 얼마나 큰지(에너지)를 보려면 제곱합을 쓴다.
 *
 * [기대 동작]
 *   트림 후에는 남은 표본 개수(len)만큼만 접근/계산.
 *
 * [증상]
 *   signal_trim() 이 realloc 으로 버퍼를 "축소"하고 용량(cap)은 갱신하지만, 길이
 *   필드(len)를 갱신하지 않는다. 이후 signal_energy() 는 여전히 옛 len(수백만)으로
 *   순회하므로, 축소된(해제되어 unmap 된) 영역까지 읽어 SIGSEGV.
 *   크래시는 energy 루프의 samples[i] 에서 나지만, 원인은 "트림 시 len 미갱신".
 *
 * [gdb 로 잡기]
 *   make gdb NAME=19_realloc_shrink_overflow
 *   (gdb) run                        → 크래시(SIGSEGV)
 *   (gdb) bt                         → signal_energy 의 s->samples[i] 지점
 *   (gdb) print i ; print s->len ; print s->cap
 *        → len 이 cap 보다 훨씬 큼(트림으로 cap 만 줄었고 len 은 옛값)
 *   (gdb) break signal_trim          → 트림 후 len/cap 이 어긋나는지 확인
 *
 * [printf(로그)로 잡기]
 *   순회 인덱스와 len/cap 을 비교 출력:
 *     fprintf(stderr, "i=%zu len=%zu cap=%zu\n", i, s->len, s->cap);
 *   → i 가 cap 을 넘어서는(=축소된 버퍼 밖) 순간이 위험 지점.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 버퍼를 축소하면 길이(len)도 함께 새 크기로 갱신하고, 이후 접근은 갱신된
 *       len 으로만 하세요. (cap 과 len 을 항상 정합적으로 유지)
 */
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    double *samples;
    size_t len;
    size_t cap;
} Signal;

static void signal_init(Signal *s, size_t n)
{
    // 처음 할당을 할때 n의 크기 하나당 더블로 받고 이걸 s->samples로 한다.
    // samples는 그러니까 포인터 변수이고, samples데이터는 힙 메모리에 할당된다.
    s->samples = malloc(n * sizeof(double));
    // 할당에 실패하면.
    if (!s->samples)
    {
        perror("malloc");
        exit(1);
    }
    // 왜 다 n이랑 같게 하는거지.
    s->len = s->cap = n;
    // i % 7은 0~6을 반복하고, 여기에 3을 빼서 -3.0~3.0 범위의 값을 반복해서 넣는다.
    for (size_t i = 0; i < n; i++)
        // 데이터는 여기서 넣는게 끝인 것 같은데. 그럼 리얼록 한 이후에는 . 왜 데이터를 넣지 않는거지. 축소만 해서 그런가.
        s->samples[i] = (double)(i % 7) - 3.0;
}

// keep을 8로 전달하고 있고
// 리얼록은 처음에 생성안해도 바로 메모리 크기를 할당할 수 있다는데 맞나? 맞음. 처음에 널로 넣음.
// 그렇다면 keep을 과소가 아니라 최대로 해서 넣으면. 새로 데이터를 넣어주는 작업을 해야 하는건가? 함수를 만들어야 하나?
static void signal_trim(Signal *s, size_t keep)
{
    // keep이 뭔가 현재 용량보다.// 근데 여기가 뭔가 바뀐 것 같은데 화살표가.
    // [수정] 줄이는 경우(keep < cap)에만 realloc 한다.
    // (이전: keep < cap 이면 return → 축소 자체가 실행되지 않아 크래시만 가려졌음)
    if (keep >= s->cap)
        return;
    // 리얼록은 어느 주소를 반환하는거지.
    double *p = realloc(s->samples, keep * sizeof(double));
    // realloc 이 실패하면 기존 블록은 그대로 살아 있으므로 상태를 바꾸지 않는다.
    if (!p)
        return;
    s->samples = p;
    // s->cap은 현재 용량으로 한다.
    // 아 알았다 여기서 cap을 줄였는데 Len은 그대로여서. len도 같이 갱신하면 좋을 것 같음.
    // 실질적으로 데이터를 넣는다기 보다는 len을 갱신.
    s->cap = keep;
    // len 은 새 용량을 넘지 않도록만 줄인다(원래 len 이 더 작으면 그대로 유지).
    if (s->len > keep)
        s->len = keep;
}

static double signal_energy(const Signal *s)
{
    double e = 0.0;
    // 현재 사용하고 있는 크기보다 적으면
    // 왜 두배를 곱해서 더하는거지
    // 근데 여기서 만든 것을 왜 갱신은 딱히 안하고 그냥 데이터만 이용을 하나.
    for (size_t i = 0; i < s->len; i++)
    {
        e += s->samples[i] * s->samples[i];
    }
    return e;
}

int main(void)
{
    // s구조체를 쓰겠다.
    Signal s;
    signal_init(&s, 2000000);
    // 8은 뭔 의미지
    signal_trim(&s, 8);
    // e를 반환하는데 더블로 반환한다.
    double e = signal_energy(&s);
    printf("energy = %.1f (len=%zu cap=%zu)\n", e, s.len, s.cap);
    // s.sampels에 대해 프리한다. samples를 가리키는 포인터 변수를 해제한다.
    free(s.samples);
    // 처음에는 말록으로 할당을 하는거니까 굳이 해제안해줘도 되나
    s.samples = NULL;
    return 0;
}
