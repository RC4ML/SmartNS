#include <hdr/hdr_histogram.h>

namespace SmartNS {
    class Histogram {
    public:
        explicit Histogram(int64_t lowest, int64_t highest, int sigfigs, double scale = 1);

        explicit Histogram(int64_t lowest, int64_t highest, double scale = 1);

        ~Histogram();

        void reset();

        void record(int64_t value, int64_t count = 1);

        void record_atomic(int64_t value, int64_t count = 1);

        double get_mean();

        double get_value_at_percentile(double percentile);

        void print(FILE *stream, int32_t ticks);

        void print_csv(FILE *stream, int32_t ticks);
    private:
        hdr_histogram *latency_hist;
        double scale_value{ 1 };
    };
}
