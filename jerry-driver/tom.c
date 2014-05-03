//#define MODULE
#define LINUX
//#define __KERNEL__
#if defined(CONFIG_MODVERSIONS) && ! defined(MODVERSIONS)
   #include <config/modversions.h>
   #define MODVERSIONS
#endif


#include <linux/kernel.h>       /* We're doing kernel work */
#include <linux/module.h>       /* Specifically, a module */
#include <linux/fs.h>
#include <linux/interrupt.h>    /* We want an interrupt */
#include <linux/init.h>		/* Needed for the macros */
#include <asm/errno.h>
#include <asm/uaccess.h>  /* for put_user */
#include <linux/workqueue.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/poll.h>


#define MOUSE_IRQ 21
#define MY_WORK_QUEUE_NAME "WQsched.c"

#define MOUSE_MEMORY_BASE 0x300
#define READ_X() inb(MOUSE_MEMORY_BASE)
#define READ_Y() inb(MOUSE_MEMORY_BASE+1)
#define READ_BUTTONS() (unsigned char)inb(MOUSE_MEMORY_BASE+2)


#define SUCCESS 0
#define DEVICE_NAME "/dev/jerry" /* Dev name as it appears in /proc/devices   */


// .h


int init_module(void);
void cleanup_module(void);
static int mouse_open(struct inode *, struct file *);
static ssize_t mouse_read(struct file *, char *, size_t, loff_t *);
static ssize_t mouse_write(struct file *, const char *, size_t, loff_t *);
static int mouse_release(struct inode *, struct file *);
static unsigned int mouse_poll (struct file *, struct poll_table_struct *);
void irq_handler(int irq, void *dev_id, struct pt_regs *regs);

static struct file_operations fops = {
  .read = mouse_read,
  .write = mouse_write,
  .open = mouse_open,
  .release = mouse_release,
  .poll = mouse_poll
};



typedef struct tom {
	int is_open;
	int users;
	int major;
	
}TomControlBlock;

typedef struct jerry {
	wait_queue_head_t wait;
	spinlock_t lock;
	char delta_x ;
	char delta_y ;
	char delta_t ;
	time_t last_record;
	bool something_happened;
} Jerry;



// Module configuration 

static struct workqueue_struct *my_workqueue;

TomControlBlock tom = { false, 0, 0 };
Jerry jerry = {
	.delta_x = 0 , 
	.delta_y = 0 , 
	.delta_t =0 , 
	.last_record = 0 , 
	.something_happened = false , 
	.lock = __SPIN_LOCK_UNLOCKED(.lock), 
	
};


int __init init_module(void) {
	//jerry.wait = init_waitqueue_head(&jerry.wait)
	printk(KERN_INFO "Loading TOM Mouse differential meter\n");
	my_workqueue = create_workqueue(MY_WORK_QUEUE_NAME);

	tom.major = register_chrdev(0, DEVICE_NAME, &fops);
	
	printk(KERN_INFO "'mknod %s c %d 0'.\n", DEVICE_NAME, tom.major);
	printk(KERN_INFO "Subscribing user to TOM Mouse differential meter\n");
	

	return SUCCESS;
}

void __exit cleanup_module(void) {
	printk(KERN_INFO "Goodbye, world 3\n");
	unregister_chrdev(tom.major, DEVICE_NAME);

}

// Module definition


static int mouse_open(struct inode * inode, struct file * file) {
	int error = 0;
	if (!tom.is_open) {
		printk(KERN_INFO "Requesting IRQ");
		error = request_irq(MOUSE_IRQ, (void*)irq_handler, IRQF_SHARED, "mouse_based_differential_measure",(void *)(irq_handler));
		
		if (error != SUCCESS) {
			printk(KERN_INFO "Error requesting IRQ" );
			return error;
		}
		printk(KERN_INFO "Successful requesting IRQ" );
		tom.is_open = true;
	}
	tom.users ++;	
	
	try_module_get(THIS_MODULE);
	printk(KERN_INFO "Device open" );
	return SUCCESS;	
	
}

static int mouse_release(struct inode * inode, struct file * file) {
	
	tom.users --;
	if ( tom.users == 0 ){
		free_irq(MOUSE_IRQ, NULL);
		tom.is_open = false;
	}
	module_put(THIS_MODULE);

	return SUCCESS;
}

static ssize_t mouse_read(struct file * file , char * buffer, size_t count, loff_t * ppos) {

	char dx, dy;
	time_t dt;
	unsigned long flags;

	// This read a size-enough buffer. This size is the needed by a 2chars+1 long. So you can use a structure casted to char*.
	if (count != sizeof(char)*2 + sizeof(time_t)){
		return -EINVAL;
	}


	// Waiting in a loop
	while (!jerry.something_happened){

		// If nodelay was specified, we return a ´try later´ 			
		if(file->f_flags&O_NDELAY) return -EAGAIN;
			
		// Sleep on a ´queue for waiting´. Strange but works. 			
		wait_event_interruptible (jerry.wait, jerry.something_happened); 

		// If there is any signal needed to be processed, we go back with error.
		if(signal_pending(current)) return -ERESTARTSYS;
	}	

	// Enter into critic memory in between handler for irq and reader thread. 
	spin_lock_irqsave(&jerry.lock, flags);


	// Get dx/dy/dt 
	dx = jerry.delta_x;
	dy = jerry.delta_y;
	dt = jerry.delta_t;

	// We just discount the amount for the case that the handler added something in between our assignations. 
	jerry.delta_x -= dx; 
	jerry.delta_y -= dy; 
	jerry.delta_t -= dt; 
	
	// Again, if the movements are 0, there is nothing to process, nothing has happened.
	if  ( jerry.delta_y == 0 && jerry.delta_x == 0) jerry.something_happened = false;

	// Out of critic memory.
	spin_unlock_irqrestore(&jerry.lock, flags);

	// The buffer is in the user-memory world. We cannot just assign data. We need to go through kernel with put_user.

	if (put_user((char) dx, buffer)) return -EFAULT;
	if (put_user((char) dy, buffer + sizeof(char) )) return -EFAULT;
	if (put_user((time_t) dt, buffer + sizeof(char) * 2)) return -EFAULT;


	// 
	return count;
}



void irq_handler(int irq, void *dev_id, struct pt_regs *regs) {
	char dx, dy;

	dx = READ_X();
	dy = READ_Y();

	if (dx || dy ) {

		struct timeval spec;

		do_gettimeofday(&spec);

		// Transform the time accuracy to milliseconds 
		time_t now = spec.tv_sec;
		now  *= 1000000000;
		now += spec.tv_usec;
		now /= 1000000;


		spin_lock(&jerry.lock);
		jerry.delta_x = dx;
		jerry.delta_y = dy;
		jerry.delta_t = now - (jerry.last_record == 0 ? now : jerry.last_record) ;
		jerry.last_record = now;
		jerry.something_happened = true;
		spin_unlock(&jerry.lock);
		wake_up_interruptible(&jerry.wait);
	}
		
}


static ssize_t mouse_write(struct file * file , const char * buffer, size_t count, loff_t * ppos) {
	return -EINVAL;
}

static unsigned int mouse_poll (struct file * file, struct poll_table_struct * table) {
	poll_wait(file, &jerry.wait, table);
	return jerry.something_happened ? POLLIN | POLLRDNORM 
					: 0;	
}






MODULE_LICENSE("GPL");











