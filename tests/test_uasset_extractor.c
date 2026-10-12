/*
 * test_uasset_extractor.c — Unit tests for semantic Blueprint extractor.
 */
#include "foundation/compat.h"
#include "foundation/compat_fs.h"

int tf_pass_count = 0;
int tf_fail_count = 0;
int tf_skip_count = 0;
int tf_deselected_count = 0;
bool tf_suite_enter(const char *suite) { (void)suite; return true; }
void tf_suite_leave(const char *suite) { (void)suite; }
bool tf_test_selected(const char *test) { (void)test; return true; }
void cbm_memev_flush_thread(void) {}
void cbm_log(int level, const char *event, ...) { (void)level; (void)event; }

#include "test_framework.h"
#include "cbm.h"

CBMFileResult *cbm_result_alloc(void) {
    return (CBMFileResult *)calloc(1, sizeof(CBMFileResult));
}
void cbm_free_result(CBMFileResult *r) {
    free(r);
}
void cbm_defs_push(CBMDefArray *arr, CBMArena *a, CBMDefinition def) {
    if (arr->count >= arr->cap) {
        int new_cap = arr->cap == 0 ? 32 : arr->cap * 2;
        CBMDefinition *new_items = (CBMDefinition *)cbm_arena_alloc(a, (size_t)new_cap * sizeof(CBMDefinition));
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
#include "arena.h"

static void write_i32(uint8_t *p, int32_t val) {
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
}

TEST(uasset_extract_blueprint_definitions_and_calls) {
    uint8_t buf[2048] = {0};
    size_t off = 0;

    // Header
    write_i32(buf + off, 0x9E2A83C1); off += 4;
    write_i32(buf + off, -7); off += 4;
    off += 16; // version info
    size_t total_hdr_pos = off; off += 4;
    write_i32(buf + off, 0); off += 4; // folder name
    write_i32(buf + off, 0); off += 4; // flags
    size_t name_meta = off; off += 8;
    off += 4; // loc id
    off += 8; // gatherable
    size_t exp_meta = off; off += 8;
    size_t imp_meta = off; off += 8;
    off += 4; // depends
    write_i32(buf + total_hdr_pos, (int32_t)off);

    // Names:
    // 0: None, 1: BlueprintGeneratedClass, 2: BP_Hero_C, 3: Character,
    // 4: Function, 5: ReceiveBeginPlay, 6: GameplayStatics, 7: PlaySound2D
    const char *names[] = {
        "None", "BlueprintGeneratedClass", "BP_Hero_C", "Character",
        "Function", "ReceiveBeginPlay", "GameplayStatics", "PlaySound2D"
    };
    int num_names = 8;
    size_t name_off = off;
    write_i32(buf + name_meta, num_names);
    write_i32(buf + name_meta + 4, (int32_t)name_off);
    for (int i = 0; i < num_names; i++) {
        int len = (int)strlen(names[i]) + 1;
        write_i32(buf + off, len); off += 4;
        memcpy(buf + off, names[i], len); off += len;
        off += 4; // hash
    }

    // Imports:
    // Import 0: ClassName = Character (3), ObjectName = Character (3)
    // Import 1: ClassName = Package (0), ObjectName = GameplayStatics (6)
    // Import 2: ClassName = Function (4), OuterIndex = -2 (points to Import 1), ObjectName = PlaySound2D (7)
    size_t imp_off = off;
    write_i32(buf + imp_meta, 3);
    write_i32(buf + imp_meta + 4, (int32_t)imp_off);

    // Import 0: Character
    write_i32(buf + off, 0); off += 4; write_i32(buf + off, 0); off += 4;
    write_i32(buf + off, 3); off += 4; write_i32(buf + off, 0); off += 4;
    write_i32(buf + off, 0); off += 4;
    write_i32(buf + off, 3); off += 4; write_i32(buf + off, 0); off += 4;
    off += 8;

    // Import 1: GameplayStatics
    write_i32(buf + off, 0); off += 4; write_i32(buf + off, 0); off += 4;
    write_i32(buf + off, 0); off += 4; write_i32(buf + off, 0); off += 4;
    write_i32(buf + off, 0); off += 4;
    write_i32(buf + off, 6); off += 4; write_i32(buf + off, 0); off += 4;
    off += 8;

    // Import 2: Function PlaySound2D, outer = -2 (GameplayStatics)
    write_i32(buf + off, 0); off += 4; write_i32(buf + off, 0); off += 4;
    write_i32(buf + off, 4); off += 4; write_i32(buf + off, 0); off += 4; // ClassName = Function
    write_i32(buf + off, -2); off += 4; // outer = -2
    write_i32(buf + off, 7); off += 4; write_i32(buf + off, 0); off += 4; // ObjectName = PlaySound2D
    off += 8;

    // Exports:
    // Export 0: BP_Hero_C (class_idx = 1 (BlueprintGeneratedClass), super_idx = -1 (Character))
    // Export 1: ReceiveBeginPlay (class_idx = 4 (Function), outer = 1 (BP_Hero_C))
    size_t exp_off = off;
    write_i32(buf + exp_meta, 2);
    write_i32(buf + exp_meta + 4, (int32_t)exp_off);

    // Export 0: BP_Hero_C
    write_i32(buf + off, 1); off += 4;  // class_index
    write_i32(buf + off, -1); off += 4; // super_index -> Character
    write_i32(buf + off, 0); off += 4;
    write_i32(buf + off, 0); off += 4;
    write_i32(buf + off, 2); off += 4;  // obj_name = BP_Hero_C
    off += 28;

    // Export 1: ReceiveBeginPlay
    write_i32(buf + off, 4); off += 4;  // class_index = Function
    write_i32(buf + off, 0); off += 4;
    write_i32(buf + off, 0); off += 4;
    write_i32(buf + off, 1); off += 4;  // outer = 1 (BP_Hero_C)
    write_i32(buf + off, 5); off += 4;  // obj_name = ReceiveBeginPlay
    off += 28;

    CBMArena arena, scratch;
    cbm_arena_init(&arena);
    cbm_arena_init(&scratch);

    CBMFileResult *result = cbm_result_alloc();
    result->module_qn = "MyProject.Content.BP_Hero";

    CBMExtractCtx ctx = {
        .arena = &arena,
        .scratch = &scratch,
        .result = result,
        .source = (const char *)buf,
        .source_len = (int)off,
        .language = CBM_LANG_BLUEPRINT,
        .project = "MyProject",
        .rel_path = "Content/BP_Hero.uasset",
        .module_qn = result->module_qn,
    };

    cbm_uasset_extract_blueprint(&ctx);

    // Verify Class definition
    ASSERT_EQ(result->defs.count, 2);
    const CBMDefinition *class_def = &result->defs.items[0];
    ASSERT_STR_EQ(class_def->name, "BP_Hero_C");
    ASSERT_STR_EQ(class_def->label, "Class");
    ASSERT_NOT_NULL(class_def->base_classes);
    ASSERT_STR_EQ(class_def->base_classes[0], "Character");

    // Verify Method definition
    const CBMDefinition *method_def = &result->defs.items[1];
    ASSERT_STR_EQ(method_def->name, "ReceiveBeginPlay");
    ASSERT_STR_EQ(method_def->label, "Method");
    ASSERT_STR_EQ(method_def->parent_class, class_def->qualified_name);

    // Verify Call
    ASSERT_EQ(result->calls.count, 1);
    const CBMCall *call = &result->calls.items[0];
    ASSERT_STR_EQ(call->callee_name, "GameplayStatics.PlaySound2D");

    cbm_free_result(result);
    cbm_arena_destroy(&scratch);
    cbm_arena_destroy(&arena);
    PASS();
}

SUITE(uasset_extractor) {
    RUN_TEST(uasset_extract_blueprint_definitions_and_calls);
}

int main(void) {
    RUN_SUITE(uasset_extractor);
    printf("Tests passed: %d, failed: %d\n", tf_pass_count, tf_fail_count);
    return tf_fail_count == 0 ? 0 : 1;
}
