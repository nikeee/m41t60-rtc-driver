#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <stdbool.h>
#include <asm/uaccess.h>

const int major_number = 250;
const char *driver_name = "RTC driver";

// Type definitions
typedef struct inode inode;             // fs.h
typedef struct file file;               // fs.h

typedef struct i2c_adapter i2c_adapter; // i2c.h
typedef struct rtc_time rtc_time;
typedef struct i2c_client i2c_client;   // i2c.h
typedef struct i2c_driver i2c_driver;   // i2c.h

// I2C driver: handles all the devices connected to the bus
static i2c_driver rtc_driver;

// I2C client: stands for a single RTC device
static i2c_client *rtc_client;

// I2C client addresses: contains all the addresses of connected devices
static unsigned short normal_i2c[] = { 0x68, I2C_CLIENT_END };

// Struct contains addresses of kernel functions
static struct file_operations fops;

// Opens the driver
static int rtc_open(inode *inode, file *file);

// Closes the driver
static int rtc_close(inode *inode, file *file);

// Reads data from the driver
static ssize_t rtc_read(file *file, char *user, size_t count, loff_t *offset);

// Write data to the driver
static ssize_t rtc_write(file *file, const char __user *user, size_t count, loff_t *offset);

// Universal interface for I/O
// 'cmd' defines the command being executed
static int rtc_ioctl(inode *inode, file *file, unsigned int cmd, unsigned long arg);

// Add the client to the bus
static int rtc_probe(i2c_adapter *adapter, int addr, int kind);
// Registers the bus at the kernel
static int rtc_attach(i2c_adapter *adap);
// Removes the bus from the kernel
static int rtc_detach(i2c_client *client);


// Executes when the module loads
static int __init rtc_module_init(void);
// Executes when the module unloads
static void __exit rtc_module_exit(void);

module_init(rtc_module_init);
module_exit(rtc_module_exit);

// Module information
MODULE_AUTHOR("Group 1-1");
MODULE_DESCRIPTION("RTC module");
MODULE_LICENSE("GPL");

struct rtc_status {
    int stop;
    int interrupt;
    int calibration;
};
typedef struct rtc_status rtc_status;

rtc_status get_status(i2c_client *client);
rtc_time read_time(i2c_client* client);
rtc_time parse_time(char *input, size_t input_length);
bool is_valid_time(rtc_time time, bool strict_hwclock);
bool is_valid_time_string(const char *input, size_t input_length);
bool is_number_range(const char *input, size_t from_inclusive, size_t to_exclusive);
void write_time(i2c_client* client, rtc_time time, rtc_status status);
static inline bool is_leap_year(unsigned int year);
