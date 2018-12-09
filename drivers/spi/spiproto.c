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
#include <linux/spi/spiproto.h>
#include <linux/uaccess.h>


#define CHAR_DEV_NAME "spiproto"
#define SPIPROTO_MAJOR	210

/* Commands 
 * ECG
 * PM1R, PM1W
 *
 * PRINT
 * PM2R, PM2W
 * 
 * BP
 * PM3R, PM3W
 *
 * SPO2
 * PM4R, PM4W
 * 
 * BAT
 * SY1R, SY1W
 *
 * Power 
 * SY2R, SY2W
 * 
 * Reserved
 * SY3R, SY3W
 */

#define ECG_PAR_P	0x50
#define ECG_PAR_M	0x4D

#define ECG_PAR_1	0x31
#define ECG_PAR_2	0x32
#define ECG_PAR_3	0x33
#define ECG_PAR_4	0x34

#define ECG_PAR_R	0x52
#define ECG_PAR_W	0x57

#define ECG_PAR_S	0x53
#define ECG_PAR_Y	0x59



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

static unsigned bufsize = 32;

struct spiproto_data {
	dev_t                   devt;
	spinlock_t              spi_lock;
	struct spi_device 	*spi;
	struct list_head        device_entry;
	struct mutex            buf_lock;
        unsigned                users;
        u8                      tx_buffer[32];
        u8                      rx_buffer[32];

};

static int spiproto_open(struct inode *inode, struct file *filp)
{
	struct spiproto_data *spdata;	
	int status = -ENXIO;

		
	mutex_lock(&device_list_lock);
	list_for_each_entry(spdata, &device_list, device_entry) {
                if (spdata->devt == inode->i_rdev) {
                        status = 0;
                        break;
                }
        }
#if 0
	if (!spdata->tx_buffer) {
	printk("func:%s=>line:%d\n", __FUNCTION__,__LINE__);
                spdata->tx_buffer = kmalloc(bufsize, GFP_KERNEL);
                if (!spdata->tx_buffer) {
                                dev_dbg(&spdata->spi->dev, "open/ENOMEM\n");
                                status = -ENOMEM;
                        goto err_find_dev;
                        }
                }
	printk("func:%s=>line:%d\n", __FUNCTION__,__LINE__);

        if (!spdata->rx_buffer) {
	printk("func:%s=>line:%d\n", __FUNCTION__,__LINE__);
                spdata->rx_buffer = kmalloc(bufsize, GFP_KERNEL);
                if (!spdata->rx_buffer) {
                        dev_dbg(&spdata->spi->dev, "open/ENOMEM\n");
                        status = -ENOMEM;
                        goto err_alloc_rx_buf;
                }
        }
#endif

        spdata->users++;
        filp->private_data = spdata;
        nonseekable_open(inode, filp);

        mutex_unlock(&device_list_lock);

//err_alloc_rx_buf:
        //kfree(spdata->tx_buffer);
        //spdata->tx_buffer = NULL;
err_find_dev:
        mutex_unlock(&device_list_lock);
        return status;
}

static int spiproto_release(struct inode *inode, struct file *filp)
{
	struct spiproto_data	*spdata; 
        int                     status = 0;

        mutex_lock(&device_list_lock);
        spdata = filp->private_data;
        filp->private_data = NULL;

        /* last close? */
        spdata->users--;
        if (!spdata->users) {
                int             dofree;

                //kfree(spdata->tx_buffer);
                //spdata->tx_buffer = NULL;

                //kfree(spdata->rx_buffer);
                //spdata->rx_buffer = NULL;

		spin_lock_irq(&spdata->spi_lock);
                dofree = (spdata->spi == NULL);
		spin_unlock_irq(&spdata->spi_lock);

                if (dofree)
                        kfree(spdata);
        }
        mutex_unlock(&device_list_lock);

        return status;
}

static ssize_t spiproto_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct spiproto_data      *spdata;
        ssize_t                 status = 0;

        /* chipselect only toggles at start or end of operation */
        if (count > bufsize)
                return -EMSGSIZE;

	spdata = filp->private_data;

	mutex_lock(&spdata->buf_lock);
        unsigned long   missing;

	//printk("rx_buffer[0]:%d\n", spdata->rx_buffer[0]);
	//printk("rx_buffer[1]:%d\n", spdata->rx_buffer[1]);
	//printk("rx_buffer[2]:%d\n", spdata->rx_buffer[2]);
	//printk("rx_buffer[3]:%d\n", spdata->rx_buffer[3]);

        missing = copy_to_user(buf, spdata->rx_buffer, count);
     	status = count - missing;

	mutex_unlock(&spdata->buf_lock);
	
	return status;
}

static int spiproto_message(struct spiproto_data *spdata, struct spi_device *spi)
{
	struct spi_transfer     t = {
		.tx_buf         = spdata->tx_buffer,
                .len            = bufsize,
        	.rx_buf         = spdata->rx_buffer,
                .len            = bufsize,
        };
#if 0
	struct spi_transfer    r = {
        	.rx_buf         = spdata->rx_buffer,
                .len            = bufsize,

	};
#endif
        struct spi_message      m;

        spi_message_init(&m);
        spi_message_add_tail(&t, &m);
       // spi_message_add_tail(&r, &m);
        return spi_sync(spi, &m);
	
}

static long spiproto_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int                     err = 0;
	int                     ret = 0;
	struct spiproto_data      *spdata;
        struct spi_device       *spi;
        //u32                     tmp;
        //unsigned                n_ioc;
        //struct spi_ioc_transfer *ioc;
	    //uint8_t data[32];


	/* Check type and command number */
        if (_IOC_TYPE(cmd) != SPIPROTO_IOC_MAGIC)
                return -ENOTTY;

        /* Check access direction once here; don't repeat below.
         * IOC_DIR is from the user perspective, while access_ok is
         * from the kernel perspective; so they look reversed.
         */
        if (_IOC_DIR(cmd) & _IOC_READ)
                err = !access_ok(VERIFY_WRITE,
                                (void __user *)arg, _IOC_SIZE(cmd));
        if (err == 0 && _IOC_DIR(cmd) & _IOC_WRITE)
                err = !access_ok(VERIFY_READ,
                                (void __user *)arg, _IOC_SIZE(cmd));
        if (err)
                return -EFAULT;

        spdata = (struct spiproto_data      *)filp->private_data;
	spin_lock_irq(&spdata->spi_lock);
        spi = spi_dev_get(spdata->spi);
        spin_unlock_irq(&spdata->spi_lock);

	if (spi == NULL)
                return -ESHUTDOWN;

	mutex_lock(&spdata->buf_lock);
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0) {
                dev_err(&spi->dev, "SPI setup wasn't successful %d", ret);
                ret = -ENODEV;
        }
	memset(spdata->tx_buffer, 0, 32);
	switch(cmd) {
		case SPIPROTO_IOC_ECG_R:
			spdata->tx_buffer[0] = ECG_PAR_P;
			spdata->tx_buffer[1] = ECG_PAR_M;
			spdata->tx_buffer[2] = ECG_PAR_1;
			spdata->tx_buffer[3] = ECG_PAR_R;
			break;
		case SPIPROTO_IOC_ECG_W:
			spdata->tx_buffer[0] = ECG_PAR_P;
			spdata->tx_buffer[1] = ECG_PAR_M;
			spdata->tx_buffer[2] = ECG_PAR_1;
			spdata->tx_buffer[3] = ECG_PAR_W;
			break;
		case SPIPROTO_IOC_PRINT_R:
			spdata->tx_buffer[0] = ECG_PAR_P;
			spdata->tx_buffer[1] = ECG_PAR_M;
			spdata->tx_buffer[2] = ECG_PAR_2;
			spdata->tx_buffer[3] = ECG_PAR_R;
			break;
		case SPIPROTO_IOC_PRINT_W:
			spdata->tx_buffer[0] = ECG_PAR_P;
			spdata->tx_buffer[1] = ECG_PAR_M;
			spdata->tx_buffer[2] = ECG_PAR_2;
			spdata->tx_buffer[3] = ECG_PAR_W;
			break;
		case SPIPROTO_IOC_BP_R:
			spdata->tx_buffer[0] = ECG_PAR_P;
			spdata->tx_buffer[1] = ECG_PAR_M;
			spdata->tx_buffer[2] = ECG_PAR_3;
			spdata->tx_buffer[3] = ECG_PAR_R;
			break;
		case SPIPROTO_IOC_BP_W:
			spdata->tx_buffer[0] = ECG_PAR_P;
			spdata->tx_buffer[1] = ECG_PAR_M;
			spdata->tx_buffer[2] = ECG_PAR_3;
			spdata->tx_buffer[3] = ECG_PAR_W;
			break;
		case SPIPROTO_IOC_SPO2_R:
			spdata->tx_buffer[0] = ECG_PAR_P;
			spdata->tx_buffer[1] = ECG_PAR_M;
			spdata->tx_buffer[2] = ECG_PAR_4;
			spdata->tx_buffer[3] = ECG_PAR_R;
			break;
		case SPIPROTO_IOC_SPO2_W:
			spdata->tx_buffer[0] = ECG_PAR_P;
			spdata->tx_buffer[1] = ECG_PAR_M;
			spdata->tx_buffer[2] = ECG_PAR_4;
			spdata->tx_buffer[3] = ECG_PAR_W;
			break;
		case SPIPROTO_IOC_BAT_R:
			spdata->tx_buffer[0] = ECG_PAR_S;
			spdata->tx_buffer[1] = ECG_PAR_Y;
			spdata->tx_buffer[2] = ECG_PAR_1;
			spdata->tx_buffer[3] = ECG_PAR_R;
			break;
		case SPIPROTO_IOC_BAT_W:
			spdata->tx_buffer[0] = ECG_PAR_S;
			spdata->tx_buffer[1] = ECG_PAR_Y;
			spdata->tx_buffer[2] = ECG_PAR_1;
			spdata->tx_buffer[3] = ECG_PAR_W;
			break;
		case SPIPROTO_IOC_PWR_R:
			spdata->tx_buffer[0] = ECG_PAR_S;
			spdata->tx_buffer[1] = ECG_PAR_Y;
			spdata->tx_buffer[2] = ECG_PAR_2;
			spdata->tx_buffer[3] = ECG_PAR_R;
			break;
		case SPIPROTO_IOC_PWR_W:
			spdata->tx_buffer[0] = ECG_PAR_S;
			spdata->tx_buffer[1] = ECG_PAR_Y;
			spdata->tx_buffer[2] = ECG_PAR_2;
			spdata->tx_buffer[3] = ECG_PAR_W;
			break;
		case SPIPROTO_IOC_RES_R:
			spdata->tx_buffer[0] = ECG_PAR_S;
			spdata->tx_buffer[1] = ECG_PAR_Y;
			spdata->tx_buffer[2] = ECG_PAR_3;
			spdata->tx_buffer[3] = ECG_PAR_R;
			break;
		case SPIPROTO_IOC_RES_W:
			spdata->tx_buffer[0] = ECG_PAR_S;
			spdata->tx_buffer[1] = ECG_PAR_Y;
			spdata->tx_buffer[2] = ECG_PAR_3;
			spdata->tx_buffer[3] = ECG_PAR_W;
			break;			
	}
	ret = spiproto_message(spdata, spi);	
	mutex_unlock(&spdata->buf_lock);
	


	return 0;
}

static struct file_operations spiproto_fops = {
	.owner 		= THIS_MODULE,
	.open 		= spiproto_open,
	.release 	= spiproto_release,
	.read		= spiproto_read,
	.unlocked_ioctl = spiproto_ioctl,
};

static int spiproto_probe(struct spi_device *spi)
{
	int status;
	struct spiproto_data *spdata;
	//struct spiproto_data spdata1;
	struct device *dev;

	spdata = kzalloc(sizeof(*spdata), GFP_KERNEL);
        if(!spdata)
                return -ENOMEM;

	spdata->spi = spi;
	spin_lock_init(&spdata->spi_lock);
	mutex_init(&spdata->buf_lock);
	INIT_LIST_HEAD(&spdata->device_entry);
	
	mutex_lock(&device_list_lock);	
	spdata->devt = MKDEV(SPIPROTO_MAJOR, 1);
        dev = device_create(spiproto_class, &spi->dev, spdata->devt,spdata, "spdata%d", spi->master->bus_num);
        status = PTR_ERR_OR_ZERO(dev);

	if (status == 0) {
                list_add(&spdata->device_entry, &device_list);
        }
	mutex_unlock(&device_list_lock);

	if (status == 0) {
                spi_set_drvdata(spi, spdata);
        } else {
                kfree(spdata);
	}
        return status;	
}

static int spiproto_remove(struct spi_device *spi)
{
	struct spiproto_data      *spdata = spi_get_drvdata(spi);

	spin_lock_irq(&spdata->spi_lock);
        spdata->spi = NULL;
	spin_unlock_irq(&spdata->spi_lock);

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

//	printk("func:%s=>line:%d\n", __FUNCTION__,__LINE__);
	status = register_chrdev(SPIPROTO_MAJOR, "spiproto", &spiproto_fops);
        if (status < 0)
                return status;
	
	spiproto_class = class_create(THIS_MODULE, "spiproto");
        if (IS_ERR(spiproto_class)) {
                unregister_chrdev(SPIPROTO_MAJOR, spiproto.driver.name);
                return PTR_ERR(spiproto_class);
        }
	
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

