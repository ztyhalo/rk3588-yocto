#include <linux/clk.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/regmap.h>
 
#define REGISTER_NUM 34
 
struct gm8775c_priv {
	struct regmap *regmap;
	struct i2c_client *client;
	
};
 
//Modify screen parameters and modify default values
static const struct reg_default gm8775c_reg_defaults[] = {   
		{0x00,0xAA},
		{0x48,0x02},
		{0xB6,0x20},
		{0x01,0x80},
		{0x02,0x38},
		{0x03,0x47},
		{0x04,0x06},
		{0x05,0x04},
		{0x06,0x50},
		{0x07,0x00},
	    {0x08,0x06},
		{0x09,0x04},
		{0x0A,0x28},
		{0x0B,0x02},
		{0x0C,0x52},
		{0x0D,0x01},
		{0x0E,0x80},
		{0x0F,0x20},
		{0x10,0x20},
		{0x11,0x03},
		{0x12,0x1B},
		{0x13,0x53},
		{0x14,0x01},
		{0x15,0x23},
		{0x16,0x40},
		{0x17,0x00},
		{0x18,0x01},
		{0x19,0x23},
     	{0x1A,0x40},
		{0x1B,0x00},
		{0x1E,0x46},
		{0x51,0x30},
		{0x1F,0x10},
		{0x2A,0x01},
};
 
static int gm8775c_read(u8 reg, u8 * rt_value, struct i2c_client *client)
{
	int ret;
	u8 read_cmd[3] = { 0 };
	u8 cmd_len = 0;
 
	read_cmd[0] = reg;
	cmd_len = 1;
 
	if (client->adapter == NULL)
		printk("gm8775c_read client->adapter==NULL\n");
 
	ret = i2c_master_send(client, read_cmd, cmd_len);
	if (ret != cmd_len) {
		printk("gm8775c_read error1\n");
		return -1;
	}
 
	ret = i2c_master_recv(client, rt_value, 1);
	if (ret != 1) {
		printk("gm8775c_read error2, ret = %d.\n", ret);
		return -1;
	}
 
	return 0;
}
 
static int gm8775c_write(u8 reg, unsigned char value, struct i2c_client *client)
{
	int ret = 0;
	u8 write_cmd[2] = { 0 };
 
	write_cmd[0] = reg;
	write_cmd[1] = value;
 
	ret = i2c_master_send(client, write_cmd, 2);
	if (ret != 2) {
		printk("gm8775c_write error->[REG-0x%02x,val-0x%02x]\n",
		       reg, value);
		return -1;
	}
 
	return 0;
}
 
static const struct of_device_id gm8775_of_match[] = { 
	{
		.compatible = "gm8775c,mipi_to_lvds",
	}, 
	{ }
};
MODULE_DEVICE_TABLE(of, gm8775_of_match);
 
static const struct i2c_device_id gm8775_i2c_id[] = {
	{"GM8775C_MTL", 0},
	{}
};
 
static ssize_t gm8775c_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val=0, flag=0;
	u8 i=0, reg, num, value_w, value_r;
 
	
	struct gm8775c_priv *gm8775c = dev_get_drvdata(dev);
	
	val = simple_strtol(buf, NULL, 16);
	flag = (val >> 16) & 0xFF;
	
	if (flag) {
		reg = (val >> 8) & 0xFF;
		value_w = val & 0xFF;
		printk("\nWrite: start REG:0x%02x,val:0x%02x,count:0x%02x\n", reg, value_w, flag);
		while(flag--) {
			gm8775c_write(reg, value_w,  gm8775c->client);
			printk("Write 0x%02x to REG:0x%02x\n", value_w, reg);
			reg++;
		}
	} else {
		reg = (val >> 8) & 0xFF;
		num = val & 0xff;
		printk("\nRead: start REG:0x%02x,count:0x%02x\n", reg, num);
		do {
			value_r = 0;
			gm8775c_read(reg, &value_r, gm8775c->client);
			printk("REG[0x%02x]: 0x%02x;  ", reg, value_r);
			reg++;
			i++;
			if ((i==num) || (i%4==0))	printk("\n");
		} while (i<num);
	}
	
	return count;
}
 
static ssize_t gm8775c_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk("echo flag|reg|val > gm8775c\n");
	printk("eg read star addres=0x06,count 0x10:echo 0610 >gm8775c\n");
	printk("eg write star addres=0x90,value=0x3c,count=4:echo 4903c >gm8775c\n");
	return 0;
}
 
static DEVICE_ATTR(gm8775c, 0644, gm8775c_show, gm8775c_store);
 
static struct attribute *egm8775c_debug_attrs[] = {
	&dev_attr_gm8775c.attr,
	NULL,
};
 
static struct attribute_group gm8775c_debug_attr_group = {
	.name   = "gm8775c_debug",
	.attrs  = egm8775c_debug_attrs,
};
 
/*
 *****************************************************************************
 *
 *	Driver Interface
 *
 *****************************************************************************
 */
 
static int gm8775c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct gm8775c_priv *gm8775c;
	int i;	
	int ret;
	u8 retreg = 0;
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_I2C_BLOCK |
						 I2C_FUNC_SMBUS_BYTE_DATA)) {
			dev_err(&adapter->dev, "doesn't support I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_I2C_BLOCK\n");
			return -ENODEV;
		}
	
	gm8775c = devm_kzalloc(&client->dev, sizeof(*gm8775c), GFP_KERNEL);
	if (!gm8775c)
		return -ENOMEM;
	
  	gm8775c->client = client;
	mdelay(500);
	printk("hndz gm8775 dev id is 0x%x!\n", client->addr);
	i2c_set_clientdata(client, gm8775c);

	 ret = gm8775c_read(0x00, &retreg, client);
	 printk("hndz read ret %d val %d!\n", ret, retreg);
	for(i = 0;i < REGISTER_NUM; i++)
			gm8775c_write(gm8775c_reg_defaults[i].reg, gm8775c_reg_defaults[i].def, gm8775c->client);	
 
	ret = sysfs_create_group(&client->dev.kobj, &gm8775c_debug_attr_group);
	if (ret) {
		pr_err("failed to create attr group\n");
	}
	
	return ret;
}
 
static int gm8775c_remove(struct i2c_client *client)
{ 
	return 0;
}
 
static struct i2c_driver gm8775c_driver = {
	.driver = {
		.name = "gm8775c-mipi-to-lvds",
		.of_match_table = of_match_ptr(gm8775_of_match),
	},
	.probe = gm8775c_probe,
	.remove = gm8775c_remove,
	.id_table = gm8775_i2c_id,
};
 
static int __init gm8775c_modinit(void)
{
	int ret;
	ret = i2c_add_driver(&gm8775c_driver);
	if (ret != 0)
		printk("Failed to register gm8775c i2c driver : %d \n", ret);
	return ret;
}
//late_initcall(gm8775c_modinit);
module_init(gm8775c_modinit);
 
static void __exit gm8775c_exit(void)
{
	i2c_del_driver(&gm8775c_driver);
}
 
module_exit(gm8775c_exit);
 
 
//module_i2c_driver(gm8775c_driver);
 
MODULE_AUTHOR("Alexander Bigga <ab@mycable.de>");
MODULE_DESCRIPTION("ST Microelectronics M41T80 series RTC I2C Client Driver");
MODULE_LICENSE("GPL");
