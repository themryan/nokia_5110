/*******************************************************************
 
Title: nokia_5110.c
Author: Michael Ryan
Date: 10/7/2018
Version: 0.1
Purpose:  This file provides an example of a simple driver used to 
interface to the Nokia 5110 LCD breakout boards from Sparkfun 
(https://www.sparkfun.com/products/10168).  The driver has only
been tested on a Beagle Bone Black with Debian 9.4.


This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

*******************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/jiffies.h>
#include <linux/kobject.h>

#include "nokia_5110.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Ryan");
MODULE_DESCRIPTION("A driver for the Nokia 5110 display");
MODULE_VERSION("0.1");

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char __user *, size_t, loff_t *);

static int command_out(uint8_t *buffer, size_t buffer_len);
static int data_out(const uint8_t *buffer, size_t buffer_len);
static int raw_out(const uint8_t *buffer, size_t buffer_len);

static int lcd_init(void);
//static int lcd_raw_write(uint8_t *buffer, size_t buffer_len);
static int lcd_char_write(uint8_t *buffer, size_t buffer_lne);

// Attributes functions
static ssize_t bias_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t mode_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);

static ssize_t mode_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t buf_len);

// BeagleBone Black pinouts used

static int gpioDc = 44;
static int gpioRst = 68;
static int gpioSce = 67;

static int gpioDout = 26;
static int gpioSclk = 46;

static uint8_t nokiaBias = 4;

typedef enum
{
	NOKIA_5110_MODE_TEXT = 0,
	NOKIA_5110_MODE_GRPH = 1,
	NOKIA_5110_MODE_COM = 3,
	NOKIA_5110_MODE_END = 4	
} nokia_5110_mode ;

static nokia_5110_mode nokiaMode = 0; 

// buffer for video
static uint8_t *VBUFFER = displayMap;
static size_t vbuffer_len = sizeof(displayMap);

#define DEVICE_NAME "nokiacdev"
#define CLASS_NAME "nokia_5110"

static struct nokia_struct
{
    int majorNo;
    struct class *class;
    struct device *dev;
    struct kobject *kobject;
    dev_t dev_no;

} nokia = {0};

static struct file_operations fops =
{
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release
};

/* Attributes */

static struct kobj_attribute bias_attr =
__ATTR_RO(bias);

static struct kobj_attribute instr_mode = 
__ATTR(mode, 0660, mode_show, mode_store);

static struct attribute *nokia_attrs[] = 
{
    &bias_attr.attr,
    &instr_mode.attr,
    NULL,
};

static struct attribute_group nokia_attr_group = 
{
    .attrs = nokia_attrs
};

/*   Module Initiazlization and Exit */

// INIT
static int __init nokia_5110_init(void)
{
    int ret;
    unsigned long now = get_jiffies_64();
    unsigned long delta = 2 * HZ / 1000;
    unsigned long next = now + delta;

    printk(KERN_INFO "Opening the Nokia 5110 driver\n");

    printk(KERN_INFO "Configuring the pins\n");

    // generic output pins

    gpio_request(gpioRst, "sysfs");
    gpio_direction_output(gpioRst, 0);

    while (!time_after(now, next))
    {
        now = get_jiffies_64();
    }

    gpio_set_value(gpioRst, 1);

    gpio_request(gpioSce, "sysfs");
    gpio_direction_output(gpioSce, 1);
    gpio_request(gpioDc, "sysfs");
    gpio_direction_output(gpioDc, 0);

    // Data and Clock
    gpio_request(gpioSclk, "sysfs");
    gpio_direction_output(gpioSclk, 0);
    gpio_request(gpioDout, "sysfs");
    gpio_direction_output(gpioDout, 0);

    printk(KERN_INFO "Done with configuring pins\n");
    printk(KERN_INFO "Initializing chardev\n");

    nokia.majorNo = register_chrdev(0, DEVICE_NAME, &fops);
    if (nokia.majorNo < 0)
    {
        printk(KERN_ALERT "\033[31mNokia 5110 driver failed to register.\n\033[0m");
        return nokia.majorNo;
    }

    printk(KERN_INFO "Major No. %d create for nokia device", nokia.majorNo);
    printk(KERN_INFO "Creating nokia class.");
    nokia.class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(nokia.class))
    {
        printk(KERN_ALERT "\033[31mCould not register class for %d\033[0m", nokia.majorNo);
        unregister_chrdev(nokia.majorNo, DEVICE_NAME);
        return PTR_ERR(nokia.class);
    }

    printk(KERN_INFO "Create device.");

    nokia.dev_no = MKDEV(nokia.majorNo, 0);
    register_chrdev_region(nokia.dev_no, 1, DEVICE_NAME);
    nokia.dev = device_create(nokia.class, NULL, nokia.dev_no, NULL, "nokia%d", 0);
    printk(KERN_INFO "Device created.");
    if (IS_ERR(nokia.dev))
    {
        printk(KERN_ALERT "\033[31mCould not create nokia device.\033[0m");
        class_destroy(nokia.class);
        unregister_chrdev(nokia.majorNo, DEVICE_NAME);
        return PTR_ERR(nokia.class);
    }

    printk(KERN_INFO "Creating kobject interface");
    nokia.kobject = kobject_create_and_add("nokia_5110", kernel_kobj->parent);
    if (IS_ERR(nokia.kobject))
    {
        printk(KERN_ALERT "\033[31mCould not create kobject\033[0m");
        class_unregister(nokia.class);
        class_destroy(nokia.class);
        unregister_chrdev(nokia.majorNo, DEVICE_NAME);
        return PTR_ERR(nokia.kobject);
    }

    ret = sysfs_create_group(nokia.kobject, &nokia_attr_group);
    if(ret) {
        printk(KERN_ALERT "\033[31mFailed to create attr group\033[0m");
        class_unregister(nokia.class);
        class_destroy(nokia.class);
        unregister_chrdev(nokia.majorNo, DEVICE_NAME);
        kobject_put(nokia.kobject);
        return ret;
    }

    printk(KERN_INFO "\033[32mnokia_5110 succesfully initialized.\033[0m");

    if (lcd_init() == 0)
    {
        printk(KERN_INFO "\033[32mLCD Initialized.\033[0m");
    }
    else
    {
        printk(KERN_ALERT "\033[31mCould not initialize LCD control.\033[0m");
        class_unregister(nokia.class);
        class_destroy(nokia.class);
        unregister_chrdev(nokia.majorNo, DEVICE_NAME);
    }

    return 0;
}

// EXIT
static void __exit nokia_5110_exit(void)
{
    printk(KERN_INFO "\033[31mExiting the Nokia 5110 driver\033[0m");

    gpio_unexport(gpioDc);
    gpio_unexport(gpioRst);
    gpio_unexport(gpioSce);

    gpio_unexport(gpioDout);
    gpio_unexport(gpioSclk);

    gpio_free(gpioDc);
    gpio_free(gpioRst);
    gpio_free(gpioSce);

    gpio_free(gpioDout);
    gpio_free(gpioSclk);

    device_destroy(nokia.class, nokia.dev_no);
    class_unregister(nokia.class);
    class_destroy(nokia.class);
    unregister_chrdev(nokia.majorNo, DEVICE_NAME);
    kobject_put(nokia.kobject);
    printk(KERN_INFO "Devices unregistered and released.\n");
}

module_init(nokia_5110_init);
module_exit(nokia_5110_exit);

 /***************** Device Controls *****************/

static int dev_open(struct inode *pinode, struct file *filep)
{
    int ret = 0;

    return ret;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    size_t num_copy = 0;
    int err = 0;

    if (*offset >= vbuffer_len)
    {
        return 0;
    }

    if (len == 0 || !buffer)
    {
        printk(KERN_ALERT "Invalid buffer.");
        return -EFAULT;
    }

    num_copy = (vbuffer_len > len + *offset) ? len : vbuffer_len - *offset;

    err = copy_to_user(buffer, VBUFFER + *offset, num_copy);

    if (err != 0)
    {
        printk(KERN_WARNING "Unable to copy %d bytes to buffer.\n", num_copy);
        return -EFAULT;
    }

    return num_copy;
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    size_t num_copy = 0;
    int err = 0;

    if (*offset >= vbuffer_len)
    {
        return 0;
    }

    if (len == 0 || !buffer)
    {
        printk(KERN_ALERT "Invalid buffer.");
        return -EFAULT;
    }

    num_copy = (vbuffer_len > len + *offset) ? len : vbuffer_len - *offset;

    err = copy_from_user(VBUFFER + *offset, buffer, num_copy);

    if (err != 0)
    {
        printk(KERN_WARNING "Unable to copy %u bytes to VBUFFER.", num_copy);
        return -EFAULT;
    }

    printk(KERN_INFO "Print %u bytes and %llu", num_copy, *offset);

    lcd_char_write(VBUFFER, num_copy);

    return num_copy;
}

static int dev_release(struct inode *pinode, struct file *filep)
{
    return 0;
}

 /***************** LCD Controls *****************/


// Initializes the lcd 
 static int lcd_init(void)
{
    // default startup settings
    uint8_t init_commands[] = {LCD_COMMAND_FUNCT_SET | 0x01,
                               LCD_COMMAND_Vop | 0x30,
                               LCD_COMMAND_TEMP_CTRL,
                               LCD_COMMAND_BIAS_SYS | nokiaBias,
                               LCD_COMMAND_FUNCT_SET,
                               LCD_COMMAND_DISP_CTRL | 0x04};

    printk(KERN_INFO "\033[32mInitializing LCD and setting pins.\033[0m");

    printk(KERN_INFO "Sending commands.");
    command_out(init_commands, 6);

    // write default screen 
    return data_out(displayMap, sizeof(displayMap));
}

/********************************************************
 *
 * Writes a character to 8x5 rectangle at current position
 *  params: 
 *       buffer - ASCII character array
 *       buffer_len - number of bytes in buffer    
 *       
 *********************************************************/
static int lcd_char_write(uint8_t *buffer, size_t buffer_len)
{
    const uint8_t *out_bits = NULL;
    int escaped = 0;

    while (buffer_len)
    {
        uint8_t index = *buffer - 0x20;
        if (index < 0x5F)
        {
            out_bits = ASCII[index];

            data_out(out_bits, 5);
            gpio_set_value(gpioSce, 1);
        }
        else if ( !escaped && *buffer == '\\' )
        {
            escaped = 1;
        }
        else
        {
            printk(KERN_ALERT "\033[31mChar index %d out of bounds.\033[0m", index);
        }

        buffer++;
        buffer_len--;
    }

    return 0;
}



static int command_out(uint8_t *buffer, size_t buffer_len)
{
    gpio_set_value(gpioDc, 0);

    return raw_out(buffer, buffer_len);
}


static int data_out(const uint8_t *buffer, size_t buffer_len)
{
    gpio_set_value(gpioDc, 1);

    return raw_out(buffer, buffer_len);
}


static int raw_out(const uint8_t *buffer, size_t buffer_len)
{
    unsigned long delta = 1 * HZ / 10000; // every 25 ms
    unsigned long now = get_jiffies_64();
    unsigned long next = now + delta;

    gpio_set_value(gpioSce, 0);

    while (buffer_len)
    {
        int bits = 8;
        uint8_t out = *buffer;

        while (bits)
        {
            // MSB first
            gpio_set_value(gpioDout, (0x80 & out) ? 1 : 0);

            gpio_set_value(gpioSclk, 1);

            out <<= 1;

            while (!time_after(now, next))
            {
                now = get_jiffies_64();
            }

            next = now + delta;

            gpio_set_value(gpioSclk, 0);

            while (!time_after(now, next))
            {
                now = get_jiffies_64();
            }

            next = now + delta;

            bits--;
        }
        buffer++;
        buffer_len--;
    }

    gpio_set_value(gpioSce, 1);
    gpio_set_value(gpioDout, 0);
    gpio_set_value(gpioSclk, 0);

    return 0;
}


 /***************** LCD Commands *****************/
#if 0
 // set y
 static int set_y(int y_pos)
 {
     uint8_t command = LCD_COMMAND_SET_Y;
     if( y_pos >= 5 )
     {
         printk(KERN_WARNING "Invalid y position %d", y_pos);
         return -1;
     }

     command |= y_pos;

     command_out(&command, 1 );

     return 0;
 }

 // set x
 static int set_x(int x_pos)
 {
     uint8_t command = LCD_COMMAND_SET_X;

     if( x_pos >= 83 )
     {
         printk(KERN_WARNING "Invalid x position %d", x_pos);
         return -1;
     }

     command |= x_pos;

     command_out(&command, 1 );

     return 0;
 }

// set display to normal
static int set_display_normal(void)
{
    uint8_t command = LCD_COMMAND_DISP_CTRL | 0x04;
    command_out(&command, 1 );

    return 0;
}

// set display pixels to black
static int set_display_black(void)
{
    uint8_t command = LCD_COMMAND_DISP_CTRL | 0x01;
    command_out(&command, 1 );

    return 0;
}

// set display to inverse mode
static int set_display_inverse(void)
{
    uint8_t command = LCD_COMMAND_DISP_CTRL | 0x05;
    command_out(&command, 1);

    return 0;
}

// set temperature coefficient
static int set_temperature_control(uint8_t temp_coeff)
{
    uint8_t commands_lst[] = 
    {
        LCD_COMMAND_FUNCT_SET | LCD_COMMAND_FUNCT_EXT_H,
        LCD_COMMAND_TEMP_CTRL | (temp_coeff & 0x03),
        LCD_COMMAND_FUNCT_SET
    };

    command_out(commands_lst, sizeof(commands_lst));

    return 0;
}

// set contrast
static int set_lcd_contrast(uint8_t bias)
{
    uint8_t commands_lst[] = 
    {
        LCD_COMMAND_FUNCT_SET | LCD_COMMAND_FUNCT_EXT_H,
        LCD_COMMAND_BIAS_SYS | (bias & 0x07),
        LCD_COMMAND_FUNCT_SET
    };

    command_out(commands_lst, sizeof(commands_lst));

    return 0;
}
#endif // 0
// Attribute show store wrappers

static ssize_t bias_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%du", nokiaBias);
}

static ssize_t mode_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%du", nokiaMode);
}

static ssize_t mode_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t buf_len)
{
    ssize_t ret = 0;
    int mode;

    char string[256] = { 0 };

    snprintf(string, buf_len, buf);

    ret = sscanf(string, "%du", &mode);

    printk(KERN_INFO "Nokia Mode %d from %s", mode, string);
    if( mode < NOKIA_5110_MODE_END )
    {
	nokiaMode = mode;
    }

    return ret;
}
