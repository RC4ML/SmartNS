import time


BFPERF = "hwmon4"
CHANNELS = 8


def read_hex_value(filename):
    with open(filename, "r", encoding="utf-8") as file:
        hex_value = file.read().strip()
        return int(hex_value, 16)


def calculate_hex_diff(new_value, old_value):
    return round((new_value - old_value) * 64 / (1024 * 1024), 1)


def print_decimal_value(desc, values):
    print(f"{desc} {values} MB")


def read_channel_counters(idx):
    cache_read_file1 = f"/sys/class/hwmon/{BFPERF}/llt{idx}/counter0"
    cache_read_file2 = f"/sys/class/hwmon/{BFPERF}/llt{idx}/counter1"
    cache_write_file1 = f"/sys/class/hwmon/{BFPERF}/llt{idx}/counter2"
    cache_write_file2 = f"/sys/class/hwmon/{BFPERF}/llt{idx}/counter3"
    cache_miss_read_file1 = f"/sys/class/hwmon/{BFPERF}/llt_miss{idx}/counter0"
    cache_miss_write_file1 = f"/sys/class/hwmon/{BFPERF}/llt_miss{idx}/counter1"

    cache_read = read_hex_value(cache_read_file1) + read_hex_value(cache_read_file2)
    cache_write = read_hex_value(cache_write_file1) + read_hex_value(cache_write_file2)
    cache_miss_read = read_hex_value(cache_miss_read_file1)
    cache_miss_write = read_hex_value(cache_miss_write_file1)
    return cache_read, cache_write, cache_miss_read, cache_miss_write


old_cache_read_all = [0] * CHANNELS
old_cache_write_all = [0] * CHANNELS
old_cache_miss_read_all = [0] * CHANNELS
old_cache_miss_write_all = [0] * CHANNELS

diff_cache_read_all = [0] * CHANNELS
diff_cache_write_all = [0] * CHANNELS
diff_cache_miss_read_all = [0] * CHANNELS
diff_cache_miss_write_all = [0] * CHANNELS

# Initialize with current counter values to avoid a very large first sample.
for i in range(CHANNELS):
    (
        old_cache_read_all[i],
        old_cache_write_all[i],
        old_cache_miss_read_all[i],
        old_cache_miss_write_all[i],
    ) = read_channel_counters(i)

while True:
    for i in range(CHANNELS):
        new_cache_read, new_cache_write, new_cache_miss_read, new_cache_miss_write = read_channel_counters(i)
        diff_cache_read_all[i] = calculate_hex_diff(new_cache_read, old_cache_read_all[i])
        diff_cache_write_all[i] = calculate_hex_diff(new_cache_write, old_cache_write_all[i])
        diff_cache_miss_read_all[i] = calculate_hex_diff(new_cache_miss_read, old_cache_miss_read_all[i])
        diff_cache_miss_write_all[i] = calculate_hex_diff(new_cache_miss_write, old_cache_miss_write_all[i])

        old_cache_read_all[i] = new_cache_read
        old_cache_write_all[i] = new_cache_write
        old_cache_miss_read_all[i] = new_cache_miss_read
        old_cache_miss_write_all[i] = new_cache_miss_write

    print_decimal_value("Cache Read", diff_cache_read_all)
    print_decimal_value("Cache Write", diff_cache_write_all)
    print_decimal_value("Read Miss", diff_cache_miss_read_all)
    print_decimal_value("Write Miss", diff_cache_miss_write_all)
    print("")
    print(f"Cache Read: {sum(diff_cache_read_all):.2f} MB   Write: {sum(diff_cache_write_all):.2f} MB")
    print(f"Miss Read: {sum(diff_cache_miss_read_all):.2f} MB   Write: {sum(diff_cache_miss_write_all):.2f} MB")
    print("---------------")
    time.sleep(1)
