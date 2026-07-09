// Header-only copy of the paper/harness workload generator for perf binaries.
//
// This mirrors benchmark/workload/generator.cpp closely enough that
// `perf/babo_perf` and `perf/liqui_perf` exercise the same market-shape model:
// NVDA-calibrated GBM mid-price, power-law depth with near-touch hump,
// IOC/modify/cancel lifecycle, shuffled arrival order, duplicate stale requests,
// and deterministic portable RNG on top of std::mt19937.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace bench {

enum class Op : std::uint8_t { Add, Modify, Cancel };

struct Msg {
    Op            op;
    std::uint32_t id;
    bool          is_buy;
    std::uint32_t price;   // ticks
    std::uint32_t qty;
    bool          ioc;     // Add only
};

namespace detail {

inline constexpr double TICK_SIZE      = 0.005;
inline constexpr double START_MID      = 167.52;
inline constexpr double ALPHA          = 2.23;
inline constexpr std::size_t MAX_LEVEL = 799;
inline constexpr double HUMP_AMP       = 5.0;
inline constexpr double HUMP_CENTER    = 8.0;
inline constexpr double CANCEL_PCT     = 0.95;
inline constexpr double MODIFY_PCT     = 0.20;
inline constexpr double DUP_CANCEL_PCT = 0.02;
inline constexpr double DUP_MODIFY_PCT = 0.02;
inline constexpr double IOC_PCT        = 0.15;
inline constexpr double MEDIAN_LIFE_MS = 0.431;

struct Rng {
    std::mt19937 mt;

    explicit Rng(std::uint32_t seed) : mt(seed) {}

    // Portable uniform [0,1), consuming exactly the same mt19937 words on every STL.
    double uniform() {
        const std::uint64_t hi = mt() >> 5; // 27 bits
        const std::uint64_t lo = mt() >> 6; // 26 bits
        return (hi * 67108864.0 + lo) * (1.0 / 9007199254740992.0);
    }

    std::uint32_t uniform_int(std::uint32_t lo, std::uint32_t hi) {
        return lo + static_cast<std::uint32_t>(uniform() * (hi - lo + 1));
    }

    double normal() {
        double u1 = uniform();
        const double u2 = uniform();
        if (u1 < 1e-300) u1 = 1e-300;
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);
    }

    double exponential(double lambda) {
        double u = uniform();
        if (u < 1e-300) u = 1e-300;
        return -std::log(u) / lambda;
    }
};

struct LevelSampler {
    std::vector<double> cdf;

    LevelSampler() {
        cdf.resize(MAX_LEVEL + 1);
        double acc = 0.0;
        for (std::size_t k = 0; k <= MAX_LEVEL; ++k) {
            const double base = 1.0 / std::pow(k + 1.0, ALPHA);
            const double z = static_cast<double>(k) - HUMP_CENTER;
            const double bump = HUMP_AMP * std::exp(-0.5 * z * z);
            acc += base + bump;
            cdf[k] = acc;
        }
    }

    int sample(Rng& rng) const {
        const double x = rng.uniform() * cdf.back();
        const auto it = std::lower_bound(cdf.begin(), cdf.end(), x);
        const int k = static_cast<int>(it - cdf.begin());
        return k > static_cast<int>(MAX_LEVEL) ? static_cast<int>(MAX_LEVEL) : k;
    }
};

inline std::int64_t to_ticks(double price) {
    return static_cast<std::int64_t>(std::llround(price / TICK_SIZE));
}

} // namespace detail

struct Scenario {
    const char* name;
    double      volatility;
    double      target_swing;
};

inline constexpr Scenario kScenarios[] = {
    {"static",         0.00, 0.00},
    {"normal",         0.15, 0.02},
    {"swing25",        0.50, 0.25},
    {"flash_crash_40", 0.50, 0.40},
    {"flash_crash_60", 0.50, 0.60},
};

struct WorkloadParams {
    std::uint32_t num_new = 1'000'000;
    std::uint32_t seed = 23;        // canonical benchmark generator seed
    double volatility = 0.15;
    double target_swing = 0.02;
};

class WorkloadGen {
public:
    explicit WorkloadGen(WorkloadParams p) : p_(p) {}

    std::vector<Msg> generate() const {
        using namespace detail;

        Rng rng(p_.seed);
        LevelSampler levels;

        const double dt = (p_.volatility > 0.0)
            ? std::pow(p_.target_swing / p_.volatility, 2.0) / static_cast<double>(p_.num_new)
            : 0.0;
        const double sigma_step = p_.volatility * std::sqrt(dt);

        struct NewOrder {
            bool is_buy;
            std::uint32_t price_ticks;
            std::uint32_t qty;
            int level;
        };

        std::vector<NewOrder> news;
        news.reserve(p_.num_new);

        double mid = START_MID;
        for (std::uint32_t i = 0; i < p_.num_new; ++i) {
            mid *= std::exp(-0.5 * sigma_step * sigma_step + sigma_step * rng.normal());
            const auto mid_ticks = static_cast<std::int64_t>(
                to_ticks(std::round(mid / TICK_SIZE) * TICK_SIZE));

            const int level = levels.sample(rng);
            const bool is_buy = i < p_.num_new / 2;
            std::int64_t px = is_buy ? mid_ticks - (level + 1) : mid_ticks + (level + 1);
            if (px < 1) px = 1;

            news.push_back(NewOrder{
                is_buy,
                static_cast<std::uint32_t>(px),
                rng.uniform_int(1, 100),
                level
            });
        }

        std::vector<std::size_t> order(p_.num_new);
        std::iota(order.begin(), order.end(), 0);
        for (std::size_t i = p_.num_new - 1; i > 0; --i) {
            const auto j = static_cast<std::size_t>(rng.uniform_int(0, static_cast<std::uint32_t>(i)));
            std::swap(order[i], order[j]);
        }

        struct TimedMsg {
            std::int64_t ts;
            Msg msg;
        };

        std::vector<TimedMsg> timeline;
        timeline.reserve(static_cast<std::size_t>(p_.num_new * 2.25));

        const double base_lambda = std::log(2.0) / (MEDIAN_LIFE_MS * 1000.0);
        std::int64_t now = 0;

        for (std::uint32_t k = 0; k < p_.num_new; ++k) {
            const std::size_t source_index = order[k];
            const NewOrder& n = news[source_index];
            const std::uint32_t id = static_cast<std::uint32_t>(source_index + 1);

            const bool is_ioc = rng.uniform() < IOC_PCT;
            timeline.push_back({now, Msg{Op::Add, id, n.is_buy, n.price_ticks, n.qty, is_ioc}});
            if (is_ioc) {
                now += 1;
                continue;
            }

            bool modified = false;
            std::uint32_t mprice = n.price_ticks;
            const std::uint32_t mqty = n.qty + 1;
            if (rng.uniform() < MODIFY_PCT) {
                const std::int64_t tmod = now + 30 + static_cast<std::int64_t>(rng.uniform() * 70);
                if (rng.uniform() < 0.80) {
                    if (n.is_buy) {
                        mprice += 1;
                    } else if (mprice > 1) {
                        mprice -= 1;
                    }
                }
                timeline.push_back({tmod, Msg{Op::Modify, id, n.is_buy, mprice, mqty, false}});
                now = tmod;
                modified = true;
            }

            bool cancelled = false;
            if (rng.uniform() < CANCEL_PCT) {
                const double lambda = base_lambda / std::sqrt(1.0 + n.level);
                const auto ttl = static_cast<std::int64_t>(rng.exponential(lambda));
                timeline.push_back({now + ttl, Msg{Op::Cancel, id, n.is_buy, n.price_ticks, n.qty, false}});
                now += ttl;
                cancelled = true;

                if (rng.uniform() < DUP_CANCEL_PCT) {
                    const auto gap = 1 + static_cast<std::int64_t>(rng.uniform() * 20);
                    timeline.push_back({now + gap, Msg{Op::Cancel, id, n.is_buy, n.price_ticks, n.qty, false}});
                    now += gap;
                }
            }

            if (modified && cancelled && rng.uniform() < DUP_MODIFY_PCT) {
                const auto gap = 1 + static_cast<std::int64_t>(rng.uniform() * 20);
                timeline.push_back({now + gap, Msg{Op::Modify, id, n.is_buy, mprice, mqty, false}});
                now += gap;
            }

            now += 1;
        }

        std::stable_sort(timeline.begin(), timeline.end(),
            [](const TimedMsg& a, const TimedMsg& b) { return a.ts < b.ts; });

        std::vector<Msg> out;
        out.reserve(timeline.size());
        for (const TimedMsg& timed : timeline) out.push_back(timed.msg);
        return out;
    }

private:
    WorkloadParams p_;
};

} // namespace bench
