/*
 * test_uasset_lang.c — Unit tests for CBM_LANG_BLUEPRINT registration.
 */
#include "foundation/compat.h"
#include "foundation/compat_fs.h"
#include "discover/discover.h"
#include "cbm.h"

int tf_pass_count = 0;
int tf_fail_count = 0;
int tf_skip_count = 0;
int tf_deselected_count = 0;
bool tf_suite_enter(const char *suite) {
    (void)suite;
    return true;
}
void tf_suite_leave(const char *suite) {
    (void)suite;
}
bool tf_test_selected(const char *test) {
    (void)test;
    return true;
}

const cbm_userconfig_t *cbm_get_user_lang_config(void) {
    return NULL;
}
CBMLanguage cbm_userconfig_lookup(const cbm_userconfig_t *ucfg, const char *ext) {
    (void)ucfg;
    (void)ext;
    return CBM_LANG_COUNT;
}
FILE *cbm_fopen(const char *path, const char *mode) {
    return fopen(path, mode);
}
char *cbm_strcasestr(const char *h, const char *n) {
    return (char *)strstr(h, n);
}

#include "test_framework.h"

TEST(uasset_lang_registration) {
    ASSERT_EQ(cbm_language_for_extension(".uasset"), CBM_LANG_BLUEPRINT);
    ASSERT_STR_EQ(cbm_language_name(CBM_LANG_BLUEPRINT), "Blueprint");
    PASS();
}

SUITE(uasset_lang) {
    RUN_TEST(uasset_lang_registration);
}

int main(void) {
    RUN_SUITE(uasset_lang);
    printf("Tests passed: %d, failed: %d\n", tf_pass_count, tf_fail_count);
    return tf_fail_count == 0 ? 0 : 1;
}
