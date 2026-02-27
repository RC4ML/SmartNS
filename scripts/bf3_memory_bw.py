import time


BFPERF = "hwmon4"
SAMPLE_INTERVAL_SEC = 0.5


def read_hex_value(filename):
    with open(filename, "r", encoding="utf-8") as file:
        hex_value = file.read().strip()
        return int(hex_value, 16)


def counter_diff_to_mb_per_sec(old_value, new_value, elapsed_sec):
    if elapsed_sec <= 0:
        return 0.0
    diff_mb = (new_value - old_value) * 64.0 / (1024.0 * 1024.0)
    return diff_mb / elapsed_sec


def print_bandwidth(read_bw_mb_s, write_bw_mb_s):
    print(f"Read: {read_bw_mb_s:.2f} MB/s   Write: {write_bw_mb_s:.2f} MB/s")


read_file1 = f"/sys/class/hwmon/{BFPERF}/mss0/counter0"
read_file2 = f"/sys/class/hwmon/{BFPERF}/mss1/counter0"
write_file1 = f"/sys/class/hwmon/{BFPERF}/mss0/counter1"
write_file2 = f"/sys/class/hwmon/{BFPERF}/mss1/counter1"

old_read = read_hex_value(read_file1) + read_hex_value(read_file2)
old_write = read_hex_value(write_file1) + read_hex_value(write_file2)
last_ts = time.monotonic()

while True:
    time.sleep(SAMPLE_INTERVAL_SEC)
    now_ts = time.monotonic()
    elapsed = now_ts - last_ts

    new_read = read_hex_value(read_file1) + read_hex_value(read_file2)
    new_write = read_hex_value(write_file1) + read_hex_value(write_file2)

    read_bw = counter_diff_to_mb_per_sec(old_read, new_read, elapsed)
    write_bw = counter_diff_to_mb_per_sec(old_write, new_write, elapsed)
    print_bandwidth(read_bw, write_bw)

    old_read = new_read
    old_write = new_write
    last_ts = now_ts
