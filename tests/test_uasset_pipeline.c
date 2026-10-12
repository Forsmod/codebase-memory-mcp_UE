/*
 * test_uasset_pipeline.c — Test cbm_extract_file_ex dispatching to CBM_LANG_BLUEPRINT.
 */
#include "foundation/compat.h"
#include "foundation/compat_fs.h"

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
void cbm_memev_flush_thread(void) {}
void cbm_log(int level, const char *event, ...) {
    (void)level;
    (void)event;
}

#include "test_framework.h"
#include "cbm.h"

CBMFileResult *cbm_result_alloc(void) {
    return (CBMFileResult *)calloc(1, sizeof(CBMFileResult));
}
void cbm_defs_push(CBMDefArray *arr, CBMArena *a, CBMDefinition def) {
    if (arr->count >= arr->cap) {
        int new_cap = arr->cap == 0 ? 32 : arr->cap * 2;
        CBMDefinition *new_items =
            (CBMDefinition *)cbm_arena_alloc(a, (size_t)new_cap * sizeof(CBMDefinition));
        if (arr->items && arr->count > 0) {
            memcpy(new_items, arr->items, (size_t)arr->count * sizeof(CBMDefinition));
        }
        arr->items = new_items;
        arr->cap = new_cap;
    }
    arr->items[arr->count++] = def;
}
void cbm_calls_push(CBMCallArray *arr, CBMArena *a, CBMCall call) {
    if (arr->count >= arr->cap) {
        int new_cap = arr->cap == 0 ? 32 : arr->cap * 2;
        CBMCall *new_items = (CBMCall *)cbm_arena_alloc(a, (size_t)new_cap * sizeof(CBMCall));
        if (arr->items && arr->count > 0) {
            memcpy(new_items, arr->items, (size_t)arr->count * sizeof(CBMCall));
        }
        arr->items = new_items;
        arr->cap = new_cap;
    }
    arr->items[arr->count++] = call;
}

#include "extract_uasset.h"

static void write_i32(uint8_t *p, int32_t val) {
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
}

TEST(uasset_pipeline_extraction_dispatch) {
    uint8_t buf[2048] = {0};
    size_t off = 0;

    // Header
    write_i32(buf + off, 0x9E2A83C1);
    off += 4;
    write_i32(buf + off, -7);
    off += 4;
    off += 16;
    size_t total_hdr_pos = off;
    off += 4;
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 0);
    off += 4;
    size_t name_meta = off;
    off += 8;
    off += 4;
    off += 8;
    size_t exp_meta = off;
    off += 8;
    size_t imp_meta = off;
    off += 8;
    off += 4;
    write_i32(buf + total_hdr_pos, (int32_t)off);

    const char *names[] = {"None", "BlueprintGeneratedClass", "BP_Player_C", "Actor"};
    int num_names = 4;
    size_t name_off = off;
    write_i32(buf + name_meta, num_names);
    write_i32(buf + name_meta + 4, (int32_t)name_off);
    for (int i = 0; i < num_names; i++) {
        int len = (int)strlen(names[i]) + 1;
        write_i32(buf + off, len);
        off += 4;
        memcpy(buf + off, names[i], len);
        off += len;
        off += 4;
    }

    // Imports: Actor
    size_t imp_off = off;
    write_i32(buf + imp_meta, 1);
    write_i32(buf + imp_meta + 4, (int32_t)imp_off);
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 3);
    off += 4;
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 3);
    off += 4;
    write_i32(buf + off, 0);
    off += 4;
    off += 8;

    // Exports: BP_Player_C
    size_t exp_off = off;
    write_i32(buf + exp_meta, 1);
    write_i32(buf + exp_meta + 4, (int32_t)exp_off);
    write_i32(buf + off, 1);
    off += 4; // class_index
    write_i32(buf + off, -1);
    off += 4; // super_index
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 0);
    off += 4;
    write_i32(buf + off, 2);
    off += 4; // obj_name = BP_Player_C
    off += 28;

    CBMFileResult *res = cbm_result_alloc();
    res->module_qn = "MyProject.Content.BP_Player";

    CBMArena arena, scratch;
    cbm_arena_init(&arena);
    cbm_arena_init(&scratch);

    CBMExtractCtx ctx = {
        .arena = &arena,
        .scratch = &scratch,
        .result = res,
        .source = (const char *)buf,
        .source_len = (int)off,
        .language = CBM_LANG_BLUEPRINT,
        .project = "MyProject",
        .rel_path = "Content/BP_Player.uasset",
        .module_qn = res->module_qn,
    };

    cbm_uasset_extract_blueprint(&ctx);

    ASSERT_EQ(res->defs.count, 1);
    ASSERT_STR_EQ(res->defs.items[0].name, "BP_Player_C");
    ASSERT_NOT_NULL(res->defs.items[0].base_classes);
    ASSERT_STR_EQ(res->defs.items[0].base_classes[0], "Actor");

    cbm_arena_destroy(&scratch);
    cbm_arena_destroy(&arena);
    free(res);
    PASS();
}

SUITE(uasset_pipeline) {
    RUN_TEST(uasset_pipeline_extraction_dispatch);
}

int main(void) {
    RUN_SUITE(uasset_pipeline);
    printf("Tests passed: %d, failed: %d\n", tf_pass_count, tf_fail_count);
    return tf_fail_count == 0 ? 0 : 1;
}
