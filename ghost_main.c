#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <linux/uaccess.h>              //copy_to/from_user()
#include <linux/ioctl.h>

#define GHOST _IOW('a','a',struct ghost_map*)
#define UNGHOST _IOW('a','b', int)

struct ghost_map 
{
    // usermode members
    int pid;
    int count_page;
    unsigned long *page_addr;

    // kernelmode members
    pte_t *to_restore;
    struct list_head list;
};

// our list of ghost_map
struct list_head lh;

dev_t dev = 0;
static struct class *dev_class;
static struct cdev etx_cdev;
 
static int  __init etx_driver_init(void);
static void __exit etx_driver_exit(void);
static long etx_ioctl(struct file *, unsigned int, unsigned long );
static long map_ghost(struct ghost_map*);
static long restore_ghost(struct ghost_map*);
static pte_t map_ghost_page(
    struct mm_struct *, 
    struct mm_struct *, 
    unsigned long);
static int restore_ghost_page(
    struct mm_struct * t_mm, 
    unsigned long page_vaddr,
    pte_t to_restore);
 
static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .unlocked_ioctl = etx_ioctl,
};

static pte_t map_ghost_page(
    struct mm_struct * c_mm, 
    struct mm_struct * t_mm, 
    unsigned long page_vaddr)
{
    pgd_t *pgd, *r_pgd;

    p4d_t *p4d, *r_p4d;
    pud_t *pud, *r_pud;
    pmd_t *pmd, *r_pmd;
    pte_t *pte, *r_pte;
    pte_t ret;

    pgd = pgd_offset(t_mm, page_vaddr);
    r_pgd = pgd_offset(c_mm, page_vaddr);

    p4d = p4d_offset(pgd, page_vaddr);
    r_p4d = p4d_offset(r_pgd, page_vaddr);

    pud = pud_offset(p4d, page_vaddr);
    r_pud = pud_offset(r_p4d, page_vaddr);

    pmd = pmd_offset(pud, page_vaddr);
    r_pmd = pmd_offset(r_pud, page_vaddr);

    // Get the original PAGE and return it ( to be restored later )
    pte = pte_offset_kernel(pmd, page_vaddr);
    ret = *pte;

    // This is the page we inject
    r_pte = pte_offset_kernel(r_pmd, page_vaddr);

    set_pte(pte, __pte(pte_val(*r_pte)));

    return ret;

}

static int restore_ghost_page(
    struct mm_struct * t_mm, 
    unsigned long page_vaddr,
    pte_t to_restore)
{
    pgd_t *pgd;

    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    pgd = pgd_offset(t_mm, page_vaddr);

    p4d = p4d_offset(pgd, page_vaddr);

    pud = pud_offset(p4d, page_vaddr);

    pmd = pmd_offset(pud, page_vaddr);

    pte = pte_offset_kernel(pmd, page_vaddr);

    set_pte(pte, __pte(pte_val(to_restore)));

    return 0;
}

static long map_ghost(struct ghost_map* gm)
{
    struct mm_struct * target_mm = 0; 
    struct task_struct* task_list;
    int i;

    // Find CR3 for the target process
    for (
        task_list = &init_task;
        (task_list = next_task(task_list)) != &init_task;)
    {
        if (task_list->pid == gm->pid)
        {
            if (task_list->active_mm)
            {
                target_mm = task_list->active_mm;
            }
            else
            {
                printk(KERN_ERR "[GHOST] is %d a process ?\n",gm->pid);
                return -1;
            }
        }
    }

    // assert we found CR3
    if (!target_mm)
    {
        printk(KERN_ERR "[GHOST] is %d a process ?\n",gm->pid);
        return -1;
    }

    // Ghosting each page !
    for (i = 0 ; i < gm->count_page ; i++)
    {
        // Saved for future restore
        gm->to_restore[i] = 
            map_ghost_page(current->active_mm, target_mm, gm->page_addr[i]);

        printk(
            KERN_INFO "[GHOST] ghosting %lx in %d\n", 
            gm->page_addr[i], gm->pid);

    }

    // invalidate cache
    __flush_tlb_all();

    return 0;
}

static long restore_ghost(struct ghost_map* gm)
{
    struct mm_struct * target_mm = 0; 
    struct task_struct* task_list;
    int i;

    // Find CR3 for the target process
    for (
        task_list = &init_task;
        (task_list = next_task(task_list)) != &init_task;)
    {
        if (task_list->pid == gm->pid)
        {
            if (task_list->active_mm)
            {
                target_mm = task_list->active_mm;
            }
            else
            {
                printk(KERN_ERR "[GHOST] is %d a process ?\n",gm->pid);
                return -1;
            }
        }
    }

    // assert we found CR3
    if (!target_mm)
    {
        printk(KERN_ERR "[GHOST] is %d a process ?\n",gm->pid);
        return -1;
    }

    // Restore each page that were previously ghosted
    for (i = 0 ; i < gm->count_page ; i++)
    {
        // Saved for restore
        restore_ghost_page(target_mm, gm->page_addr[i], gm->to_restore[i]);
        printk(
            KERN_INFO "[GHOST] restoring %lx in %d\n", 
            gm->page_addr[i], gm->pid);
    }

    // We need this to invalidate cache
    __flush_tlb_all();

    return 0;
}
 
static long etx_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct ghost_map *argg;
    struct ghost_map *to_map, *entry, *to_remove; 
    struct list_head *ptr;
    unsigned long* tmp_addr;
    int pid_to_remove;
    switch(cmd) 
    {
        case GHOST:
            // The user argument is a struct ghost_map
            argg = (struct ghost_map*)arg;

            // Allocating the struct for ghost informations
            to_map = 
                kmalloc(GFP_KERNEL | GFP_ATOMIC, sizeof(struct ghost_map));

            // Copying user infos
            // pid
            copy_from_user(&to_map->pid, &argg->pid, sizeof(int));
            // number of pages to ghost
            copy_from_user(&to_map->count_page, &argg->count_page, sizeof(int));
            // address of the array of page to ghost
            copy_from_user(&tmp_addr, &argg->page_addr, sizeof(unsigned long*));

            // allocating the same struct in kernel
            to_map->page_addr = 
                kmalloc(
                    GFP_KERNEL | GFP_ATOMIC, 
                    sizeof(unsigned long) * to_map->count_page);

            // in kernel we need to store the PAGE that will be restored later
            // so allocate an array for these pages
            to_map->to_restore= 
                kmalloc(
                    GFP_KERNEL | GFP_ATOMIC, 
                    sizeof(pte_t) * to_map->count_page);

            printk(
                KERN_INFO 
                "[GHOST] PID = %d, page_count = %d\n",
                to_map->pid,
                to_map->count_page);

            // Copy the array of page to ghost
            copy_from_user(
                to_map->page_addr, 
                tmp_addr, 
                to_map->count_page * sizeof(unsigned long));

            // Ghost it
            map_ghost(to_map);

            // Add the structure to the linked list
            list_add(&to_map->list, &lh);

            break;

         case UNGHOST:
            // user argument is a pid
            to_remove = NULL;
            pid_to_remove = arg;
            
            printk(
                KERN_INFO "[GHOST] restoring pid = %d\n",
                pid_to_remove);

            // find the struct in the linked list using pid
            list_for_each(ptr, &lh)
            {
                entry = list_entry(ptr, struct ghost_map, list);
                if ( entry->pid == pid_to_remove)
                {
                    to_remove = entry;
                    break;
                }
            }

            // if we found the entry, unghost it
            if (to_remove)
            {
                restore_ghost(to_remove);
            }
            else 
            {
                printk(
                    KERN_ERR "[GHOST] Can't find ghost_map for process %d",
                    pid_to_remove);
                return -1;
            }

            // free all the structs we allocated
            list_del(&to_remove->list);
            kfree(to_remove->page_addr);
            kfree(to_remove->to_restore);
            kfree(to_remove);

            break;
    }
    return 0;
}
 
static int __init etx_driver_init(void)
{
    /* Allocating Major number */
    if((alloc_chrdev_region(&dev, 0, 1, "ghost_dev")) < 0)
    {
        printk(KERN_INFO "[GHOST] Cannot allocate major number\n");
        return -1;
    }
    printk(
        KERN_INFO "[GHOST] Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));
 
    /* Creating cdev structure */
    cdev_init(&etx_cdev, &fops);

    /* Adding character device to the system */
    if((cdev_add(&etx_cdev, dev, 1)) < 0)
    {
        printk( KERN_INFO "[GHOST] Cannot add the device to the system\n" );
        goto r_class;
    }

    /*Creating struct class*/
    if((dev_class = class_create(THIS_MODULE, "ghost_class")) == NULL)
    {
        printk(KERN_INFO "[GHOST] Cannot create the struct class\n");
        goto r_class;
    }

    /* Creating device */
    if((device_create(dev_class, NULL, dev, NULL, "ghost_device")) == NULL)
    {
        printk(KERN_INFO "[GHOST] Cannot create the Device 1\n");
        goto r_device;
    }

    INIT_LIST_HEAD(&lh);

    printk(KERN_INFO "[GHOST] Device driver inserted\n");
    return 0;

r_device:
    class_destroy(dev_class);
r_class:
    unregister_chrdev_region(dev,1);
    return -1;
}
 
static void __exit etx_driver_exit(void)
{
    device_destroy(dev_class,dev);
    class_destroy(dev_class);
    cdev_del(&etx_cdev);
    unregister_chrdev_region(dev, 1);
    printk(KERN_INFO "[GHOST] Device driver removed\n");
}
 
module_init(etx_driver_init);
module_exit(etx_driver_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("stan");
MODULE_DESCRIPTION("ghost");
MODULE_VERSION("1");
