#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RETRIES_BUCKETS 16

typedef struct {
    uint64_t exec_calls;
    uint64_t valid_hits;
    uint64_t dirty_conflicts;
    uint64_t dirty_list_hits;
    uint64_t promote_byte_mask;
    uint64_t promote_no_immediates;
    uint64_t defer_no_immediates;
    uint64_t retry_resets;
    uint64_t recompiled_execs;
    uint64_t rebuild_paths;
    int      has_defer_field;
    int      has_retry_resets_field;
    int      saw_summary;
} summary_t;

typedef struct {
    uint64_t total;
    uint64_t action_byte_mask;
    uint64_t action_no_immediates;
    uint64_t action_defer_no_immediates;
    uint64_t action_other;
} transitions_t;

typedef struct {
    uint64_t total;
    uint64_t transition;
    uint64_t periodic;
    uint64_t rebuild;
    uint64_t new_block;
    uint64_t unexpected_noimm_without_bmask;
    uint64_t other;
} tags_t;

typedef struct {
    uint64_t before[RETRIES_BUCKETS];
    uint64_t after[RETRIES_BUCKETS];
} retries_hist_t;

typedef struct {
    uint64_t call_rel;
    uint64_t call_abs_nonlocal;
    uint64_t call_abs_range;
    uint64_t jump_rel;
    uint64_t jump_abs_nonlocal;
    uint64_t jump_abs_range;
    uint64_t cbnz_rel19;
    uint64_t cbnz_rel26;
    uint64_t cbnz_abs_nonlocal;
    uint64_t cbnz_abs_range;
    uint64_t cbnz_total;
    uint64_t beq_rel19;
    uint64_t beq_rel26;
    uint64_t beq_abs_nonlocal;
    uint64_t beq_abs_range;
    uint64_t beq_total;
    uint64_t bcond_rel19;
    uint64_t bcond_rel26;
    uint64_t bcond_total;
    uint64_t tbxz_rel14;
    uint64_t tbxz_rel26;
    uint64_t tbxz_total;
    uint64_t total;
    int      has_cbnz_fields;
    int      has_beq_fields;
    int      has_bcond_fields;
    int      has_tbxz_fields;
    int      saw;
} a013_path_t;

static int
is_key_boundary(const char c)
{
    return c == '\0' || isspace((unsigned char) c);
}

static int
extract_u64(const char *line, const char *key, uint64_t *out)
{
    char        needle[96];
    char       *end_ptr = NULL;
    const char *match   = NULL;
    const char *search  = line;
    size_t      nlen;

    snprintf(needle, sizeof(needle), "%s=", key);
    nlen  = strlen(needle);
    while ((match = strstr(search, needle)) != NULL) {
        if (match == line || is_key_boundary(*(match - 1))) {
            const char *value = match + nlen;

            *out = strtoull((char *) value, &end_ptr, 10);
            if ((const char *) end_ptr == value)
                return 0;
            return 1;
        }
        search = match + 1;
    }

    return 0;
}

static void
extract_token(const char *line, const char *key, char *out, size_t out_len)
{
    char        needle[96];
    const char *p;
    size_t      i = 0;

    if (!out_len)
        return;
    out[0] = '\0';
    snprintf(needle, sizeof(needle), "%s=", key);
    p = strstr(line, needle);
    if (!p)
        return;
    p += strlen(needle);

    while (p[i] && !isspace((unsigned char) p[i]) && i + 1 < out_len) {
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';
}

static void
bucket_add(uint64_t *buckets, uint64_t v)
{
    if (v >= (RETRIES_BUCKETS - 1))
        buckets[RETRIES_BUCKETS - 1]++;
    else
        buckets[v]++;
}

static void
update_tag_from_line(const char *line, tags_t *tags)
{
    char tag[64];

    if (!strstr(line, "DYNAREC_S03A_SUMMARY"))
        return;

    tags->total++;
    extract_token(line, "tag", tag, sizeof(tag));

    if (!strcmp(tag, "transition"))
        tags->transition++;
    else if (!strcmp(tag, "periodic"))
        tags->periodic++;
    else if (!strcmp(tag, "rebuild"))
        tags->rebuild++;
    else if (!strcmp(tag, "new_block"))
        tags->new_block++;
    else if (!strcmp(tag, "unexpected_noimm_without_bmask"))
        tags->unexpected_noimm_without_bmask++;
    else
        tags->other++;
}

static void
update_a013_from_line(const char *line, a013_path_t *a)
{
    uint64_t v;
    int      saw_cbnz  = 0;
    int      saw_beq   = 0;
    int      saw_bcond = 0;
    int      saw_tbxz  = 0;

    if (!strstr(line, "A013_PATH_SUMMARY"))
        return;

    if (extract_u64(line, "call_rel", &v))
        a->call_rel = v;
    if (extract_u64(line, "call_abs_nonlocal", &v))
        a->call_abs_nonlocal = v;
    if (extract_u64(line, "call_abs_range", &v))
        a->call_abs_range = v;
    if (extract_u64(line, "jump_rel", &v))
        a->jump_rel = v;
    if (extract_u64(line, "jump_abs_nonlocal", &v))
        a->jump_abs_nonlocal = v;
    if (extract_u64(line, "jump_abs_range", &v))
        a->jump_abs_range = v;
    if (extract_u64(line, "cbnz_rel19", &v)) {
        a->cbnz_rel19 = v;
        saw_cbnz      = 1;
    }
    if (extract_u64(line, "cbnz_rel26", &v)) {
        a->cbnz_rel26 = v;
        saw_cbnz      = 1;
    }
    if (extract_u64(line, "cbnz_abs_nonlocal", &v)) {
        a->cbnz_abs_nonlocal = v;
        saw_cbnz             = 1;
    }
    if (extract_u64(line, "cbnz_abs_range", &v)) {
        a->cbnz_abs_range = v;
        saw_cbnz          = 1;
    }
    if (extract_u64(line, "cbnz_total", &v)) {
        a->cbnz_total = v;
        saw_cbnz      = 1;
    }
    if (extract_u64(line, "beq_rel19", &v)) {
        a->beq_rel19 = v;
        saw_beq      = 1;
    }
    if (extract_u64(line, "beq_rel26", &v)) {
        a->beq_rel26 = v;
        saw_beq      = 1;
    }
    if (extract_u64(line, "beq_abs_nonlocal", &v)) {
        a->beq_abs_nonlocal = v;
        saw_beq             = 1;
    }
    if (extract_u64(line, "beq_abs_range", &v)) {
        a->beq_abs_range = v;
        saw_beq          = 1;
    }
    if (extract_u64(line, "beq_total", &v)) {
        a->beq_total = v;
        saw_beq      = 1;
    }
    if (extract_u64(line, "bcond_rel19", &v)) {
        a->bcond_rel19 = v;
        saw_bcond      = 1;
    }
    if (extract_u64(line, "bcond_rel26", &v)) {
        a->bcond_rel26 = v;
        saw_bcond      = 1;
    }
    if (extract_u64(line, "bcond_total", &v)) {
        a->bcond_total = v;
        saw_bcond      = 1;
    }
    if (extract_u64(line, "tbxz_rel14", &v)) {
        a->tbxz_rel14 = v;
        saw_tbxz      = 1;
    }
    if (extract_u64(line, "tbxz_rel26", &v)) {
        a->tbxz_rel26 = v;
        saw_tbxz      = 1;
    }
    if (extract_u64(line, "tbxz_total", &v)) {
        a->tbxz_total = v;
        saw_tbxz      = 1;
    }
    if (extract_u64(line, "total", &v))
        a->total = v;
    if (saw_cbnz)
        a->has_cbnz_fields = 1;
    if (saw_beq)
        a->has_beq_fields = 1;
    if (saw_bcond)
        a->has_bcond_fields = 1;
    if (saw_tbxz)
        a->has_tbxz_fields = 1;
    a->saw = 1;
}

static void
update_summary_from_line(const char *line, summary_t *s)
{
    uint64_t v;

    if (!strstr(line, "DYNAREC_S03A_SUMMARY"))
        return;

    if (extract_u64(line, "exec_calls", &v))
        s->exec_calls = v;
    if (extract_u64(line, "valid_hits", &v))
        s->valid_hits = v;
    if (extract_u64(line, "dirty_conflicts", &v))
        s->dirty_conflicts = v;
    if (extract_u64(line, "dirty_list_hits", &v))
        s->dirty_list_hits = v;
    if (extract_u64(line, "promote_byte_mask", &v))
        s->promote_byte_mask = v;
    if (extract_u64(line, "promote_no_immediates", &v))
        s->promote_no_immediates = v;
    if (extract_u64(line, "defer_no_immediates", &v)) {
        s->defer_no_immediates = v;
        s->has_defer_field     = 1;
    }
    if (extract_u64(line, "retry_resets", &v)) {
        s->retry_resets          = v;
        s->has_retry_resets_field = 1;
    }
    if (extract_u64(line, "recompiled_execs", &v))
        s->recompiled_execs = v;
    if (extract_u64(line, "rebuild_paths", &v))
        s->rebuild_paths = v;

    s->saw_summary = 1;
}

static void
update_transition_from_line(const char *line, transitions_t *t, retries_hist_t *rh)
{
    const char *a;
    uint64_t    v;

    if (!strstr(line, "DYNAREC_S03A_TRANSITION"))
        return;

    t->total++;
    a = strstr(line, "action=");
    if (!a) {
        t->action_other++;
        return;
    }
    a += strlen("action=");

    if (!strncmp(a, "BYTE_MASK", strlen("BYTE_MASK")))
        t->action_byte_mask++;
    else if (!strncmp(a, "NO_IMMEDIATES", strlen("NO_IMMEDIATES")))
        t->action_no_immediates++;
    else if (!strncmp(a, "DEFER_NO_IMMEDIATES", strlen("DEFER_NO_IMMEDIATES")))
        t->action_defer_no_immediates++;
    else
        t->action_other++;

    if (extract_u64(line, "retries_before", &v))
        bucket_add(rh->before, v);
    if (extract_u64(line, "retries_after", &v))
        bucket_add(rh->after, v);
}

static int
parse_log(const char *path, summary_t *s, transitions_t *t, tags_t *tags, retries_hist_t *rh, a013_path_t *a013)
{
    char  line[8192];
    FILE *f = fopen(path, "r");

    if (!f) {
        fprintf(stderr, "error: cannot open log: %s\n", path);
        return 0;
    }

    memset(s, 0, sizeof(*s));
    memset(t, 0, sizeof(*t));
    memset(tags, 0, sizeof(*tags));
    memset(rh, 0, sizeof(*rh));
    memset(a013, 0, sizeof(*a013));

    while (fgets(line, sizeof(line), f)) {
        update_summary_from_line(line, s);
        update_transition_from_line(line, t, rh);
        update_tag_from_line(line, tags);
        update_a013_from_line(line, a013);
    }

    fclose(f);
    return 1;
}

static void
print_report(const char *label, const summary_t *s, const transitions_t *t)
{
    double noimm_ratio = 0.0;
    double defer_ratio = 0.0;

    if (s->dirty_list_hits) {
        noimm_ratio = (double) s->promote_no_immediates / (double) s->dirty_list_hits;
        if (s->has_defer_field)
            defer_ratio = (double) s->defer_no_immediates / (double) s->dirty_list_hits;
    }

    printf("S03_LOG %s\n", label);
    printf("  summary_seen=%s\n", s->saw_summary ? "yes" : "no");
    printf("  exec_calls=%" PRIu64 "\n", s->exec_calls);
    printf("  valid_hits=%" PRIu64 "\n", s->valid_hits);
    printf("  dirty_conflicts=%" PRIu64 "\n", s->dirty_conflicts);
    printf("  dirty_list_hits=%" PRIu64 "\n", s->dirty_list_hits);
    printf("  promote_byte_mask=%" PRIu64 "\n", s->promote_byte_mask);
    printf("  promote_no_immediates=%" PRIu64 "\n", s->promote_no_immediates);
    if (s->has_defer_field)
        printf("  defer_no_immediates=%" PRIu64 "\n", s->defer_no_immediates);
    if (s->has_retry_resets_field)
        printf("  retry_resets=%" PRIu64 "\n", s->retry_resets);
    printf("  recompiled_execs=%" PRIu64 "\n", s->recompiled_execs);
    printf("  rebuild_paths=%" PRIu64 "\n", s->rebuild_paths);
    printf("  transitions_total=%" PRIu64 "\n", t->total);
    printf("  transitions_action_byte_mask=%" PRIu64 "\n", t->action_byte_mask);
    printf("  transitions_action_no_immediates=%" PRIu64 "\n", t->action_no_immediates);
    printf("  transitions_action_defer_no_immediates=%" PRIu64 "\n", t->action_defer_no_immediates);
    printf("  transitions_action_other=%" PRIu64 "\n", t->action_other);
    printf("  ratio_promote_no_immediates_per_dirty_hit=%.6f\n", noimm_ratio);
    if (s->has_defer_field)
        printf("  ratio_defer_no_immediates_per_dirty_hit=%.6f\n", defer_ratio);
}

static void
print_tags(const char *label, const tags_t *tags)
{
    printf("S03_TAGS %s\n", label);
    printf("  total=%" PRIu64 "\n", tags->total);
    printf("  transition=%" PRIu64 "\n", tags->transition);
    printf("  periodic=%" PRIu64 "\n", tags->periodic);
    printf("  rebuild=%" PRIu64 "\n", tags->rebuild);
    printf("  new_block=%" PRIu64 "\n", tags->new_block);
    printf("  unexpected_noimm_without_bmask=%" PRIu64 "\n", tags->unexpected_noimm_without_bmask);
    printf("  other=%" PRIu64 "\n", tags->other);
}

static void
print_hist(const char *label, const retries_hist_t *rh)
{
    int i;

    printf("S03_RETRIES_HIST %s before\n", label);
    for (i = 0; i < RETRIES_BUCKETS; i++) {
        if (i == RETRIES_BUCKETS - 1)
            printf("  %dplus=%" PRIu64 "\n", i, rh->before[i]);
        else
            printf("  %d=%" PRIu64 "\n", i, rh->before[i]);
    }
    printf("S03_RETRIES_HIST %s after\n", label);
    for (i = 0; i < RETRIES_BUCKETS; i++) {
        if (i == RETRIES_BUCKETS - 1)
            printf("  %dplus=%" PRIu64 "\n", i, rh->after[i]);
        else
            printf("  %d=%" PRIu64 "\n", i, rh->after[i]);
    }
}

static void
print_a013(const char *label, const a013_path_t *a)
{
    double rel_ratio        = 0.0;
    double abs_ratio        = 0.0;
    double range_ratio      = 0.0;
    double cbnz_rel_ratio   = 0.0;
    double cbnz_abs_ratio   = 0.0;
    double cbnz_range_ratio = 0.0;
    double beq_rel_ratio    = 0.0;
    double beq_abs_ratio    = 0.0;
    double beq_range_ratio  = 0.0;
    double bcond_rel_ratio  = 0.0;
    double bcond_abs_ratio  = 0.0;
    double tbxz_rel_ratio   = 0.0;
    double tbxz_abs_ratio   = 0.0;
    uint64_t rel_total;
    uint64_t abs_total;
    uint64_t cbnz_rel_total;
    uint64_t beq_rel_total;
    uint64_t bcond_rel_total;
    uint64_t tbxz_rel_total;

    rel_total = a->call_rel + a->jump_rel;
    abs_total = a->call_abs_nonlocal + a->call_abs_range + a->jump_abs_nonlocal + a->jump_abs_range;
    cbnz_rel_total = a->cbnz_rel19 + a->cbnz_rel26;
    beq_rel_total  = a->beq_rel19 + a->beq_rel26;
    bcond_rel_total = a->bcond_rel19 + a->bcond_rel26;
    tbxz_rel_total  = a->tbxz_rel14 + a->tbxz_rel26;
    if (a->total) {
        rel_ratio   = (double) rel_total / (double) a->total;
        abs_ratio   = (double) abs_total / (double) a->total;
        range_ratio = (double) (a->call_abs_range + a->jump_abs_range) / (double) a->total;
    }
    if (a->cbnz_total) {
        cbnz_rel_ratio   = (double) cbnz_rel_total / (double) a->cbnz_total;
        cbnz_abs_ratio   = (double) (a->cbnz_abs_nonlocal + a->cbnz_abs_range) / (double) a->cbnz_total;
        cbnz_range_ratio = (double) a->cbnz_abs_range / (double) a->cbnz_total;
    }
    if (a->beq_total) {
        beq_rel_ratio   = (double) beq_rel_total / (double) a->beq_total;
        beq_abs_ratio   = (double) (a->beq_abs_nonlocal + a->beq_abs_range) / (double) a->beq_total;
        beq_range_ratio = (double) a->beq_abs_range / (double) a->beq_total;
    }
    if (a->bcond_total) {
        bcond_rel_ratio = (double) bcond_rel_total / (double) a->bcond_total;
        bcond_abs_ratio = 1.0 - bcond_rel_ratio;
    }
    if (a->tbxz_total) {
        tbxz_rel_ratio = (double) tbxz_rel_total / (double) a->tbxz_total;
        tbxz_abs_ratio = 1.0 - tbxz_rel_ratio;
    }

    printf("A013_PATH %s\n", label);
    printf("  seen=%s\n", a->saw ? "yes" : "no");
    printf("  call_rel=%" PRIu64 "\n", a->call_rel);
    printf("  call_abs_nonlocal=%" PRIu64 "\n", a->call_abs_nonlocal);
    printf("  call_abs_range=%" PRIu64 "\n", a->call_abs_range);
    printf("  jump_rel=%" PRIu64 "\n", a->jump_rel);
    printf("  jump_abs_nonlocal=%" PRIu64 "\n", a->jump_abs_nonlocal);
    printf("  jump_abs_range=%" PRIu64 "\n", a->jump_abs_range);
    printf("  total=%" PRIu64 "\n", a->total);
    printf("  cbnz_fields_seen=%s\n", a->has_cbnz_fields ? "yes" : "no");
    printf("  cbnz_rel19=%" PRIu64 "\n", a->cbnz_rel19);
    printf("  cbnz_rel26=%" PRIu64 "\n", a->cbnz_rel26);
    printf("  cbnz_abs_nonlocal=%" PRIu64 "\n", a->cbnz_abs_nonlocal);
    printf("  cbnz_abs_range=%" PRIu64 "\n", a->cbnz_abs_range);
    printf("  cbnz_total=%" PRIu64 "\n", a->cbnz_total);
    printf("  beq_fields_seen=%s\n", a->has_beq_fields ? "yes" : "no");
    printf("  beq_rel19=%" PRIu64 "\n", a->beq_rel19);
    printf("  beq_rel26=%" PRIu64 "\n", a->beq_rel26);
    printf("  beq_abs_nonlocal=%" PRIu64 "\n", a->beq_abs_nonlocal);
    printf("  beq_abs_range=%" PRIu64 "\n", a->beq_abs_range);
    printf("  beq_total=%" PRIu64 "\n", a->beq_total);
    printf("  bcond_fields_seen=%s\n", a->has_bcond_fields ? "yes" : "no");
    printf("  bcond_rel19=%" PRIu64 "\n", a->bcond_rel19);
    printf("  bcond_rel26=%" PRIu64 "\n", a->bcond_rel26);
    printf("  bcond_total=%" PRIu64 "\n", a->bcond_total);
    printf("  tbxz_fields_seen=%s\n", a->has_tbxz_fields ? "yes" : "no");
    printf("  tbxz_rel14=%" PRIu64 "\n", a->tbxz_rel14);
    printf("  tbxz_rel26=%" PRIu64 "\n", a->tbxz_rel26);
    printf("  tbxz_total=%" PRIu64 "\n", a->tbxz_total);
    printf("  ratio_relative_total=%.6f\n", rel_ratio);
    printf("  ratio_absolute_total=%.6f\n", abs_ratio);
    printf("  ratio_abs_range_total=%.6f\n", range_ratio);
    printf("  ratio_cbnz_relative_total=%.6f\n", cbnz_rel_ratio);
    printf("  ratio_cbnz_absolute_total=%.6f\n", cbnz_abs_ratio);
    printf("  ratio_cbnz_abs_range_total=%.6f\n", cbnz_range_ratio);
    printf("  ratio_beq_relative_total=%.6f\n", beq_rel_ratio);
    printf("  ratio_beq_absolute_total=%.6f\n", beq_abs_ratio);
    printf("  ratio_beq_abs_range_total=%.6f\n", beq_range_ratio);
    printf("  ratio_bcond_relative_total=%.6f\n", bcond_rel_ratio);
    printf("  ratio_bcond_absolute_total=%.6f\n", bcond_abs_ratio);
    printf("  ratio_tbxz_relative_total=%.6f\n", tbxz_rel_ratio);
    printf("  ratio_tbxz_absolute_total=%.6f\n", tbxz_abs_ratio);
}

static void
print_delta(const summary_t *base, const summary_t *cur, const transitions_t *base_t, const transitions_t *cur_t)
{
    double base_ratio = 0.0;
    double cur_ratio  = 0.0;

    if (base->dirty_list_hits)
        base_ratio = (double) base->promote_no_immediates / (double) base->dirty_list_hits;
    if (cur->dirty_list_hits)
        cur_ratio = (double) cur->promote_no_immediates / (double) cur->dirty_list_hits;

    printf("S03_DELTA baseline_to_current\n");
    printf("  dirty_list_hits_delta=%" PRId64 "\n", (int64_t) cur->dirty_list_hits - (int64_t) base->dirty_list_hits);
    printf("  promote_byte_mask_delta=%" PRId64 "\n", (int64_t) cur->promote_byte_mask - (int64_t) base->promote_byte_mask);
    printf("  promote_no_immediates_delta=%" PRId64 "\n", (int64_t) cur->promote_no_immediates - (int64_t) base->promote_no_immediates);
    if (cur->has_defer_field || base->has_defer_field)
        printf("  defer_no_immediates_delta=%" PRId64 "\n", (int64_t) cur->defer_no_immediates - (int64_t) base->defer_no_immediates);
    if (cur->has_retry_resets_field || base->has_retry_resets_field)
        printf("  retry_resets_delta=%" PRId64 "\n", (int64_t) cur->retry_resets - (int64_t) base->retry_resets);
    printf("  rebuild_paths_delta=%" PRId64 "\n", (int64_t) cur->rebuild_paths - (int64_t) base->rebuild_paths);
    printf("  transitions_total_delta=%" PRId64 "\n", (int64_t) cur_t->total - (int64_t) base_t->total);
    printf("  transitions_no_immediates_delta=%" PRId64 "\n", (int64_t) cur_t->action_no_immediates - (int64_t) base_t->action_no_immediates);
    printf("  ratio_promote_no_immediates_per_dirty_hit_delta=%.6f\n", cur_ratio - base_ratio);
}

static void
print_a013_delta(const a013_path_t *base, const a013_path_t *cur)
{
    uint64_t base_rel, cur_rel;
    double   base_rel_ratio      = 0.0;
    double   cur_rel_ratio       = 0.0;
    double   base_cbnz_rel_ratio = 0.0;
    double   cur_cbnz_rel_ratio  = 0.0;
    double   base_beq_rel_ratio  = 0.0;
    double   cur_beq_rel_ratio   = 0.0;
    double   base_bcond_rel_ratio = 0.0;
    double   cur_bcond_rel_ratio  = 0.0;
    double   base_tbxz_rel_ratio = 0.0;
    double   cur_tbxz_rel_ratio  = 0.0;

    base_rel = base->call_rel + base->jump_rel;
    cur_rel  = cur->call_rel + cur->jump_rel;
    if (base->total)
        base_rel_ratio = (double) base_rel / (double) base->total;
    if (cur->total)
        cur_rel_ratio = (double) cur_rel / (double) cur->total;
    if (base->cbnz_total)
        base_cbnz_rel_ratio = (double) (base->cbnz_rel19 + base->cbnz_rel26) / (double) base->cbnz_total;
    if (cur->cbnz_total)
        cur_cbnz_rel_ratio = (double) (cur->cbnz_rel19 + cur->cbnz_rel26) / (double) cur->cbnz_total;
    if (base->beq_total)
        base_beq_rel_ratio = (double) (base->beq_rel19 + base->beq_rel26) / (double) base->beq_total;
    if (cur->beq_total)
        cur_beq_rel_ratio = (double) (cur->beq_rel19 + cur->beq_rel26) / (double) cur->beq_total;
    if (base->bcond_total)
        base_bcond_rel_ratio = (double) (base->bcond_rel19 + base->bcond_rel26) / (double) base->bcond_total;
    if (cur->bcond_total)
        cur_bcond_rel_ratio = (double) (cur->bcond_rel19 + cur->bcond_rel26) / (double) cur->bcond_total;
    if (base->tbxz_total)
        base_tbxz_rel_ratio = (double) (base->tbxz_rel14 + base->tbxz_rel26) / (double) base->tbxz_total;
    if (cur->tbxz_total)
        cur_tbxz_rel_ratio = (double) (cur->tbxz_rel14 + cur->tbxz_rel26) / (double) cur->tbxz_total;

    printf("A013_DELTA baseline_to_current\n");
    printf("  call_rel_delta=%" PRId64 "\n", (int64_t) cur->call_rel - (int64_t) base->call_rel);
    printf("  jump_rel_delta=%" PRId64 "\n", (int64_t) cur->jump_rel - (int64_t) base->jump_rel);
    printf("  call_abs_nonlocal_delta=%" PRId64 "\n", (int64_t) cur->call_abs_nonlocal - (int64_t) base->call_abs_nonlocal);
    printf("  jump_abs_nonlocal_delta=%" PRId64 "\n", (int64_t) cur->jump_abs_nonlocal - (int64_t) base->jump_abs_nonlocal);
    printf("  call_abs_range_delta=%" PRId64 "\n", (int64_t) cur->call_abs_range - (int64_t) base->call_abs_range);
    printf("  jump_abs_range_delta=%" PRId64 "\n", (int64_t) cur->jump_abs_range - (int64_t) base->jump_abs_range);
    printf("  ratio_relative_total_delta=%.6f\n", cur_rel_ratio - base_rel_ratio);
    if (cur->has_cbnz_fields || base->has_cbnz_fields) {
        printf("  cbnz_rel19_delta=%" PRId64 "\n", (int64_t) cur->cbnz_rel19 - (int64_t) base->cbnz_rel19);
        printf("  cbnz_rel26_delta=%" PRId64 "\n", (int64_t) cur->cbnz_rel26 - (int64_t) base->cbnz_rel26);
        printf("  cbnz_abs_nonlocal_delta=%" PRId64 "\n", (int64_t) cur->cbnz_abs_nonlocal - (int64_t) base->cbnz_abs_nonlocal);
        printf("  cbnz_abs_range_delta=%" PRId64 "\n", (int64_t) cur->cbnz_abs_range - (int64_t) base->cbnz_abs_range);
        printf("  ratio_cbnz_relative_total_delta=%.6f\n", cur_cbnz_rel_ratio - base_cbnz_rel_ratio);
    }
    if (cur->has_beq_fields || base->has_beq_fields) {
        printf("  beq_rel19_delta=%" PRId64 "\n", (int64_t) cur->beq_rel19 - (int64_t) base->beq_rel19);
        printf("  beq_rel26_delta=%" PRId64 "\n", (int64_t) cur->beq_rel26 - (int64_t) base->beq_rel26);
        printf("  beq_abs_nonlocal_delta=%" PRId64 "\n", (int64_t) cur->beq_abs_nonlocal - (int64_t) base->beq_abs_nonlocal);
        printf("  beq_abs_range_delta=%" PRId64 "\n", (int64_t) cur->beq_abs_range - (int64_t) base->beq_abs_range);
        printf("  ratio_beq_relative_total_delta=%.6f\n", cur_beq_rel_ratio - base_beq_rel_ratio);
    }
    if (cur->has_bcond_fields || base->has_bcond_fields) {
        printf("  bcond_rel19_delta=%" PRId64 "\n", (int64_t) cur->bcond_rel19 - (int64_t) base->bcond_rel19);
        printf("  bcond_rel26_delta=%" PRId64 "\n", (int64_t) cur->bcond_rel26 - (int64_t) base->bcond_rel26);
        printf("  ratio_bcond_relative_total_delta=%.6f\n", cur_bcond_rel_ratio - base_bcond_rel_ratio);
    }
    if (cur->has_tbxz_fields || base->has_tbxz_fields) {
        printf("  tbxz_rel14_delta=%" PRId64 "\n", (int64_t) cur->tbxz_rel14 - (int64_t) base->tbxz_rel14);
        printf("  tbxz_rel26_delta=%" PRId64 "\n", (int64_t) cur->tbxz_rel26 - (int64_t) base->tbxz_rel26);
        printf("  ratio_tbxz_relative_total_delta=%.6f\n", cur_tbxz_rel_ratio - base_tbxz_rel_ratio);
    }
}

int
main(int argc, char **argv)
{
    summary_t     cur_s, base_s;
    transitions_t cur_t, base_t;
    tags_t        cur_tags, base_tags;
    retries_hist_t cur_rh, base_rh;
    a013_path_t   cur_a013, base_a013;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s <current-log> [baseline-log]\n", argv[0]);
        return 2;
    }

    if (!parse_log(argv[1], &cur_s, &cur_t, &cur_tags, &cur_rh, &cur_a013))
        return 1;

    print_report("current", &cur_s, &cur_t);
    print_tags("current", &cur_tags);
    print_hist("current", &cur_rh);
    print_a013("current", &cur_a013);

    if (argc == 3) {
        if (!parse_log(argv[2], &base_s, &base_t, &base_tags, &base_rh, &base_a013))
            return 1;
        print_report("baseline", &base_s, &base_t);
        print_tags("baseline", &base_tags);
        print_hist("baseline", &base_rh);
        print_a013("baseline", &base_a013);
        print_delta(&base_s, &cur_s, &base_t, &cur_t);
        print_a013_delta(&base_a013, &cur_a013);
    }

    return 0;
}
