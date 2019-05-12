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
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>


#include "siggen_driver.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hung Lam");
MODULE_DESCRIPTION("XTRIG Generator for the Jetson-Xavier");
MODULE_VERSION("0.1");

static unsigned int gpio_xtrig = xtrigPin;          					///< Default GPIO for the xTrig is gpio249
module_param(gpio_xtrig, uint, S_IRUGO);       							///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpio_xtrig, " XTRIG GPIO number (default=249)");     	///< parameter description

static unsigned int gpio_DLPtrig = DLPTriggerPin;          					///< Default GPIO for the xTrig is gpio250
module_param(gpio_DLPtrig, uint, S_IRUGO);       							///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpio_DLPtrig, " DLP trigger GPIO number (default=250)");     	///< parameter description

static unsigned int gpio_redLaserEn = redLaserEnPin;          					///< Default GPIO for the xTrig is gpio351
module_param(gpio_redLaserEn, uint, S_IRUGO);       							///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpio_redLaserEn, " RED LASER GPIO number (default=351)");     	///< parameter description

static unsigned int gpio_blueLaserEn = blueLaserEnPin;          					///< Default GPIO for the xTrig is gpio424
module_param(gpio_blueLaserEn, uint, S_IRUGO);       							///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpio_blueLaserEn, " BLUE LASER GPIO number (default=424)");     	///< parameter description

static unsigned int exposure = 200;     								///< The exposure period in us
module_param(exposure, uint, S_IRUGO);   								///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(exposure, "Exposure period in us (min=1, default=200, max=100000)");

static unsigned int datawritetime = 2400;     							///< The data write time period in us
module_param(datawritetime, uint, S_IRUGO);   							///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(datawritetime, "Data write time period in us (min=1, default=200, max=100000)");

static unsigned int numframe = 13;
module_param(numframe, uint, S_IRUGO);   								///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(numframe, "Number of frame (min=1, default=13, max=10000)");

static char xtrigName[10] = "xtrigXXX";      							///< Null terminated default string -- just in case
//static bool xtrigOn = 0;                    							///< xtrigGPIO = HIGH???

static char DLPtriggerName[10] = "DLPXXX";      							///< Null terminated default string -- just in case
//static bool DLPOn = 0;                    							///< xtrigGPIO = HIGH???

static char redLaserName[10] = "redLaXXX";      							///< Null terminated default string -- just in case
//static bool redLaserOn = 0;                    							///< xtrigGPIO = HIGH???

static char blueLaserName[10] = "blueLaXXX";      							///< Null terminated default string -- just in case
//static bool blueLaserOn = 0;                    							///< xtrigGPIO = HIGH???

static enum modes mode = CONTINUOUS;             						///< Default mode is CONTINUOUS
static enum STSP_modes DLP_mode = STOP;
static enum STSP_modes rLaser_mode = STOP;
static enum STSP_modes bLaser_mode = STOP;

static unsigned int cycle 				= 0;
static unsigned int cycle_counter 		= 0;
static unsigned int tickcount			= 0;
static unsigned int DLP_counter 		= 0;
static unsigned int exposure_counter 	= 0;
static bool 		timeout_flag 		= 0;


/****************************************************************************/
/* Timer variables block                                                    */
/****************************************************************************/
static enum hrtimer_restart function_timer(struct hrtimer *);
static struct hrtimer htimer;
static ktime_t kt_periode;



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
   if (strncmp(buf,"once",count-1)==0) { mode = ONCE; cycle=0;}   // strncmp() compare with fixed number chars
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
   if ((period>1)&&(period<=100000)){        // Must be 2us or greater
	   exposure = period;                 // Within range, assign to blinkPeriod variable
   }
   cycle = exposure + datawritetime;
   cycle_counter = (1000/TIMER_STAMP)*cycle;
   exposure_counter = (1000/TIMER_STAMP)*exposure;
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
   cycle = exposure + datawritetime;
   cycle_counter = (1000/TIMER_STAMP)*cycle;
   exposure_counter = (1000/TIMER_STAMP)*exposure;
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


static ssize_t mode_show_DLP(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   switch(DLP_mode){
      case STOP:   return sprintf(buf, "stop\n");       // Display the state -- simplistic approach
      case START:    return sprintf(buf, "start\n");
      default:    return sprintf(buf, "LKM Error\n"); // Cannot get here
   }
}
/** @brief A callback function to store the LED mode using the enum above */
static ssize_t mode_store_DLP(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   // the count-1 is important as otherwise the \n is used in the comparison
   if (strncmp(buf,"stop",count-1)==0) { DLP_mode = STOP; }   // strncmp() compare with fixed number chars
   else if (strncmp(buf,"start",count-1)==0) { DLP_mode = START; }
   return count;
}


/** @brief A callback function to store the LED mode using the enum above */
static ssize_t mode_store_rLaser(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   // the count-1 is important as otherwise the \n is used in the comparison
   if (strncmp(buf,"stop",count-1)==0) { rLaser_mode = STOP; }   // strncmp() compare with fixed number chars
   else if (strncmp(buf,"start",count-1)==0) { rLaser_mode = START; }
   return count;
}
static ssize_t mode_show_rLaser(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   switch(rLaser_mode){
      case STOP:   return sprintf(buf, "stop\n");       // Display the state -- simplistic approach
      case START:    return sprintf(buf, "start\n");
      default:    return sprintf(buf, "LKM Error\n"); // Cannot get here
   }
}


/** @brief A callback function to store the LED mode using the enum above */
static ssize_t mode_store_bLaser(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   // the count-1 is important as otherwise the \n is used in the comparison
   if (strncmp(buf,"stop",count-1)==0) { bLaser_mode = STOP; }   // strncmp() compare with fixed number chars
   else if (strncmp(buf,"start",count-1)==0) { bLaser_mode = START; }
   return count;
}
static ssize_t mode_show_bLaser(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   switch(bLaser_mode){
      case STOP:   return sprintf(buf, "stop\n");       // Display the state -- simplistic approach
      case START:    return sprintf(buf, "start\n");
      default:    return sprintf(buf, "LKM Error\n"); // Cannot get here
   }
}

/** Use these helper macros to define the name and access levels of the kobj_attributes
 *  The kobj_attribute has an attribute attr (name and mode), show and store function pointers
 */

static struct kobj_attribute mode_attr = __ATTR(mode, 0660, mode_show, mode_store);
static struct kobj_attribute exposure_attr = __ATTR(exposure, 0660, exposure_show, exposure_store);
static struct kobj_attribute datawritetime_attr = __ATTR(datawritetime, 0660, datawritetime_show, datawritetime_store);
static struct kobj_attribute numframe_attr = __ATTR(numframe, 0660, numframe_show, numframe_store);

static struct kobj_attribute DLP_attr = __ATTR(DLP_mode, 0660, mode_show_DLP, mode_store_DLP);
static struct kobj_attribute rLaser_attr = __ATTR(rLaser_mode, 0660, mode_show_rLaser, mode_store_rLaser);
static struct kobj_attribute bLaser_attr = __ATTR(rLaser_mode, 0660, mode_show_bLaser, mode_store_bLaser);

/** The ebots_attrs[] is an array of attributes that is used to create the attribute group below.
 *  The attr property of the kobj_attribute is used to extract the attribute struct
 */
static struct attribute *ebots_xtrig_attrs[] = {
   &exposure_attr.attr,
   &mode_attr.attr,
   &datawritetime_attr.attr,
   &numframe_attr.attr,
   NULL,
};

static struct attribute *ebots_DLP_attrs[] = {
   &DLP_attr.attr,
   NULL,
};

static struct attribute *ebots_rLaser_attrs[] = {
   &rLaser_attr.attr,
   NULL,
};

static struct attribute *ebots_bLaser_attrs[] = {
   &bLaser_attr.attr,
   NULL,
};

/** The attribute group uses the attribute array and a name, which is exposed on sysfs -- in this
 *  case it is gpio49, which is automatically defined in the ebotsLED_init() function below
 *  using the custom kernel parameter that can be passed when the module is loaded.
 */
static struct attribute_group attr_group_xtrig = {
   .name  = xtrigName,                        // The name is generated in ebotsLED_init()
   .attrs = ebots_xtrig_attrs,                      // The attributes array defined just above
};

static struct attribute_group attr_group_DLP = {
   .name  = DLPtriggerName,                        // The name is generated in ebotsLED_init()
   .attrs = ebots_DLP_attrs,                      // The attributes array defined just above
};

static struct attribute_group attr_group_rLaser = {
   .name  = redLaserName,                        // The name is generated in ebotsLED_init()
   .attrs = ebots_rLaser_attrs,                      // The attributes array defined just above
};

static struct attribute_group attr_group_bLaser = {
   .name  = blueLaserName,                        // The name is generated in ebotsLED_init()
   .attrs = ebots_bLaser_attrs,                      // The attributes array defined just above
};

static struct kobject *ebots_kobj;            /// The pointer to the kobject
static struct task_struct *task;            /// The pointer to the thread task

// function timer
static enum hrtimer_restart function_timer(struct hrtimer * unused)
{
	tickcount ++;
	if (tickcount >= cycle_counter) {tickcount = 0; cycle++;}			// reset tickcount, cycle increment by 1
	timeout_flag = 1; 								// set timeout_flag to 1
	hrtimer_forward_now(& htimer, kt_periode);
    return HRTIMER_RESTART;
}

/** @brief The GPIO xtrig main kthread loop
 *
 *  @param arg A void pointer used in order to pass data to the thread
 *  @return returns 0 if successful
 */
static int siggen_driver(void *arg){
   printk(KERN_INFO "EBOTS: Thread has started running \n");
   hrtimer_start(& htimer, kt_periode, HRTIMER_MODE_REL);
   while(!kthread_should_stop()){           // Returns true when kthread_stop() is called
      set_current_state(TASK_RUNNING);

      if (timeout_flag){
    	  // DLP Pulses
      	  if (tickcount >= DLP_counter)
  		  {
			  // DLP_mode is ST
      		  gpio_set_value(gpio_DLPtrig, DLP_INACTIVE);       // Use the LED state to light/turn off the LED
    	  }
    	  else
    	  {
    		  if (DLP_mode)gpio_set_value(gpio_DLPtrig, DLP_ACTIVE); else gpio_set_value(gpio_DLPtrig, DLP_INACTIVE);
    	  }
    	  // xtrigger Pulses, rLaser, bLaser
    	  if (tickcount >= exposure_counter)
		  {
    		  gpio_set_value(gpio_redLaserEn, rLaser_INACTIVE);
    		  gpio_set_value(gpio_blueLaserEn, bLaser_INACTIVE);
    		  gpio_set_value(gpio_xtrig, XTRIG_INACTIVE);       // Use the LED state to light/turn off the LED
		  }
		  else
		  {
			  if (rLaser_mode) gpio_set_value(gpio_redLaserEn, rLaser_ACTIVE); else gpio_set_value(gpio_redLaserEn, rLaser_INACTIVE);
			  if (bLaser_mode) gpio_set_value(gpio_blueLaserEn, bLaser_ACTIVE); else gpio_set_value(gpio_blueLaserEn, bLaser_INACTIVE);
			  // xtrigger pin with mode ( OFF=0, ONCE=1, CONTINUOUS=2)
			  // Use the LED state to light/turn off the LED
		      switch (mode)
			  {
		      case CONTINUOUS:
		    	  gpio_set_value(gpio_xtrig, XTRIG_ACTIVE);
		    	  break;

		      case OFF:
		    	  gpio_set_value(gpio_xtrig, XTRIG_INACTIVE);        	// XTRIG HIGH --> HIGH
		    	  break;

		      case ONCE:
		    	  if (numframe > cycle-1) // still not complete the pulse generation
		    	  {
		    		  gpio_set_value(gpio_xtrig, XTRIG_ACTIVE);       	// XTRIG HIGH --> LOW
		    	  }
		    	  else
		    	  {
		    		  mode = OFF;
		    		  gpio_set_value(gpio_xtrig, XTRIG_INACTIVE);
		    	  }
		    	  break;
		      default: break;
			  }
		  }
		  timeout_flag = 0;							// clear timeout_flag
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

   sprintf(DLPtriggerName, "DLPtrig%d", gpio_DLPtrig);
   sprintf(redLaserName, "rLaser%d", gpio_redLaserEn);
   sprintf(blueLaserName, "bLaser%d", gpio_blueLaserEn);

//   printk("gpio_xtrig %d \n", gpio_xtrig);
//
//   for (result = 0; result < sizeof(xtrigName); result ++)
//   {
//	   printk(" %d \n", xtrigName[result]);
//   }

   ebots_kobj = kobject_create_and_add("ebots", kernel_kobj->parent); // kernel_kobj points to /sys/kernel
   if(!ebots_kobj){
      printk(KERN_ALERT "EBOTS: failed to create kobject\n");
      return -ENOMEM;
   }
   // add the attributes to /sys/ebots/ -- for example, /sys/ebots/xtrig249/mode
   result = sysfs_create_group(ebots_kobj, &attr_group_xtrig);
   if(result) {
      printk(KERN_ALERT "EBOTS: failed to create sysfs group\n");
      kobject_put(ebots_kobj);                // clean up -- remove the kobject sysfs entry
      return result;
   }

   // add the attributes to /sys/ebots/ -- for example, /sys/ebots/DLPtriggerXXX/mode
   result = sysfs_create_group(ebots_kobj, &attr_group_DLP);
   if(result) {
      printk(KERN_ALERT "EBOTS: failed to create sysfs group\n");
      kobject_put(ebots_kobj);                // clean up -- remove the kobject sysfs entry
      return result;
   }

   // add the attributes to /sys/ebots/ -- for example, /sys/ebots/rLaserXXX/mode
   result = sysfs_create_group(ebots_kobj, &attr_group_rLaser);
   if(result) {
      printk(KERN_ALERT "EBOTS: failed to create sysfs group\n");
      kobject_put(ebots_kobj);                // clean up -- remove the kobject sysfs entry
      return result;
   }

   // add the attributes to /sys/ebots/ -- for example, /sys/ebots/bLaserXXX/mode
   result = sysfs_create_group(ebots_kobj, &attr_group_bLaser);
   if(result) {
      printk(KERN_ALERT "EBOTS: failed to create sysfs group\n");
      kobject_put(ebots_kobj);                // clean up -- remove the kobject sysfs entry
      return result;
   }

   //xtrigOn = true;
   gpio_request(gpio_xtrig, "sysfs");          // gpio_xtrig is 249 by default, request it
   gpio_direction_output(gpio_xtrig, XTRIG_INACTIVE);   // Set the gpio to be in output mode and turn on --> HIGH
   gpio_export(gpio_xtrig, false);  // causes gpio249 to appear in /sys/class/gpio, the second argument prevents the direction from being changed

   //DLPOn = false;
   gpio_request(gpio_DLPtrig, "sysfs");          // gpio_xtrig is 249 by default, request it
   gpio_direction_output(gpio_DLPtrig, DLP_INACTIVE);   // Set the gpio to be in output mode and turn on --> LOW
   gpio_export(gpio_DLPtrig, false);  // causes gpio249 to appear in /sys/class/gpio, the second argument prevents the direction from being changed

   //redLaserOn = false;
   gpio_request(gpio_redLaserEn, "sysfs");          // gpio_xtrig is 249 by default, request it
   gpio_direction_output(gpio_redLaserEn, rLaser_INACTIVE);   // Set the gpio to be in output mode and turn on --> LOW
   gpio_export(gpio_redLaserEn, false);  // causes gpio249 to appear in /sys/class/gpio, the second argument prevents the direction from being changed

   //blueLaserOn = false;
   gpio_request(gpio_blueLaserEn, "sysfs");          // gpio_xtrig is 249 by default, request it
   gpio_direction_output(gpio_blueLaserEn, bLaser_INACTIVE);   // Set the gpio to be in output mode and turn on --> LOW
   gpio_export(gpio_blueLaserEn, false);  // causes gpio249 to appear in /sys/class/gpio, the second argument prevents the direction from being changed

   // This is Timer initialization
   kt_periode = ktime_set(0, TIMER_STAMP); //seconds,nanoseconds
   hrtimer_init (& htimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
   htimer.function = function_timer;

   // counters calculation - applicable in case that TIMER_STAMP < 1000 nanosecond, otherwise calculation need to be revised
   cycle = exposure + datawritetime;
   cycle_counter = (1000/TIMER_STAMP)*cycle;
   DLP_counter = (1000/TIMER_STAMP)*DLP_PULSE_WIDTH;
   exposure_counter = (1000/TIMER_STAMP)*exposure;

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

   // gpio_xtrig release
   gpio_set_value(gpio_xtrig, 0);              // Turn the LED off, indicates device was unloaded
   gpio_unexport(gpio_xtrig);                  // Unexport the Button GPIO
   gpio_free(gpio_xtrig);                      // Free the LED GPIO

   // gpio_DLP release
   gpio_set_value(gpio_DLPtrig, 0);              // Turn the LED off, indicates device was unloaded
   gpio_unexport(gpio_DLPtrig);                  // Unexport the Button GPIO
   gpio_free(gpio_DLPtrig);                      // Free the LED GPIO

   // gpio_rLaser release
   gpio_set_value(gpio_redLaserEn, 0);              // Turn the LED off, indicates device was unloaded
   gpio_unexport(gpio_redLaserEn);                  // Unexport the Button GPIO
   gpio_free(gpio_redLaserEn);                      // Free the LED GPIO

   // gpio_bLaser release
   gpio_set_value(gpio_blueLaserEn, 0);              // Turn the LED off, indicates device was unloaded
   gpio_unexport(gpio_blueLaserEn);                  // Unexport the Button GPIO
   gpio_free(gpio_blueLaserEn);                      // Free the LED GPIO

   // timer cancelation
   hrtimer_cancel(& htimer);

   printk(KERN_INFO "EBOTS: Goodbye from the EBOTS SIGGEN LKM!\n");
}

/// This next calls are  mandatory -- they identify the initialization function
/// and the cleanup function (as above).
module_init(xtrig_siggen_init);
module_exit(xtrig_siggen_exit);
