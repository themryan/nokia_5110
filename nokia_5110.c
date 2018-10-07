#include <generated/autoconf.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/jiffies.h>
#include <linux/kobject.h>
#include <linux/spinlock.h>

#include "nokia_5110.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Ryan");
MODULE_DESCRIPTION("A driver for the Nokia 5110 display");
MODULE_VERSION("0.1");

static int gpioDc = 44;
static int gpioRst = 68;
static int gpioSce = 67;

static int gpioDout = 26;
static int gpioSclk = 46;

// buffer for video
static uint8_t VBUFFER[LCD_HEIGHT * LCD_WIDTH / 8] = {0};
static size_t vbuffer_len = sizeof(VBUFFER);

#define DEVICE_NAME "nokiacdev"
#define CLASS_NAME "nokiaclass"

static struct nokia_struct
{
    int majorNo;
    struct class *class;
    struct device *dev;
    struct kobject *kobject;
    dev_t dev_no;
    spin_lock_t lock;
    int initialized;
} nokia = {0};

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char __user *, size_t, loff_t *);

static int command_out(uint8_t *buffer, size_t buffer_len);
static int data_out(const uint8_t *buffer, size_t buffer_len);
static int raw_out(uint8_t *buffer, size_t buffer_len);

static struct file_operations fops =
{
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release
};

static int lcd_raw_write(uint8_t *buffer, size_t buffer_len);
static int lcd_char_write(uint8_t *buffer, size_t buffer_lne);

/*   Module Initiazlization and Exit */

static int lcd_init(void)
{
    uint8_t init_commands[] = {LCD_COMMAND_FUNCT_SET | 0x01, 
                                LCD_COMMAND_Vop | 0x30,
                                LCD_COMMAND_TEMP_CTRL, 
                                LCD_COMMAND_BIAS_SYS | 0x04,
                                LCD_COMMAND_FUNCT_SET, 
                                LCD_COMMAND_DISP_CTRL | 0x04};

    printk(KERN_INFO "\033[32mInitializing LCD and setting pins.\033[0m");

    printk(KERN_INFO "Sending commands.");
    command_out(init_commands, 6);
}

static int __init nokia_5110_init(void)
{
    int i = 0;
    printk(KERN_INFO "Opening the Nokia 5110 driver\n");

    printk(KERN_INFO "Configuring the pins\n");

    // generic output pins
    gpio_request(gpioSce, "sysfs");
    gpio_direction_output(gpioSce, 0);
    gpio_request(gpioRst, "sysfs");
    gpio_direction_output(gpioRst, 1);
    gpio_request(gpioDc, "sysfs");
    gpio_direction_output(gpioDc, 0);

    // Data and Clock
    gpio_request(gpioSclk, "sysfs");
    gpio_direction_output(gpioSclk, 1);
    gpio_request(gpioDout, "sysfs");
    gpio_direction_output(gpioDout, 1);

    gpio_set_value(gpioRst, 0);
    gpio_set_value(gpioSce, 1);

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
    register_chrdev_region(nokia.dev_no, 1, "Nokia 5110");
    nokia.dev = device_create(nokia.class, NULL, nokia.dev_no, NULL, DEVICE_NAME);
    printk(KERN_INFO "Device created.");
    if (IS_ERR(nokia.dev))
    {
        printk(KERN_ALERT "\033[31mCould not create nokia device.\033[0m");
        class_destroy(nokia.class);
        unregister_chrdev(nokia.majorNo, DEVICE_NAME);
        return PTR_ERR(nokia.class);
    }

    printk(KERN_INFO "Creating kobject interface");
    nokia.kobject = kobject_create_and_add("nokia_5110", kernel_kobj);
    if (IS_ERR(nokia.kobject))
    {
        printk(KERN_ALERT "\033[31mCould not create kobject\033[0m");
        class_unregister(nokia.class);
        class_destroy(nokia.class);
        unregister_chrdev(nokia.majorNo, DEVICE_NAME);
        return PTR_ERR(nokia.kobject);
    }

    printk(KERN_INFO "\033[32mnokia_5110 succesfully initialized.\033[0m");

    spin_lock_init(&nokia.lock);

    return 0;
}

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
    kobject_del(nokia.kobject);
    printk(KERN_INFO "Devices unregistered and released.\n");
}

module_init(nokia_5110_init);
module_exit(nokia_5110_exit);

static int dev_open(struct inode *pinode, struct file *filep)
{
    int ret = 0;
    spin_lock(nokia.lock); 

    if ( !nokia.initialized )
    {
        if( lcd_init() == 0 )
        {
            nokia.initialized = 1;
            printk(KERN_IFO "\033[32mLCD Initialized.\033[0m");
        }
        else
        {
            printk(KERN_ALERT "\033[31mCould not initialize LCD control.\033[0m");
            class_unregister(nokia.class);
            class_destroy(nokia.class);
            unregister_chrdev(nokia.majorNo, DEVICE_NAME);
            spin_unlock(nokia.lock);
            ret = -1;
        }
    }
    return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    size_t num_copy = (vbuffer_len > len + *offset) ? len : vbuffer_len - *offset;
    int err = 0;   

    err = copy_to_user(buffer, VBUFFER + *offset, num_copy);

    if (err != 0)
    {
        printk(KERN_WARNING "Unable to copy %d bytes to buffer.\n", num_copy);
        return -EFAULT;
    }

    return 0;
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    size_t num_copy = (vbuffer_len > len + *offset) ? len : vbuffer_len - *offset;
    int err = 0;
    
    err = copy_from_user(VBUFFER + *offset, buffer, num_copy);

    if (err != 0)
    {
        printk(KERN_WARNING "Unable to copy %d bytes to VBUFFER.\n", num_copy);
        return -EFAULT;
    }

    lcd_char_write(VBUFFER, num_copy);

    return 0;
}

static int dev_release(struct inode *pinode, struct file *filep)
{
    spin_unlock(nokia.lock); 
    return 0;
}

// LCD Device controls
//
//
//

static int lcd_char_write(uint8_t *buffer, size_t buffer_len)
{
    const uint8_t *out_bits = NULL;

    while (buffer_len)
    {
        uint8_t index = *buffer - 0x20;
        if (index < 0x5F)
        {
            out_bits = ASCII[index];

            data_out(out_bits, 5);
            gpio_set_value(gpioSce, 1);
        }
        else
        {
            printk(KERN_ALERT "\033[31mIndex %d out of bounds.\033[0m", index);
            return -1;
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

static int raw_out(uint8_t *buffer, size_t buffer_len)
{
    int i;
    unsigned long delta = 5 * HZ / 1000; // every 25 ms
    unsigned long now = get_jiffies_64();
    unsigned long next = now + delta;

    gpio_set_value(gpioSce, 0);

    while (buffer_len)
    {
        int bits = 8;
        uint8_t out = *buffer;

        while (bits)
        {
            gpio_set_value(gpioSclk, 0);

            // MSB first
            gpio_set_value(gpioDout, (0x80 & out) ? 1 : 0);

            out <<= 1;

            while (!time_after(now, next))
                now = get_jiffies_64();

            next = now + delta;

            gpio_set_value(gpioSclk, 1);

            while (!time_after(now, next))
                now = get_jiffies_64();

            next = now + delta;

            bits--;
        }
        buffer++;
        buffer_len--;
    }

    gpio_set_value(gpioSce, 1);

    return 0;
}
