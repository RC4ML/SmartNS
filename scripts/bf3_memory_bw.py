import time

def read_hex_value(filename):
    with open(filename, 'r') as file:
        hex_value = file.read().strip()
        return int(hex_value, 16)

def calculate_hex_diff(old_value, new_value):
    return  (new_value - old_value) *64 / (1024 * 1024) 

def print_decimal_value(value1, value2):
    print(f"Read: {value1:.2f} MB   Write: {value2:.2f} MB")

bfperf = "hwmon4"

read_file1 = '/sys/class/hwmon/{0}/mss0/counter0'.format(bfperf)
read_file2 = '/sys/class/hwmon/{0}/mss1/counter0'.format(bfperf)

write_file1 = '/sys/class/hwmon/{0}/mss0/counter1'.format(bfperf)
write_file2 = '/sys/class/hwmon/{0}/mss1/counter1'.format(bfperf)



old_value1 = read_hex_value(read_file1) + read_hex_value(read_file2)
old_value2 = read_hex_value(write_file1) + read_hex_value(write_file2)

while True:
    new_value1 = read_hex_value(read_file1) + read_hex_value(read_file2)
    new_value2 = read_hex_value(write_file1) + read_hex_value(write_file2)

    diff1 = calculate_hex_diff(old_value1, new_value1)
    diff2 = calculate_hex_diff(old_value2, new_value2)

    print_decimal_value(diff1, diff2)

    old_value1 = new_value1
    old_value2 = new_value2

    time.sleep(1)