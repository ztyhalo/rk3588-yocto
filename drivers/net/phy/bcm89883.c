#include <linux/delay.h>
#include <linux/module.h>
#include <linux/phy.h>

enum {
	MDIO_AN_C22 = 0xffe0,
};

static int bcm89883_wait_init(struct phy_device *phydev)
{
	int val;

	return phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1,
					 val, !(val & MDIO_CTRL1_RESET),
					 100000, 2000000, false);
}

static int bcm89883_config_init(struct phy_device *phydev)
{
    printk(KERN_INFO "phydev->interface: %d, Line: %d, Fun: %d\n",
                     phydev->interface, __LINE__, __func__);
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_100BASEX:
	case PHY_INTERFACE_MODE_1000BASEX:
		break;
	default:
		return -ENODEV;
	}
	return 0;
}

static int bcm89883_probe(struct phy_device *phydev)
{
	/* This driver requires PMAPMD and AN blocks */
	const u32 mmd_mask = MDIO_DEVS_PMAPMD | MDIO_DEVS_AN;

    printk(KERN_INFO "phydev->is_c45: %d, Line: %d, Fun: %d\n",
                     phydev->is_c45, __LINE__, __func__);

	if (!phydev->is_c45 ||
	    (phydev->c45_ids.devices_in_package & mmd_mask) != mmd_mask)
		return -ENODEV;

	return 0;
}

static int bcm89883_get_features(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_pma_read_abilities(phydev);
    printk(KERN_INFO "ret: %d, Line: %d, Fun: %d\n",
                     ret, __LINE__, __func__);
	if (ret)
		return ret;

	return 0;
}

static int bcm89883_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	u32 adv;
	int ret;

	/* Wait for the PHY to finish initialising, otherwise our
	 * advertisement may be overwritten.
	 */
	ret = bcm89883_wait_init(phydev);
	if (ret)
		return ret;

	/* We don't support manual MDI control */
	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;

	ret = genphy_c45_an_config_aneg(phydev);
    printk(KERN_INFO "ret: %d, Line: %d, Fun: %d\n",
                     ret, __LINE__, __func__);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	adv = linkmode_adv_to_mii_ctrl1000_t(phydev->advertising);
	ret = phy_modify_mmd_changed(phydev, MDIO_MMD_AN,
				     MDIO_AN_C22 + MII_CTRL1000,
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
	int bmsr, val;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);

    printk(KERN_INFO "val: %d, Line: %d, Fun: %d\n",
                     val, __LINE__, __func__);
	if (val < 0)
		return val;

	bmsr = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_C22 + MII_BMSR);
	if (bmsr < 0)
		return val;

	return !!(val & MDIO_AN_STAT1_COMPLETE) &&
	       !!(bmsr & BMSR_ANEGCOMPLETE);
}

static int bcm89883_read_status(struct phy_device *phydev)
{
	unsigned int mode;
	int bmsr, val;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1);
    printk(KERN_INFO "val: %d, Line: %d, Fun: %d\n",
                     val, __LINE__, __func__);
	if (val < 0)
		return val;

	if (val & MDIO_AN_CTRL1_RESTART) {
		phydev->link = 0;
		return 0;
	}

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	if (val < 0)
		return val;

	bmsr = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_C22 + MII_BMSR);
	if (bmsr < 0)
		return val;

	phydev->autoneg_complete = !!(val & MDIO_AN_STAT1_COMPLETE) &&
				   !!(bmsr & BMSR_ANEGCOMPLETE);
	phydev->link = !!(val & MDIO_STAT1_LSTATUS) &&
		       !!(bmsr & BMSR_LSTATUS);
	if (phydev->autoneg == AUTONEG_ENABLE && !phydev->autoneg_complete)
		phydev->link = false;

	linkmode_zero(phydev->lp_advertising);
	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;
	phydev->mdix = 0;

	if (!phydev->link)
		return 0;

	if (phydev->autoneg_complete) {
		val = genphy_c45_read_lpa(phydev);
		if (val < 0)
			return val;

		val = phy_read_mmd(phydev, MDIO_MMD_AN,
				   MDIO_AN_C22 + MII_STAT1000);
		if (val < 0)
			return val;

		mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising, val);

		if (phydev->autoneg == AUTONEG_ENABLE)
			phy_resolve_aneg_linkmode(phydev);
	}

	/* Set the host link mode - we set the phy interface mode and
	 * the speed according to this register so that downshift works.
	 * We leave the duplex setting as per the resolution from the
	 * above.
	 */
	val = phy_read_mmd(phydev, 0x1, 0xA015);
	mode = (val >> 3) & 0x07;
	if (mode == 0)
		phydev->interface = PHY_INTERFACE_MODE_RGMII;

	val = phy_read_mmd(phydev, 0x1, 0x0000);
	mode = (val >> 6) & 0xFF;
	switch (mode) {
	case 1:
		phydev->speed = SPEED_1000;
		break;
	case 0x80:
		phydev->speed = SPEED_100;
		break;
	}

	return genphy_c45_read_mdix(phydev);
}

static struct phy_driver bcm89883_drivers[] = {
    {	
        .phy_id = 0xae02503a,
        .phy_id_mask = 0xfffffff0,
        .name = "Broadcom BCM89883",
        .config_init = bcm89883_config_init,
        .probe = bcm89883_phy_probe,
	    .get_features = bcm89883_get_features,
	    .config_aneg	= bcm89883_config_aneg,
	    .aneg_done	= bcm89883_aneg_done,
	    .read_status = bcm89883_read_status,
    },
};

module_phy_driver(bcm89883_drivers);

static struct mdio_device_id __maybe_unused bcm89883_tbl[] = {
    { 0xae02503a, 0xfffffff0 },
    { },
};

MODULE_DESCRIPTION("Broadcom PHY driver");
MODULE_AUTHOR("Han");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(mdio, bcm89883_tbl);
