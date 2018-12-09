#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>


#define CHAR_DEV_NAME "spiproto"
#define SPIPROTO_MAJOR	310

static struct class *spiproto_class;
#ifdef CONFIG_OF
static const struct of_device_id spiproto_dt_ids[] = {
        { .compatible = "WindleDesigns,SPI Proto" },
        {},
};
MODULE_DEVICE_TABLE(of, spiproto_dt_ids);
#endif

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

struct spiproto_data {
	dev_t                   devt;
	struct spi_device 	*spi;
	struct list_head        device_entry;
	struct mutex            buf_lock;
        unsigned                users;
        u8                      *tx_buffer;
        u8                      *rx_buffer;

};


static int spiproto_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int spiproto_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long spiproto_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static struct file_operations spiproto_fops = {
	.owner 		= THIS_MODULE,
	.open 		= spiproto_open,
	.release 	= spiproto_release,
	.unlocked_ioctl = spiproto_ioctl,
};

static int spiproto_probe(struct spi_device *spi)
{
	int status;
	struct spiproto_data *spdata;
	struct device *dev;

	if(spi->dev.of_node && !of_match_device(spiproto_dt_ids, &spi->dev)) {
                dev_err(&spi->dev, "ERROR DT: spiproto listed directly in DT\n");
                WARN_ON(spi->dev.of_node &&
                        !of_match_device(spiproto_dt_ids, &spi->dev));
        }

	spdata = kzalloc(sizeof(*spdata), GFP_KERNEL);
        if(!spdata)
                return -ENOMEM;
	spdata->spi = spi;
	mutex_init(&spdata->buf_lock);
	INIT_LIST_HEAD(&spdata->device_entry);

	
	mutex_lock(&device_list_lock);	
	spdata->devt = MKDEV(SPIPROTO_MAJOR, 1);
        dev = device_create(spiproto_class, &spi->dev, spdata->devt,spdata, "spdata%d", spi->master->bus_num);
        status = PTR_ERR_OR_ZERO(dev);

	if (status == 0) {
                list_add(&spdata->device_entry, &device_list);
        }
	mutex_lock(&device_list_lock);

	if (status == 0)
                spi_set_drvdata(spi, spdata);
        else
                kfree(spdata);

        return status;
	
}

static int spiproto_remove(struct spi_device *spi)
{
	struct spiproto_data      *spdata = spi_get_drvdata(spi);

        spdata->spi = NULL;

        mutex_lock(&device_list_lock);
        list_del(&spdata->device_entry);
        device_destroy(spiproto_class, spdata->devt);
        kfree(spdata);
        mutex_unlock(&device_list_lock);

	return 0;
}

static struct spi_driver spiproto = {
	.probe 	= spiproto_probe,
	.remove = spiproto_remove,
	.driver = {
		.name = "spiproto",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(spiproto_dt_ids),
	},
};

static int __init spiproto_init(void)
{
	int status;

	printk("func:%s=>line:%d\n", __FUNC__,__LINE__);
	status = register_chrdev(SPIPROTO_MAJOR, "spiproto", &spiproto_fops);
        if (status < 0)
                return status;
	
	printk("func:%s=>line:%d\n", __FUNC__,__LINE__);
	spiproto_class = class_create(THIS_MODULE, "spiproto");
        if (IS_ERR(spiproto_class)) {
                unregister_chrdev(SPIPROTO_MAJOR, spiproto.driver.name);
                return PTR_ERR(spiproto_class);
        }
	
	printk("func:%s=>line:%d\n", __FUNC__,__LINE__);
		
	status = spi_register_driver(&spiproto);
	if(status < 0) {
		class_destroy(spiproto_class);
                unregister_chrdev(SPIPROTO_MAJOR, spiproto.driver.name);
	}

	return status;
}

static void __exit spiproto_exit(void)
{
	spi_unregister_driver(&spiproto);
	class_destroy(spiproto_class);
        unregister_chrdev(SPIPROTO_MAJOR, spiproto.driver.name);
}

module_init(spiproto_init);
module_exit(spiproto_exit);

MODULE_AUTHOR("vasubabu, kandimalla.vasubabu@gmail.com");
MODULE_DESCRIPTION("Custom SPI protocol");
MODULE_LICENSE("GPL");

