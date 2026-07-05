#pragma once

#include <rigtorp/SPSCQueue.h>
#include "matching_engine_api.h"   // me_report_t

class ReportQueue {
    rigtorp::SPSCQueue<me_report_t> q_;

public:
    explicit ReportQueue(size_t capacity) : q_(capacity) {}

    // producer: non-blocking enqueue -> your transport's push(bool)
    bool push(const me_report_t& r) { return q_.try_push(r); }

    // consumer: drain up to `max` into `out` -> your transport's pop(count)
    size_t pop(me_report_t* out, size_t max) {
        size_t n = 0;
        while (n < max) {
            me_report_t* f = q_.front();
            if (!f) break;
            out[n++] = *f;
            q_.pop();
        }
        return n;
    }
};