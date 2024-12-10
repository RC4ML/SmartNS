#include "hdr_histogram.h"

namespace SmartNS {
    Histogram::Histogram(int64_t lowest, int64_t highest, int sigfigs, double scale) {
        hdr_init(lowest, highest, sigfigs, &latency_hist);
        scale_value = scale;
    }

    Histogram::Histogram(int64_t lowest, int64_t highest, double scale) {
        hdr_init(lowest, highest, 3, &latency_hist);
        scale_value = scale;
    }


    Histogram::~Histogram() {
        hdr_close(latency_hist);
    }

    void Histogram::reset() {
        hdr_reset(latency_hist);
    }

    void Histogram::record(int64_t value, int64_t count) {
        hdr_record_values(latency_hist, value * scale_value, count);
    }

    void Histogram::record_atomic(int64_t value, int64_t count) {
        hdr_record_values_atomic(latency_hist, value * scale_value, count);
    }

    double Histogram::get_mean() {
        return hdr_mean(latency_hist) / scale_value;
    }

    double Histogram::get_value_at_percentile(double percentile) {
        return hdr_value_at_percentile(latency_hist, percentile) / scale_value;
    }

    void Histogram::print(FILE *stream, int32_t ticks) {
        hdr_percentiles_print(latency_hist, stream, ticks, scale_value, CLASSIC);
    }

    void Histogram::print_csv(FILE *stream, int32_t ticks) {
        hdr_percentiles_print(latency_hist, stream, ticks, scale_value, CSV);
    }

}