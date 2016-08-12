#!/bin/bash
# Test misc stuff


#
# Variables
#

# Paths
src_path="${HOME}/src"
ko="${src_path}/rtc.ko" # Kernel object
pw="vai/v'oj"
drtc="/dev/rtc"

# Terminal information
tw=$(tput cols)  # Width
th=$(tput lines) # Height

# Colors
red='\033[0;31m'
green='\033[0;32m'
blue='\033[0;34m'
nc='\033[0m' # No color

# Boxes
b_ok="[ ${green}OK${nc} ]"
b_no="[ ${red}NO${nc} ]"
b_tt="[ ${blue}TT${nc} ]"

# Messages
msg_last=

# RTC
# Expected format:
#  YYYY-MM-DD hh:mm:ss
rtc_valid_in="2015-04-01 14:33:51"
rtc_valid_out="01. April 14:33:51 2015"

declare -a timestamps_upper=( \
    "3015-04-01\ 14:33:51" \
    "2015-13-01\ 14:33:51" \
    "2015-04-40\ 14:33:51" \
    "2015-04-01\ 87:33:51" \
    "2015-04-01\ 14:75:51" \
    "2015-04-01\ 14:33:99" \
    )

declare -a timestamps_lower=( \
    "1015-04-01\ 14:33:51" \
    "1815-04-01\ 14:33:51" \
    )

declare -a timestamps_wrong=( \
    "X015-04-01\ 14:33:51" \
    "2X15-04-01\ 14:33:51" \
    "20X5-04-01\ 14:33:51" \
    "201X-04-01\ 14:33:51" \
    "2015-X4-01\ 14:33:51" \
    "2015-0X-01\ 14:33:51" \
    "2015-04-X1\ 14:33:51" \
    "2015-04-XX\ 14:33:51" \
    "2015-04-01\ XX:33:51" \
    "2015-04-01\ 1X:33:51" \
    "2015-04-01\ 14:X3:51" \
    "2015-04-01\ 14:3X:51" \
    "2015-04-01\ 14:33:X1" \
    "2015-04-01\ 14:33:XX" \
    )

declare -a timestamps_valid_in=( \
    "2015-03-01\ 14:33:51" \
    "2015-04-01\ 14:33:51" \
    "2015-05-01\ 14:33:51" \
    )

# German because of locale
declare -a timestamps_valid_out=( \
    "01.\ Maerz\ 14:33:51\ 2015" \
    "01.\ April\ 14:33:51\ 2015" \
    "01.\ Mai\ 14:33:51\ 2015" \
    )

#
# Functions
#

# Messages
print() {
    msg_last="${@}"
    echo -ne "${b_tt} ${@}..."
    line_begin
}

print_test_count() {
    count=$(cat "${c_count}")
    echo -ne "\033[754D\033[7C${count}%"
    [[ "${1}" -gt "${count}" ]] && echo "${1}" > "${c_count}"
}

dmesg_skip=0
set_dmesg_skip() {
    dmesg_skip=$(dmesg | wc -l)
}

# Clear the current line
line_clear() {
    echo -ne "\033[2K"
}

# Go to the beginning of a line
line_begin() {
    echo -ne "\033[${tw}D"
}

# Print success and continue
ok() {
    msg="${msg_last} succeeded."
    line_begin
    line_clear
    echo -e "${b_ok} ${msg}"
}

# Print error and bail with return code 1
bail() {
    msg="${msg_last} failed."
    line_begin
    line_clear
    echo -e "${b_no} ${msg}"

    [[ "${#}" -gt 1 ]] && echo -e "${b_no} '${1}' != '${2}'"

    # Try unloading module
    brmmod "${ko}" &>/dev/null
    exit 1
}

# Custom sudo using password stored in variable
bsudo() {
    echo "${pw}" | sudo -S ${@}
}

# Custom insmod using custom sudo
binsmod() {
    bsudo insmod ${@}
    check_mod
}

# Custom rmmod using custom sudo
brmmod() {
    bsudo rmmod ${@}
}

# Check, whether inserting mod provides /dev/rtc
check_mod() {
    if [[ ! -c "${drtc}" ]]; then
        print "Providing char device '${drtc}' through kernel module"
        bail
    fi
}

# Reading / writing clock via hwclock
read_time_hwclock() {
    # Hardware Clock registers out of bounds
    [[ $(hwclock --show 2>&1 | grep "contain values that are either invalid" | wc -l) -gt 0 ]] && return 1
    hwclock --show
    return 0
}

write_time_hwclock() {
    [[ $(sudo hwclock --set --date "${1}" 2>&1 | wc -l) -gt 0 ]] && return 1
    return 0
}

# Check existence of rtc device file
check_time_dev() {
    if [[ ! -c "${drtc}" ]]; then
        echo; echo; echo; echo
        print "Missing character device '${drtc}'"
        bail
    fi
}

# Reading / writing clock via direct access to device file
read_time_dev() {
    check_time_dev
    cat "${drtc}"
}

write_time_dev() {
    #tmp=$(read_time_hwclock)
    check_time_dev
    echo "${1}" > "${drtc}"
    read_time_dev
    #read_time_hwclock
    #[[ "${tmp}" != $(read_time_hwclock) ]] && return 1
    #return 0
}

# Checks $0 > $1
check_gt() {
    [[ ${1} -gt ${2} ]] && ok || bail "${1}" "${2}"
}

# Checks $0 < $1
check_lt() {
    [[ ${1} -lt ${2} ]] && ok || bail "${1}" "${2}"
}

# Checks $0 == $1
check_eq_str() {
    [[ "${1}" == "${2}" ]] && ok || bail "${1}" "${2}"
}

# Checks $0 != $1
check_ne_str() {
    [[ "${1}" != "${2}" ]] && ok || bail "${1}" "${2}"
}

# Sets the RTC to a valid date
# and echoes it for further comparison
rtc_set_valid() {
    check_mod
    echo "${rtc_valid_in}" > "${drtc}"
    if [[ $(cat ${drtc} | head -2 | wc -l) -gt 1 ]]; then
        print "Driver outputs continuous data."
        bail
    fi
    read_time_dev
}

concurrent_test() {
    # Randomly read or write
    [[ $(($RANDOM%2)) -eq 1 ]] && write_time_dev "${rtc_valid_in}" &> /dev/null || read_time_dev &> /dev/null

    # Report possible failures
    [[ "${?}" -gt 1 ]] && (fails=$(cat "$c_fails"); ((fails++)); echo "${fails}" > "${c_fails}")
    print_test_count "${1}"
}



#
# Execution
#

# Check for correct mod insertion and removal
# -------------------------------------------

# Silently remove previously loaded mod
brmmod "${ko}" &>/dev/null

print "Testing module insertion and removal"
echo

binsmod "${ko}"
print "Loading module"
check_gt $(lsmod | grep '^rtc' | wc -l) 0

brmmod "${ko}"
print "Unloading module"
check_lt $(lsmod | grep '^rtc' | wc -l) 1

# Silently insert mod for further tests
binsmod "${ko}"


# Check upper boundaries
# ----------------------

# Set valid date for comparison
comp=$(rtc_set_valid)

print "Testing refusal of crossing upper bounds"
echo

for ((i = 0; i < ${#timestamps_upper[@]}; i++)); do
    print "Refusing '${timestamps_upper[$i]}' via device file"
    write_time_dev "${timestamps_upper[$i]}" > /dev/null
    check_eq_str "${comp}" "${rtc_valid_out}"

    # hwclock not needed
done


# Check lower boundaries
# ----------------------

print "Testing refusal of crossing lower bounds"
echo

# Set valid date for comparison
comp=$(rtc_set_valid)

for ((i = 0; i < ${#timestamps_lower[@]}; i++)); do
    print "Refusing '${timestamps_lower[$i]}' via device file"
    write_time_dev "${timestamps_lower[$i]}" > /dev/null
    check_eq_str "${comp}" "${rtc_valid_out}"

    print "Refusing '${timestamps_lower[$i]}' via hwclock"
    write_time_hwclock "${timestamps_lower[$i]}" > /dev/null
    check_eq_str "${comp}" "${rtc_valid_out}"
done


# Check wrong formats
# -------------------

print "Testing refusal of writing wrong formats"
echo

# Set valid date for comparison
comp=$(rtc_set_valid)

for ((i = 0; i < ${#timestamps_wrong[@]}; i++)); do
    print "Refusing '${timestamps_wrong[$i]}' via device file"
    write_time_dev "${timestamps_wrong[$i]}" > /dev/null
    check_eq_str "${comp}" "${rtc_valid_out}"

    print "Refusing '${timestamps_wrong[$i]}' via hwclock"
    write_time_hwclock "${timestamps_wrong[$i]}" > /dev/null
    check_eq_str "${comp}" "${rtc_valid_out}"
done


# Check valid dates
# -----------------

print "Testing acceptance of writing valid formats"
echo

for ((i = 0; i < ${#timestamps_valid_in[@]}; i++)); do
    # Reset to other valid date to see further change
    rtc_set_valid > /dev/null

    print "Accepting '${timestamps_valid_in[$i]}' via device file"
    write_time_dev "${timestamps_valid_in[$i]}" > /dev/null
    check_eq_str "${timestamps_valid_out[$i]}" "$(read_time_dev)"

    # Reset to other valid date to see further change
    rtc_set_valid > /dev/null

    # hwclock not needed
done

# Check concurrent read / write
# -----------------------------

# Export functions needed by xargs
export -f bail
export -f check_time_dev
export -f concurrent_test
export -f line_begin
export -f line_clear
export -f ok
export -f print
export -f print_test_count
export -f read_time_dev

# Export variables needed by xargs
export c_count='/tmp/c_count'
export c_fails='/tmp/c_fails'
export drtc

# Initialize counters and locks
echo 0 > "${c_count}"
echo 0 > "${c_fails}"

print "Testing concurrent read/write operations"
echo

# Execute concurrent tests
printf '%s ' {1..100} | xargs -n 1 -P 10 bash -c 'concurrent_test "$@"' _
print_test_count "100"

# Report on failure
fails=$(cat "${c_fails}")
if [[ "${fails}" -gt 0 ]]; then
    print "${fails} concurrent tests"
    bail
fi

print "All concurrent tests"
ok

# All tests completed successfully :)
echo -e "${b_ok} All tests passed, the kernel module seems to be working properly."
