{\rtf1\ansi\ansicpg1252\cocoartf1671\cocoasubrtf600
{\fonttbl\f0\fswiss\fcharset0 Helvetica;}
{\colortbl;\red255\green255\blue255;}
{\*\expandedcolortbl;;}
\margl1440\margr1440\vieww12600\viewh10200\viewkind0
\pard\tx720\tx1440\tx2160\tx2880\tx3600\tx4320\tx5040\tx5760\tx6480\tx7200\tx7920\tx8640\pardirnatural\partightenfactor0

\f0\fs28 \cf0 #include <linux/module.h>\
#include <linux/init.h>\
#include <linux/fs.h>\
#include <linux/uaccess.h>\
\
#include <linux/init.h>               // Macros used to mark up functions e.g. __init __exit\
#include <linux/module.h>      // Core header for loading LKMs into the kernel\
#include <linux/device.h>       // Header to support the kernel Driver Model\
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel\
#include <linux/errno.h>\
#include <linux/string.h>\
#include <linux/types.h>\
#include <linux/kdev_t.h>\
#include <linux/fs.h>\
#include <linux/ioport.h>\
#include <linux/highmem.h>\
#include <linux/pfn.h>\
#include <linux/version.h>\
#include <linux/ioctl.h>\
#include <linux/fs.h>                 // Header for the Linux file system support\
//#include <asm/uaccess.h>     // Required for the copy to user function ubuntu 16.04 and lower\
#include <linux/uaccess.h>     // Required for the copy to user function\
#include <linux/mutex.h>\
#include <net/sock.h>\
#include <net/tcp.h>\
\
#include <linux/sched/signal.h>\
#include <linux/timer.h>\
  \
#define  DEVICE_NAME "mcDriver"    ///< The device will appear at /dev/testchar using this value  \
#define  CLASS_NAME  "test"    ///< The device class -- this is a character device driver  \
#define WR_VALUE _IOW('a','a',int32_t*)\
#define RD_VALUE _IOR('a','b',int32_t*)\
\
#define GPIO1_START_ADDR 0x4804C000\
#define GPIO1_END_ADDR 0x4804e000\
#define GPIO1_SIZE (GPIO1_END_ADDR - GPIO1_START_ADDR)\
\
#define GPIO_SETDATAOUT 0x194\
#define GPIO_CLEARDATAOUT 0x190\
#define USR3 (1<<24)\
#define usr0 (1<<21)\
\
#define USR_LED USR3\
\
#define LED0_PATH "/sys/class/leds/beaglebone:green:usr0/trigger"\
\
#define CQ_DEFAULT	0\
\
char *morse_code[40] = \{"",\
".-","-...","-.-.","-..",".","..-.","--.","....","..",".---","-.-",\
".-..","--","-.","---",".--.","--.-",".-.","...","-","..-","...-",\
".--","-..-","-.--","--..","-----",".----","..---","...--","....-",\
".....","-....","--...","---..","----.","--..--","-.-.-.","..--.."\};\
\
\
char * mcodestring(int asciicode);\
  \
MODULE_LICENSE("GPL");            ///< The license type -- this affects available functionality  \
MODULE_AUTHOR("O'Leary, Pons");     ///< The author -- visible when you use modinfo  \
MODULE_DESCRIPTION("A Linux char driver");  ///< The description -- see modinfo  \
MODULE_VERSION("0.2");            ///< A version number to inform users  \
  \
static int    majorNumber;                  ///< Stores the device number -- determined automatically  \
static char   message[256] = \{0\};\
static char   morseString[256] = \{0\}; \
bool flag = 0;\
int iterator = 0;            ///< Memory for the string that is passed from userspace    \
static short  size_of_message;              ///< Used to remember the size of the string stored  \
static int    numberOpens = 0;              ///< Counts the number of times the device is opened  \
static struct class*  testcharClass  = NULL; ///< The device-driver class struct pointer  \
static struct device* testcharDevice = NULL; ///< The device-driver device struct pointer\
int32_t value = 0;\
\
static DEFINE_MUTEX(ebbchar_mutex);  /// A macro that is used to declare a new mutex that is visible in this file\
                                     /// results in a semaphore variable ebbchar_mutex with value 1 (unlocked)\
                                     /// DEFINE_MUTEX_LOCKED() results in a variable with value 0 (locked)\
void simple_timer_function(struct timer_list *);\
struct timer_list simple_timer;\
int timerms;  \
  \
\
// The prototype functions for the character driver -- must come before the struct definition\
static int     my_open(struct inode *, struct file *);\
static int     dev_release(struct inode *, struct file *);\
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);\
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);\
static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg);\
\
/** @brief Devices are represented as file structure in the kernel. The file_operations structure \
*  from  /linux/fs.h lists the callback functions that you wish to associated with your file operations\
*  using a C99 syntax structure. char devices usually implement open, read, write and release calls\
*/\
static struct file_operations fops =\
\{\
   .open = my_open,\
   .read = dev_read,\
   .write = dev_write,\
   .release = dev_release,\
   .unlocked_ioctl = my_ioctl\
\};\
\
ssize_t write_vaddr_disk(void *, size_t);\
int setup_disk(void);\
void cleanup_disk(void);\
void BBBremoveTrigger(void);\
void BBBstartHeartbeat(void);\
void BBBledOn(void);\
void BBBledOff(void);\
void convertToMorse(char *);\
\
static char buf[] ="none";\
static char buf1[32];\
struct file *fp;\
static volatile void *gpio_addr;\
static volatile unsigned int *gpio_setdataout_addr;\
static volatile unsigned int *gpio_cleardataout_addr;\
\
\
static int __init testchar_init(void)\{\
   timer_setup(&simple_timer,simple_timer_function,0);\
\
   mutex_init(&ebbchar_mutex);	/// Initialize the mutex lock dynamically at runtime\
   printk(KERN_INFO "TestChar: Initializing the TestChar LKM\\n");\
\
   // Try to dynamically allocate a major number for the device -- more difficult but worth it\
   majorNumber = register_chrdev(0, DEVICE_NAME, &fops);\
   if (majorNumber<0)\{\
      printk(KERN_ALERT "TestChar failed to register a major number\\n");\
      return majorNumber;\
   \}\
   printk(KERN_INFO "TestChar: registered correctly with major number %d\\n", majorNumber);\
\
   // Register the device class\
   testcharClass = class_create(THIS_MODULE, CLASS_NAME);\
   if (IS_ERR(testcharClass))\{                // Check for error and clean up if there is\
      unregister_chrdev(majorNumber, DEVICE_NAME);\
      printk(KERN_ALERT "Failed to register device class\\n");\
      return PTR_ERR(testcharClass);          // Correct way to return an error on a pointer\
   \}\
   printk(KERN_INFO "TestChar: device class registered correctly\\n");\
\
   // Register the device driver\
   testcharDevice = device_create(testcharClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);\
   if (IS_ERR(testcharDevice))\{               // Clean up if there is an error\
      class_destroy(testcharClass);           // Repeated code but the alternative is goto statements\
      unregister_chrdev(majorNumber, DEVICE_NAME);\
      printk(KERN_ALERT "Failed to create the device\\n");\
      return PTR_ERR(testcharDevice);\
   \}\
   printk(KERN_INFO "TestChar: device class created correctly\\n"); // Made it! device was initialized\
   \
   \
   \
    \
   mm_segment_t fs;\
    loff_t pos;\
    printk("test enter\\n");\
\
    gpio_addr = ioremap(GPIO1_START_ADDR, GPIO1_SIZE);\
       if(!gpio_addr) \{\
       	printk(KERN_ERR "HI: ERROR: FAILED TO REMAP\\N");\
       \}\
       gpio_setdataout_addr = gpio_addr + GPIO_SETDATAOUT;\
       gpio_cleardataout_addr = gpio_addr + GPIO_CLEARDATAOUT;\
\
       BBBremoveTrigger();\
       strcpy(message,"Welcome to Linux");\
   convertToMorse(message);\
   mod_timer(&simple_timer, jiffies + msecs_to_jiffies(50));\
       \
       \
     \
   return 0;\
\}\
\
static void __exit testchar_exit(void)\{\
   mutex_destroy(&ebbchar_mutex);        /// destroy the dynamically-allocated mutex\
   device_destroy(testcharClass, MKDEV(majorNumber, 0));     // remove the device\
   class_unregister(testcharClass);                          // unregister the device class\
   class_destroy(testcharClass);                             // remove the device class\
   unregister_chrdev(majorNumber, DEVICE_NAME);             // unregister the major number\
   printk(KERN_INFO "TestChar: Goodbye from the LKM!\\n");\
   BBBstartHeartbeat();\
	printk("test exit\\n");\
\}\
\
void BBBremoveTrigger()\{\
   int err = 0;\
   printk(KERN_INFO "File to Open: %s\\n", LED0_PATH);\
   err = setup_disk();\
   err = write_vaddr_disk("none", 4);\
   cleanup_disk();\
\}\
\
void BBBstartHeartbeat()\{\
   int err = 0;\
   printk(KERN_INFO "File to Open: %s\\n", LED0_PATH);\
   err = setup_disk();\
   err = write_vaddr_disk("heartbeat", 9);\
   cleanup_disk();\
\}\
\
void BBBledOn()\{\
   *gpio_setdataout_addr = USR_LED;\
\}\
\
void BBBledOff()\{\
   *gpio_cleardataout_addr = USR_LED;\
\}\
\
int setup_disk(void)\{\
	fp =filp_open( LED0_PATH ,O_RDWR | O_CREAT,0644);\
	    if (IS_ERR(fp))\{\
	        printk("create file error\\n");\
	        return -1;\
	    \}\
	return 0;\
\}\
\
void cleanup_disk(void)\{\
   mm_segment_t fs;\
\
   fs = get_fs();\
   set_fs(KERNEL_DS);\
   if(fp) filp_close(fp, NULL);\
   set_fs(fs);\
\}\
\
ssize_t write_vaddr_disk(void * v, size_t is)\{\
   mm_segment_t fs;\
   ssize_t s;\
   loff_t pos;\
   fs = get_fs();\
   set_fs(KERNEL_DS);\
\
   pos = fp->f_pos;\
   s = vfs_write(fp, v, is, &pos);\
   if (s == is)\{\
      fp->f_pos = pos;\
   \}\
   set_fs(fs);\
   return s;\
\}\
\
/** @brief The device open function that is called each time the device is opened\
*  This will only increment the numberOpens counter in this case.\
*  @param inodep A pointer to an inode object (defined in linux/fs.h)\
*  @param filep A pointer to a file object (defined in linux/fs.h)\
*/\
static int my_open(struct inode *inodep, struct file *filep)\{\
	if(!mutex_trylock(&ebbchar_mutex))\{    /// Try to acquire the mutex (i.e., put the lock on/down)\
		                                  /// returns 1 if successful and 0 if there is contention\
	      printk(KERN_ALERT "EBBChar: Device in use by another process");\
	      return -EBUSY;\
	   \}\
   numberOpens++;\
   printk(KERN_INFO "TestChar: Device has been opened %d time(s)\\n", numberOpens);\
   return 0;\
\}\
\
/** @brief This function is called whenever device is being read from user space i.e. data is\
*  being sent from the device to the user. In this case is uses the copy_to_user() function to\
*  send the buffer string to the user and captures any errors.\
*  @param filep A pointer to a file object (defined in linux/fs.h)\
*  @param buffer The pointer to the buffer to which this function writes the data\
*  @param len The length of the b\
*  @param offset The offset if required\
*/\
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset)\{\
   int error_count = 0;\
   // copy_to_user has the format ( * to, *from, size) and returns 0 on success\
   error_count = copy_to_user(buffer, message, size_of_message);\
   \
\
   if (error_count==0)\{            // if true then have success\
      printk(KERN_INFO "TestChar: Sent %d characters to the user\\n", size_of_message);\
      return (size_of_message=0);  // clear the position to the start and return 0\
   \}\
   else \{\
      printk(KERN_INFO "TestChar: Failed to send %d characters to the user\\n", error_count);\
      return -EFAULT;              // Failed -- return a bad address message (i.e. -14)\
   \}\
\}\
\
/** @brief This function is called whenever the device is being written to from user space i.e.\
*  data is sent to the device from the user. The data is copied to the message[] array in this\
*  LKM using the sprintf() function along with the length of the string.\
*  @param filep A pointer to a file object\
*  @param buffer The buffer to that contains the string to write to the device\
*  @param len The length of the array of data that is being passed in the const char buffer\
*  @param offset The offset if required\
*/\
static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)\{\
   strcpy(message, buffer);   // appending received string with its length\
   size_of_message = strlen(message);                 // store the length of the stored message\
   printk(KERN_INFO "TestChar: message from the user: %s\\n", message);\
   convertToMorse(message);\
   mod_timer(&simple_timer, jiffies + msecs_to_jiffies(50));\
   return len;\
\}\
\
/** @brief The device release function that is called whenever the device is closed/released by\
*  the userspace program\
*  @param inodep A pointer to an inode object (defined in linux/fs.h)\
*  @param filep A pointer to a file object (defined in linux/fs.h)\
*/\
static int dev_release(struct inode *inodep, struct file *filep)\{\
   mutex_unlock(&ebbchar_mutex);          /// Releases the mutex (i.e., the lock goes up)\
   printk(KERN_INFO "TestChar: Device successfully closed\\n");\
   return 0;\
\}\
\
static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg)\
\{\
         switch(cmd) \{\
                case WR_VALUE:\
                        copy_from_user(&value ,(int32_t*) arg, sizeof(value));  \
                                                	                                                                      \
                        printk(KERN_INFO "Value = %d\\n", value);\
                        break;\
                case RD_VALUE:\
                        copy_to_user((int32_t*) arg, &value, sizeof(value));\
                        break;\
        \}\
        return 0;\
\}\
\
void convertToMorse(char * phrase)\
\{\
BBBremoveTrigger();\
int i = 0;\
int numLetters = strlen(phrase);\
			while(phrase[i] != '\\n' && i < numLetters) \
			\{\
		    		if (phrase[i] == ' ') \{\
		    		strcat(morseString, "!");\
		    		i++;\
		    		continue;\
		    		\}\
		    		strcat(morseString, mcodestring(phrase[i]));\
		    		strcat(morseString, " ");\
		    		i++;\
			\}\
strcat(morseString, "?");\
printk(KERN_INFO "TestChar: morse string from the user: %s\\n", morseString);\
\}\
\
void simple_timer_function(struct timer_list *timer)\
\{\
	if (flag) \{\
	BBBledOff();\
	mod_timer(&simple_timer, jiffies + msecs_to_jiffies(1000));\
	flag = 0;\
	return;\
	\}\
	\
	switch(morseString[iterator]) \{\
\
   case '.' :\
      BBBledOn();\
      mod_timer(&simple_timer, jiffies + msecs_to_jiffies(1000));\
      flag = 1;\
      iterator++;\
      break; /* optional */\
	\
   case '-' :\
      BBBledOn();\
      mod_timer(&simple_timer, jiffies + msecs_to_jiffies(3000));\
      flag = 1;\
      iterator++;\
      break; /* optional */\
   \
   case ' ' :\
      mod_timer(&simple_timer, jiffies + msecs_to_jiffies(2000));\
      iterator++;\
      break; /* optional */\
      \
   case '!' :\
      mod_timer(&simple_timer, jiffies + msecs_to_jiffies(6000));\
      iterator++;\
      break; /* optional */\
   \
   case '?' :\
      iterator = 0;\
      BBBstartHeartbeat();\
      break; /* optional */\
  \
\
   \}\
\}\
\
\
\
char * mcodestring(int asciicode)\
\{\
   char *mc;   // this is the mapping from the ASCII code into the mcodearray of strings.\
\
   if (asciicode > 122)  // Past 'z'\
      mc = morse_code[CQ_DEFAULT];\
   else if (asciicode > 96)  // Upper Case\
      mc = morse_code[asciicode - 96];\
   else if (asciicode > 90)  // uncoded punctuation\
      mc = morse_code[CQ_DEFAULT];\
   else if (asciicode > 64)  // Lower Case \
      mc = morse_code[asciicode - 64];\
   else if (asciicode == 63)  // Question Mark\
      mc = morse_code[39];    // 36 + 3 \
   else if (asciicode > 57)  // uncoded punctuation\
      mc = morse_code[CQ_DEFAULT];\
   else if (asciicode > 47)  // Numeral\
      mc = morse_code[asciicode - 21];  // 27 + (asciicode - 48) \
   else if (asciicode == 46)  // Period\
      mc = morse_code[38];  // 36 + 2 \
   else if (asciicode == 44)  // Comma\
      mc = morse_code[37];   // 36 + 1\
   else\
      mc = morse_code[CQ_DEFAULT];\
   return mc;\
\}\
\
/** @brief A module must use the module_init() module_exit() macros from linux/init.h, which\
*  identify the initialization function at insertion time and the cleanup function (as\
*  listed above)\
*/\
module_init(testchar_init);\
module_exit(testchar_exit);}
