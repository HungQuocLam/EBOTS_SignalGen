/*
 *  siggen_driver.c
 *
 *  Created on: May 7, 2019
 *      Author: Hung Lam - ebots
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>       // Required for the GPIO functions
#include <linux/kobject.h>    // Using kobjects for the sysfs bindings
#include <linux/kthread.h>    // Using kthreads for the flashing functionality
#include <linux/delay.h>      // Using this header for the msleep() function
#include "siggen_driver.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hung Lam");
MODULE_DESCRIPTION("XTRIG Generator for the Jetson-Xavier");
MODULE_VERSION("0.1");

static unsigned int gpio_xtrig = 249;          ///< Default GPIO for the LED is gpio249
module_param(gpio_xtrig, uint, S_IRUGO);       ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpio_xtrig, " XTRIG GPIO number (default=249)");     ///< parameter description

static unsigned int exposure = 200;     	///< The exposure period in us
module_param(exposure, uint, S_IRUGO);   	///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(exposure, "Exposure period in us (min=1, default=200, max=100000)");

static unsigned int datawritetime = 2400;     	///< The data write time period in us
module_param(datawritetime, uint, S_IRUGO);   	///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(datawritetime, "Data write time period in us (min=1, default=200, max=100000)");

static unsigned int numframe = 13;
module_param(numframe, uint, S_IRUGO);   	///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(numframe, "Number of frame (min=1, default=13, max=10000)");

static char xtrigName[10] = "xtrigXXX";      ///< Null terminated default string -- just in case
static bool xtrigOn = 0;                    ///< xtrigGPIO = HIGH???

static enum modes mode = CONTINUOUS;             	///< Default mode is CONTINUOUS

/** @brief A callback function to display the xtrig mode
 *  @param kobj represents a kernel object device that appears in the sysfs filesystem
 *  @param attr the pointer to the kobj_attribute struct
 *  @param buf the buffer to which to write the number of presses
 *  @return return the number of characters of the mode string successfully displayed
 */
static ssize_t mode_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   switch(mode){
      case OFF:   return sprintf(buf, "off\n");       // Display the state -- simplistic approach
      case ONCE:    return sprintf(buf, "once\n");
      case CONTINUOUS: return sprintf(buf, "continuous\n");
      default:    return sprintf(buf, "LKM Error\n"); // Cannot get here
   }
}

/** @brief A callback function to store the LED mode using the enum above */
static ssize_t mode_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   // the count-1 is important as otherwise the \n is used in the comparison
   if (strncmp(buf,"once",count-1)==0) { mode = ONCE; }   // strncmp() compare with fixed number chars
   else if (strncmp(buf,"off",count-1)==0) { mode = OFF; }
   else if (strncmp(buf,"continuous",count-1)==0) { mode = CONTINUOUS; }
   return count;
}

/** @brief A callback function to display the LED period */
static ssize_t exposure_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", exposure);
}

/** @brief A callback function to store the LED period value */
static ssize_t exposure_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   unsigned int period;                     // Using a variable to validate the data sent
   sscanf(buf, "%du", &period);             // Read in the period as an unsigned int
   if ((period>1)&&(period<=100000)){        // Must be 2ms or greater, 10secs or less
	   exposure = period;                 // Within range, assign to blinkPeriod variable
   }
   return period;
}

static ssize_t datawritetime_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", datawritetime);
}

static ssize_t datawritetime_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   unsigned int period;                     // Using a variable to validate the data sent
   sscanf(buf, "%du", &period);             // Read in the period as an unsigned int
   if ((period>1)&&(period<=100000)){        // Must be 2ms or greater, 10secs or less
	   datawritetime = period;                 // Within range, assign to blinkPeriod variable
   }
   return period;
}

static ssize_t numframe_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", numframe);
}

static ssize_t numframe_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   unsigned int frame;                     // Using a variable to validate the data sent
   sscanf(buf, "%du", &frame);             // Read in the period as an unsigned int
   if ((frame>1)&&(frame<=10000)){        // Must be 2ms or greater, 10secs or less
	   numframe = frame;                 // Within range, assign to blinkPeriod variable
   }
   return frame;
}

/** Use these helper macros to define the name and access levels of the kobj_attributes
 *  The kobj_attribute has an attribute attr (name and mode), show and store function pointers
 */

static struct kobj_attribute mode_attr = __ATTR(mode, 0660, mode_show, mode_store);
static struct kobj_attribute exposure_attr = __ATTR(exposure, 0660, exposure_show, exposure_store);
static struct kobj_attribute datawritetime_attr = __ATTR(datawritetime, 0660, datawritetime_show, datawritetime_store);
static struct kobj_attribute numframe_attr = __ATTR(numframe, 0660, numframe_show, numframe_store);

/** The ebots_attrs[] is an array of attributes that is used to create the attribute group below.
 *  The attr property of the kobj_attribute is used to extract the attribute struct
 */
static struct attribute *ebots_attrs[] = {
   &exposure_attr.attr,
   &mode_attr.attr,
   &datawritetime_attr.attr,
   &numframe_attr.attr,
   NULL,
};

/** The attribute group uses the attribute array and a name, which is exposed on sysfs -- in this
 *  case it is gpio49, which is automatically defined in the ebotsLED_init() function below
 *  using the custom kernel parameter that can be passed when the module is loaded.
 */
static struct attribute_group attr_group = {
   .name  = xtrigName,                        // The name is generated in ebotsLED_init()
   .attrs = ebots_attrs,                      // The attributes array defined just above
};

static struct kobject *ebots_kobj;            /// The pointer to the kobject
static struct task_struct *task;            /// The pointer to the thread task

/** @brief The GPIO xtrig main kthread loop
 *
 *  @param arg A void pointer used in order to pass data to the thread
 *  @return returns 0 if successful
 */
static int siggen_driver(void *arg){
   printk(KERN_INFO "EBOTS: Thread has started running \n");
   while(!kthread_should_stop()){           // Returns true when kthread_stop() is called
      set_current_state(TASK_RUNNING);
      switch (mode)
	  {
      case CONTINUOUS:
    	  gpio_set_value(gpio_xtrig, LOW);       	// XTRIG HIGH --> LOW
    	  set_current_state(TASK_INTERRUPTIBLE);
    	  udelay(exposure);                			// usecond delay for exposure
    	  set_current_state(TASK_RUNNING);
    	  gpio_set_value(gpio_xtrig, HIGH);       	// XTRIG HIGH --> HIGH
    	  set_current_state(TASK_INTERRUPTIBLE);
    	  udelay(datawritetime);
    	  break;

      case OFF:
    	  gpio_set_value(gpio_xtrig, HIGH);       	// XTRIG HIGH --> HIGH
    	  break;

      case ONCE:
    	  if (numframe > 0)
    	  {
        	  gpio_set_value(gpio_xtrig, LOW);       	// XTRIG HIGH --> LOW
        	  set_current_state(TASK_INTERRUPTIBLE);
        	  udelay(exposure);                			// usecond delay for exposure
        	  set_current_state(TASK_RUNNING);
        	  gpio_set_value(gpio_xtrig, HIGH);       	// XTRIG HIGH --> HIGH
        	  set_current_state(TASK_INTERRUPTIBLE);
        	  udelay(datawritetime);
        	  --numframe;
        	  if (numframe == 0) mode = OFF;			// pulse generation off
    	  }

    	  break;
      default: break;
	  }


   }
   printk(KERN_INFO "EBOTS: Thread has run to completion \n");
   return 0;
}

/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point. In this example this
 *  function sets up the GPIOs and the IRQ
 *  @return returns 0 if successful
 */
static int __init xtrig_siggen_init(void){
   int result = 0;

   printk(KERN_INFO "EBOTS: Initializing the EBOTS XTRIG SIGGEN LKM\n");
   sprintf(xtrigName, "xtrig%d", gpio_xtrig);      // Create the gpio249 name for /sys/ebots/xtrig249

   printk("gpio_xtrig %d \n", gpio_xtrig);

   for (result = 0; result < sizeof(xtrigName); result ++)
   {
	   printk(" %d \n", xtrigName[result]);
   }

   ebots_kobj = kobject_create_and_add("ebots", kernel_kobj->parent); // kernel_kobj points to /sys/kernel
   if(!ebots_kobj){
      printk(KERN_ALERT "EBOTS: failed to create kobject\n");
      return -ENOMEM;
   }
   // add the attributes to /sys/ebots/ -- for example, /sys/ebots/xtrig249/mode
   result = sysfs_create_group(ebots_kobj, &attr_group);
   if(result) {
      printk(KERN_ALERT "EBOTS: failed to create sysfs group\n");
      kobject_put(ebots_kobj);                // clean up -- remove the kobject sysfs entry
      return result;
   }
   xtrigOn = true;
   gpio_request(gpio_xtrig, "sysfs");          // gpio_xtrig is 249 by default, request it
   gpio_direction_output(gpio_xtrig, xtrigOn);   // Set the gpio to be in output mode and turn on
   gpio_export(gpio_xtrig, false);  // causes gpio249 to appear in /sys/class/gpio
                                 // the second argument prevents the direction from being changed

   task = kthread_run(siggen_driver, NULL, "Siggen_thread");  // Start the LED flashing thread
   if(IS_ERR(task)){                                     // Kthread name is LED_flash_thread
      printk(KERN_ALERT "EBOTS: failed to create the task\n");
      return PTR_ERR(task);
   }
   return result;
}

/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required.
 */
static void __exit xtrig_siggen_exit(void){
   kthread_stop(task);                      // Stop the LED flashing thread
   kobject_put(ebots_kobj);                   // clean up -- remove the kobject sysfs entry
   gpio_set_value(gpio_xtrig, 0);              // Turn the LED off, indicates device was unloaded
   gpio_unexport(gpio_xtrig);                  // Unexport the Button GPIO
   gpio_free(gpio_xtrig);                      // Free the LED GPIO
   printk(KERN_INFO "EBOTS: Goodbye from the EBOTS SIGGEN LKM!\n");
}

/// This next calls are  mandatory -- they identify the initialization function
/// and the cleanup function (as above).
module_init(xtrig_siggen_init);
module_exit(xtrig_siggen_exit);
