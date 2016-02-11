#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <linux/mutex.h>
#include <asm/io.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MiCh");
MODULE_DESCRIPTION("Template for Drivers");
MODULE_SUPPORTED_DEVICE("RaspberryPi");
MODULE_VERSION("0.0.1");

#define DRV_NAME "mygpio"
#define DRV_FILE_NAME "mygpio" //what file mdev should create
#define CLASS_NAME "mygpio" // name of our class
#define DESIRED_MINOR 0
#define MINOR_COUNT 1
#define HEADER KERN_ALERT DRV_NAME": "
#define RETRY_SLEEP 200


#define OUT_GPIO 18
#define IN_GPIO 25
#define GPFSEL1 gpio + (OUT_GPIO / 10)
#define GPFSEL2 gpio + (IN_GPIO / 10)
#define GPIOSET gpio + 7
#define GPIOCLR gpio + 10
#define GPIOLEV gpio + 13

//Physical addresses range from 0x20000000 to 0x20FFFFFF for peripherals. 
//The bus addresses for peripherals are set up to map onto the peripheral bus address range starting at 0x7E000000. 
//Thus a peripheral advertised here at bus address 0x7Ennnnnn is available at physical address 0x20nnnnnn.

#define BCM2708_PERI_BASE 0x20000000
#define GPIO_BASE (BCM2708_PERI_BASE + 0x200000)
#define MEMLEN 4096

static volatile unsigned *gpio;
static struct file_operations fops;
static dev_t drvNums;
static struct cdev *drvObj;
static struct class *drvClass;


static int registerDriver(void);
static void unregisterDriver(void);
static DEFINE_MUTEX(mutex);

static int open(struct inode *inode, struct file *fp);
static int close(struct inode *inode, struct file *fp);
static ssize_t read(struct file *fp, char *buff, size_t count, loff_t *offsetp);
static ssize_t write(struct file *fp, const char *buff, size_t count, loff_t *offsetp);


// init fileoperations
static struct file_operations fops = {
    .write = write,
    .read = read,
    .open = open,
    .release = close
};


static int __init ModInit(void)
{
        if(registerDriver() < 0)
            return -EIO;

        printk(HEADER "Hello world,\n");
        printk(HEADER "I'm %s with Major: %d, Minor: %d\n", DRV_NAME, MAJOR(drvNums), MINOR(drvNums));
        
        gpio = ioremap(GPIO_BASE, MEMLEN);
        if(gpio == NULL)
        {
            printk(HEADER "GPIO error\n");
            release_mem_region(GPIO_BASE, MEMLEN);
            return -EBUSY;
        }

        return 0;
}


static int registerDriver(void){
        // get a Major/minor number for driver
        if(alloc_chrdev_region(&drvNums, DESIRED_MINOR, MINOR_COUNT, DRV_NAME) < 0)
            return -EIO;

        //init the cdev object
        drvObj = cdev_alloc();
        if (drvObj == NULL)
            goto freedevNum;

        // set the needed fields in the cdev struct
        drvObj->owner = THIS_MODULE;
        drvObj->ops = &fops;

        // call cdev_add to tell kernel about out driver
        // its is important that our driver is ready-to-run when this function is called
        if(cdev_add(drvObj, drvNums, MINOR_COUNT))
            goto freedevObj;

        // tell which driver class we belong to, so mdev knows how to handle us
        drvClass = class_create( THIS_MODULE, CLASS_NAME);
        device_create( drvClass, NULL, drvNums, NULL, DRV_FILE_NAME);

        return 0;

freedevObj:
        kobject_put(&drvObj->kobj );
freedevNum:
        unregister_chrdev_region( drvNums, MINOR_COUNT );
        return -EIO;
}

static void unregisterDriver(void){

        device_destroy(drvClass, drvNums);
        class_destroy(drvClass);
        cdev_del(drvObj);
        unregister_chrdev_region(drvNums, MINOR_COUNT);
}



static void __exit ModExit(void)
{
        printk(HEADER "I, %s with Major: %d and Minor: %d, "
                 "am going to end my life here\n",
                  DRV_NAME, MAJOR(drvNums), MINOR(drvNums));
        unregisterDriver();
        release_mem_region(GPIO_BASE, MEMLEN);
        printk(HEADER "Goodbye, cruel world :-(\n");
}


static int open(struct inode *inode, struct file *fp)
{
        // gpio 18 = ausgang
        u32 *gpioSel1 = (u32 *)GPFSEL1;
        // gpio 25 = eingang
        u32 *gpioSel2 = (u32 *)GPFSEL2;

        u32 buffer;
                
        while(mutex_trylock(&mutex) != 1){
            msleep(RETRY_SLEEP);
        }

        buffer = readl(gpioSel1);
        rmb();
        buffer = buffer & 0xf8ffffff;  // clear Bits für GPIO-18
        wmb();
        writel( buffer | 0x01000000, gpioSel1 ); // configure GPIO-18 as output

        buffer = readl(gpioSel2);
        rmb();
        wmb();
        writel( buffer & 0xfffc7fff, gpioSel2 ); // configure GPIO-25 as input
        
    return 0;
}


static int close(struct inode *inode, struct file *fp)
{    
        mutex_unlock(&mutex);
        //implement close function here
        return 0;
}

static ssize_t read(struct file *fp, char *buff, size_t count, loff_t *offsetp)
{
        // gpio 25 lesen
        char retval;
        int not_copied;
        u32 buffer = readl((u32 *)GPIOLEV);

        rmb();

        // shift right
        buffer = (buffer >> IN_GPIO) & 0x1;
        
        if (buffer == 0x1) {
            retval = '1';
        } else {
            retval = '0';
        }
        not_copied = copy_to_user(buff, &retval, 1);
        
        return 1 - not_copied;
}

static ssize_t write(struct file *fp, const char *buff, size_t count, loff_t *offsetp)
{
        char zahl;
        u32* ptr;
        copy_from_user( &zahl, buff, sizeof(char));

        if ( zahl == '1' ) {
            //LED ON
            ptr = (u32*) GPIOCLR;
        } else {
            //LED OFF
            ptr = (u32*) GPIOSET;
        }
        wmb();
        writel( (1 << OUT_GPIO), ptr );

        return count;
}

module_init(ModInit);
module_exit(ModExit);
