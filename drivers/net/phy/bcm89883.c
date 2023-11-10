#include <linux/delay.h>
#include <linux/module.h>
#include <linux/phy.h>

#define PHY_ID_BCM89883 0xae02503a

static const int bcm89883_features_array[6] = {
	ETHTOOL_LINK_MODE_100baseT_Full_BIT,
	ETHTOOL_LINK_MODE_TP_BIT,
	ETHTOOL_LINK_MODE_MII_BIT,
	ETHTOOL_LINK_MODE_FIBRE_BIT,
	ETHTOOL_LINK_MODE_AUI_BIT,
	ETHTOOL_LINK_MODE_BNC_BIT,
};

static int bcm89883_wait_init(struct phy_device *phydev)
{
	int val;

	return phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1,
					 val, !(val & MDIO_CTRL1_RESET),
					 100000, 2000000, false);
}

static inline int zty_c45_read(struct phy_device *dev, int devad, int reg)
{

	return  phy_read_mmd(dev, devad, reg);
}

static inline int zty_c45_write(struct phy_device *dev, int devad, int reg, int val)
{
	return phy_write_mmd(dev, devad, reg, val);
}

static int bcm89883_suspend(struct phy_device *phydev)
{
	return 0;
}

static int bcm89883_resume(struct phy_device *phydev)
{
	return 0;
}

static int bcm89883_match_phy_device(struct phy_device *phydev)
{
	unsigned int mark = 0;
	int i;
	if(phydev->is_c45)
	{
		mark = phydev->c45_ids.devices_in_package;
		for (i = 1; i < ARRAY_SIZE(phydev->c45_ids.device_ids); i++) {
			if (!(mark & (1 << i)))
				continue;
			if(phydev->c45_ids.device_ids[i] == PHY_ID_BCM89883)
			{
				printk("zty bcm89883 match 0x%x!\n", phydev->phy_id);
				//dump_stack();
				phydev->phy_id = PHY_ID_BCM89883;
				phydev->autoneg = AUTONEG_DISABLE;
				phydev->link = 0;
				return 1;
			}
		}

	}
	return 0;
}

static int bcm89883_get_features(struct phy_device *phydev)
{

	linkmode_set_bit_array(bcm89883_features_array,
				ARRAY_SIZE(bcm89883_features_array),
				phydev->supported);
	return 0;
}

static int bcm89883_config_init(struct phy_device *phydev)
{
    // printk(KERN_INFO "phydev->interface: %d, Line: %d, Fun: %s\n",
    //                  phydev->interface, __LINE__, __func__);
	int val = 0;
	unsigned int features = 0;

	int speed = 100;
	int polarity_ena_100 = 1;
	int tc10_ctrl, tc10_disable, superisolate;
	int autoneg = 0;
	int workmode= 0;
	int testoff_100m = 0;
	int AutoNegForce_MS = 0; //能设置

	phydev->link = 0;

	features = (SUPPORTED_TP | SUPPORTED_MII
		| SUPPORTED_AUI | SUPPORTED_FIBRE |
		SUPPORTED_BNC);

	// phydev->advertising = SUPPORTED_10000baseT_Full;

	printk("zty phy mdio dev addr 0x%x!\n", phydev->mdio.addr);

	val = phy_read_mmd(phydev, MDIO_MMD_AN, 0x0200);
	printk("zty neg val 0x%x!\n", val);

	if(val & (1 << 12))
	{
		printk("zty rxd3 hight autoneg!\n");
		phydev->autoneg = AUTONEG_ENABLE;
		features |= SUPPORTED_Autoneg;
		autoneg = 1;
		
	}
	else
	{
		printk("zty rxd3 is low no autoneg!\n");
		phydev->autoneg = AUTONEG_DISABLE;
		autoneg = 0;
		phydev->speed = 100;
		phydev->duplex = DUPLEX_FULL;

	}

	val = phy_read_mmd(phydev, 0x1, 0x0834);
	if(val < 0)
	{
		printk("zty read 834 error!\n");
		return val;
	}
	else
	{
//		printk("zty devad 0x1 address 0x834 val 0x%x!\n", val);

		if(val & 0x01)
		{
			speed = 1000;
		}
		else
		{
			speed = 100;
		}
	//	speed = 100;

		if(speed == 100)
		{
			phydev->speed = 100;
			features |= SUPPORTED_100baseT_Full;
			printk("zty phy speed %d!\n",speed);
			linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT1_Full_BIT,
			 phydev->supported, 1);

		}
		else
		{
			phydev->speed = 1000;
			features |= SUPPORTED_1000baseT_Full;
			printk("zty phy speed %d!\n",speed);
		}

		if(val & (1 << 14))
		{
			workmode = 1;
			printk("zty configure phy as master!\n");
		}
		else
			printk("zty configure phy as slave!\n");

		if(speed == 100 && autoneg == 0)
			testoff_100m = 0;
	}

	val = phy_read_mmd(phydev, 0x1, 0xa015);

   printk("zty read rgmii mode 0x%x!\n", val);

   	tc10_ctrl = zty_c45_read(phydev, 0x1e, 0x00f0);
	if(tc10_ctrl < 0)
	{

		printk("zty read tc10 ctrl error!\n");
		return  tc10_ctrl;
	}
	else
	{
		//printk("zty devad 0x1e address 0xf0 val 0x%x!\n", tc10_ctrl);
		tc10_disable = tc10_ctrl & 0x8000;
	}

	superisolate = zty_c45_read(phydev, 1, 0x932a);
	if(superisolate < 0)
	{
		printk("zty read superisolate error!\n");
		return superisolate;
	}
	else
	{
		//printk("zty devad 0x1 address 0x932a val 0x%x!\n", superisolate);
		superisolate = superisolate & 0x0020;
	}

	if(tc10_disable == 0)//tc10 able
	{
		zty_c45_write(phydev, 1, 0x8f02, 0x0001);
		if(speed == 1000)
		{
			zty_c45_write(phydev, 0x1e, 0x00f0, tc10_ctrl|0x800);
			zty_c45_write(phydev, 0x1e, 0x00f0, tc10_ctrl);
		}

	}
	else if (superisolate == 0)
	{
		zty_c45_write(phydev, 1, 0x8f02, 0x0001);
	}

	zty_c45_write(phydev, 1, 0x900b, 0x0000);

		if(polarity_ena_100)
	{
//		zty_c45_write(phydev, 1, 0x8b02, 0x0001);
		zty_c45_write(phydev, 1, 0x8b02, 0xb265);
	}

	if(speed == 100 || autoneg)
	{
		zty_c45_write(phydev, 0x01, 0x8130, 0x798d);
		zty_c45_write(phydev, 0x01, 0x8131, 0x0688);
		zty_c45_write(phydev, 0x01, 0x8132, 0xa405);
		zty_c45_write(phydev, 0x01, 0x8133, 0x2110);
		zty_c45_write(phydev, 0x01, 0x8134, 0xbf04);
		zty_c45_write(phydev, 0x01, 0x8135, 0x1818);
		zty_c45_write(phydev, 0x01, 0x8136, 0x2181);
		zty_c45_write(phydev, 0x01, 0x8140, 0x94a9);
		zty_c45_write(phydev, 0x01, 0x8141, 0x0688);
		zty_c45_write(phydev, 0x01, 0x8142, 0xa405);
		zty_c45_write(phydev, 0x01, 0x8143, 0x2110);
		zty_c45_write(phydev, 0x01, 0x8144, 0xbf84);
		zty_c45_write(phydev, 0x01, 0x8145, 0x1818);
		zty_c45_write(phydev, 0x01, 0x8146, 0x0209);

		if(autoneg)
		{
			  zty_c45_write(phydev, 0x01, 0x8150, 0x0080);
			  zty_c45_write(phydev, 0x01, 0x8151, 0x0001);
			  zty_c45_write(phydev, 0x01, 0x8152, 0x0000);
			  zty_c45_write(phydev, 0x01, 0x8153, 0x0014);
			  zty_c45_write(phydev, 0x01, 0x8154, 0x0304);
			  zty_c45_write(phydev, 0x01, 0x8155, 0x8d3e);
			  zty_c45_write(phydev, 0x01, 0x8156, 0x25bc);
			  zty_c45_write(phydev, 0x01, 0x8160, 0x00e7);
			  zty_c45_write(phydev, 0x01, 0x8161, 0x8000);
			  zty_c45_write(phydev, 0x01, 0x8162, 0x0000);
			  zty_c45_write(phydev, 0x01, 0x8163, 0x0104);
			  zty_c45_write(phydev, 0x01, 0x8164, 0x7f04);
			  zty_c45_write(phydev, 0x01, 0x8165, 0x1a18);
			  zty_c45_write(phydev, 0x01, 0x8166, 0x002d);
			  zty_c45_write(phydev, 0x01, 0x8170, 0x00e8);
			  zty_c45_write(phydev, 0x01, 0x8171, 0xc088);
			  zty_c45_write(phydev, 0x01, 0x8172, 0x0000);
			  zty_c45_write(phydev, 0x01, 0x8173, 0x0114);
			  zty_c45_write(phydev, 0x01, 0x8174, 0x7f04);
			  zty_c45_write(phydev, 0x01, 0x8175, 0x9d1a);
			  zty_c45_write(phydev, 0x01, 0x8176, 0x04ac);
			  zty_c45_write(phydev, 0x01, 0x8180, 0x00e9);
			  zty_c45_write(phydev, 0x01, 0x8181, 0x8010);
			  zty_c45_write(phydev, 0x01, 0x8182, 0x0000);
			  zty_c45_write(phydev, 0x01, 0x8183, 0x0114);
			  zty_c45_write(phydev, 0x01, 0x8184, 0x7f04);
			  zty_c45_write(phydev, 0x01, 0x8185, 0x9d1a);
			  zty_c45_write(phydev, 0x01, 0x8186, 0x04ac);
			  zty_c45_write(phydev, 0x01, 0x8190, 0x00ea);
			  zty_c45_write(phydev, 0x01, 0x8191, 0x8008);
			  zty_c45_write(phydev, 0x01, 0x8192, 0x0000);
			  zty_c45_write(phydev, 0x01, 0x8193, 0x0114);
			  zty_c45_write(phydev, 0x01, 0x8194, 0x7f04);
			  zty_c45_write(phydev, 0x01, 0x8195, 0x1d1a);
			  zty_c45_write(phydev, 0x01, 0x8196, 0x04ac);
			  zty_c45_write(phydev, 0x01, 0x81a0, 0x00eb);
			  zty_c45_write(phydev, 0x01, 0x81a1, 0x8009);
			  zty_c45_write(phydev, 0x01, 0x81a2, 0x0000);
			  zty_c45_write(phydev, 0x01, 0x81a3, 0x0114);
			  zty_c45_write(phydev, 0x01, 0x81a4, 0x7f14);
			  zty_c45_write(phydev, 0x01, 0x81a5, 0x1d1a);
			  zty_c45_write(phydev, 0x01, 0x81a6, 0x00ad);
			  zty_c45_write(phydev, 0x01, 0x81b0, 0xeca3);
			  zty_c45_write(phydev, 0x01, 0x81b1, 0x8a8a);
			  zty_c45_write(phydev, 0x01, 0x81b2, 0xaa8c);
			  zty_c45_write(phydev, 0x01, 0x81b3, 0x0114);
			  zty_c45_write(phydev, 0x01, 0x81b4, 0x7f14);
			  zty_c45_write(phydev, 0x01, 0x81b5, 0x1818);
			  zty_c45_write(phydev, 0x01, 0x81b6, 0x002d);
			  zty_c45_write(phydev, 0x01, 0x81c0, 0x00e7);
			  zty_c45_write(phydev, 0x01, 0x81c1, 0x0001);
			  zty_c45_write(phydev, 0x01, 0x81c2, 0x0000);
			  zty_c45_write(phydev, 0x01, 0x81c3, 0x0114);
			  zty_c45_write(phydev, 0x01, 0x81c4, 0x7f14);
			  zty_c45_write(phydev, 0x01, 0x81c5, 0x1818);
			  zty_c45_write(phydev, 0x01, 0x81c6, 0x002d);
		}
		else
		{
			  zty_c45_write(phydev, 0x01, 0x81d0, 0x009d);
			  zty_c45_write(phydev, 0x01, 0x81d1, 0x8000);
			  zty_c45_write(phydev, 0x01, 0x81d2, 0x0000);
			  zty_c45_write(phydev, 0x01, 0x81d3, 0x0104);
			  zty_c45_write(phydev, 0x01, 0x81d4, 0x7f04);
			  zty_c45_write(phydev, 0x01, 0x81d5, 0x1a18);
			  zty_c45_write(phydev, 0x01, 0x81d6, 0x002d);
			  zty_c45_write(phydev, 0x01, 0x81e0, 0x7eef);
			  zty_c45_write(phydev, 0x01, 0x81e1, 0x800a);
			  zty_c45_write(phydev, 0x01, 0x81e2, 0x0007);
			  zty_c45_write(phydev, 0x01, 0x81e3, 0x0014);
			  zty_c45_write(phydev, 0x01, 0x81e4, 0x0300);
			  zty_c45_write(phydev, 0x01, 0x81e5, 0x893e);
			  zty_c45_write(phydev, 0x01, 0x81e6, 0x25bf);
			  zty_c45_write(phydev, 0x01, 0x81f0, 0x007e);
			  zty_c45_write(phydev, 0x01, 0x81f1, 0x6f95);
			  zty_c45_write(phydev, 0x01, 0x81f2, 0x0000);
			  zty_c45_write(phydev, 0x01, 0x81f3, 0x0014);
			  zty_c45_write(phydev, 0x01, 0x81f4, 0x0300);
			  zty_c45_write(phydev, 0x01, 0x81f5, 0x893e);
			  zty_c45_write(phydev, 0x01, 0x81f6, 0x25bf);
		}

		// if(workmode)
		// {
		// 	if(autoneg)
		// 	{
		// 		zty_c45_write(phydev, 0x01, 0x8032, 0xe38c);
		// 		zty_c45_write(phydev, 0x01, 0x8033, 0xe57f);
		// 	}
		// }

		if(testoff_100m && autoneg)
		{
			if(workmode)//master mode
			{
			      zty_c45_write(phydev, 0x01, 0x8032, 0xe38c);
			      zty_c45_write(phydev, 0x01, 0x8033, 0xe57f);
			}
			else
			{
			      zty_c45_write(phydev, 0x01, 0x8031, 0xe4a8);
			      zty_c45_write(phydev, 0x01, 0x8033, 0xe69c);
			}
		}
		else if(testoff_100m == 0)
		{
			if(workmode)
			{
				if(autoneg)
				{
			          zty_c45_write(phydev, 0x01, 0x8031, 0x0079);
			          zty_c45_write(phydev, 0x01, 0x8033, 0xe57f);
				}
				else
				{
					zty_c45_write(phydev, 0x01, 0x8033, 0xee7d);
				}
				zty_c45_write(phydev, 0x01, 0x8032, 0xe38c);
			}
			else
			{
				if(autoneg)
				{
			          zty_c45_write(phydev, 0x01, 0x8032, 0x0094);
			          zty_c45_write(phydev, 0x01, 0x8033, 0xe69c);
				}
				else
				{
					zty_c45_write(phydev, 0x01, 0x8033, 0xed9c);
				}
				zty_c45_write(phydev, 0x01, 0x8031, 0xe4a8);
			}

		}
		else
		{
			printk("zty phy configure error!\n");
		}
	}

	if(autoneg == 0) //auto neg off
	{
		if(workmode) //master
		{
			if(speed == 1000)
			{
				zty_c45_write(phydev, 0x01, 0x834, 0xc001);
			}
			else
				zty_c45_write(phydev, 0x01, 0x834, 0xc000);
		}
		else
		{
			if(speed == 1000)
			{
				zty_c45_write(phydev, 0x01, 0x834, 0x8001);
//				zty_c45_write(phydev, 0x07, 0x0203, 0x0080);
//				zty_c45_write(phydev, 0x07, 0x0202, 0x1001);
			}
			else
				zty_c45_write(phydev, 0x01, 0x834, 0x8000);
		}
//		val = zty_c45_read(phydev, 0x07, 0x0200);
//		printk("zty autoneg ieee 0x0200 0x%x!\n", val);
//		val = zty_c45_read(phydev, 0x07, 0x0202);
//		printk("zty autoneg ieee 0x0202 0x%x!\n", val);
//		val = zty_c45_read(phydev, 0x07, 0x0203);
//		printk("zty autoneg ieee 0x0203 0x%x!\n", val);
//
//		val = zty_c45_read(phydev, 0x01, 0x8b00);
//		printk("zty autoneg ieee 0x8b00 0x%x!\n", val);
//		zty_c45_write(phydev, 0x01, 0x8b00, 0x0003);

		zty_c45_write(phydev, 0x07, 0x0200, 0x0200);

		// val = zty_c45_read(phydev, 0x07, 0x0200);
		// printk("zty autoneg ieee 0x0200 0x%x!\n", val);

	//	if(speed == 1000)
		{		
			val = zty_c45_read(phydev, 0x01, 0xa010);
			printk("zty  0xa010 0x%x!\n", val);
			//zty_c45_write(phydev, 0x01, 0xa010, val|0x01);
			 zty_c45_write(phydev, 0x01, 0xa010, 0x1101);
			// val = zty_c45_read(phydev, 0x01, 0xa010);
			// printk("zty  0xa010 0x%x!\n", val);
			//zty_c45_write(phydev, 0x01, 0xa010, 0x000);
		}


	}
	else
	{
		if(workmode) //master
		{
			if(speed == 1000)
			{
				zty_c45_write(phydev, 0x07, 0x0203, 0x00b0);
			}
			else
				zty_c45_write(phydev, 0x07, 0x0203, 0x0030);
			if(AutoNegForce_MS)
			{
				zty_c45_write(phydev, 0x07, 0x0202, 0x1001);
			}
			else
				zty_c45_write(phydev, 0x07, 0x0202, 0x0001);
		}
		else
		{
			if(speed == 1000)
			{
				zty_c45_write(phydev, 0x07, 0x0203, 0x00a0);
			}
			else
				zty_c45_write(phydev, 0x07, 0x0203, 0x0020);
			if(AutoNegForce_MS)
			{
				zty_c45_write(phydev, 0x07, 0x0202, 0x1001);
			}
			else
				zty_c45_write(phydev, 0x07, 0x0202, 0x0001);
		}
		zty_c45_write(phydev, 0x07, 0x0200, 0x1200);
	}

	if(tc10_disable == 0 || superisolate == 0)
	{
		val = zty_c45_read(phydev, 0x01, 0x8f02);
		//printk("zty read 0x8f02 val 0x%x!\n", val);
		zty_c45_write(phydev, 0x01, 0x8f02, 0x0000);
	}
	else
	{
		//zty_c45_write(phydev, 0x01, 0x932a, 0x0002);
		printk("zty disable superisolate!\n");
	}
	printk("zty phy interface %d!\n", phydev->interface);
    
	return 0;
}

static int bcm89883_phy_probe(struct phy_device *phydev)
{
	/* This driver requires PMAPMD and AN blocks */
	const u32 mmd_mask = MDIO_DEVS_PMAPMD | MDIO_DEVS_AN;

    printk(KERN_INFO "phydev->is_c45: %d, Line: %d, Fun: %s\n",
                     phydev->is_c45, __LINE__, __func__);

	if (!phydev->is_c45 ||
	    (phydev->c45_ids.devices_in_package & mmd_mask) != mmd_mask)
		return -ENODEV;

	return 0;
}


static int bcm89883_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	u32 adv;
	int ret;

	return 0;

	/* Wait for the PHY to finish initialising, otherwise our
	 * advertisement may be overwritten.
	 */
    printk(KERN_INFO "changed: %d, Line: %d, Fun: %s\n",
                     changed, __LINE__, __func__);

	ret = bcm89883_wait_init(phydev);
    printk(KERN_INFO "ret: %d, Line: %d, Fun: %s\n",
                     ret, __LINE__, __func__);
	if (ret)
		return ret;

	/* We don't support manual MDI control */
	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;

	ret = genphy_c45_an_config_aneg(phydev);
    printk(KERN_INFO "ret: %d, Line: %d, Fun: %s\n",
                     ret, __LINE__, __func__);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	adv = linkmode_adv_to_mii_ctrl1000_t(phydev->advertising);
	ret = phy_modify_mmd_changed(phydev, MDIO_MMD_AN, 0x1200,
				     ADVERTISE_1000FULL | ADVERTISE_1000HALF,
				     adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	return genphy_c45_check_and_restart_aneg(phydev, changed);
}

static int bcm89883_aneg_done(struct phy_device *phydev)
{
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_AN, 0x0201);
	if (ret < 0)
		return ret;
    
    return phydev->autoneg_complete;

}

static int bcm89883_read_status(struct phy_device *phydev)
{
//	int devad, reg;
	int val;
	int adv;
	// int err;
	int lpa;
	int lpagb = 0;
	int common_adv;
	int common_adv_gb = 0;

	val = zty_c45_read(phydev, 1, 1);
//	printk("zty read link state 0x%x!\n", val);

	if((val >= 0) && (val &MDIO_STAT1_LSTATUS))
	{
		if(phydev->link ==0)
			printk("zty find link!\n");
		phydev->link = 1;
	}
	else
	{
		//printk("zty find link down!\n");
		phydev->link = 0;
	}

	if (AUTONEG_ENABLE == phydev->autoneg) {
		printk("zty find state auto neg !\n");
		// if (phydev->supported & (SUPPORTED_1000baseT_Half
		// 			| SUPPORTED_1000baseT_Full)) {
		// 	lpagb = phy_read(phydev, MII_STAT1000);
		// 	if (lpagb < 0)
		// 		return lpagb;

		// 	adv = phy_read(phydev, MII_CTRL1000);
		// 	if (adv < 0)
		// 		return adv;

		// 	phydev->lp_advertising =
		// 		mii_stat1000_to_ethtool_lpa_t(lpagb);
		// 	common_adv_gb = lpagb & adv << 2;
		// }

		lpa = phy_read(phydev, MII_LPA);
		if (lpa < 0)
			return lpa;

		// phydev->lp_advertising |= mii_lpa_to_ethtool_lpa_t(lpa);

		adv = phy_read(phydev, MII_ADVERTISE);
		if (adv < 0)
			return adv;

		common_adv = lpa & adv;

		phydev->speed = SPEED_10;
		phydev->duplex = DUPLEX_HALF;
		phydev->pause = 0;
		phydev->asym_pause = 0;

		if (common_adv_gb & (LPA_1000FULL | LPA_1000HALF)) {
			phydev->speed = SPEED_1000;

			if (common_adv_gb & LPA_1000FULL)
				phydev->duplex = DUPLEX_FULL;
		} else if (common_adv & (LPA_100FULL | LPA_100HALF)) {
			phydev->speed = SPEED_100;

			if (common_adv & LPA_100FULL)
				phydev->duplex = DUPLEX_FULL;
		} else
			if (common_adv & LPA_10FULL)
				phydev->duplex = DUPLEX_FULL;

		if (phydev->duplex == DUPLEX_FULL) {
			phydev->pause = lpa & LPA_PAUSE_CAP ? 1 : 0;
			phydev->asym_pause = lpa & LPA_PAUSE_ASYM ? 1 : 0;
		}
	} else {
		//int bmcr = mdiobus_c45_read(phydev->bus, phydev->addr, 1, 0);
		int bmcr = zty_c45_read(phydev, 1, 0);

		if (bmcr < 0)
		{
			printk("zty read 1 0 err!\n");
			return bmcr;
		}



		if((bmcr & (1 << 6)) && !(bmcr & (1 <<13)))
		{
	//		printk("zty control speed 1000!\n");
			phydev->duplex = DUPLEX_FULL;
			phydev->speed = SPEED_1000;
		}
		else if(!(bmcr & (1 << 6)) && (bmcr & (1 <<13)))
		{
	//		printk("zty control speed 100!\n");
			phydev->duplex = DUPLEX_FULL;
			phydev->speed = SPEED_100;
		}
		else
		{
			printk("zty control speed error!\n");
		}

		//bmcr = mdiobus_c45_read(phydev->bus, phydev->addr, 3, 0);
		bmcr = zty_c45_read(phydev, 3, 0);
		//printk("zty read 3 0 val 0x%x!\n", bmcr);


		phydev->pause = 0;
		phydev->asym_pause = 0;
	}

	return 0;
}

static int bcm89883_soft_reset(struct phy_device *phydev)
{

	// zty_c45_write(phydev, 0x01, 0, 0x8040); /*reset*/
	// msleep(10);
	return 0;
}

static struct phy_driver bcm89883_drivers[] = {
    {	
        .phy_id = 0xae02503a,
        .phy_id_mask = 0xffffffff,
        .name = "Broadcom BCM89883",
		.features = 0,
        .config_init = bcm89883_config_init,
        .probe = bcm89883_phy_probe,
	    .get_features = bcm89883_get_features,
	    .config_aneg	= bcm89883_config_aneg,
	    .aneg_done	= bcm89883_aneg_done,
	    .read_status = bcm89883_read_status,
    },
};


static struct phy_driver bcm89881_drivers[] = {
	{
	.phy_id		= PHY_ID_BCM89883,
	.name		= "Broadcom BCM89883",
	.phy_id_mask	= 0xffffffff,
	.features       = 0,
//	.flags		= PHY_HAS_INTERRUPT,
	.config_init	= bcm89883_config_init,
	.config_aneg	= bcm89883_config_aneg,
	.get_features	= bcm89883_get_features,
	.read_status	= bcm89883_read_status,
	.match_phy_device = bcm89883_match_phy_device,
	.config_intr	= NULL,
	.suspend	= bcm89883_suspend,
	.resume		= bcm89883_resume,
	.soft_reset = bcm89883_soft_reset,
	},
};

module_phy_driver(bcm89881_drivers);

static struct mdio_device_id __maybe_unused bcm89883_tbl[] = {
    { 0xae02503a, 0xffffffff },
    { },
};

MODULE_DESCRIPTION("Broadcom PHY driver");
MODULE_AUTHOR("Han");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(mdio, bcm89883_tbl);
