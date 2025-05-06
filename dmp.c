#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/device-mapper.h>

static struct kobject* dmp_kobj;
static uint64_t read_ops = 0;
static uint64_t write_ops = 0;
static uint64_t read_sz = 0;
static uint64_t write_sz = 0;

struct dmp_dm_target {
	struct dm_dev* dev;
	sector_t start;
};

static ssize_t volumes_show(struct kobject* kobj, struct kobj_attribute* attr, char* buf) {
	uint64_t ravg = read_ops == 0 ? 0 : read_sz/read_ops;
	uint64_t wavg = write_ops == 0 ? 0 : write_sz/write_ops;
	uint64_t rsz = read_sz;
	uint64_t wsz = write_sz;
	while ((uint64_t)0xFFFFFFFFFFFFFFFF-rsz < wsz) {
		rsz -= ravg;
		wsz -= wavg;
	}
	uint64_t tops = read_ops;
	if ((uint64_t)0xFFFFFFFFFFFFFFFF - tops < write_ops) {
		tops = 0xFFFFFFFFFFFFFFFF;
	}
	tops += write_ops;
	return sprintf(buf, "read:\n\treqs: %llu\n\tavg size: %llu\nwrite:\n\treqs: %llu\n\tavg size: %llu\ntotal:\n\treqs: %llu\n\tavg size: %llu\n", read_ops, ravg, write_ops, wavg, tops, tops == 0 ? 0 : (rsz+wsz)/tops);
}

static  ssize_t volumes_store(struct kobject* kobj, struct kobj_attribute* attr, const char* buf, size_t count) {
	return count;
}

static int dmp_map(struct dm_target* ti, struct bio* bio) {
	struct dmp_dm_target* ddt = (struct dmp_dm_target*)ti->private;
	bio_set_dev(bio, ddt->dev->bdev);
	bio->bi_iter.bi_sector += ddt->start;
	printk(KERN_CRIT "\nNew request\n");
	if (bio_op(bio) == REQ_OP_READ && bio_data_dir(bio) == READ) {
		printk(KERN_CRIT "\nRead\n");
		read_ops++;
		if (read_ops == 0) {
			read_ops = 0xFFFFFFFFFFFFFFFF;
		}
		uint64_t req = bio->bi_iter.bi_size;
		if ((uint64_t)0xFFFFFFFFFFFFFFFF-read_sz >= req) {
			read_sz += req;
		}
		else {
			uint64_t avg = read_sz/read_ops;
			uint64_t del = (((uint64_t)0xFFFFFFFFFFFFFFFF-req)/avg)*avg;
			read_sz -= del;
			read_sz += req;
		}
	}
	else if (bio_has_data(bio) && bio_data_dir(bio) == WRITE) {
		write_ops++;
		if (write_ops == 0) {
			write_ops = 0xFFFFFFFFFFFFFFFF;
		}
		uint64_t req = bio->bi_iter.bi_size;
		if ((uint64_t)0xFFFFFFFFFFFFFFFF-write_sz >= req) {
			write_sz += req;
		}
		else {
			uint64_t avg = write_sz/write_ops;
			uint64_t del = (((uint64_t)0xFFFFFFFFFFFFFFFF-req+avg-1)/avg)*avg;
			write_sz -= del;
			write_sz += req;
		}
	}
	submit_bio(bio);
	return DM_MAPIO_SUBMITTED;
}

static int dmp_ctr(struct dm_target* ti, unsigned int argc, char** argv) {
	struct dmp_dm_target* ddt;
	if (argc != 1) {
		printk(KERN_CRIT "\n Invalid number of arguments\n");
		ti->error = "Invalid arguments count";
		return -EINVAL;
	}
	ddt = kmalloc(sizeof(struct dmp_dm_target), GFP_KERNEL);
	if (ddt == NULL) {
		printk(KERN_CRIT "\nUnable to allocate memory\n");
		ti->error = "dmp-constructor: Cannot allocate linear context";
		return -ENOMEM;
	}
	ddt->start = (sector_t)0;
	if (dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &ddt->dev)) {
		ti->error = "dmp-constructor: Device lookup failed";
		goto bad;
	}
	ti->private = ddt;
	printk(KERN_CRIT "\n>>exiting dmp constructor\n");
	return 0;
bad:
	kfree(ddt);
	printk(KERN_CRIT "\n>>error occured while running dmp_ctr\n");
	return -EINVAL;
}

static void dmp_dtr(struct dm_target* ti) {
	struct dmp_dm_target* ddt = (struct dmp_dm_target*) ti->private;
	dm_put_device(ti, ddt->dev);
	kfree(ddt);
	printk(KERN_CRIT "\n>>exiting dmp destructor\n");
}

static struct target_type dmp = {
	.name = "dmp",
	.version = {1,0,0},
	.module = THIS_MODULE,
	.ctr = dmp_ctr,
	.dtr = dmp_dtr,
	.map = dmp_map,
};

static struct kobj_attribute volumes_attr = __ATTR(volumes, 0664, volumes_show, volumes_store);

static int init_dmp(void) {
	dmp_kobj = kobject_create_and_add("stat", &THIS_MODULE->mkobj.kobj);
	if (!dmp_kobj) {
		printk(KERN_CRIT "\nError when creating kobject\n");
		return -ENOMEM;
	}
	sysfs_create_file(dmp_kobj, &volumes_attr.attr); 
	int result;
	result = dm_register_target(&dmp);
	if (result < 0) {
		printk(KERN_CRIT "\nError while registering target\n");
	}
	return 0;
}

static void cleanup_dmp(void) {
	if (dmp_kobj) {
		sysfs_remove_file(dmp_kobj, &volumes_attr.attr);
		kobject_put(dmp_kobj);
		dmp_kobj = NULL;
	}
	dm_unregister_target(&dmp);
}

module_init(init_dmp);
module_exit(cleanup_dmp);
MODULE_LICENSE("GPL");
