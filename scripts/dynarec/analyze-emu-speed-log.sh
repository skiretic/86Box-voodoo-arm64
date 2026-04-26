#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "" ]; then
    echo "usage: $0 <86box.log>"
    exit 1
fi

LOGFILE="$1"
if [ ! -f "${LOGFILE}" ]; then
    echo "error: logfile not found: ${LOGFILE}"
    exit 1
fi

awk '
function phase_name_for_seq(seq) {
    if (seq <= 0) return "q3";
    if (seq == 1) return "3dmark99";
    if (seq == 2) return "wl05";
    if (seq == 3) return "post_wl05";
    return "post_wl05";
}

function add_sample(phase, p, idx, key) {
    n++;
    all_vals[n] = p;
    sum += p;
    if ((n == 1) || (p < min)) min = p;
    if ((n == 1) || (p > max)) max = p;
    if (p < 100) dips100++;
    if (p < 95) dips95++;
    if (p < 90) dips90++;

    phase_n[phase]++;
    idx = phase_n[phase];
    key = phase SUBSEP idx;
    phase_vals[key] = p;
    phase_sum[phase] += p;
    if (!(phase in phase_min) || (p < phase_min[phase])) phase_min[phase] = p;
    if (!(phase in phase_max) || (p > phase_max[phase])) phase_max[phase] = p;
    if (p < 100) phase_dips100[phase]++;
    if (p < 95) phase_dips95[phase]++;
    if (p < 90) phase_dips90[phase]++;
}

function get_sample(mode, phase, idx, key) {
    if (mode == "all")
        return all_vals[idx];
    key = phase SUBSEP idx;
    return phase_vals[key];
}

function percentile(mode, phase, count, pct, i, j, tmp, swap, target, value) {
    if (count <= 0)
        return 0;
    for (i = 1; i <= count; i++)
        tmp[i] = get_sample(mode, phase, i);
    for (i = 1; i <= count; i++) {
        for (j = i + 1; j <= count; j++) {
            if (tmp[j] < tmp[i]) {
                swap = tmp[i];
                tmp[i] = tmp[j];
                tmp[j] = swap;
            }
        }
    }
    target = int(((pct / 100.0) * count) + 0.999999);
    if (target < 1) target = 1;
    if (target > count) target = count;
    value = tmp[target];
    delete tmp;
    return value;
}

function print_phase_summary(phase, count, avg, p50, p95, p99) {
    if (count <= 0)
        return;
    avg = phase_sum[phase] / count;
    p50 = percentile("phase", phase, count, 50);
    p95 = percentile("phase", phase, count, 95);
    p99 = percentile("phase", phase, count, 99);
    printf("EMU_PHASE_SPEED_SUMMARY phase=%s samples=%d avg=%.3f p50=%d p95=%d p99=%d min=%d max=%d dips_lt100=%d dips_lt95=%d dips_lt90=%d\n",
           phase, count, avg, p50, p95, p99,
           phase_min[phase], phase_max[phase],
           phase_dips100[phase] + 0, phase_dips95[phase] + 0, phase_dips90[phase] + 0);
}

BEGIN {
    n = 0;
    sum = 0.0;
    min = 0;
    max = 0;
    dips100 = 0;
    dips95 = 0;
    dips90 = 0;

    marker_total = 0;
    marker_start_seen = 0;
    marker_max_seq = 0;
    marker_current_seq = 0;
}

/PERF_PHASE_MARK/ {
    if (match($0, /seq=[0-9]+/)) {
        seq = substr($0, RSTART + 4, RLENGTH - 4) + 0;
        marker_total++;
        marker_current_seq = seq;
        if (seq == 0)
            marker_start_seen = 1;
        if (seq > marker_max_seq)
            marker_max_seq = seq;
    }
    next;
}

/EMU_SPEED_SAMPLE/ {
    if (match($0, /percent=[0-9]+/)) {
        p = substr($0, RSTART + 8, RLENGTH - 8) + 0;
        add_sample(phase_name_for_seq(marker_current_seq), p);
    }
}

END {
    if (n == 0) {
        print "EMU_SPEED_SUMMARY samples=0";
        printf("EMU_PHASE_MARKERS markers_total=%d start_seen=%d max_seq=%d manual_presses=%d expected_manual_presses=3 valid_for_q3_3dmark_wl05=%d\n",
               marker_total, marker_start_seen, marker_max_seq, marker_max_seq,
               (marker_start_seen && (marker_max_seq >= 3)) ? 1 : 0);
        exit 0;
    }

    avg = sum / n;
    p50 = percentile("all", "", n, 50);
    p95 = percentile("all", "", n, 95);
    p99 = percentile("all", "", n, 99);
    printf("EMU_SPEED_SUMMARY samples=%d avg=%.3f p50=%d p95=%d p99=%d min=%d max=%d dips_lt100=%d dips_lt95=%d dips_lt90=%d\n",
           n, avg, p50, p95, p99, min, max, dips100, dips95, dips90);

    printf("EMU_PHASE_MARKERS markers_total=%d start_seen=%d max_seq=%d manual_presses=%d expected_manual_presses=3 valid_for_q3_3dmark_wl05=%d\n",
           marker_total, marker_start_seen, marker_max_seq, marker_max_seq,
           (marker_start_seen && (marker_max_seq >= 3)) ? 1 : 0);

    print_phase_summary("q3", phase_n["q3"] + 0);
    print_phase_summary("3dmark99", phase_n["3dmark99"] + 0);
    print_phase_summary("wl05", phase_n["wl05"] + 0);
    print_phase_summary("post_wl05", phase_n["post_wl05"] + 0);
}
' "${LOGFILE}"
