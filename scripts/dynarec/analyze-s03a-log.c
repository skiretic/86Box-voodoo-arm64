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
    uint64_t recompiled_execs;
    uint64_t rebuild_paths;
    int      has_defer_field;
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

static int
is_key_boundary(const char c)
{
    return c == '\0' || isspace((unsigned char) c);
}

static int
extract_u64(const char *line, const char *key, uint64_t *out)
{
    char   needle[96];
    char  *end_ptr = NULL;
    char  *match   = NULL;
    size_t nlen;

    snprintf(needle, sizeof(needle), "%s=", key);
    nlen  = strlen(needle);
    match = strstr((char *) line, needle);
    if (!match)
        return 0;
    if (match != line && !is_key_boundary(*(match - 1)))
        return 0;

    match += nlen;
    *out = strtoull(match, &end_ptr, 10);
    if (end_ptr == match)
        return 0;
    return 1;
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
parse_log(const char *path, summary_t *s, transitions_t *t, tags_t *tags, retries_hist_t *rh)
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

    while (fgets(line, sizeof(line), f)) {
        update_summary_from_line(line, s);
        update_transition_from_line(line, t, rh);
        update_tag_from_line(line, tags);
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
    printf("  rebuild_paths_delta=%" PRId64 "\n", (int64_t) cur->rebuild_paths - (int64_t) base->rebuild_paths);
    printf("  transitions_total_delta=%" PRId64 "\n", (int64_t) cur_t->total - (int64_t) base_t->total);
    printf("  transitions_no_immediates_delta=%" PRId64 "\n", (int64_t) cur_t->action_no_immediates - (int64_t) base_t->action_no_immediates);
    printf("  ratio_promote_no_immediates_per_dirty_hit_delta=%.6f\n", cur_ratio - base_ratio);
}

int
main(int argc, char **argv)
{
    summary_t     cur_s, base_s;
    transitions_t cur_t, base_t;
    tags_t        cur_tags, base_tags;
    retries_hist_t cur_rh, base_rh;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s <current-log> [baseline-log]\n", argv[0]);
        return 2;
    }

    if (!parse_log(argv[1], &cur_s, &cur_t, &cur_tags, &cur_rh))
        return 1;

    print_report("current", &cur_s, &cur_t);
    print_tags("current", &cur_tags);
    print_hist("current", &cur_rh);

    if (argc == 3) {
        if (!parse_log(argv[2], &base_s, &base_t, &base_tags, &base_rh))
            return 1;
        print_report("baseline", &base_s, &base_t);
        print_tags("baseline", &base_tags);
        print_hist("baseline", &base_rh);
        print_delta(&base_s, &cur_s, &base_t, &cur_t);
    }

    return 0;
}
