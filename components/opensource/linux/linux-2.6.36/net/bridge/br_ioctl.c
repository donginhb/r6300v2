/*
 *	Ioctl handler
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/if_bridge.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/times.h>
#include <net/net_namespace.h>
#include <asm/uaccess.h>
#include "br_private.h"
#ifdef HNDCTF
#include <ctf/hndctf.h>
#endif /* HNDCTF */

/*  add start, Zz Shan@MutiSsidControl 03/13/2009*/
#ifdef MULTIPLE_SSID

/* modified start, water, 01/07/10*/
//#include "../../../../router/multissidcontrol/MultiSsidControl.h"
#include "../../../../../../ap/acos/multissidcontrol/MultiSsidControl.h"
/* modified end, water, 01/07/10*/

T_MSsidCtlProfile *gProfile = NULL;
int gProfilenum = 0;
/*  added start pling 10/06/2010 */
T_MSsidCtlProfile *gProfile_5g = NULL;
int gProfilenum_5g = 0;
/*  added end pling 10/06/2010 */

EXPORT_SYMBOL(gProfilenum);
EXPORT_SYMBOL(gProfilenum_5g);
EXPORT_SYMBOL(gProfile);
EXPORT_SYMBOL(gProfile_5g);

/*Fxcn added start by dennis,02/16/2012,for access control*/
#ifdef INCLUDE_ACCESSCONTROL
#include "../../../../../../ap/acos/access_control/AccessControl.h"
/*Fxcn added end by dennis,02/16/2012,for access control*/
/*Fxcn added start by dennis,02/16/2012,for access control*/
T_AccessControlTable *gAccessTable = NULL;
int gAccessControlMode = 0; /*0:access control disabled,1:access all new devices,2:block all new devices*/
int gAccessTablenum = 0;
/*Fxcn added end by dennis,02/16/2012,for access control*/
/* add start by Hank 08/29/2012*/
EXPORT_SYMBOL(gAccessTable);
EXPORT_SYMBOL(gAccessControlMode);
EXPORT_SYMBOL(gAccessTablenum);
/* add end by Hank 08/29/2012*/
#endif
void profileprint(void)
{
    int i = 0;
    for (i = 0;i < gProfilenum;i++)
    {
        printk("<0>Profile[%d]:name:%s,enable:%d\n",i,gProfile[i].IfName,gProfile[i].enable);
    }
    for (i = 0;i < gProfilenum_5g;i++)
    {
        printk("<0>Profile_5g[%d]:name:%s,enable:%d\n",i,gProfile_5g[i].IfName,gProfile_5g[i].enable);
    }
}
#endif
/*  add end, Zz Shan 03/13/2009*/

/* called with RTNL */
static int get_bridge_ifindices(struct net *net, int *indices, int num)
{
	struct net_device *dev;
	int i = 0;

	for_each_netdev(net, dev) {
		if (i >= num)
			break;
		if (dev->priv_flags & IFF_EBRIDGE)
			indices[i++] = dev->ifindex;
	}

	return i;
}

/* called with RTNL */
static void get_port_ifindices(struct net_bridge *br, int *ifindices, int num)
{
	struct net_bridge_port *p;

	list_for_each_entry(p, &br->port_list, list) {
		if (p->port_no < num)
			ifindices[p->port_no] = p->dev->ifindex;
	}
}

/*
 * Format up to a page worth of forwarding table entries
 * userbuf -- where to copy result
 * maxnum  -- maximum number of entries desired
 *            (limited to a page for sanity)
 * offset  -- number of records to skip
 */
static int get_fdb_entries(struct net_bridge *br, void __user *userbuf,
			   unsigned long maxnum, unsigned long offset)
{
	int num;
	void *buf;
	size_t size;

	/* Clamp size to PAGE_SIZE, test maxnum to avoid overflow */
	if (maxnum > PAGE_SIZE/sizeof(struct __fdb_entry))
		maxnum = PAGE_SIZE/sizeof(struct __fdb_entry);

	size = maxnum * sizeof(struct __fdb_entry);

	buf = kmalloc(size, GFP_USER);
	if (!buf)
		return -ENOMEM;

	num = br_fdb_fillbuf(br, buf, maxnum, offset);
	if (num > 0) {
		if (copy_to_user(userbuf, buf, num*sizeof(struct __fdb_entry)))
			num = -EFAULT;
	}
	kfree(buf);

	return num;
}

/* called with RTNL */
static int add_del_if(struct net_bridge *br, int ifindex, int isadd)
{
	struct net_device *dev;
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	dev = __dev_get_by_index(dev_net(br->dev), ifindex);
	if (dev == NULL)
		return -EINVAL;

	if (isadd)
		ret = br_add_if(br, dev);
	else
		ret = br_del_if(br, dev);

	return ret;
}

/*
 * Legacy ioctl's through SIOCDEVPRIVATE
 * This interface is deprecated because it was too difficult to
 * to do the translation for 32/64bit ioctl compatability.
 */
static int old_dev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct net_bridge *br = netdev_priv(dev);
	unsigned long args[4];

	if (copy_from_user(args, rq->ifr_data, sizeof(args)))
		return -EFAULT;

	switch (args[0]) {
	case BRCTL_ADD_IF:
	case BRCTL_DEL_IF:
		return add_del_if(br, args[1], args[0] == BRCTL_ADD_IF);

	case BRCTL_GET_BRIDGE_INFO:
	{
		struct __bridge_info b;

		memset(&b, 0, sizeof(struct __bridge_info));
		rcu_read_lock();
		memcpy(&b.designated_root, &br->designated_root, 8);
		memcpy(&b.bridge_id, &br->bridge_id, 8);
		b.root_path_cost = br->root_path_cost;
		b.max_age = jiffies_to_clock_t(br->max_age);
		b.hello_time = jiffies_to_clock_t(br->hello_time);
		b.forward_delay = br->forward_delay;
		b.bridge_max_age = br->bridge_max_age;
		b.bridge_hello_time = br->bridge_hello_time;
		b.bridge_forward_delay = jiffies_to_clock_t(br->bridge_forward_delay);
		b.topology_change = br->topology_change;
		b.topology_change_detected = br->topology_change_detected;
		b.root_port = br->root_port;

		b.stp_enabled = (br->stp_enabled != BR_NO_STP);
		b.ageing_time = jiffies_to_clock_t(br->ageing_time);
		b.hello_timer_value = br_timer_value(&br->hello_timer);
		b.tcn_timer_value = br_timer_value(&br->tcn_timer);
		b.topology_change_timer_value = br_timer_value(&br->topology_change_timer);
		b.gc_timer_value = br_timer_value(&br->gc_timer);
		rcu_read_unlock();

		if (copy_to_user((void __user *)args[1], &b, sizeof(b)))
			return -EFAULT;

		return 0;
	}

	case BRCTL_GET_PORT_LIST:
	{
		int num, *indices;

		num = args[2];
		if (num < 0)
			return -EINVAL;
		if (num == 0)
			num = 256;
		if (num > BR_MAX_PORTS)
			num = BR_MAX_PORTS;

		indices = kcalloc(num, sizeof(int), GFP_KERNEL);
		if (indices == NULL)
			return -ENOMEM;

		get_port_ifindices(br, indices, num);
		if (copy_to_user((void __user *)args[1], indices, num*sizeof(int)))
			num =  -EFAULT;
		kfree(indices);
		return num;
	}

	case BRCTL_SET_BRIDGE_FORWARD_DELAY:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		spin_lock_bh(&br->lock);
		br->bridge_forward_delay = clock_t_to_jiffies(args[1]);
		if (br_is_root_bridge(br))
			br->forward_delay = br->bridge_forward_delay;
		spin_unlock_bh(&br->lock);
		return 0;

	case BRCTL_SET_BRIDGE_HELLO_TIME:
	{
		unsigned long t = clock_t_to_jiffies(args[1]);
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (t < HZ)
			return -EINVAL;

		spin_lock_bh(&br->lock);
		br->bridge_hello_time = t;
		if (br_is_root_bridge(br))
			br->hello_time = br->bridge_hello_time;
		spin_unlock_bh(&br->lock);
		return 0;
	}

	case BRCTL_SET_BRIDGE_MAX_AGE:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		spin_lock_bh(&br->lock);
		br->bridge_max_age = clock_t_to_jiffies(args[1]);
		if (br_is_root_bridge(br))
			br->max_age = br->bridge_max_age;
		spin_unlock_bh(&br->lock);
		return 0;

	case BRCTL_SET_AGEING_TIME:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		br->ageing_time = clock_t_to_jiffies(args[1]);
		return 0;

	case BRCTL_GET_PORT_INFO:
	{
		struct __port_info p;
		struct net_bridge_port *pt;

		rcu_read_lock();
		if ((pt = br_get_port(br, args[2])) == NULL) {
			rcu_read_unlock();
			return -EINVAL;
		}

		memset(&p, 0, sizeof(struct __port_info));
		memcpy(&p.designated_root, &pt->designated_root, 8);
		memcpy(&p.designated_bridge, &pt->designated_bridge, 8);
		p.port_id = pt->port_id;
		p.designated_port = pt->designated_port;
		p.path_cost = pt->path_cost;
		p.designated_cost = pt->designated_cost;
		p.state = pt->state;
		p.top_change_ack = pt->topology_change_ack;
		p.config_pending = pt->config_pending;
		p.message_age_timer_value = br_timer_value(&pt->message_age_timer);
		p.forward_delay_timer_value = br_timer_value(&pt->forward_delay_timer);
		p.hold_timer_value = br_timer_value(&pt->hold_timer);

		rcu_read_unlock();

		if (copy_to_user((void __user *)args[1], &p, sizeof(p)))
			return -EFAULT;

		return 0;
	}

	case BRCTL_SET_BRIDGE_STP_STATE:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		br_stp_set_enabled(br, args[1]);
		return 0;

	case BRCTL_SET_BRIDGE_PRIORITY:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		spin_lock_bh(&br->lock);
		br_stp_set_bridge_priority(br, args[1]);
		spin_unlock_bh(&br->lock);
		return 0;

	case BRCTL_SET_PORT_PRIORITY:
	{
		struct net_bridge_port *p;
		int ret = 0;

		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (args[2] >= (1<<(16-BR_PORT_BITS)))
			return -ERANGE;

		spin_lock_bh(&br->lock);
		if ((p = br_get_port(br, args[1])) == NULL)
			ret = -EINVAL;
		else
			br_stp_set_port_priority(p, args[2]);
		spin_unlock_bh(&br->lock);
		return ret;
	}

	case BRCTL_SET_PATH_COST:
	{
		struct net_bridge_port *p;
		int ret = 0;

		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if ((p = br_get_port(br, args[1])) == NULL)
			ret = -EINVAL;
		else
			br_stp_set_path_cost(p, args[2]);

		return ret;
	}

	case BRCTL_GET_FDB_ENTRIES:
		return get_fdb_entries(br, (void __user *)args[1],
				       args[2], args[3]);
	}

	return -EOPNOTSUPP;
}

static int old_deviceless(struct net *net, void __user *uarg)
{
	unsigned long args[3];

	if (copy_from_user(args, uarg, sizeof(args)))
		return -EFAULT;

	switch (args[0]) {
	case BRCTL_GET_VERSION:
		return BRCTL_VERSION;

	case BRCTL_GET_BRIDGES:
	{
		int *indices;
		int ret = 0;

		if (args[2] >= 2048)
			return -ENOMEM;
		indices = kcalloc(args[2], sizeof(int), GFP_KERNEL);
		if (indices == NULL)
			return -ENOMEM;

		args[2] = get_bridge_ifindices(net, indices, args[2]);

		ret = copy_to_user((void __user *)args[1], indices, args[2]*sizeof(int))
			? -EFAULT : args[2];

		kfree(indices);
		return ret;
	}

	case BRCTL_ADD_BRIDGE:
	case BRCTL_DEL_BRIDGE:
	{
		char buf[IFNAMSIZ];

		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (copy_from_user(buf, (void __user *)args[1], IFNAMSIZ))
			return -EFAULT;

		buf[IFNAMSIZ-1] = 0;

		if (args[0] == BRCTL_ADD_BRIDGE)
			return br_add_bridge(net, buf);

		return br_del_bridge(net, buf);
	}
	/*  add start, Zz Shan@MutiSsidControl 03/13/2009*/
#ifdef MULTIPLE_SSID	
	case BRCTL_SET_MSSIDPROFILE:
	{
		unsigned long num = 0;

		gProfilenum = 0;
		if (gProfile)
		{
			kfree(gProfile);
			gProfile = NULL;
		}

		if (copy_from_user(&num, (void __user *)args[1], sizeof(int)))
			return -EFAULT;

		if (num == 0)
			return 0;

		gProfilenum = num;
		gProfile = (T_MSsidCtlProfile *)kmalloc(num*sizeof(T_MSsidCtlProfile),GFP_ATOMIC);
		if (!gProfile)
		{
			gProfilenum = 0;
			return -EFAULT;
		}
		if (copy_from_user(gProfile, (void __user *)args[2], num*sizeof(T_MSsidCtlProfile)))
		{
			gProfilenum = 0;
			kfree(gProfile);
			gProfile = NULL;
			return -EFAULT;
		}

		//profileprint();

		return 0;
	}
    /*  added start pling 10/06/2010 */
    /* For 5G guest network control */
	case BRCTL_SET_5G_MSSIDPROFILE:
	{
		unsigned long num = 0;

		gProfilenum_5g = 0;
		if (gProfile_5g)
		{
			kfree(gProfile_5g);
			gProfile_5g = NULL;
		}

		if (copy_from_user(&num, (void __user *)args[1], sizeof(int)))
			return -EFAULT;

		if (num == 0)
			return 0;

		gProfilenum_5g = num;
		gProfile_5g = (T_MSsidCtlProfile *)kmalloc(num*sizeof(T_MSsidCtlProfile),GFP_ATOMIC);
		if (!gProfile_5g)
		{
			gProfilenum_5g = 0;
			return -EFAULT;
		}
		if (copy_from_user(gProfile_5g, (void __user *)args[2], num*sizeof(T_MSsidCtlProfile)))
		{
			gProfilenum_5g = 0;
			kfree(gProfile_5g);
			gProfile_5g = NULL;
			return -EFAULT;
		}

		//profileprint();

		return 0;
	}
    /*  added end pling 10/06/2010 */
#endif	
	/*  add end, Zz Shan 03/13/2009*/
	/*  added start, zacker, 03/24/2011 */
	case BRCTL_SET_BCMCTF_ENABLE:
	{
#ifdef HNDCTF
		struct net_device *dev;
		int ret = 0;
		int ctf_is_enabled = 0;

		/* modify start by Hank 08/10/2012 */
		/*kernel function be modified*/
		dev = dev_get_by_index(net,args[1]);
		/* modify end by Hank 08/10/2012 */
		if (dev == NULL)
			return -EINVAL;

		ctf_is_enabled = ctf_isenabled(kcih, dev);
		if ((args[2] && !ctf_is_enabled)
			|| (!args[2] && ctf_is_enabled))
		{
			printk("<0>SET_BCMCTF:ifname:%s,enable:%lu\n",
					dev->name, args[2]);
			ret = ctf_enable(kcih, dev, args[2],NULL);
		}

		dev_put(dev);
		return ret;
#else
		break;
#endif /* HNDCTF */
	}
	/*  added end, zacker, 03/24/2011 */
	 /*Fxcn added start dennis,02/18/2012,for access control*/
#ifdef INCLUDE_ACCESSCONTROL
    case BRCTL_SET_ACCESS_CONTROL:
    {

       unsigned long num = 0;
       gAccessTablenum = 0;

       if (gAccessTable)
		{
			kfree(gAccessTable);
			gAccessTable = NULL;
		}

		if (copy_from_user(&num, (void __user *)args[1], sizeof(int)))
			return -EFAULT;

		if (num == 0)
			return 0;

		gAccessTablenum = num;
		gAccessTable = (T_AccessControlTable *)kmalloc(num*sizeof(T_AccessControlTable),GFP_ATOMIC);
		if (!gAccessTable)
		{
			gAccessTablenum = 0;
			return -EFAULT;
		}
		if (copy_from_user(gAccessTable, (void __user *)args[2], num*sizeof(T_AccessControlTable)))
		{
			gAccessTablenum = 0;
			kfree(gAccessTable);
			gAccessTable = NULL;
			return -EFAULT;
		}       
         
       return 0;

    }

   case BRCTL_SET_ACCESS_CONTROL_MODE:
   {
        unsigned long mode = 0;
        gAccessControlMode = 0;
        

        if (copy_from_user(&mode, (void __user *)args[1], sizeof(int)))
			return -EFAULT;
	printk("BRCTL_SET_ACCESS_CONTROL_MODE>> mode %d\n",mode);
        gAccessControlMode = mode;
        printk("BRCTL_SET_ACCESS_CONTROL_MODE>> gAccessControlMode  %d\n",gAccessControlMode);
                  
        return 0;

   }
#endif
    
    /*Fxcn added end dennis,02/18/2012,for access control*/
	}

	return -EOPNOTSUPP;
}

int br_ioctl_deviceless_stub(struct net *net, unsigned int cmd, void __user *uarg)
{
	switch (cmd) {
	case SIOCGIFBR:
	case SIOCSIFBR:
		return old_deviceless(net, uarg);

	case SIOCBRADDBR:
	case SIOCBRDELBR:
	{
		char buf[IFNAMSIZ];

		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (copy_from_user(buf, uarg, IFNAMSIZ))
			return -EFAULT;

		buf[IFNAMSIZ-1] = 0;
		if (cmd == SIOCBRADDBR)
			return br_add_bridge(net, buf);

		return br_del_bridge(net, buf);
	}
	}
	return -EOPNOTSUPP;
}

int br_dev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct net_bridge *br = netdev_priv(dev);

	switch(cmd) {
	case SIOCDEVPRIVATE:
		return old_dev_ioctl(dev, rq, cmd);

	case SIOCBRADDIF:
	case SIOCBRDELIF:
		return add_del_if(br, rq->ifr_ifindex, cmd == SIOCBRADDIF);

	}

	br_debug(br, "Bridge does not support ioctl 0x%x\n", cmd);
	return -EOPNOTSUPP;
}
