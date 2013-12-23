/****************************************************************************
 *
 * 単体テストテンプレート
 *
 ****************************************************************************/

/*** ここからしばらくドライバコードなので変更しない ***/
/*** テストを書く場所はずっと下のほう               ***/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMTEST 0

/*** 最大テスト数 ***/
#define TEST_MAX (300)

/*** ログ出力関連 ***/
#define LOG(format, args...) \
        printf("%s L=%d: " format , __FILE__ , __LINE__ , ## args )
#if 1
#define DEBUGLOG(format, args...)
#else
#define DEBUGLOG(format, args...) \
        printf("%s L=%d: " format , __FILE__ , __LINE__ , ## args )
#endif
void dumplog(void* ptr, unsigned long len)
{
    unsigned char* p = (unsigned char*)ptr;
    char hexstr[(3*16)+1];
    char ascstr[(1*16)+1];
    unsigned int i, j;

    LOG("     | +0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +A +B +C +D +E +F |\n");
    LOG("-----|-------------------------------------------------|"
        "-----------------\n");
    for (i = 0; i < len; i += 16) {
        memset(hexstr, 0, sizeof(hexstr));
        memset(ascstr, 0, sizeof(ascstr));
        for (j = 0; j < 16; j++) {
            if (i+j < len) {
                sprintf(hexstr + (j*3), "%02X ", p[i+j]);
                if (p[i+j] < 0x1F || 0x7E < p[i+j]) {
                    ascstr[j] = '.';
                } else {
                    ascstr[j] = p[i+j];
                }
            } else {
                strcpy(hexstr + (j*3), "   ");
                ascstr[j] = ' ';
            }
        }
        LOG("%04X | %s| %s\n", i, hexstr, ascstr);
    }
}

/*** テストテーブル ***/
typedef int (*TestFunc)(void);
typedef struct {
    TestFunc func;
    const char* name;
    int result;
    const char* comment;
} TestCase;
static TestCase g_testtbl[TEST_MAX];

/*** テスト登録関連 ***/
void addtest(TestFunc func, const char* name, const char* comment) {
    int i;
    for (i = 0; i < TEST_MAX; i++) {
        if (g_testtbl[i].func == NULL) {
            g_testtbl[i].func = func;
            g_testtbl[i].name = name;
            g_testtbl[i].comment = comment;
            g_testtbl[i].result = 0;
            break;
        }
    }
    if (i >= TEST_MAX) {
        LOG("<< %s >> invalid. Test max over \n", name);
    }
    return;
}
#ifdef __cplusplus
class test_register
{
public:
    test_register(TestFunc func, const char* name, const char* comment) {
        addtest(func, name, comment);
    }
};
#define MAKE_TEST(func, comment) \
        int func(void); \
        test_register _registerobj_##func(func, #func, comment); \
        int func(void)
#define ADD_TEST(func, comment)
#else
#define MAKE_TEST(func, comment) int func(void)
#define ADD_TEST(func, comment) addtest(func, #func, comment)
#endif

/*** アサーション関連 ***/
#define ASSERT_ERROR (-1)
#define ASSERT(cond) \
        if (!(cond)) { \
            LOG("** NG ** [%s] is \"%s\"\n", __func__, #cond); \
            return ASSERT_ERROR; \
        }
#define ASSERT_BASE(cond, param, form) \
        if (!(cond)) { \
            LOG("** NG ** [%s] is \"%s\" >>> %s: (" #form ")\n", \
                __func__, #cond, #param, param); \
            return ASSERT_ERROR; \
        }
#define ASSERT_D(cond, param, size) \
        if (!(cond)) { \
            LOG("** NG ** [%s] is \"%s\" >>> %s:\n",__func__, #cond, #param); \
            dumplog(param, size); \
            return ASSERT_ERROR; \
        }
#define ASSERT_I(cond, param)  ASSERT_BASE(cond, param, %d)
#define ASSERT_L(cond, param)  ASSERT_BASE(cond, param, %ld)
#define ASSERT_X(cond, param)  ASSERT_BASE(cond, param, 0x%x)
#define ASSERT_LX(cond, param) ASSERT_BASE(cond, param, 0x%lx)
#define ASSERT_P(cond, param)  ASSERT_BASE(cond, param, %p)
#define ASSERT_S(cond, param)  ASSERT_BASE(cond, param, %s)

/*** 簡易スマートポインタ ***/
#ifdef __cplusplus
#define FREE(p) free(p)
template <class T>
class auto_ptr
{
public:
    auto_ptr(T* p = NULL) : p_(p) {}
    ~auto_ptr() { if (p_) FREE(p_); }     /* スコープ外で解放             */
    T** operator&() { return &p_; }       /* (&):実ポインタのアドレス取得 */
    T* operator*() { return p_; }         /* (*):実ポインタ取得           */
    T* operator=(T* p) { return p_ = p; } /* (=):実ポインタ書き換え       */
private:
    auto_ptr(auto_ptr& rhs);
    auto_ptr& operator=(auto_ptr& rhs);
    T* p_;
};
#endif

/*** メモリリークチェック ***/
const char* g_active_testname = "";
#if MEMTEST /* 有効時コンパイル警告が出ることがある */
#include <malloc.h>
#include <sys/sem.h>
#define MEMCTXMAX (300)
static struct {
    void* adr;
    size_t size;
    const void* ra;
    const char* testname;
    int sumflg;
} g_memctx[MEMCTXMAX];
int g_semid = -1;
void*(*org_malloc)(size_t, const void*);
void*(*org_realloc)(void*, size_t, const void*);
void*(*org_memalign)(size_t, size_t, const void*);
void (*org_free)(void*, const void*);
void* new_malloc(size_t size, const void* ra);
void* new_realloc(void* ptr, size_t size, const void* ra);
void* new_memalign(size_t alignment, size_t size, const void* ra);
void  new_free(void* ptr, const void* ra);
void g_lock(void)
{
    if (g_semid != -1) {
        struct sembuf semb;
        semb.sem_num = 0;
        semb.sem_op  = -1;
        semb.sem_flg = SEM_UNDO;
        if (semop(g_semid, &semb, 1) == -1) {
            LOG("semop(lock) error\n");
        }
    }
    return;
}
void g_unlock(void)
{
    if (g_semid != -1) {
        struct sembuf semb;
        semb.sem_num = 0;
        semb.sem_op  = 1;
        semb.sem_flg = SEM_UNDO;
        if (semop(g_semid, &semb, 1) == -1) {
            LOG("semop(unlock) error\n");
        }
    }
    return;
}
void addmemctx(void* adr, size_t size, const void* ra)
{
    int i;
    for (i = 0; i < MEMCTXMAX; i++) {
        if (g_memctx[i].adr == NULL) {
            break;
        }
    }
    if (i >= MEMCTXMAX) {
        LOG("addmemctx max over\n");
        return;
    }
    g_memctx[i].adr  = adr;
    g_memctx[i].size = size;
    g_memctx[i].ra   = ra;
    g_memctx[i].testname = g_active_testname;
    return;
}
void delmemctx(void* adr)
{
    int i;
    for (i = 0; i < MEMCTXMAX; i++) {
        if (g_memctx[i].adr == adr) {
            g_memctx[i].adr  = NULL;
            g_memctx[i].size = 0;
            g_memctx[i].ra   = NULL;
            g_memctx[i].testname = NULL;
        }
    }
    return;
}
void hook_start(void)
{
    __malloc_hook   = new_malloc;
    __realloc_hook  = new_realloc;
    __memalign_hook = new_memalign;
    __free_hook     = new_free;
    return;
}
void hook_end(void)
{
    __malloc_hook   = org_malloc;
    __realloc_hook  = org_realloc;
    __memalign_hook = org_memalign;
    __free_hook     = org_free;
    return;
}
void* new_malloc(size_t size, const void* ra)
{
    void* p;
    g_lock();
    hook_end();
    p = malloc(size);
    if (p) {
        addmemctx(p, size, ra);
    }
    hook_start();
    g_unlock();
    return p;
}
void* new_realloc(void* ptr, size_t size, const void* ra)
{
    void* p;
    g_lock();
    hook_end();
    p = realloc(ptr, size);
    if (p) {
        delmemctx(ptr);
        addmemctx(p, size, ra);
    }
    hook_start();
    g_unlock();
    return p;
}
void* new_memalign(size_t alignment, size_t size, const void* ra)
{
    void* p;
    g_lock();
    hook_end();
    p = memalign(alignment, size);
    if (p) {
        addmemctx(p, size, ra);
    }
    hook_start();
    g_unlock();
    return p;
}
void new_free(void* ptr, const void* ra)
{
    g_lock();
    hook_end();
    if (ptr) {
        delmemctx(ptr);
    }
    free(ptr);
    hook_start();
    g_unlock();
    return;
}
void memchk_start(void)
{
    if ((g_semid = semget(IPC_PRIVATE, 1, 0666)) == -1) {
        LOG("semget error\n");
    } else {
        if (semctl(g_semid, 0, SETVAL, 1) == -1) {
            LOG("semctl(initial unlock) error\n");
        }
    }
    memset(g_memctx, 0, sizeof(g_memctx));
    org_malloc   = __malloc_hook;
    org_realloc  = __realloc_hook;
    org_memalign = __memalign_hook;
    org_free     = __free_hook;
    hook_start();
    return;
}
void memchk_end(void)
{
    int i, j, cnt, size;
    hook_end();
    if (g_semid != -1) { semctl(g_semid, 0, IPC_RMID); }
    for (i = 0; i < MEMCTXMAX; i++) {
        if (g_memctx[i].adr != NULL) {
            break;
        }
    }
    if (i >= MEMCTXMAX) {
        LOG("MEMORY LEAK NONE\n");
        return;
    }
    LOG("------ MEMORY LEAK FOUND ------\n");
    LOG(" [SUMMARY]\n");
    for (i = 0; i < MEMCTXMAX; i++) {
        if (g_memctx[i].adr != NULL && g_memctx[i].sumflg == 0) {
            cnt = 0;
            size = 0;
            for (j = i; j < MEMCTXMAX; j++) {
                if (g_memctx[j].adr != NULL &&
                    !strcmp(g_memctx[i].testname, g_memctx[j].testname)) {
                    cnt++;
                    size += g_memctx[j].size;
                    g_memctx[j].sumflg = 1;
                }
            }
            LOG("  << %s >> leak count (%d) size (%d)\n",
                g_memctx[i].testname, cnt, size);
        }
    }
    LOG(" [LEAK ADDRESS]\n");
    for (i = 0; i < MEMCTXMAX; i++) {
        if (g_memctx[i].adr != NULL) {
            LOG(" [%s] address(%p) ra(%p) size(%d)\n",
                g_memctx[i].testname, g_memctx[i].adr,
                g_memctx[i].ra, g_memctx[i].size);
        }
    }
    LOG("-------------------------------\n");
    return;
}
#define MEMCHK_START() memchk_start()
#define MEMCHK_END() memchk_end()
#else
#define MEMCHK_START()
#define MEMCHK_END()
#endif

/*** テストドライバメイン ***/
int g_argc;
char** g_argv;
void test_init(void);
void test_driver(int argc, char** argv)
{
    int i = 0, ok = 0, ng = 0;
    g_argc = argc;
    g_argv = argv;

    test_init();

    MEMCHK_START();
    for (i = 0; g_testtbl[i].func != NULL; i++) {
        DEBUGLOG("<<<<< START [%s] (%s) >>>>>\n",
                 g_testtbl[i].name, g_testtbl[i].comment);
        g_active_testname = g_testtbl[i].name;
        g_testtbl[i].result = g_testtbl[i].func();
    }
    for (i = 0; g_testtbl[i].func != NULL; i++) {
        if (g_testtbl[i].result == 0) { ok++; }
        else { ng++; }
    }
    LOG("TEST RESULT [ALL:%d OK:%d NG:%d %.2f%%]\n", ok+ng, ok, ng,
        ((double)ok)/(ok+ng)*100);
    if (ng > 0) {
        LOG("------ ERROR TEST LIST --------\n");
        for (i = 0; g_testtbl[i].func != NULL; i++) {
            if (g_testtbl[i].result != 0) {
                LOG("No.%d %s %s\n", i+1,
                    g_testtbl[i].name, g_testtbl[i].comment);
            }
        }
        LOG("-------------------------------\n");
    }
    MEMCHK_END();
    return;
}
/*** テストドライバソースここまで ***/
/*** ここからテストを書く         ***/

/**********************************/
/******** テスト初期化処理 ********/
/**********************************/
void test_init(void)
{
}

/**********************************/
/******** テストコード本体 ********/
/**********************************/
MAKE_TEST(test001, "sample test")
{
    int a = 1;
    int b = 2;
    ASSERT((a + b) == 3);
    return 0; /* 正常終了:0 エラー:1 */
}


/*** エントリ関数 ***/
#ifdef __cplusplus
extern "C"
#endif
int unittest(int argc, char** argv)
{
    /*******************************************/
    /******** Cソースはここでテスト登録 ********/
    /******** (C++ソースは不要)         ********/
    /*******************************************/
    ADD_TEST(test001, "");

    test_driver(argc, argv);
    return 0;
}
