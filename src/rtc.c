#include "rtc.h"

// read/write definitions for char device
static struct file_operations fops = {
    .open = &rtc_open,
    .release = &rtc_close,
    .read = &rtc_read,
    .write = &rtc_write,
    .ioctl = &rtc_ioctl
};

// create mutex that is used to ensure the file is only opened by one process
DECLARE_MUTEX(file_lock);

const char *months[] = {
    "Januar",
    "Februar",
    "Maerz",
    "April",
    "Mai",
    "Juni",
    "Juli",
    "August",
    "September",
    "Oktober",
    "November",
    "Dezember"
};
const unsigned char month_days[] = {
    31, /* Januar */
    28, /* Februar */
    31, /* Maerz */
    30, /* April */
    31, /* Mai */
    30, /* Juni */
    31, /* Juli */
    31, /* August */
    30, /* September */
    31, /* Oktober */
    30, /* November */
    31 /* Dezember */
};
const unsigned char month_days_leap_year[] = {
    31, /* Januar */
    29, /* Februar */
    31, /* Maerz */
    30, /* April */
    31, /* Mai */
    30, /* Juni */
    31, /* Juli */
    31, /* August */
    30, /* September */
    31, /* Oktober */
    30, /* November */
    31 /* Dezember */
};

// definitions for i2c driver
static i2c_driver rtc_driver = {
    .driver = {
        .name = "rtc",
        .owner = THIS_MODULE
    },
    .attach_adapter = &rtc_attach,
    .detach_client = &rtc_detach
};

bool is_reading = false;

// Creates addr_data
I2C_CLIENT_INSMOD;

// Opens the driver
static int rtc_open(inode *inode, file *file)
{
    // lock mutex
    down(&file_lock);
    is_reading = false;
    return 0;
}

// Closes the driver
static int rtc_close(inode *inode, file *file)
{
    // unlock mutex
    up(&file_lock);
    is_reading = false;
    return 0;
}

// Reads data from the driver
static ssize_t rtc_read(file *file, char __user *user, size_t count, loff_t *offset)
{
    if (is_reading)
    {
        return 0;
    }
    is_reading = true;

    rtc_time current_time = read_time(rtc_client);

    const char *current_month = months[current_time.tm_mon - 1];

    // 17 constant positions
    // + 1 for the line feed (\n)
    // + 1 for the string terminator (\0)
    // + length of the month name
    size_t string_date_length = 17 + 1 + strlen(current_month) + 1;

    char result[string_date_length];
    memset(result, '\0', sizeof(result));

    // Print the date values to the result buffer
    // Format:
    // DD. M hh:mm:ss YYYY
    sprintf(result, "%02d. %s %02d:%02d:%02d %04d\n",
        current_time.tm_mday,
        current_month,
        current_time.tm_hour,
        current_time.tm_min,
        current_time.tm_sec,
        current_time.tm_year
    );

    // copy result to user space
    __copy_to_user(user, result, sizeof(result));

    return sizeof(result);
}

// Write data to the driver
static ssize_t rtc_write(file *file, const char __user *user, size_t count, loff_t *offset)
{
    const size_t max_string_length = strlen("YYYY-MM-DD hh:mm:ss");
    if (count < max_string_length)
    {
        printk(KERN_INFO "User input was too short: %d, offset: %d\n", count, *offset);
        return count;
    }

    char new_date_string[max_string_length];
    memset(new_date_string, '\0', sizeof(new_date_string));

    // get date string from user
    __copy_from_user(new_date_string, user, sizeof(new_date_string));

    // check if the user input is okay
    if (!is_valid_time_string(new_date_string, max_string_length))
    {
        printk(KERN_INFO "User input has an invalid format.\n");
        return -EINVAL;
    }

    // parse the time
    rtc_time new_time = parse_time(new_date_string, max_string_length);

    // check if the time is valid
    if (!is_valid_time(new_time, false))
    {
        printk(KERN_INFO "The date specified is not a valid date.\n");
        return -EINVAL;
    }

    rtc_status current_status = get_status(rtc_client);

    // everything is okay. Write date to clock.
    write_time(rtc_client, new_time, current_status);

    return max_string_length;
}

// Add the client to the bus
static int rtc_probe(i2c_adapter *adapter, int addr, int kind)
{
    int err = 0;
    const char client_name[] = "rtc_driver";

    if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_WRITE_BYTE))
    {
        err = -EIO;
        goto ERROR_NO_FUNCTIONALITY;
    }

    rtc_client = kzalloc(sizeof(i2c_client), GFP_KERNEL);
    if (rtc_client == NULL)
    {
        err = -ENOMEM;
        goto ERROR_NO_MEM;
    }

    rtc_client->adapter = adapter   ;
    rtc_client->flags = 0;
    rtc_client->addr = addr;
    rtc_client->driver = &rtc_driver;
    strcpy(rtc_client->name, client_name);

    err = i2c_attach_client(rtc_client);
    if (err != 0)
    {
        goto ERROR_ATTACH_FAILED;
    }

    return 0;

ERROR_ATTACH_FAILED:
    kfree(rtc_client);
ERROR_NO_MEM:
ERROR_NO_FUNCTIONALITY:
    return err;
}

// Registers the i2c client to the bus
static int rtc_attach(i2c_adapter *adap)
{
    return i2c_probe(adap, &addr_data, &rtc_probe);
}

// Removes the i2c client from the bus
static int rtc_detach(i2c_client *client)
{
    int err = i2c_detach_client(client);
    if (err != 0)
    {
        goto ERR_DETACH;
    }

    void *client_data = i2c_get_clientdata(client);
    if (client_data)
    {
        kfree(client_data);
    }

    kfree(client);

    return 0;

ERR_DETACH:
    return err;
}

// Executes when the module loads
static int __init rtc_module_init(void)
{
    printk(KERN_INFO "Loading rtc driver...\n");

    int res = register_chrdev(major_number, driver_name, &fops);
    if (res != 0)
    {
        printk(KERN_ERR "Failed to register char device.\n");
        goto ERROR_NO_CHR_DEV;
    }

    res = i2c_add_driver(&rtc_driver);
    if (res != 0)
    {
        printk(KERN_ERR "Failed to add i2c driver.\n");
        goto ERROR_FAILED_TO_ADD_DRIVER;
    }

    printk(KERN_INFO "Kernel module intialized. :)\n");
    return 0;

ERROR_FAILED_TO_ADD_DRIVER:
    unregister_chrdev(major_number, driver_name);
ERROR_NO_CHR_DEV:
    return res;
}

// Executes when the module unloads
static void __exit rtc_module_exit(void)
{
    printk(KERN_INFO "Unloading rtc driver...\n");

    unregister_chrdev(major_number, driver_name);
    printk(KERN_INFO "Unregistered char device.\n");

    int res = i2c_del_driver(&rtc_driver);
    if (res != 0)
    {
        printk(KERN_ERR "i2c_del_driver failed.\n");
    }

    printk(KERN_INFO "Unloaded kernel module :)\n");
}

// Universal interface for I/O (used by hwclock)
// 'cmd' defines the command being executed
// 'arg' is a pointer (as long) to the destination buffer
static int rtc_ioctl(inode *inode, file *instanz, unsigned int cmd, unsigned long arg)
{
    switch (cmd)
    {
        case RTC_RD_TIME:
        {
            down(&file_lock);

            rtc_time current_time = read_time(rtc_client);

            --current_time.tm_mon;
            current_time.tm_year -= 1900;

            __copy_to_user((void*)arg, &current_time, sizeof(rtc_time));

            up(&file_lock);
            return 0;
        }
        case RTC_SET_TIME:
        {
            down(&file_lock);

            rtc_time new_time;
            memset(&new_time, 0, sizeof(rtc_time));

            __copy_from_user(&new_time, (void*)arg, sizeof(rtc_time));

            ++new_time.tm_mon;
            new_time.tm_year += 1900;

            if (!is_valid_time(new_time, true)) {
                printk("ioctl tried setting an invalid time.\n");
                up(&file_lock); return -1;
            }

            rtc_status current_status = get_status(rtc_client);

            write_time(rtc_client, new_time, current_status);

            up(&file_lock);
            return 0;
        }
        default:
            return 0;
    }
}

rtc_status get_status(i2c_client *client)
{
    // read bits from register
    int stop = i2c_smbus_read_byte_data(client, 0);
    int interrupt = i2c_smbus_read_byte_data(client, 1);
    int calibration = i2c_smbus_read_byte_data(client, 7);

    stop &= 0x80;
    interrupt &= 0x80;
    calibration &= 0x1F;

    rtc_status result = {
        .stop = stop,
        .interrupt = interrupt,
        .calibration = calibration
    };

    return result;
}

rtc_time read_time(i2c_client* client)
{
    // read data from bus using client "client" and specified offset
    // See: Page 12/24
    // http://www.uni-kassel.de/eecs/fileadmin/groups/w_221700/edu/SS_13/Sysprog/RTC.pdf
    int second = i2c_smbus_read_byte_data(client, 0);
    int minute = i2c_smbus_read_byte_data(client, 1);
    int hour = i2c_smbus_read_byte_data(client, 2);
    int day_of_week = i2c_smbus_read_byte_data(client, 3);
    int day_of_month = i2c_smbus_read_byte_data(client, 4);
    int month_century = i2c_smbus_read_byte_data(client, 5);
    int year = i2c_smbus_read_byte_data(client, 6);

    // todo: minus check?

    second &= 0x7F;
    minute &= 0x7F;
    hour &= 0x3F;
    day_of_week &= 0x07;
    day_of_month &= 0x3F;

    int month = month_century & 0x1F;
    int century = month_century & 0xC0;

    century >>= 6;

    second = BCD2BIN(second);
    minute = BCD2BIN(minute);
    hour = BCD2BIN(hour);
    day_of_week = BCD2BIN(day_of_week);
    day_of_month = BCD2BIN(day_of_month);
    month = BCD2BIN(month);
    century = BCD2BIN(century);
    year = BCD2BIN(year);

    year += 2000;
    year += 100 * century;

    rtc_time time = {
        .tm_sec = second,
        .tm_min = minute,
        .tm_hour = hour,
        .tm_mday = day_of_month,
        .tm_mon = month,
        .tm_year = year,
        // remaining fields are zero
        .tm_wday = 0,
        .tm_yday = 0,
        .tm_isdst = 0
    };
    return time;
}

bool is_number_range(const char *input, size_t from_inclusive, size_t to_exclusive)
{
    int i;
    for(i = from_inclusive; i < to_exclusive; i++)
    {
        if (input[i] < '0' || input[i] > '9')
        {
            return false;
        }
    }
    return true;
}

bool is_valid_time_string(const char *input, size_t input_length)
{
    if (input_length != 19 && input_length != 20) // accept with and without \n
    {
        return false;
    }

    // Format:
    // YYYY-MM-DD hh:mm:ss

    // check dashes, colons and space
    if (input[4] != '-' || input[7] != '-' || input[10] != ' ' || input[13] != ':' || input[16] != ':')
    {
        printk(KERN_INFO "User input has an invalid format: Invalid colons.\n");
        return false;
    }

    // check date (YYYY-MM-DD)
    if (!is_number_range(input, 0, 3)
        || !is_number_range(input, 5, 7)
        || !is_number_range(input, 11, 13))
    {
        printk(KERN_INFO "User input has an invalid format: Invalid date.\n");
        return false;
    }

    // check time (hh:mm:ss)
    if (!is_number_range(input, 11, 13)
        || !is_number_range(input, 14, 16)
        || !is_number_range(input, 17, 19))
    {
        printk(KERN_INFO "User input has an invalid format: Invalid time.\n");
        return false;
    }

    // everything seems to be okay
    return true;
}

rtc_time parse_time(char *input, size_t input_length)
{
    // Format:
    // YYYY-MM-DD hh:mm:ss

    // the initial values will be overriden
    char year_str[] = "YYYY";
    year_str[0] = input[0];
    year_str[1] = input[1];
    year_str[2] = input[2];
    year_str[3] = input[3];
    int year = simple_strtol(year_str, NULL, 10);

    char month_str[] = "MM";
    month_str[0] = input[5];
    month_str[1] = input[6];
    int month = simple_strtol(month_str, NULL, 10);

    char day_str[] = "DD";
    day_str[0] = input[8];
    day_str[1] = input[9];
    int day = simple_strtol(day_str, NULL, 10);

    char hour_str[] = "hh";
    hour_str[0] = input[11];
    hour_str[1] = input[12];
    int hour = simple_strtol(hour_str, NULL, 10);

    char minute_str[] = "mm";
    minute_str[0] = input[14];
    minute_str[1] = input[15];
    int minute = simple_strtol(minute_str, NULL, 10);

    char second_str[] = "ss";
    second_str[0] = input[17];
    second_str[1] = input[18];
    int second = simple_strtol(second_str, NULL, 10);

    rtc_time parsed = {
        .tm_mday = (int)day,
        .tm_mon = (int)month,
        .tm_year = (int)year,
        .tm_hour = (int)hour,
        .tm_min = (int)minute,
        .tm_sec = (int)second
    };
    return parsed;
}

bool is_valid_time(rtc_time time, bool strict_hwclock)
{
    // check for clock-specific range
    if (time.tm_year >= 2000 && time.tm_year <= 2399)
    {
        // check general time/date values
        if (time.tm_mon >= 1 && time.tm_mon <= 12
        && time.tm_mday >= 1 && time.tm_mday <= 31
        && time.tm_hour >= 0 && time.tm_hour <= 23
        && time.tm_min >= 0 && time.tm_min <= 59
        && time.tm_sec >= 0 && time.tm_sec <= 59)
        {
            // check for valid month range
            bool leap = is_leap_year(time.tm_year);
            int month_index = time.tm_mon - 1;
            // get the max month day
            unsigned char month_max = leap ? month_days[month_index] : month_days_leap_year[month_index];
            if (time.tm_mon <= month_max)
            {
                if (strict_hwclock)
                {
                    // Also restrict to:
                    // 01. Januar 2000 um 00:00:00 Uhr bis zum 19. Januar 2038 um 03:14:08 Uhr.
                    if (time.tm_year == 2038)
                    {
                        if(time.tm_mon <= 1
                            && time.tm_mday <= 19
                            && time.tm_hour <= 3
                            && time.tm_min <= 14
                            && time.tm_sec <= 8)
                        {
                            return true;
                        }
                        else
                        {
                            return false;
                        }
                    }
                    else if(time.tm_year > 2038)
                    {
                        return false;
                    }
                    else
                    {
                        return true;
                    }
                }
                else
                {
                    return true;
                }
            }
        }
    }
    return false;
}

void write_time(i2c_client* client, rtc_time time, rtc_status status)
{
    int seconds_bcd = BIN2BCD(time.tm_sec);
    int minutes_bcd = BIN2BCD(time.tm_min);
    int hours_bcd = BIN2BCD(time.tm_hour);
    int day_bcd = BIN2BCD(time.tm_mday);
    int year_bcd = BIN2BCD(time.tm_year % 100);

    int century_bcd = BIN2BCD(time.tm_year / 100 - 20);
    int month_bcd = BIN2BCD(time.tm_mon);

    int century_month_bcd = (century_bcd << 6) | month_bcd;

    // set rtc status bits (so they wont get overwritten)
    seconds_bcd |= (status.stop & 1) << 7;
    minutes_bcd |= (status.interrupt & 1) << 7;

    i2c_smbus_write_byte_data(client, 0, seconds_bcd);
    i2c_smbus_write_byte_data(client, 1, minutes_bcd);
    i2c_smbus_write_byte_data(client, 2, hours_bcd);
    i2c_smbus_write_byte_data(client, 4, day_bcd);
    i2c_smbus_write_byte_data(client, 5, century_month_bcd);
    i2c_smbus_write_byte_data(client, 6, year_bcd);
}

static inline bool is_leap_year(unsigned int year)
{
    return (!(year % 4) && (year % 100)) || !(year % 400);
}
