#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/of_irq.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/delay.h>


#define spiproto_MAJOR	241
#define spiproto_STB	70
#define spiproto_RESET	168
#define spiproto_SLEEP	167

#define  DEBUG_STEPEER1 0 

struct spiproto_data {
	dev_t devt;
	struct device 	*dev;
	struct class	*spiproto_class;
};


static char tx_buf[2];
static struct spi_device *spiproto_spi;
static struct mutex spiproto_lock;

static int spiproto_open(struct inode *inode, struct file *file)
{
	int ret = 0;
	return ret;
}

static int spiproto_release(struct inode *inode, struct file *file)
{
	int ret = 0;
	return ret;
}

static ssize_t spiproto_read(struct file *file, char __user *buf, size_t len, loff_t *f_ops)
{
	int ret = 0;
	return ret;
}

static ssize_t spiproto_write(struct file *file, const char __user *buf, size_t len, loff_t *f_ops)
{
	int ret = 0;	
	return ret;
}

static long spiproto_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	

	return 0;
}
 
static struct file_operations spiproto_fops = {
	.open 		= spiproto_open,
	.release	= spiproto_release,
	.read		= spiproto_read,
	.write		= spiproto_write,
	.unlocked_ioctl = spiproto_ioctl,
};

static const struct of_device_id spiproto_spi_dt_ids[] = {
        { .compatible = "spiproto"},
        {}
};
MODULE_DEVICE_TABLE(of, spiproto_spi_dt_ids);

static int spiproto_probe(struct spi_device *spi)
{
	struct spiproto_data *spiproto_data;
	int ret;

#if DEBUG_STEPEER1
	printk("func:%s==>line:%d\n", __FUNCTION__,__LINE__);	
#endif

	if(spi->dev.of_node && !of_match_device(spiproto_spi_dt_ids, &spi->dev)) {
                dev_err(&spi->dev, "ERROR DT: spiproto listed directly in DT\n");
                WARN_ON(spi->dev.of_node &&
                        !of_match_device(spiproto_spi_dt_ids, &spi->dev));
        }

	spiproto_data = kzalloc(sizeof(*spiproto_data), GFP_KERNEL);
	if(!spiproto_data)
		return -ENOMEM;
	
	spiproto_spi=spi;
	spiproto_spi->mode = SPI_MODE_0 | SPI_CS_HIGH;
	
	mutex_init(&spiproto_lock);

	

	ret = register_chrdev(spiproto_MAJOR, "spiproto", &spiproto_fops);
    	if (ret < 0)
    		return ret;

        spiproto_data->spiproto_class = class_create(THIS_MODULE, "spiproto");

        if(IS_ERR(spiproto_data->spiproto_class)) 
	{
		ret = -1; 
		goto err;
        }

	spiproto_data->devt = MKDEV(spiproto_MAJOR, 0);
        spiproto_data->dev = device_create(spiproto_data->spiproto_class, &spi->dev, spiproto_data->devt, spiproto_data, "spiproto");

        if(IS_ERR(spiproto_data->dev))
	{
                ret = -ENODEV;
		goto err1;
	}
		
	ret = spi_setup(spi);
        if (ret < 0) 
	{
                dev_err(&spi->dev, "SPI setup wasn't successful %d", ret);
                ret = -ENODEV;
        }

	spi_set_drvdata(spi, spiproto_data);
	tx_buf[0] = 0x00;
	tx_buf[1] = 0x00;
	
	
	return ret;
	
err1:
	class_destroy(spiproto_data->spiproto_class);
    	unregister_chrdev(spiproto_MAJOR, "spiproto");
	kfree(spiproto_data);
	return ret;

err:
	unregister_chrdev(spiproto_MAJOR, "spiproto");
	kfree(spiproto_data);
	return ret;

}

static int spiproto_remove(struct spi_device *spi)
{
	struct spiproto_data *spiproto_data	= spi_get_drvdata(spi);

        device_destroy(spiproto_data->spiproto_class, spiproto_data->devt);
	class_destroy(spiproto_data->spiproto_class);
        unregister_chrdev(spiproto_MAJOR, "spiproto");
	kfree(spiproto_data);
	return 0;
}

static struct spi_driver spiproto_spi_driver = {
	.probe 		= spiproto_probe,
	.remove 	= spiproto_remove,
	.driver 	= {
		.name 	= "spiproto",
		.owner	= THIS_MODULE,
	},
};

static int __init spiproto_init(void)
{
	int ret = -1;
#if DEBUG_STEPEER1
	printk("func:%s==>line:%d\n", __FUNCTION__,__LINE__);	
#endif
	ret = spi_register_driver(&spiproto_spi_driver);
        if (ret < 0) {
		printk("spiproto SPi driver registration failed\n");
        }
	
	return ret;
}

module_init(spiproto_init);

static void __exit spiproto_exit(void)
{
	spi_unregister_driver(&spiproto_spi_driver);
}

module_exit(spiproto_exit);

MODULE_AUTHOR("sivakoti, sivakoti.danda@cyient.com");
MODULE_DESCRIPTION(" spiproto driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:spiproto");

