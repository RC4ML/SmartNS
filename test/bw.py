import time

def read_hex_value(filename):
    with open(filename, 'r') as file:
        return int(file.read().strip(), 16)

def calculate_hex_diff(old_value, new_value, max_value=0xFFFFFFFF):
    if new_value < old_value:  # Handle overflow
        new_value += max_value + 1
    return (new_value - old_value) * 64 / (1024 * 1024 * 1024)  # Convert to GB/s

def print_table(seconds, cache_bw, memory_bw):
    print(f"{seconds:<10} {cache_bw:<20} {memory_bw:<20}")

# Initial values
old_cache_read_all = [0] * 8
old_cache_write_all = [0] * 8
old_value1 = 0
old_value2 = 0

bfperf = "hwmon4"

# File paths for memory bandwidth
read_file1 = f'/sys/class/hwmon/{bfperf}/mss0/counter0'
read_file2 = f'/sys/class/hwmon/{bfperf}/mss1/counter0'
write_file1 = f'/sys/class/hwmon/{bfperf}/mss0/counter1'
write_file2 = f'/sys/class/hwmon/{bfperf}/mss1/counter1'

# Initialize old values for memory bandwidth
old_value1 = read_hex_value(read_file1) + read_hex_value(read_file2)
old_value2 = read_hex_value(write_file1) + read_hex_value(write_file2)

seconds = 0
# print start time utc
print(time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime()))
# Print table header
print_table("second(s)", "cache bw (GB/s)", "memory bw (GB/s)")

while True:
    diff_cache_read_all = [0] * 8
    diff_cache_write_all = [0] * 8

    # Read new hex values for cache bandwidth
    for i in range(8):
        cache_read_file1 = f'/sys/class/hwmon/{bfperf}/llt{i}/counter0'
        cache_read_file2 = f'/sys/class/hwmon/{bfperf}/llt{i}/counter1'
        cache_write_file1 = f'/sys/class/hwmon/{bfperf}/llt{i}/counter2'
        cache_write_file2 = f'/sys/class/hwmon/{bfperf}/llt{i}/counter3'

        new_cache_read = read_hex_value(cache_read_file1) + read_hex_value(cache_read_file2)
        new_cache_write = read_hex_value(cache_write_file1) + read_hex_value(cache_write_file2)

        diff_cache_read_all[i] = calculate_hex_diff(old_cache_read_all[i], new_cache_read)
        diff_cache_write_all[i] = calculate_hex_diff(old_cache_write_all[i], new_cache_write)

        old_cache_read_all[i] = new_cache_read
        old_cache_write_all[i] = new_cache_write

    # Read new hex values for memory bandwidth
    new_value1 = read_hex_value(read_file1) + read_hex_value(read_file2)
    new_value2 = read_hex_value(write_file1) + read_hex_value(write_file2)

    diff1 = calculate_hex_diff(old_value1, new_value1)
    diff2 = calculate_hex_diff(old_value2, new_value2)

    old_value1 = new_value1
    old_value2 = new_value2

    # Print table
    cache_bw = f"{sum(diff_cache_read_all) + sum(diff_cache_write_all):.2f}"
    memory_bw = f"{diff1 + diff2:.2f}"
    print_table(seconds, cache_bw, memory_bw)

    seconds += 1
    time.sleep(1)
