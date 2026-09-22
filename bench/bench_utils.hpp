#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

struct RunStats {
    double mean;
    double stddev;
    double median;
};

// Run fn() `reps` times and return summary statistics.
// Use median for reporting; stddev shows spread.
inline RunStats run_stats(std::function<double()> fn, int reps = 5) {
    std::vector<double> s;
    s.reserve(reps);
    for (int i = 0; i < reps; ++i) s.push_back(fn());

    std::sort(s.begin(), s.end());

    double sum = 0;
    for (double v : s) sum += v;
    double mean = sum / reps;

    double sq = 0;
    for (double v : s) sq += (v - mean) * (v - mean);

    return { mean, std::sqrt(sq / reps), s[reps / 2] };
}
