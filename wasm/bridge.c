// wasm/bridge.c (2-인스턴스: analyze + solve 병렬)
//  - analyze 엔진: 백그라운드 전수 분석 (N-1 코어 + hash 2^21; WASM 은 solve 와 공유 엔진이라 2^22)
//  - solve   엔진: 실시간 per-position 평가 (평상시 N 코어/analyze중 1 + hash 2^22, 딥 엔드게임 완주용 상향)
//  두 엔진은 독립 Play/Search/Eval/HashTable/TaskStack/RNG 를 가지며,
//  EVAL_WEIGHT(가중치)·Book·LEVEL·bit/edge 테이블은 read-only 공유(추가 메모리 0).
//  → analyze 가 도는 동안에도 solve 가 막히지 않는다(별 인스턴스라 상호 비차단).
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
#else
  /* 네이티브(iOS/Android) 빌드: emscripten 헤더 없음. KEEPALIVE는 심볼 보존 속성으로 대체. */
  #ifndef EMSCRIPTEN_KEEPALIVE
  #define EMSCRIPTEN_KEEPALIVE __attribute__((used, visibility("default")))
  #endif
#endif

#include "../src/base.h"
#include "../src/bit.h"       // ← bit_init
#include "../src/board.h"     // ← edge_stability_init
#include "../src/stats.h"     // ← statistics_init
#include "../src/search.h"    // ← search_global_init / search_set_task_number / search_resize_hashtable
#include "../src/options.h"   // ← 전역 options
#include "../src/move.h"
#include "../src/const.h"
#include "../src/play.h"
#include "../src/book.h"
#include "../src/eval.h"
#include "../src/util.h"
#include "../src/ybwc.h"     // ← Task/TaskStack (live node counter)

/* ───── 네이티브 콜백 시그니처 ───── */
#ifndef __EMSCRIPTEN__
typedef void (*fs_row_cb)(void *ctx, int ply, char turnBW,
                          const char *playedMove, int playedScore,
                          const char *bestMove, int bestScore,
                          const char *bestOtherMove, int bestOtherScore, int hasBestOther,
                          int legalCount, int depth, int percent,
                          const char *source);
typedef void (*fs_solve_row_cb)(void *ctx, const char *move, int score,
                                int depth, int selectivity, int percent, int fromBook);
typedef void (*fs_solve_progress_cb)(void *ctx, int depth, int percent, const char *currentMove);
typedef void (*fs_solve_done_cb)(void *ctx, int count, int stopped);
#endif

/* ───── 엔진 인스턴스 ───── */
typedef struct FsEngine {
    Play play[1];               // search 는 play->search (Play 멤버)

    int level;                  // 이 엔진의 탐색 레벨
    int n_task;                 // 워커 수
    int hash_bits;              // hash 크기(2^hash_bits)
    int inited;                 // play_init 됐는지(lazy 생성용)

    Search * volatile master;   // 진행 중 master Search (cancel 대상). 없으면 NULL.

    /* analyze 상태 */
    int analyze_emitted;
    int analyze_ply_offset;

    /* solve 상태 */
    int solve_emitted;
    int solve_active;
    int solve_done_emitted;

    /* hint 상태(analyze 엔진에서만 사용) */
    char bestmove[8];
    int  lastscore;

#ifndef __EMSCRIPTEN__
    fs_row_cb            row_cb;        void *row_ctx;
    fs_solve_row_cb      solve_row_cb;
    fs_solve_progress_cb solve_prog_cb;
    fs_solve_done_cb     solve_done_cb; void *solve_ctx;
#endif
} FsEngine;

// WASM 워커는 자기 역할을 단일 인스턴스로 처리해 왔다(eval 워커=_s: solve + 포그라운드 analyze 를
// 같은 g_play 로. WASM 은 _s 워커만 사용). analyze/solve 가 같은 인스턴스를 공유하므로 두 hash_bits 가
// 반드시 같아야 한다(먼저 ensure 된 값이 고정되므로 불일치 시 순서의존 버그). 네이티브는 별개 인스턴스.
// 딥 엔드게임(29-30 empties exact) 완주를 위해 solve hash 를 2^22 로 상향(2^18→2^22, ~113MB).
#ifdef __EMSCRIPTEN__
  static FsEngine g_eng_shared;
  #define g_eng_analyze g_eng_shared
  #define g_eng_solve   g_eng_shared
  #ifdef FS_LOWMEM
    // 저메모리판 flipsight_l(2026-11-02): _s 의 256MB 첫 메모리를 못 잡는 브라우저(Mac Safari 16.6.2
    // "RangeError: Out of memory", Android 10 Chrome "could not allocate memory") 용. 2^19(~14MB).
    #define ANALYZE_HASH_BITS 19
    #define SOLVE_HASH_BITS   19
  #else
    #define ANALYZE_HASH_BITS 22    // 공유 엔진 → solve 와 동일해야 함
    #define SOLVE_HASH_BITS   22
  #endif
#else
  static FsEngine g_eng_analyze;
  static FsEngine g_eng_solve;
  #define ANALYZE_HASH_BITS 21    // 네이티브 analyze 는 별도 엔진 → 기존 2^21 유지
  #define SOLVE_HASH_BITS   22    // 네이티브 solve 상향(딥 엔드게임)
#endif
static Book     g_book[1];           // 두 엔진이 공유하는 opening book (read-only)
static int      g_shared_inited = 0;

/* ───── task 배열 보호 뮤텍스 (2026-09-14) ─────
 * search_set_task_number() → task_stack_resize() 는 task 배열을 free() 후 다시 malloc 한다.
 * 진행 감시(flipsight_*_live_nodes)가 그 배열을 순회하는 중에 겹치면 해제된 메모리를 읽는다
 * (build 41 현장 크래시: SIGSEGV in flipsight_solve_live_nodes, edax-watchdog 스레드).
 * 우리 빌드에서 배열을 바꾸는 경로는 bridge.c 가 전부 소유하므로(ui/edax.c/ggs.c 미포함) 같은 뮤텍스로 닫는다.
 * 감시 쪽은 trylock 만 쓴다: 탐색/스레드 변경을 절대 막지 않고, 못 잡으면 "지금은 알 수 없음"(-1)으로 답한다. */
/* 단일 스레드 WASM(_s)만 no-op. 멀티스레드 WASM(_m, -pthread)은 네이티브와 똑같이 진짜 뮤텍스를 쓴다:
 * 여기서 no-op 이면 build 41 에서 앱을 죽인 use-after-free 가 웹에서 그대로 재현된다(2026-09-14). */
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
  #define FS_TASKS_LOCK()    ((void)0)
  #define FS_TASKS_UNLOCK()  ((void)0)
  #define FS_TASKS_TRYLOCK() 1
#else
  #include <pthread.h>
  static pthread_mutex_t g_tasks_mutex = PTHREAD_MUTEX_INITIALIZER;
  #define FS_TASKS_LOCK()    pthread_mutex_lock(&g_tasks_mutex)
  #define FS_TASKS_UNLOCK()  pthread_mutex_unlock(&g_tasks_mutex)
  #define FS_TASKS_TRYLOCK() (pthread_mutex_trylock(&g_tasks_mutex) == 0)
#endif

/* ───── master(취소) — 엔진별 독립 ───── */
static void fs_register_master(FsEngine *e, Search *m) { e->master = m; }
static void fs_clear_master(FsEngine *e)               { e->master = NULL; }

EMSCRIPTEN_KEEPALIVE
void flipsight_cancel_analyze(void) {
    Search *m = g_eng_analyze.master;
    if (m) search_stop_all(m, STOP_ON_DEMAND);    // analyze 만 중단(solve 영향 없음)
}
EMSCRIPTEN_KEEPALIVE
void flipsight_cancel_solve(void) {
    Search *m = g_eng_solve.master;
    if (m) search_stop_all(m, STOP_ON_DEMAND);    // solve 만 즉시 선점(analyze 영향 없음)
}
EMSCRIPTEN_KEEPALIVE
void flipsight_cancel_now(void) {                 // deprecated alias: 둘 다 취소
    flipsight_cancel_analyze();
    flipsight_cancel_solve();
}

// ASYNCIFY: WASM search hot path에서 JS event loop로 제어 양도(cancel 메시지 처리).
EMSCRIPTEN_KEEPALIVE
void edax_yield_to_host(void) {
#ifdef __EMSCRIPTEN__
    emscripten_sleep(0);
#endif
    /* 네이티브: no-op. cancel 은 백그라운드 스레드의 flipsight_cancel_*()→search->stop 폴링으로 처리. */
}

/* ───── 유틸 ───── */
static void lower_ascii(char* s) {
    if (!s) return;
    while (*s) { *s = (char)tolower((unsigned char)*s); ++s; }
}
static int solve_hint60_percent(int selectivity) {
    if (selectivity >= 0 && selectivity <= NO_SELECTIVITY)
        return selectivity_table[selectivity].percent;
    return 0;
}

/* ───── 네이티브 콜백 setter (엔진별 ctx) ───── */
#ifndef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
void flipsight_set_row_callback(fs_row_cb cb, void *ctx) {     // analyze 행
    g_eng_analyze.row_cb = cb; g_eng_analyze.row_ctx = ctx;
}
EMSCRIPTEN_KEEPALIVE
void flipsight_set_solve_callbacks(fs_solve_row_cb row, fs_solve_progress_cb prog,
                                   fs_solve_done_cb done, void *ctx) {   // solve 3종
    g_eng_solve.solve_row_cb  = row;
    g_eng_solve.solve_prog_cb = prog;
    g_eng_solve.solve_done_cb = done;
    g_eng_solve.solve_ctx     = ctx;
}
#endif

/* ───── 엔진 hash 보장: search->options.hash_size 가 이미 hash_bits 면 no-op ─────
   주의: 전역 options.hash_table_size 를 잠깐 토글하므로 두 엔진 동시 호출 금지.
   fs_engine_init 에서 1회 확정 → run 경로 호출은 항상 no-op(방어용). */
static void fs_apply_hash(FsEngine *e) {
    if (e->play->search.options.hash_size == e->hash_bits) return;
    int saved = options.hash_table_size;
    options.hash_table_size = e->hash_bits;
    search_resize_hashtable(&e->play->search);
    options.hash_table_size = saved;
}

/* ───── 엔진 init (직렬 호출) ───── */
static void fs_engine_init(FsEngine *e, int hash_bits, int n_task, int level) {
    e->level     = level;
    /* 로더는 엔진 lazy 생성 전에 set_threads 를 부른다(flipsight_init 보다 먼저). 그때 기록해 둔 희망값이 있으면
     * 그대로 쓴다. 예전에는 여기서 1 로 덮여 멀티스레드 빌드가 영영 1 스레드로 돌았다(2026-09-14 실측). */
    if (e->n_task > 0) n_task = e->n_task;
    e->n_task    = n_task;
    e->hash_bits = hash_bits;
    e->master    = NULL;
    e->analyze_emitted = e->analyze_ply_offset = 0;
    e->solve_emitted   = e->solve_active = e->solve_done_emitted = 0;
    e->bestmove[0] = '\0';
    e->lastscore   = 0;
#ifndef __EMSCRIPTEN__
    e->row_cb = NULL; e->row_ctx = NULL;
    e->solve_row_cb = NULL; e->solve_prog_cb = NULL;
    e->solve_done_cb = NULL; e->solve_ctx = NULL;
#endif
    /* search_init 이 읽을 전역을 잠깐 이 엔진 값으로 세팅한 뒤 init */
    options.hash_table_size = hash_bits;
    options.n_task          = n_task;
    play_init(e->play, g_book);    // 내부 search_init 에서 hash/tasks 할당
    fs_apply_hash(e);              // 크기 확정(보통 no-op)
    e->inited = 1;
}

/* 엔진을 처음 쓸 때만 생성(lazy). WASM 워커는 자기 역할 엔진만 만들어 메모리 절약,
   네이티브는 flipsight_init 에서 둘 다 미리 ensure(전역 options 토글 race 방지). */
static void fs_engine_ensure(FsEngine *e, int hash_bits, int n_task, int level) {
    if (e->inited) return;
    FS_TASKS_LOCK();
    if (!e->inited) fs_engine_init(e, hash_bits, n_task, level);
    FS_TASKS_UNLOCK();
}

/* ───── 공유 init 1회 ───── */
EMSCRIPTEN_KEEPALIVE
int flipsight_init(void) {
    if (g_shared_inited) return 0;

    bit_init();
    edge_stability_init();
    statistics_init();

    options.verbosity    = 0;
    options.book_allowed  = true;
    options.eval_file    = "data/eval.dat";
    options.book_file    = "data/book.dat";
    options_bound();

    search_global_init();    // LEVEL[] 등 전역 테이블 1회
    book_init(g_book);       // 공유 book 객체 1회

    g_shared_inited = 1;

    /* analyze = 큰 hash(2^21), solve = 작은 hash(2^18). n_task 는 플러그인이 set_*_threads 로 확정(기본 1).
       네이티브: 두 엔진을 여기서 순차 생성(전역 options 토글 race 방지).
       WASM: 각 워커가 자기 역할 함수 첫 호출 시 lazy 생성(안 쓰는 엔진 메모리 절약). */
#ifndef __EMSCRIPTEN__
    fs_engine_ensure(&g_eng_analyze, ANALYZE_HASH_BITS, 1, 21);
    fs_engine_ensure(&g_eng_solve,   SOLVE_HASH_BITS, 1, 21);
#endif
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int flipsight_shutdown(void) {
    FS_TASKS_LOCK();
    play_free(g_eng_analyze.play);
    play_free(g_eng_solve.play);
    FS_TASKS_UNLOCK();
    eval_close();            // ★ 두 play_free 후에 (search 들이 EVAL_WEIGHT 참조 안 함)
    options_free();
    g_shared_inited = 0;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int flipsight_load_eval(const char* path) {
    /* 탐색 중 재로드 금지: EVAL_WEIGHT free/realloc 이 진행 중 search 를 dangling 시킴 */
    if (g_eng_analyze.master || g_eng_solve.master) return -1;
    /* ★ eval_open 은 전역 EVAL_LOADED 가드로 "프로세스당 1회"만 실제 로딩하는데, eval_close 는 EVAL_LOADED 를
       줄이지 않는다. 따라서 이미 로딩된 상태에서 eval_close()+eval_open() 을 하면 free 후 재로딩이 no-op 이 되어
       EVAL_WEIGHT 가 NULL 로 남고, 다음 분석에서 accumlate_eval 이 NULL 역참조로 크래시한다.
       (프로세스 생존 + Activity/플러그인 재생성 시 ensureBooted 가 nLoadEval 을 재호출하며 발생 — 간헐적.)
       → eval.dat 는 항상 같은 파일이므로, 이미 로딩돼 있으면 그대로 둔다(재로딩 불필요). */
    if (EVAL_WEIGHT != NULL) return 0;
    options.eval_file = (char*) path;
    eval_open(options.eval_file);   /* 최초 1회만 실제 로딩(EVAL_LOADED 0→1) */
    return (EVAL_WEIGHT != NULL) ? 0 : -2;
}

EMSCRIPTEN_KEEPALIVE
int flipsight_load_book(const char* path) {
    if (g_eng_analyze.master || g_eng_solve.master) return -1;  // 교체는 변형 → 탐색 중 금지
    options.book_file = (char*) path;
    book_load(g_book, options.book_file);    // 두 play 의 play->book 이 모두 g_book
    return 0;
}

/* ───── 스레드/depth setter (엔진별) ───── */
EMSCRIPTEN_KEEPALIVE
void flipsight_set_analyze_threads(int n) {
    if (n < 1) n = 1; if (n > 64) n = 64;
    if (g_eng_analyze.n_task == n) return;
    if (!g_eng_analyze.inited) { g_eng_analyze.n_task = n; return; }   // 생성 때 이 값으로 만든다
    FS_TASKS_LOCK();
    g_eng_analyze.n_task = n;
    search_set_task_number(&g_eng_analyze.play->search, n);   // 탐색 시작 전 1회(per-instance)
    FS_TASKS_UNLOCK();
}
EMSCRIPTEN_KEEPALIVE
void flipsight_set_solve_threads(int n) {
    if (n < 1) n = 1; if (n > 64) n = 64;
    if (g_eng_solve.n_task == n) return;
    if (!g_eng_solve.inited) { g_eng_solve.n_task = n; return; }
    FS_TASKS_LOCK();
    g_eng_solve.n_task = n;
    search_set_task_number(&g_eng_solve.play->search, n);
    FS_TASKS_UNLOCK();
}
EMSCRIPTEN_KEEPALIVE
void flipsight_set_analyze_depth(int d) { if (d < 0) d = 0; g_eng_analyze.level = d; }
EMSCRIPTEN_KEEPALIVE
void flipsight_set_solve_depth(int d)   { if (d < 0) d = 0; g_eng_solve.level   = d; }

/* deprecated alias → analyze */
EMSCRIPTEN_KEEPALIVE void flipsight_set_depth(int d)   { flipsight_set_analyze_depth(d); }
EMSCRIPTEN_KEEPALIVE void flipsight_set_threads(int n) { flipsight_set_analyze_threads(n); }

EMSCRIPTEN_KEEPALIVE
int flipsight_setboard(const char* board_plus_turn) {   // 보조(호환): analyze 엔진에 매핑
    play_set_board(g_eng_analyze.play, board_plus_turn);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int flipsight_last_score(void) { return g_eng_analyze.lastscore; }

/* solve 엔진 hash 대략 추정 바이트(getOcviewerMem 대응; 디버그용) */
EMSCRIPTEN_KEEPALIVE
long flipsight_solve_mem_bytes(void) {
    long entries = 1L << g_eng_solve.hash_bits;
    return (long)(entries * 27);    // ≈ entry(24B) * 1.125
}

/* ───── 진행 감시(heartbeat)·벤치마크 (2026-09-13) ─────
 * 시간 시한은 레벨 ∞ 심층 탐색에서 오탐이 났다(depth 38→40 사이 90초 침묵). 대신 "노드가 늘고 있는가"를 본다.
 * master 의 n_nodes 만 보면 안 된다: YBWC helper 가 판 노드는 helper 가 서브트리를 끝낸 뒤에야 parent->child_nodes
 * 로 합산되므로(ybwc.c task_search), master 가 큰 서브트리를 맡은 helper 를 기다리는 동안 master 기준 합계는 멈춰
 * 보인다. 실행 중(run) helper 의 live 카운터까지 더하면 어느 스레드든 일하고 있으면 값이 는다. 락 없이 volatile
 * 읽기라 순간 어긋날 수 있으니(helper 종료 시 child_nodes 합산과 run=false 사이) 호출측은 "연속 N회 증가 0"으로 판정한다.
 */
static volatile double g_last_search_nodes = 0.0;   // 마지막으로 끝난 solve/bench 탐색의 총 노드
static volatile int    g_last_search_ms    = 0;     // 그 탐색의 소요 ms(search->time.spent)

static double fs_live_nodes(FsEngine *e) {
    if (!e->master) return -1.0;                 /* 빠른 탈출(락 없이) */
    if (!FS_TASKS_TRYLOCK()) return -1.0;        /* 스레드 변경 중 = 지금은 알 수 없음 */
    double total = -1.0;
    Search *m = e->master;                       /* 락 안에서 다시 읽는다 */
    if (m) {
        total = (double)m->n_nodes + (double)m->child_nodes;
        TaskStack *ts = m->tasks;
        if (ts && ts->task) {
            int i, n = ts->n;                    /* 길이도 한 번만 읽는다 */
            /* ★ 0번 슬롯은 절대 읽지 말 것. task_stack_init 이 `if (i)` 로 0번만 task_init 을 건너뛴다
             * (마스터 자리라 helper 로 안 쓴다). malloc 한 그대로라 run/search 가 힙 쓰레기다.
             * 0 부터 돌면 쓰레기 run 이 참일 때 쓰레기 포인터를 역참조해 엔진 프로세스가 SIGSEGV 로 죽는다.
             * 2026-09-14~15 안드로이드 크래시(#585·#588·#590·#591)의 원인이었다. fault address 가
             * 문자열 바이트("oothello", "vives\": ")였던 것이 남은 힙 내용을 포인터로 읽은 증거다. */
            for (i = 1; i < n; ++i) {
                Task *t = &ts->task[i];
                Search *s = t->search;
                if (t->run && s && s != m) total += (double)s->n_nodes + (double)s->child_nodes;
            }
        }
    }
    FS_TASKS_UNLOCK();
    return total;
}
/* 엔진이 실제로 쓰는 task 수(= 스레드 수). 0 이면 아직 엔진 미생성.
 * 로더/플러그인이 "요청한 값"과 다를 수 있어서(2026-09-14: 요청 8, 실제 1) 근거값을 따로 노출한다. */
EMSCRIPTEN_KEEPALIVE
int flipsight_engine_threads(void) {
    FsEngine *e = &g_eng_solve;
    if (!e->inited) return 0;
    FS_TASKS_LOCK();
    Search *sr = &e->play->search;
    int n = (sr->tasks && sr->tasks->n > 0) ? sr->tasks->n : 1;
    FS_TASKS_UNLOCK();
    return n;
}

EMSCRIPTEN_KEEPALIVE
double flipsight_solve_live_nodes(void)   { return fs_live_nodes(&g_eng_solve); }     // 진행 중 solve 없으면 -1
EMSCRIPTEN_KEEPALIVE
double flipsight_analyze_live_nodes(void) { return fs_live_nodes(&g_eng_analyze); }   // 진행 중 analyze 없으면 -1
EMSCRIPTEN_KEEPALIVE
double flipsight_last_search_nodes(void)  { return g_last_search_nodes; }
EMSCRIPTEN_KEEPALIVE
int    flipsight_last_search_ms(void)     { return g_last_search_ms; }

/* 벤치마크 한 국면: obftest 와 같은 절차(hash 정리 → exact solve → 노드·시간). 최선수 하나만 찾는다(hint 스트림 없음).
 * 반환: 점수(디스크 차). 취소(flipsight_cancel_solve) 시 -1000, 오류(eval 미로딩·종국·잘못된 보드) -2000.
 * 결과는 flipsight_last_search_nodes()/flipsight_last_search_ms() 로 읽는다. solve 엔진(g_eng_solve)을 쓰므로
 * 호출측이 진행 중 solve 를 먼저 끝내거나 선점해야 한다(Android solveExec·iOS solveQueue·WASM 직렬 큐). */
EMSCRIPTEN_KEEPALIVE
int flipsight_bench_solve(const char* board_plus_turn) {
    fs_engine_ensure(&g_eng_solve, SOLVE_HASH_BITS, 1, 21);
    FsEngine *e = &g_eng_solve;
    if (EVAL_WEIGHT == NULL || !board_plus_turn || strlen(board_plus_turn) < 64) return -2000;
    Play   *play   = e->play;
    Search *search = &play->search;
    int score;

    g_last_search_nodes = 0.0; g_last_search_ms = 0;
    play_set_board(play, board_plus_turn);
    if (play_is_game_over(play)) return -2000;
    play_stop_pondering(play);
    play->state = IS_THINKING;

    search_cleanup(search);                                    // hash 비움: 매 실행 같은 조건(obftest 와 동일)
    search_set_board(search, &play->board, play->player);
    search_set_level(search, 60, search->eval.n_empties);      // exact
    search->options.multipv_depth = MULTIPV_DEPTH;
    search->options.verbosity = 0;
    search->observer = NULL;
    search->current_move_hook = NULL;
    search->current_move_result_hook = NULL;
    search->stop = STOP_END;

    fs_register_master(e, search);
    search_run(search);
    fs_clear_master(e);

    g_last_search_nodes = (double)search_count_nodes(search);
    g_last_search_ms = (int)search->time.spent;
    play->state = IS_WAITING;
    if (search->stop == STOP_ON_DEMAND) return -1000;
    score = search->result->score;
    return score;
}

/* ───── hint (analyze 엔진 경로) ───── */
EMSCRIPTEN_KEEPALIVE
const char* flipsight_hint(void) {
    FsEngine *e = &g_eng_analyze;
    int saved_level = options.level;
    options.level = e->level;                 // play_hint 가 전역 options.level 읽음
    fs_register_master(e, &e->play->search);
    play_hint(e->play, 1);
    fs_clear_master(e);
    options.level = saved_level;

    const Result* r = e->play->search.result;
    if (r && r->move != NOMOVE) {
        e->lastscore = r->score;
        int color = e->play->search.board.player;
        move_to_string(r->move, color, e->bestmove);
        return e->bestmove;
    }
    e->bestmove[0] = '\0';
    return e->bestmove;
}

/* ───── analyze 행 emit ───── */
static void emit_analyze_row_stdout(const AnalyzeHookRow *row) {   // WASM 빌드 경로
    FsEngine *e = &g_eng_analyze;
    char played_move[8] = "", best_move[8] = "", best_other_move[8] = "";
    const char *source = (row->source == ANALYZE_SOURCE_BOOK) ? "book" : "search";
    const char turn_bw = (row->turn == BLACK) ? 'B' : 'W';
    if (row->played_move != NOMOVE) { move_to_string(row->played_move, row->turn, played_move); lower_ascii(played_move); }
    if (row->best_move  != NOMOVE) { move_to_string(row->best_move,  row->turn, best_move);  lower_ascii(best_move); }
    if (row->has_best_other && row->best_other_move != NOMOVE) { move_to_string(row->best_other_move, row->turn, best_other_move); lower_ascii(best_other_move); }
    ++e->analyze_emitted;
    printf("ANALYZE_ROW\t{\"ply\":%d,\"turnBW\":\"%c\",\"playedMove\":\"%s\",\"playedScore\":%d,\"bestMove\":\"%s\",\"bestScore\":%d,\"hasBestOther\":%s,\"bestOtherMove\":\"%s\",\"bestOtherScore\":%d,\"legalCount\":%d,\"depth\":%d,\"percent\":%d,\"source\":\"%s\"}\n",
        row->ply + e->analyze_ply_offset + 1, turn_bw, played_move, row->played_score, best_move, row->best_score,
        row->has_best_other ? "true" : "false", best_other_move, row->best_other_score,
        row->legal_count, row->depth, row->percent, source);
    fflush(stdout);
}

#ifndef __EMSCRIPTEN__
static void emit_analyze_row_native(const AnalyzeHookRow *row) {   // 네이티브 경로
    FsEngine *e = &g_eng_analyze;
    char played_move[8] = "", best_move[8] = "", best_other_move[8] = "";
    const char *source = (row->source == ANALYZE_SOURCE_BOOK) ? "book" : "search";
    const char turn_bw = (row->turn == BLACK) ? 'B' : 'W';
    if (row->played_move != NOMOVE) { move_to_string(row->played_move, row->turn, played_move); lower_ascii(played_move); }
    if (row->best_move  != NOMOVE) { move_to_string(row->best_move,  row->turn, best_move);  lower_ascii(best_move); }
    if (row->has_best_other && row->best_other_move != NOMOVE) { move_to_string(row->best_other_move, row->turn, best_other_move); lower_ascii(best_other_move); }
    ++e->analyze_emitted;
    if (e->row_cb)
        e->row_cb(e->row_ctx,
            row->ply + e->analyze_ply_offset + 1, turn_bw,
            played_move, row->played_score, best_move, row->best_score,
            best_other_move, row->best_other_score, row->has_best_other,
            row->legal_count, row->depth, row->percent, source);
}
#endif

/* ───── solve 행/완료 emit (g_eng_solve) ───── */
static void emit_solve_hint60_row_stdout(int move, int score, int depth, int selectivity, int from_book) {
    FsEngine *e = &g_eng_solve;
    char move_str[8] = "";
    const char *source = from_book ? "book" : "search";
    if (move != NOMOVE) {
        move_to_string(move, e->play->player, move_str);
        lower_ascii(move_str);
    }
    ++e->solve_emitted;
#ifdef __EMSCRIPTEN__
    printf("SOLVE_ROW\t{\"move\":\"%s\",\"score\":%d,\"depth\":%d,\"selectivity\":%d,\"percent\":%d,\"source\":\"%s\"}\n",
        move_str, score, depth, selectivity, solve_hint60_percent(selectivity), source);
    fflush(stdout);
#else
    if (e->solve_row_cb)
        e->solve_row_cb(e->solve_ctx, move_str, score, depth,
                        selectivity, solve_hint60_percent(selectivity), from_book);
#endif
}

static void emit_solve_hint60_done_stdout(Search *search) {
    FsEngine *e = &g_eng_solve;
    if (e->solve_done_emitted) return;
    e->solve_done_emitted = 1;
#ifdef __EMSCRIPTEN__
    printf("SOLVE_DONE\t{\"count\":%d,\"stopped\":%s}\n",
        e->solve_emitted,
        (search && search->stop == STOP_ON_DEMAND) ? "true" : "false");
    fflush(stdout);
#else
    if (e->solve_done_cb)
        e->solve_done_cb(e->solve_ctx, e->solve_emitted,
                         (search && search->stop == STOP_ON_DEMAND) ? 1 : 0);
#endif
}

static void close_solve_hint60_stream(void) {
    FsEngine *e = &g_eng_solve;
    if (!e->solve_active) return;
    e->play->search.options.multipv_depth = MULTIPV_DEPTH;
    fs_clear_master(e);
    e->play->state = IS_WAITING;
    e->solve_active = 0;
}

EMSCRIPTEN_KEEPALIVE
int flipsight_search_stream_prepare(const char* board_plus_turn, int depth) {
    fs_engine_ensure(&g_eng_solve, SOLVE_HASH_BITS, 1, 21);   // WASM: 공유 엔진 lazy 생성
    FsEngine *e = &g_eng_solve;
    Play   *play   = e->play;
    Search *search = &play->search;

    if (!board_plus_turn) return -1;

    close_solve_hint60_stream();
    e->solve_emitted = 0;
    e->solve_done_emitted = 0;
    if (depth < 0) depth = 0;

    play_set_board(play, board_plus_turn);
    if (play_is_game_over(play)) {
        emit_solve_hint60_done_stdout(search);
        return 0;
    }

    play_stop_pondering(play);
    play->state = IS_THINKING;
    search->options.verbosity = 0;
    search_set_board(search, &play->board, play->player);
    search_set_level(search, depth, search->eval.n_empties);   // 인자 직결(전역 미사용)
    search->stop = STOP_END;
    fs_register_master(e, search);
    e->solve_active = 1;
    return search->movelist.n_moves;
}

EMSCRIPTEN_KEEPALIVE
int flipsight_search_stream_step(void) {
    FsEngine *e = &g_eng_solve;
    Play   *play   = e->play;
    Search *search = &play->search;
    MoveList book_moves;
    Move *m;

    if (!e->solve_active) return 0;

    if (search->stop == STOP_ON_DEMAND || movelist_is_empty(&search->movelist)) {
        emit_solve_hint60_done_stdout(search);
        close_solve_hint60_stream();
        return 0;
    }

    if (options.book_allowed && book_get_moves(play->book, &play->board, &book_moves)) {
        foreach_move (m, book_moves) {
            if (search->stop == STOP_ON_DEMAND) {
                emit_solve_hint60_done_stdout(search);
                close_solve_hint60_stream();
                return 0;
            }
            if (movelist_exclude(&search->movelist, m->x) != NULL) {
                emit_solve_hint60_row_stdout(m->x, m->score, 0, 0, 1);
                return 1;
            }
        }
    }

    if (movelist_is_empty(&search->movelist)) {
        emit_solve_hint60_done_stdout(search);
        close_solve_hint60_stream();
        return 0;
    }

    if (options.play_type == EDAX_TIME_PER_MOVE) search_set_move_time(search, options.time);
    else search_set_game_time(search, play->time[play->player].left);

    search_run(search);

    if (search->stop != STOP_END) {
        emit_solve_hint60_done_stdout(search);
        close_solve_hint60_stream();
        return 0;
    }

    if (search->result && search->result->move != NOMOVE) {
        emit_solve_hint60_row_stdout(
            search->result->move, search->result->score,
            search->result->depth, search->result->selectivity, 0);
        movelist_exclude(&search->movelist, search->result->move);
        return 1;
    }

    emit_solve_hint60_done_stdout(search);
    close_solve_hint60_stream();
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int flipsight_search_stream_finish(void) {
    FsEngine *e = &g_eng_solve;
    Search *search = &e->play->search;
    emit_solve_hint60_done_stdout(search);
    close_solve_hint60_stream();
    return e->solve_emitted;
}

// ── Native-progressive: single search_run with iterative deepening.
//    Root move rows emitted as each move finishes at each depth.

static void solve_hint60_run_observer(Result *result) {
    (void)result;
    edax_yield_to_host();
}

static void solve_hint60_run_current_move_hook(Search *s, Move *m) {
    FsEngine *e = &g_eng_solve;
    char mv[8] = "";
    int percent = solve_hint60_percent(s ? s->selectivity : NO_SELECTIVITY);
    if (m->x != NOMOVE) {
        move_to_string(m->x, s->player, mv);
        lower_ascii(mv);
    }
#ifdef __EMSCRIPTEN__
    (void)e;
    printf("SOLVE_PROGRESS\t{\"depth\":%d,\"percent\":%d,\"currentMove\":\"%s\"}\n", s->depth, percent, mv);
    fflush(stdout);
#else
    if (e->solve_prog_cb)
        e->solve_prog_cb(e->solve_ctx, s->depth, percent, mv);
#endif
}

static void solve_hint60_run_current_move_result_hook(Search *s, Move *m, int depth, int selectivity) {
    /* Recheck at the delivery boundary as cancellation can race the root hook. */
    if (!s || !m || m->x == NOMOVE || s->stop != RUNNING) return;
    emit_solve_hint60_row_stdout(m->x, m->score, depth, selectivity, 0);
    edax_yield_to_host();
}

static void parse_only_moves_csv(const char *only_moves_csv, int allowed[64]) {
    const char *p;
    memset(allowed, 0, sizeof(int) * 64);
    if (!only_moves_csv) return;
    for (p = only_moves_csv; *p; ) {
        if (isalpha((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
            char mv[3];
            int x;
            mv[0] = (char)tolower((unsigned char)p[0]);
            mv[1] = p[1];
            mv[2] = '\0';
            x = string_to_coordinate(mv);
            if (A1 <= x && x <= H8) allowed[x] = 1;
            p += 2;
        } else {
            ++p;
        }
    }
}

static void filter_search_movelist(Search *search, const int allowed[64]) {
    Move *m;
    int remove[64];
    int n_remove = 0;
    int i;
    foreach_move (m, search->movelist) {
        if (A1 <= m->x && m->x <= H8 && !allowed[m->x])
            remove[n_remove++] = m->x;
    }
    for (i = 0; i < n_remove; ++i)
        movelist_exclude(&search->movelist, remove[i]);
}

static int flipsight_search_stream_run_impl(const char* board_plus_turn, int max_depth, const char* only_moves_csv) {
    fs_engine_ensure(&g_eng_solve, SOLVE_HASH_BITS, 1, 21);   // WASM: 공유 엔진 lazy 생성
    FsEngine *e = &g_eng_solve;
    if (EVAL_WEIGHT == NULL) {            // ★ eval 미로딩: 탐색 금지(accumlate_eval NULL 역참조 크래시 방지)
        // 과거엔 그냥 return -1 → done 콜백 미방출 → JS(runOnePass)가 영구 대기("Evaluation not ready" 무한루프 → 90초 디스커넥트).
        // 빈 done(count=0)을 방출해 JS 가 정상 resolve → _isSolving 해제 → 재시도 가능하게 한다.
        e->solve_emitted = 0;
        e->solve_done_emitted = 0;
        emit_solve_hint60_done_stdout(NULL);
        return -1;
    }
    Play   *play   = e->play;
    Search *search = &play->search;
    MoveList book_moves;
    Move *m;
    int allowed[64];
    int has_only_filter = (only_moves_csv && only_moves_csv[0]);

    if (!board_plus_turn) return -1;

    close_solve_hint60_stream();
    e->solve_emitted = 0;
    e->solve_done_emitted = 0;

    if (max_depth < 0) max_depth = 0;

    play_set_board(play, board_plus_turn);
    if (play_is_game_over(play)) {
        emit_solve_hint60_done_stdout(search);
        return 0;
    }

    play_stop_pondering(play);
    play->state = IS_THINKING;

    search_set_board(search, &play->board, play->player);
    search_set_level(search, max_depth, search->eval.n_empties);   // 인자 직결(전역 미사용)

    if (has_only_filter) {
        parse_only_moves_csv(only_moves_csv, allowed);
        filter_search_movelist(search, allowed);
    }

    // Emit book moves first, then exclude them from the movelist.
    if (options.book_allowed && book_get_moves(play->book, &play->board, &book_moves)) {
        foreach_move (m, book_moves) {
            if (movelist_exclude(&search->movelist, m->x) != NULL)
                emit_solve_hint60_row_stdout(m->x, m->score, 0, 0, 1);
        }
    }

    if (movelist_is_empty(&search->movelist)) {
        emit_solve_hint60_done_stdout(search);
        play->state = IS_WAITING;
        return e->solve_emitted;
    }

    // multipv_depth=60: endgame depth can exceed max_depth → full-window for all moves at every depth.
    search->options.multipv_depth = 60;
    search->options.verbosity = 2;       // iterative deepening observer 활성(타이밍/yield 전용)
    search->observer = solve_hint60_run_observer;
    search->current_move_hook = solve_hint60_run_current_move_hook;
    search->current_move_result_hook = solve_hint60_run_current_move_result_hook;
    search->stop = STOP_END;

    fs_register_master(e, search);
    search_run(search);
    fs_clear_master(e);
    g_last_search_nodes = (double)search_count_nodes(search);
    g_last_search_ms = (int)search->time.spent;

    search->observer = NULL;
    search->current_move_hook = NULL;
    search->current_move_result_hook = NULL;
    search->options.multipv_depth = MULTIPV_DEPTH;
    search->options.verbosity = 0;

    emit_solve_hint60_done_stdout(search);
    play->state = IS_WAITING;
    return e->solve_emitted;
}

EMSCRIPTEN_KEEPALIVE
int flipsight_search_stream_run(const char* board_plus_turn, int max_depth) {
    return flipsight_search_stream_run_impl(board_plus_turn, max_depth, NULL);
}

EMSCRIPTEN_KEEPALIVE
int flipsight_search_stream_run_filtered(const char* board_plus_turn, int max_depth, const char* only_moves_csv) {
    return flipsight_search_stream_run_impl(board_plus_turn, max_depth, only_moves_csv);
}

static int count_board_discs64(const char *board_plus_turn) {
    int i, count = 0;
    if (!board_plus_turn) return 0;
    for (i = 0; i < 64 && board_plus_turn[i] != '\0'; ++i) {
        switch (tolower((unsigned char)board_plus_turn[i])) {
        case 'b': case 'x': case '*':
        case 'o': case 'w':
            ++count;
            break;
        default:
            break;
        }
    }
    return count;
}

/* ───── analyze (g_eng_analyze) ───── */
static int analyze_game_from_position(const char* board_plus_turn, const char* moves_str) {
    fs_engine_ensure(&g_eng_analyze, ANALYZE_HASH_BITS, 1, 21);   // WASM analyze(_s 포그라운드) / 네이티브는 no-op(이미 init)
    if (EVAL_WEIGHT == NULL) return -1;   // ★ eval 미로딩 시 탐색 금지(accumlate_eval NULL 역참조 크래시 방지)
    FsEngine *e = &g_eng_analyze;
    Play *play = e->play;
    int len, k;

    if (!moves_str) return -1;

    e->analyze_emitted = 0;
    e->analyze_ply_offset = 0;
    if (board_plus_turn && strlen(board_plus_turn) >= 64) {
        const int discs = count_board_discs64(board_plus_turn);
        e->analyze_ply_offset = (discs >= 4) ? (discs - 4) : 0;
        play_set_board(play, board_plus_turn);
    } else {
        play_new(play);
        board_init(&play->initial_board);
        board_init(&play->board);
        play->initial_player = BLACK;
        play->player = BLACK;
        play->i_game = play->n_game = 0;
    }

    len = (int)strlen(moves_str);
    for (k = 0; k + 1 < len; k += 2) {
        char mv[3] = { moves_str[k], moves_str[k + 1], 0 };
        if (play_must_pass(play)) play_move(play, PASS);
        if (!play_user_move(play, mv)) break;
    }

    /* play_analyze→play_alternative 가 전역 options.level 을 읽으므로(upstream)
       analyze 실행 구간에서만 전역 level=analyze level 로 두고 끝나면 복구. */
    int saved_level = options.level;
    options.level = e->level;

    fs_register_master(e, &play->search);
#ifdef __EMSCRIPTEN__
    g_analyze_result_hook = emit_analyze_row_stdout;
#else
    g_analyze_result_hook = emit_analyze_row_native;
#endif
    play_analyze(play, play->i_game);
    g_analyze_result_hook = NULL;
    fs_clear_master(e);

    options.level = saved_level;
    e->analyze_ply_offset = 0;

#ifdef __EMSCRIPTEN__
    printf("ANALYZE_DONE\t{\"count\":%d,\"stopped\":%s}\n",
        e->analyze_emitted,
        (play->search.stop == STOP_ON_DEMAND) ? "true" : "false");
    fflush(stdout);
#endif
    /* 네이티브: done 은 반환값(count)으로, stopped 는 플러그인의 cancel 상태로 판정 */
    return e->analyze_emitted;
}

EMSCRIPTEN_KEEPALIVE
int flipsight_analyze_game(const char* moves_str) {
    return analyze_game_from_position(NULL, moves_str);
}

EMSCRIPTEN_KEEPALIVE
int flipsight_analyze_game_from_board(const char* board_plus_turn, const char* moves_str) {
    return analyze_game_from_position(board_plus_turn, moves_str);
}
