/* Тестовий фреймворк: лічильники + макроси CHECK */
#ifndef MINITEST_H
#define MINITEST_H
#include <stdio.h>

static int mt_checks, mt_failed, mt_known;
static const char *mt_cur = "";

#define CHECK(cond) do { mt_checks++; if (!(cond)) { mt_failed++; \
    fprintf(stderr, "  [FAIL] %s:%d  %s  (тест %s)\n", __FILE__, __LINE__, #cond, mt_cur); } } while (0)

#define CHECK_EQ(exp, act) do { long mt_e = (long)(exp), mt_a = (long)(act); mt_checks++; \
    if (mt_e != mt_a) { mt_failed++; \
    fprintf(stderr, "  [FAIL] %s:%d  %s: очікувалось %ld, отримано %ld (тест %s)\n", \
           __FILE__, __LINE__, #act, mt_e, mt_a, mt_cur); } } while (0)

/* Відомий дефект прошивки: не валить make test, але виводиться у звіті */
#define KNOWN_DEFECT(cond, text) do { if (!(cond)) { mt_known++; \
    fprintf(stderr, "  [KNOWN DEFECT] %s\n", text); } else { \
    fprintf(stderr, "  [DEFECT FIXED?] %s -> умова вже виконується, перенесіть у CHECK\n", text); } } while (0)

#define RUN(fn) do { mt_cur = #fn; fprintf(stderr, "[RUN ] %s\n", #fn); fn(); } while (0)

#define MT_SUMMARY() ( fprintf(stderr, "\n=== перевірок: %d, провалено: %d, відомих дефектів: %d ===\n", \
                              mt_checks, mt_failed, mt_known), mt_failed ? 1 : 0 )
#endif
