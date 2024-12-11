#include "common.hpp"

class offset_handler {
private:
    int max_num;
    int step_size;
    int buf_offset;
    size_t cur;

public:
    offset_handler() {
        cur = 0;
    }

    offset_handler(int max_num, int step_size, int buf_offset):max_num(max_num), step_size(step_size), buf_offset(buf_offset) {
        cur = 0;
    }
    void init(int max_num, int step_size, int buf_offset) {
        cur = 0;
        this->max_num = max_num;
        this->step_size = step_size;
        this->buf_offset = buf_offset;
    }
    size_t step() {
        size_t ret = offset();
        cur += 1;
        return ret;
    }
    size_t step(int step) {
        size_t ret = offset();
        cur += step;
        return ret;
    }
    size_t offset() {
        return (cur % max_num) * step_size + buf_offset;
    }
    size_t index() {
        return cur;
    }
    int index_mod() {
        return cur % max_num;
    }
};