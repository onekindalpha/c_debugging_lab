/*
 * Challenge 02 — Stack Buffer Overflow (심화: 삼각 인덱싱 off-by-one)
 *
 * [시나리오]
 *   파스칼의 삼각형을 스택 위 "1차원" 배열에 삼각 인덱싱으로 채운다.
 *   파스칼의 삼각형(Pascal's Triangle)은 숫자를 삼각형 모양으로 배치한 것으로, 다음과 같은 규칙을 가진다.
 *    - 맨 위는 항상 1
 *    - 양쪽 가장자리는 항상 1
 *    - 가운데 숫자는 바로 위의 두 숫자의 합
 *              1
 *           1   1
 *         1   2   1
 *       1   3   3   1
 *     1   4   6   4   1
 *   1   5  10  10   5   1
 *
 *  파스칼의 삼각형을 2차원 배열로 정리하면 하기와 같다.
 *   1
 *   1 1
 *   1 2 1
 *   1 3 3 1
 *
 *   1차원 배열로 정리할때는, 행 i 의 원소 j 는 인덱스  idx = i*(i+1)/2 + j  에 저장한다.
 *   배열 크기는 정확히 ROWS 개 행(0..ROWS-1)을 담도록 SIZE = ROWS*(ROWS+1)/2 로 잡았다.
 *
 * [기대 동작]
 *   삼각형을 만들고 각 행의 합(=2^i)을 출력한 뒤 정상 종료.
 *
 * [증상]
 *   행 루프가 `i <= ROWS` 로 도는 바람에 행이 하나 더 생성된다.
 *   그 행의 인덱스는 idx = SIZE + j 가 되어 스택 배열의 끝을 넘어 쓴다.
 *   스택 카나리(스매싱 보호)가 훼손되어 main 반환 시 "stack smashing detected"
 *   로 SIGABRT. 삼각 인덱싱 산술에 가려 off-by-one 이 눈에 잘 안 띈다.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=02_stack_buffer_overflow
 *   (gdb) run                    → abort
 *   (gdb) bt                     → __stack_chk_fail / __fortify_fail 확인
 *   (gdb) break build_pascal     → 다시 run
 *   (gdb) watch tri[SIZE]        → 배열 끝(SIZE 인덱스)에 쓰는 순간 멈춤
 *   (gdb) print i                → 그때 i 가 ROWS 와 같은지(=한 행 초과) 확인
 *
 * [printf(로그)로 잡기]
 *   쓰기 직전에 (i, j, idx, SIZE) 를 찍어 idx 가 SIZE 이상이 되는 순간을 확인:
 *     fprintf(stderr, "write i=%d j=%d idx=%d SIZE=%d\n", i, j, idx, SIZE);
 *   → idx >= SIZE 가 찍히면 배열 경계를 넘은 것.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 행 루프를 `i < ROWS` 로 고치세요(유효 행은 0..ROWS-1).
 *       인덱싱 산술을 쓸 때는 "마지막으로 접근하는 인덱스"를 손으로 계산해
 *       배열 크기와 반드시 비교하세요.
 */
#include <stdio.h>
#include <stdlib.h>

#define ROWS 14
enum
{
  SIZE = ROWS * (ROWS + 1) / 2
}; /* 0..ROWS-1 행을 담는 정확한 크기 */

/* 행 i, 열 j 의 삼각 인덱스 */
static int tri_index(int i, int j)
{
  return i * (i + 1) / 2 + j;
}

/* 파스칼의 삼각형을 tri[] 에 채운다. */
static void build_pascal(int *tri, int rows)
{
  // 유효행 : 0...rows -1까지
  // i == rows일때 실행되는 바퀴는 초과된 행임
  // i < rows로 고쳐야 정확히 rows개 행 (0...rows-1)만 돈다.

  for (int i = 0; i <= rows; i++)
  {
    // 이 안쪽 루프는 i번째 헹에는 원소가 (i+1)개 있으므로
    // j는 0부터 i까지 (포함) 되는게 맞다. (예: i=2행이면 j=0, 1, 2->원소 3개)

    for (int j = 0; j <= i; j++)
    {
      // 2차원 좌표(행 i, 열 j)를 1차원 배열 tri[]안의 실제 위치로 변환.
      // 삼각수 공식 (i*(i+1)/2)으로 그 행이 시작하는 자리를 구하고, 거기에 j를 더함
      // 바로 이 idx가 바깥 루프의 버그 때문에 i = rows일때 SIZE이상으로 튀어나감
      int idx = tri_index(i, j);
      // 파스칼의 삼각형 규칙 중 양쪽 가장자리는 항상 1에 해당하는 분기
      // j == 0 (그행의 첫 원소)이거나, j == i(그 행의 마지막 원소)일때))
      if (j == 0 || j == i)
      {
        tri[idx] = 1; /* 양 끝은 1 */
        // i=rows(=14), j =0일때 idx=SIZE가 되어, 이 줄이 배열 밖(tri[SIZE])에 값을 씀.
      }
      else
      {
        int up_left = tri_index(i - 1, j - 1);
        int up_right = tri_index(i - 1, j);
        tri[idx] = tri[up_left] + tri[up_right];
      }
    }
  }
}

static long row_sum(const int *tri, int i)
{
  long sum = 0;
  for (int j = 0; j <= i; j++)
    sum += tri[tri_index(i, j)];
  return sum;
}

static void print_row(const int *tri, int i)
{
  printf("row %2d:", i);
  for (int j = 0; j <= i; j++)
    printf(" %d", tri[tri_index(i, j)]);
  printf("   (sum=%ld)\n", row_sum(tri, i));
}

int main(void)
{
  // 이 배열은 Main의 스택 프레임 안에 있음.
  // 배열을 직접 선언하고 있어서 카나리가 붙음.
  // 카나리는 배열/버퍼가 있는 함수에만 컴파일러가 붙여주기 때문임.
  // 여기서 선언
  int tri[SIZE];
  //
  // build_pascal은 그 주소(포인터)만 받아서 씀
  // build_pascal 함수에서 보면 int *tri; int rows이렇게 포인터로 받고 있음.
  // C에서 배열을 함수에 넘기면 배열 전체가 복사되는 게 아니라, 그 배열의 첫 주소(포인터)만 넘어감
  // 그래서 build_pascal에서 아무리 범위를 벗어나서 써도, 실제로 망가지는 메모리는 항상 main의 소유
  // [실제로 확인] 이걸 gdb로 확인하는 방법은 (gdb) disas build_pascal 이라는 것을 어셈블리로 확인
  build_pascal(tri, ROWS);
  // [실제로 확인] main에는 카나리 설치/검사 코드가 있다는 것을 확인 (gdb) disas main
  for (int i = 0; i < ROWS; i++)
    print_row(tri, i);

  printf("SIZE = %d\n", SIZE);

  /* [Thinking Point]
   * 이 프로그램은 위 printf 까지 정상 출력을 마치고도, 왜 하필 이 return 0; 에서
   * 크래시(SIGABRT, "stack smashing detected")가 날까?
   *   tip 1. return 은 단순히 "0을 돌려준다"가 아니라, main 의 스택 프레임을 정리하고
   *          호출처로 '되돌아가는' 동작이다. 이때 스택에 저장된 복귀 정보가 사용된다.
   *   tip 2. 컴파일러는 배열(tri[]) 같은 지역 변수 뒤에 '스택 카나리(canary)'라는
   *          감시 값을 심어두고, 함수가 return 하기 직전에 그 값이 그대로인지 검사한다.
   *   생각해보기: build_pascal 이 tri[] 경계를 넘어 쓰면 카나리가 훼손된다. 그렇다면
   *               크래시가 "배열을 넘어 쓰는 순간"이 아니라 "return 시점"에 나는 이유는?
   *               (힌트: 오버플로 자체는 조용히 일어나고, 검사는 return 직전에 이뤄진다) */
  return 0;
}