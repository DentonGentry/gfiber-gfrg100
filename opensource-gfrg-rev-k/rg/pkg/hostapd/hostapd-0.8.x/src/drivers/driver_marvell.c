/*
 * Host AP - driver interface with MARVELL driver
 * Copyright (c) 2004, Sam Leffler <sam@errno.com>
 * Copyright (c) 2010-2011, Marvell Semiconductor- added support for Marvell driver glue logics.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */
#include "includes.h"
#include <sys/types.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <netpacket/packet.h>

#include "common.h"
#include "driver.h"
#include "driver_wext.h"
#include "eloop.h"
#include "priv_netlink.h"
#include "l2_packet/l2_packet.h"
#include "common/ieee802_11_defs.h"
#include "netlink.h"
#include "linux_ioctl.h"
#include "wireless_copy.h"

#include "ap/hostapd.h"
#include "ap/ap_config.h"

#include <driver/linux/ap8xLnxIoctl.h>

#define	IEEE80211_ADDR_LEN	6

struct marvell_driver_data {
	struct hostapd_data *hapd;		/* back pointer */
	char	iface[IFNAMSIZ + 1];
	int     ifindex;
	struct l2_packet_data *sock_xmit;	/* raw packet xmit socket */
	struct l2_packet_data *sock_recv;	/* raw packet recv socket */
	int	ioctl_sock;			/* socket for ioctl() use */
	struct netlink_data *netlink;
	int	we_version;
	u8	acct_mac[ETH_ALEN];
	struct hostap_sta_driver_data acct_data;
	struct l2_packet_data *sock_raw; /* raw 802.11 management frames */
};

static int marvell_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr, int reason_code);

static int
set80211priv(struct marvell_driver_data *drv, int op, void *data, int len)
{
	struct iwreq iwr;

	memset(&iwr, 0, sizeof(iwr));
	strncpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
	if (len < IFNAMSIZ) {
		/*
		 * Argument data fits inline; put it there.
		 */
		memcpy(iwr.u.name, data, len);
	} else {
		/*
		 * Argument data too big for inline transfer; setup a
		 * parameter block instead; the kernel will transfer
		 * the data for the driver.
		 */
		iwr.u.data.pointer = data;
		iwr.u.data.length = len;
	}

	if (ioctl(drv->ioctl_sock, op, &iwr) < 0) {
		return -1;
	}

	return 0;
}

static int
get80211priv(struct marvell_driver_data *drv, int op, void *data, int len)
{
	struct iwreq iwr;

	memset(&iwr, 0, sizeof(iwr));
	strncpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
	if (len < IFNAMSIZ) {
		/*
		 * Argument data fits inline; put it there.
		 */
		memcpy(iwr.u.name, data, len);
	} else {
		/*
		 * Argument data too big for inline transfer; setup a
		 * parameter block instead; the kernel will transfer
		 * the data for the driver.
		 */
		iwr.u.data.pointer = data;
		iwr.u.data.length = len;
	}

	if (ioctl(drv->ioctl_sock, op, &iwr) < 0) {
		return -1;
	}

	if (len < IFNAMSIZ)
		memcpy(data, iwr.u.name, len);
	return iwr.u.data.length;
}

static int
set80211param(struct marvell_driver_data *drv, int op, int arg, Boolean commit)
{
	struct iwreq iwr;

	memset(&iwr, 0, sizeof(iwr));
	strncpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
	iwr.u.mode = op;
	memcpy(iwr.u.name+sizeof(__u32), &arg, sizeof(arg));

	if (ioctl(drv->ioctl_sock, WL_IOCTL_WL_PARAM, &iwr) < 0) {
		perror("ioctl[WL_IOCTL_WL_PARAM]");
		return -1;
	}

    if (commit) {
    	memset(&iwr, 0, sizeof(iwr));
    	strncpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
        if (ioctl(drv->ioctl_sock, SIOCSIWCOMMIT, &iwr) < 0) {
    		printf("ioctl[SIOCSIWCOMMIT]");
    		return -1;
    	}
    }
	return 0;
}

static const char *
ether_sprintf(const u8 *addr)
{
	static char buf[sizeof(MACSTR)];

	if (addr != NULL)
		snprintf(buf, sizeof(buf), MACSTR, MAC2STR(addr));
	else
		snprintf(buf, sizeof(buf), MACSTR, 0,0,0,0,0,0);
	return buf;
}


/*
 * Configure WPA parameters.
 */
static int
marvell_configure_wpa(struct marvell_driver_data *drv, struct wpa_bss_params *params)
{
	u8 wpawpa2mode;
	char ciphersuite[16];

	/* In WPS mode, set the WPAWPA2MODE to 0x13 (extended mixed mode)
	* with the exception of WPA2PSK-TKIP. For WPA2PSK-TKIP set the
	* the WPAWPA2MODE as 0x12 (extended WPA2PSK mode).
	*/
	if (drv->hapd->conf->wps_state)
	{
		// Set wpawpa2mode if WPA2PSK-TKIP
    	if ((params->wpa & WPA_PROTO_RSN) &&
        	!(params->wpa & WPA_PROTO_WPA) &&
        	(params->wpa_key_mgmt & WPA_KEY_MGMT_PSK) &&
       		((params->wpa_pairwise & WPA_CIPHER_CCMP) ||
       		(params->wpa_pairwise & WPA_CIPHER_TKIP)))
		{
    		wpawpa2mode = 0x12;
		}
		// Set wpawpa2mode if WPAPSK-TKIP
		else if (!(params->wpa & WPA_PROTO_RSN) &&
        	(params->wpa & WPA_PROTO_WPA) &&
        	(params->wpa_key_mgmt & WPA_KEY_MGMT_PSK) &&
       		((params->wpa_pairwise & WPA_CIPHER_CCMP) ||
       		(params->wpa_pairwise & WPA_CIPHER_TKIP)))
		{
    		wpawpa2mode = 0x11;
		}
		else
		{
    		wpawpa2mode = 0x13; // WSC custom mixed mode
		}

		if (set80211param(drv, WL_PARAM_WPAWPA2MODE, wpawpa2mode,TRUE)) {
            wpa_printf(MSG_DEBUG,"%s: WPS Extended mode %x setting failed", __func__,wpawpa2mode);
			return -1;
		}

		// Set ciphersuite if WPA2PSK-TKIP
    	if ((params->wpa & WPA_PROTO_RSN) &&
        	!(params->wpa & WPA_PROTO_WPA) &&
        	(params->wpa_key_mgmt & WPA_KEY_MGMT_PSK) &&
       		!(params->wpa_pairwise & WPA_CIPHER_CCMP) &&
       		(params->wpa_pairwise & WPA_CIPHER_TKIP))
		{
        	strcpy(ciphersuite, "wpa2 tkip");
    	
			/* Set ciphersuite and commit */
			if (set80211priv(drv, WL_IOCTL_SET_CIPHERSUITE, 
					&ciphersuite, sizeof(ciphersuite))) 
			{
                wpa_printf(MSG_DEBUG,"%s: Cipher Suite %s setting failed", __func__,ciphersuite);
				return -1;
			}
		}
		// Set ciphersuite if WPAPSK-AES
    	else if (!(params->wpa & WPA_PROTO_RSN) &&
        	(params->wpa & WPA_PROTO_WPA) &&
        	(params->wpa_key_mgmt & WPA_KEY_MGMT_PSK) &&
       		(params->wpa_pairwise & WPA_CIPHER_CCMP) &&
       		!(params->wpa_pairwise & WPA_CIPHER_TKIP))
		{
        	strcpy(ciphersuite, "wpa aes-ccmp");
    	
			/* Set ciphersuite and commit */
			if (set80211priv(drv, WL_IOCTL_SET_CIPHERSUITE, 
					&ciphersuite, sizeof(ciphersuite))) 
			{
                wpa_printf(MSG_DEBUG,"%s: WPS Cipher Suite %s setting failed", __func__,ciphersuite);
				return -1;
			}
		}
	}
	else
	{
    	if ((params->wpa & WPA_PROTO_WPA) &&
        	!(params->wpa & WPA_PROTO_RSN) &&
        	(params->wpa_key_mgmt & WPA_KEY_MGMT_PSK))
        	wpawpa2mode = 0x81;
    	else if ((params->wpa & WPA_PROTO_RSN) &&
        	!(params->wpa & WPA_PROTO_WPA) &&
        	((params->wpa_key_mgmt & WPA_KEY_MGMT_PSK) ||
        	(params->wpa_key_mgmt & WPA_KEY_MGMT_FT_PSK)))
        	wpawpa2mode = 0x82;
    	else if ((params->wpa & WPA_PROTO_RSN) &&
        	(params->wpa & WPA_PROTO_WPA) &&
        	(params->wpa_key_mgmt & WPA_KEY_MGMT_PSK))
        	wpawpa2mode = 0x83;
    	else if ((params->wpa & WPA_PROTO_WPA) &&
        	!(params->wpa & WPA_PROTO_RSN) &&
        	(params->wpa_key_mgmt & WPA_KEY_MGMT_IEEE8021X))      
        	wpawpa2mode = 0x84;
    	else if ((params->wpa & WPA_PROTO_RSN) &&
        	!(params->wpa & WPA_PROTO_WPA) &&
        	(params->wpa_key_mgmt & WPA_KEY_MGMT_IEEE8021X))
        	wpawpa2mode = 0x85;
    	else if ((params->wpa & WPA_PROTO_RSN) &&
        	(params->wpa & WPA_PROTO_WPA) &&
        	(params->wpa_key_mgmt & WPA_KEY_MGMT_IEEE8021X))
        	wpawpa2mode = 0x86;
    	else
        	wpawpa2mode = 0;
        
		if (set80211param(drv, WL_PARAM_WPAWPA2MODE, wpawpa2mode,TRUE))
        {
            wpa_printf(MSG_DEBUG,"%s: Mode %x setting failed", __func__,wpawpa2mode);
			return -1;
		}
	}

   	if ((params->wpa & WPA_PROTO_WPA) &&
       	(params->wpa_pairwise & WPA_CIPHER_TKIP))
       	strcpy(ciphersuite, "wpa tkip");        
   	else if ((params->wpa & WPA_PROTO_RSN) &&
       	(params->wpa_pairwise & WPA_CIPHER_CCMP))
       	strcpy(ciphersuite, "wpa2 aes-ccmp");
   	else if ((params->wpa & WPA_PROTO_RSN) &&
       	(params->wpa_pairwise & WPA_CIPHER_TKIP))
       	strcpy(ciphersuite, "wpa2 tkip");        
   	else if ((params->wpa & WPA_PROTO_WPA) &&
       	(params->wpa_pairwise & WPA_CIPHER_CCMP))
       	strcpy(ciphersuite, "wpa aes-ccmp");
    	
	if (set80211priv(drv, WL_IOCTL_SET_CIPHERSUITE, &ciphersuite, sizeof(ciphersuite))) {
        wpa_printf(MSG_DEBUG,"%s: Cipher Suite %s setting failed", __func__,ciphersuite);
		return -1;
	}

    wpa_printf(MSG_DEBUG,"%s:configured mode=%x cipher suite=%s", __func__,wpawpa2mode,ciphersuite);
    
	return 0;
}

static int
marvell_set_ieee8021x(void *priv, struct wpa_bss_params *params)
{
	struct marvell_driver_data *drv = priv;
    
	wpa_printf(MSG_DEBUG,"%s: enabled=%d", __func__, params->enabled);

	if (!params->enabled) {
		if (drv->hapd->conf->wps_state)
			return set80211param(priv, WL_PARAM_WPAWPA2MODE, 0x10, TRUE);
		else
			return set80211param(priv, WL_PARAM_WPAWPA2MODE, 0, TRUE);
	}

    if (!params->wpa && !params->ieee802_1x) {
		hostapd_logger(drv->hapd, NULL, HOSTAPD_MODULE_DRIVER,
			HOSTAPD_LEVEL_WARNING, "No 802.1X or WPA enabled!");
		return -1;
	}

    if (params->wpa && marvell_configure_wpa(drv, params) != 0) {
		hostapd_logger(drv->hapd, NULL, HOSTAPD_MODULE_DRIVER,
			HOSTAPD_LEVEL_WARNING, "Error configuring WPA state!");
		return -1;
	}

    if (drv->hapd->conf->wps_state && !params->wpa)
	{
		/* WPS Extended Open mode setting - WPAWPA2MODE - 0x10 */
   		if( set80211param(drv, WL_PARAM_WPAWPA2MODE, 0x10,TRUE) )
		{
            wpa_printf(MSG_DEBUG,"%s: WPS Extended Open mode setting failed\n", __func__);
    		return -1;
		}
	}

    return 0;
}


static int
marvell_del_key(void *priv, const u8 *addr, int key_idx)
{
	struct marvell_driver_data *drv = priv;
	struct wlreq_del_key wk;
	int ret;

	wpa_printf(MSG_DEBUG,"%s: addr=%s key_idx=%d",
		__func__, ether_sprintf(addr), key_idx);

	memset(&wk, 0, sizeof(wk));
	if (addr != NULL) {
		memcpy(wk.idk_macaddr, addr, IEEE80211_ADDR_LEN);
		wk.idk_keyix = (u8) WL_KEYIX_NONE;
	} else {
		wk.idk_keyix = key_idx;
	}
	ret = set80211param(drv, WL_PARAM_DELKEYS, (int)&wk,FALSE);
	if (ret < 0) {
		wpa_printf(MSG_DEBUG, "%s: Failed to delete key (addr %s"
			   " key_idx %d)", __func__, ether_sprintf(addr),
			   key_idx);
	}

	return ret;
}

static int
marvell_set_key(const char *ifname,void *priv, enum wpa_alg alg,
	     const u8 *addr, int key_idx, int set_tx, const u8 *seq,
	     size_t seq_len, const u8 *key, size_t key_len)
{
	struct marvell_driver_data *drv = priv;
	struct wlreq_key wk;
	u_int8_t cipher;
	int ret;

	if (alg == WPA_ALG_NONE)
		return marvell_del_key(priv, addr, key_idx);

	wpa_printf(MSG_DEBUG,
		"%s: alg=%d addr=%s key_idx=%d\n",
		__func__, alg, ether_sprintf(addr), key_idx);

	switch (alg) {
	case WPA_ALG_WEP:
		cipher = WL_CIPHER_WEP104;
		break;
	case WPA_ALG_TKIP:
		cipher = WL_CIPHER_TKIP;
		break;
	case WPA_ALG_CCMP:
		cipher = WL_CIPHER_CCMP;
		break;
	default:
		printf("%s: unknown/unsupported algorithm %d\n",
			__func__, alg);
		return -1;
	}

	if (key_len > sizeof(wk.ik_keydata)) {
		printf("%s: key length %lu too big\n", __func__,
		       (unsigned long) key_len);
		return -3;
	}

	memset(&wk, 0, sizeof(wk));
	wk.ik_type = cipher;
	wk.ik_flags = WL_KEY_RECV | WL_KEY_XMIT;
	if (addr == NULL) {
		memset(wk.ik_macaddr, 0xff, IEEE80211_ADDR_LEN);
		wk.ik_keyix = key_idx;
		wk.ik_flags |= WL_KEY_DEFAULT;
	} else {
		memcpy(wk.ik_macaddr, addr, IEEE80211_ADDR_LEN);
		wk.ik_keyix = WL_KEYIX_NONE;
	}
	wk.ik_keylen = key_len;
	memcpy(wk.ik_keydata, key, key_len);
    
	ret = set80211param(drv, WL_PARAM_SETKEYS, (int)&wk,FALSE);
	if (ret < 0) {
		wpa_printf(MSG_DEBUG, "%s: Failed to set key (addr %s"
			   " key_idx %d alg '%d' key_len %lu txkey %d)",
			   __func__, ether_sprintf(wk.ik_macaddr), key_idx,
			   alg, (unsigned long) key_len, set_tx);
	}

	return ret;
}

static int 
marvell_flush(void *priv)
{
	u8 allsta[IEEE80211_ADDR_LEN];
	memset(allsta, 0xff, IEEE80211_ADDR_LEN);
	return marvell_sta_deauth(priv, NULL, allsta, 3); /*IEEEtypes_REASON_DEAUTH_LEAVING*/
}


static int
marvell_read_sta_driver_data(void *priv, struct hostap_sta_driver_data *data,
				const u8 *addr)
{
	return 0;
}


static int
marvell_set_opt_ie(void *priv, const u8 *ie, size_t ie_len)
{
	/*
	 * Do nothing; we setup parameters at startup that define the
	 * contents of the beacon information element.
	 */
	return 0;
}

static int
marvell_sta_deauth(void *priv, const u8 *own_addr, const u8 *addr, int reason_code)
{
	struct marvell_driver_data *drv = priv;
	struct wlreq_mlme mlme;

	wpa_printf(MSG_DEBUG,
		"%s: addr=%s reason_code=%d\n",
		__func__, ether_sprintf(addr), reason_code);

	mlme.im_op = WL_MLME_DEAUTH;
	mlme.im_reason = reason_code;
	memcpy(mlme.im_macaddr, addr, IEEE80211_ADDR_LEN);

    return set80211param(drv, WL_PARAM_MLME_REQ, (int)&mlme,FALSE);
}

static void marvell_raw_receive(void *ctx, const u8 *src_addr, const u8 *buf,
				size_t len)
{
	struct marvell_driver_data *drv = ctx;
	const struct ieee80211_mgmt *mgmt;
	u16 fc;
	union wpa_event_data event;

	/* Send Probe Request information to WPS processing */
	if (len < IEEE80211_HDRLEN + sizeof(mgmt->u.probe_req))
		return;
	mgmt = (const struct ieee80211_mgmt *) buf;

	fc = le_to_host16(mgmt->frame_control);
	if (WLAN_FC_GET_TYPE(fc) != WLAN_FC_TYPE_MGMT ||
	    WLAN_FC_GET_STYPE(fc) != WLAN_FC_STYPE_PROBE_REQ)
		return;

	os_memset(&event, 0, sizeof(event));
	event.rx_probe_req.sa = mgmt->sa;
	event.rx_probe_req.ie = mgmt->u.probe_req.variable;
	event.rx_probe_req.ie_len =
		len - (IEEE80211_HDRLEN + sizeof(mgmt->u.probe_req));
	wpa_supplicant_event(drv->hapd, EVENT_RX_PROBE_REQ, &event);
}

static int
marvell_set_wsc_ie(void *priv, const u8 *iebuf, int iebuflen, u32 frametype)
{
	u8 buf[512];
	struct wlreq_set_appie * app_ie;

	wpa_printf(MSG_DEBUG, "%s buflen = %d", __func__, iebuflen);

	app_ie = (struct wlreq_set_appie *)buf;
	app_ie->appFrmType = frametype;
	app_ie->appBufLen = iebuflen;

    if (iebuf != NULL)
    	memcpy(&(app_ie->appBuf[0]), iebuf , iebuflen );
    else
    {
        memset(&(app_ie->appBuf[0]),0x00, 8);
        app_ie->appBufLen = 8;
    }

	return set80211priv(priv, WL_IOCTL_SET_APPIE, app_ie,
			8 + app_ie->appBufLen);

}

static int
marvell_set_ap_wps_ie(void *priv, const struct wpabuf *beacon,
		      const struct wpabuf *proberesp,const struct wpabuf *assocresp)
{
	if (marvell_set_wsc_ie(priv, beacon ? wpabuf_head(beacon) : NULL,
			       beacon ? wpabuf_len(beacon) : 0,
			       WL_APPIE_FRAMETYPE_BEACON))
		return -1;
    
	return marvell_set_wsc_ie(priv,
				  proberesp ? wpabuf_head(proberesp) : NULL,
				  proberesp ? wpabuf_len(proberesp): 0,
				  WL_APPIE_FRAMETYPE_PROBE_RESP);
}


static int
marvell_new_sta(struct marvell_driver_data *drv, u8 addr[IEEE80211_ADDR_LEN])
{
	struct wlreq_ie ie;
	struct hostapd_data *hapd = drv->hapd;

	hostapd_logger(hapd, addr, HOSTAPD_MODULE_IEEE80211,
		HOSTAPD_LEVEL_INFO, "associated");

    /* Get RSN IE */
	memset(&ie, 0, sizeof(ie));
	memcpy(ie.macAddr, addr, 6);
    ie.IEtype = WLAN_EID_RSN;

	if (get80211priv(drv, WL_IOCTL_GET_IE, &ie, sizeof(ie))<0) {
       	wpa_printf(MSG_DEBUG,"%s: IOCTL Get IE failed\n", __func__);
		return -1;		
	}

	if (ie.IELen == 0) {
     	wpa_printf(MSG_DEBUG,"%s: STA addr %s RSN IE Length is zero\n", __func__, ether_sprintf(addr));
	}

	drv_event_assoc(hapd, addr, ie.IE, ie.IELen);

	if (memcmp(addr, drv->acct_mac, ETH_ALEN) == 0) {
		/* Cached accounting data is not valid anymore. */
		memset(drv->acct_mac, 0, ETH_ALEN);
		memset(&drv->acct_data, 0, sizeof(drv->acct_data));
	}

	return 0;
}

static void
marvell_wireless_event_wireless_custom(struct marvell_driver_data *drv,
				       char *custom)
{
//	wpa_printf(MSG_DEBUG, "Custom wireless event: '%s'", custom);

	if (strncmp(custom, "MLME-MICHAELMICFAILURE.indication", 33) == 0) {
		char *pos;
		u8 addr[ETH_ALEN];
		pos = strstr(custom, "addr=");
		if (pos == NULL) {
			wpa_printf(MSG_DEBUG,
				      "MLME-MICHAELMICFAILURE.indication "
				      "without sender address ignored");
			return;
		}
		pos += 5;
		if (hwaddr_aton(pos, addr) == 0) {
			union wpa_event_data data;
			os_memset(&data, 0, sizeof(data));
			data.michael_mic_failure.unicast = 1;
			data.michael_mic_failure.src = NULL;
			wpa_supplicant_event(drv->hapd,
					     EVENT_MICHAEL_MIC_FAILURE, &data);
		} else {
			wpa_printf(MSG_DEBUG,
				      "MLME-MICHAELMICFAILURE.indication "
				      "with invalid MAC address");
		}
	}
    else if (strncmp(custom, "STA-TRAFFIC-STAT", 16) == 0)
    {
		char *key, *value;
		u32 val;
		key = custom;
		while ((key = strchr(key, '\n')) != NULL) {
			key++;
			value = strchr(key, '=');
			if (value == NULL)
				continue;
			*value++ = '\0';
			val = strtoul(value, NULL, 10);
			if (strcmp(key, "mac") == 0)
				hwaddr_aton(value, drv->acct_mac);
			else if (strcmp(key, "rx_packets") == 0)
				drv->acct_data.rx_packets = val;
			else if (strcmp(key, "tx_packets") == 0)
				drv->acct_data.tx_packets = val;
			else if (strcmp(key, "rx_bytes") == 0)
				drv->acct_data.rx_bytes = val;
			else if (strcmp(key, "tx_bytes") == 0)
				drv->acct_data.tx_bytes = val;
			key = value;
		}
	}
#ifdef MRVL_WPS2
    else if (strncmp(custom, "mlme-probe_request", strlen("mlme-probe_request")) == 0)
#else
    else if (strncmp(custom, "mlme-probe_request", 18) == 0)
#endif
    {
#define MLME_FRAME_TAG_SIZE  20 
#ifdef MRVL_WPS2
		s16 len = WPA_GET_LE16(custom + strlen("mlme-probe_request"));
#else
		s16 len = *(custom + 18);
#endif
		if (len < 0) {
			wpa_printf(MSG_DEBUG, "mlme-probe_request "
				   "length %d", len);
			return;
		}
        
		marvell_raw_receive(drv, NULL, (u8 *) custom + MLME_FRAME_TAG_SIZE, len);
	}
    else if (strstr(custom, "Unexpected event - External recovery recommended: ") != NULL)
    {
        printf("received recovery event rebooting\n");
        system("reboot");
    }
}

static void
marvell_wireless_event_wireless(struct marvell_driver_data *drv,
					    char *data, int len)
{
	struct iw_event iwe_buf, *iwe = &iwe_buf;
	char *pos, *end, *custom, *buf;

	pos = data;
	end = data + len;

	while (pos + IW_EV_LCP_LEN <= end) {
		/* Event data may be unaligned, so make a local, aligned copy
		 * before processing. */
		memcpy(&iwe_buf, pos, IW_EV_LCP_LEN);
//		wpa_printf(MSG_MSGDUMP, "Wireless event: cmd=0x%x len=%d", iwe->cmd, iwe->len);
		if (iwe->len <= IW_EV_LCP_LEN)
			return;

		custom = pos + IW_EV_POINT_LEN;
		if (drv->we_version > 18 &&
		    (iwe->cmd == IWEVMICHAELMICFAILURE ||
		     iwe->cmd == IWEVASSOCREQIE ||
		     iwe->cmd == IWEVCUSTOM)) {
			/* WE-19 removed the pointer from struct iw_point */
			char *dpos = (char *) &iwe_buf.u.data.length;
			int dlen = dpos - (char *) &iwe_buf;
			memcpy(dpos, pos + IW_EV_LCP_LEN,
			       sizeof(struct iw_event) - dlen);
		} else {
			memcpy(&iwe_buf, pos, sizeof(struct iw_event));
			custom += IW_EV_POINT_OFF;
		}

		switch (iwe->cmd) {
		case IWEVEXPIRED:
			drv_event_disassoc(drv->hapd,
					   (u8 *) iwe->u.addr.sa_data);
			break;
		case IWEVREGISTERED:
			/* First reset the station state so that if the station did not
			* send explicit deauth, it will still be ok.
			*/
			drv_event_disassoc(drv->hapd, (u8 *) iwe->u.addr.sa_data);
			marvell_new_sta(drv, (u8 *) iwe->u.addr.sa_data);
			break;
		case IWEVCUSTOM:
			if (custom + iwe->u.data.length > end)
				return;
			buf = malloc(iwe->u.data.length + 1);
			if (buf == NULL)
				return;		/* XXX */
			memcpy(buf, custom, iwe->u.data.length);
			buf[iwe->u.data.length] = '\0';
			marvell_wireless_event_wireless_custom(drv, buf);
			free(buf);
			break;
		}

		pos += iwe->len;
	}
}
static void
marvell_wireless_event_rtm_newlink(void *ctx,
				   struct ifinfomsg *ifi, u8 *buf, size_t len)
{
	struct marvell_driver_data *drv = ctx;
	int attrlen, rta_len;
	struct rtattr *attr;

	if (ifi->ifi_index != drv->ifindex)
		return;

	attrlen = len;
	attr = (struct rtattr *) buf;

	rta_len = RTA_ALIGN(sizeof(struct rtattr));
	while (RTA_OK(attr, attrlen)) {
		if (attr->rta_type == IFLA_WIRELESS) {
			marvell_wireless_event_wireless(
				drv, ((char *) attr) + rta_len,
				attr->rta_len - rta_len);
		}
		attr = RTA_NEXT(attr, attrlen);
	}
}

static int
marvell_get_we_version(struct marvell_driver_data *drv)
{
	struct iw_range *range;
	struct iwreq iwr;
	int minlen;
	size_t buflen;

	drv->we_version = 0;

	/*
	 * Use larger buffer than struct iw_range in order to allow the
	 * structure to grow in the future.
	 */
	buflen = sizeof(struct iw_range) + 500;
	range = os_zalloc(buflen);
	if (range == NULL)
		return -1;

	memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
	iwr.u.data.pointer = (caddr_t) range;
	iwr.u.data.length = buflen;

	minlen = ((char *) &range->enc_capa) - (char *) range +
		sizeof(range->enc_capa);

	if (ioctl(drv->ioctl_sock, SIOCGIWRANGE, &iwr) < 0) {
		perror("ioctl[SIOCGIWRANGE]");
		free(range);
		return -1;
	} else if (iwr.u.data.length >= minlen &&
		   range->we_version_compiled >= 18) {
		wpa_printf(MSG_DEBUG, "SIOCGIWRANGE: WE(compiled)=%d "
			   "WE(source)=%d enc_capa=0x%x",
			   range->we_version_compiled,
			   range->we_version_source,
			   range->enc_capa);
		drv->we_version = range->we_version_compiled;
	}

	free(range);
	return 0;
}


static int
marvell_wireless_event_init(struct marvell_driver_data *drv)
{
	struct netlink_config *cfg;

	marvell_get_we_version(drv);

	cfg = os_zalloc(sizeof(*cfg));
	if (cfg == NULL)
		return -1;
	cfg->ctx = drv;
	cfg->newlink_cb = marvell_wireless_event_rtm_newlink;
	drv->netlink = netlink_init(cfg);
	if (drv->netlink == NULL) {
		os_free(cfg);
		return -1;
	}

	return 0;
}

static int
marvell_send_ether(void *priv, const u8 *dst, const u8 *src, u16 proto, const u8 *data, size_t data_len)
{
	struct marvell_driver_data *drv = priv;
	unsigned char buf[3000];
	unsigned char *bp = buf;
	struct l2_ethhdr *eth;
	size_t len;
	int status;

	/*
	 * Prepend the Ethernet header.  If the caller left us
	 * space at the front we could just insert it but since
	 * we don't know we copy to a local buffer.  Given the frequency
	 * and size of frames this probably doesn't matter.
	 */
	len = data_len + sizeof(struct l2_ethhdr);
	if (len > sizeof(buf)) {
		bp = malloc(len);
		if (bp == NULL) {
			printf("EAPOL frame discarded, cannot malloc temp "
			       "buffer of size %lu!\n", (unsigned long) len);
			return -1;
		}
	}
	eth = (struct l2_ethhdr *) bp;
	memcpy(eth->h_dest, dst, ETH_ALEN);
	memcpy(eth->h_source, src, ETH_ALEN);
	eth->h_proto = htons(proto);
	memcpy(eth+1, data, data_len);

	wpa_hexdump(MSG_MSGDUMP, "TX Ether", bp, len);

	status = l2_packet_send(drv->sock_xmit, dst, proto, bp, len);

	if (bp != buf)
		free(bp);
	return status;
}

static int
marvell_send_eapol(void *priv, const u8 *addr, const u8 *data, size_t data_len,
           int encrypt, const u8 *own_addr)
{
	struct marvell_driver_data *drv = priv;
	unsigned char buf[3000];
	unsigned char *bp = buf;
	struct l2_ethhdr *eth;
	size_t len;
	int status;

	/*
	 * Prepend the Ethernet header.  If the caller left us
	 * space at the front we could just insert it but since
	 * we don't know we copy to a local buffer.  Given the frequency
	 * and size of frames this probably doesn't matter.
	 */
	len = data_len + sizeof(struct l2_ethhdr);
	if (len > sizeof(buf)) {
		bp = malloc(len);
		if (bp == NULL) {
			printf("EAPOL frame discarded, cannot malloc temp "
			       "buffer of size %lu!\n", (unsigned long) len);
			return -1;
		}
	}
	eth = (struct l2_ethhdr *) bp;
	memcpy(eth->h_dest, addr, ETH_ALEN);
	memcpy(eth->h_source, own_addr, ETH_ALEN);
	eth->h_proto = host_to_be16(ETH_P_EAPOL);
	memcpy(eth+1, data, data_len);

	wpa_hexdump(MSG_MSGDUMP, "TX EAPOL", bp, len);

	status = l2_packet_send(drv->sock_xmit, addr, ETH_P_EAPOL, bp, len);

	if (bp != buf)
		free(bp);
	return status;
}

static void
handle_read(void *ctx, const u8 *src_addr, const u8 *buf, size_t len)
{
	struct marvell_driver_data *drv = ctx;
	drv_event_eapol_rx(drv->hapd, src_addr, buf + sizeof(struct l2_ethhdr),
			   len - sizeof(struct l2_ethhdr));
}


static void *
marvell_init(struct hostapd_data *hapd, struct wpa_init_params *params)
{
	struct marvell_driver_data *drv;
	struct ifreq ifr;
	char brname[IFNAMSIZ];

	drv = os_zalloc(sizeof(struct marvell_driver_data));
	if (drv == NULL) {
		printf("Could not allocate memory for marvell driver data\n");
		return NULL;
	}

	drv->hapd = hapd;
	drv->ioctl_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (drv->ioctl_sock < 0) {
		perror("socket[PF_INET,SOCK_DGRAM]");
		goto bad;
	}
	memcpy(drv->iface, params->ifname, sizeof(drv->iface));

	memset(&ifr, 0, sizeof(ifr));
	os_strlcpy(ifr.ifr_name, drv->iface, sizeof(ifr.ifr_name));
	if (ioctl(drv->ioctl_sock, SIOCGIFINDEX, &ifr) != 0) {
		perror("ioctl(SIOCGIFINDEX)");
		goto bad;
	}
	drv->ifindex = ifr.ifr_ifindex;

	drv->sock_xmit = l2_packet_init(drv->iface, NULL, ETH_P_EAPOL,
					handle_read, drv, 1);
	if (drv->sock_xmit == NULL)
		goto bad;

	if (l2_packet_get_own_addr(drv->sock_xmit, params->own_addr))
		goto bad;

	if (params->bridge[0]) {
		wpa_printf(MSG_DEBUG, "Configure bridge %s for EAPOL traffic.",
			   params->bridge[0]);
		drv->sock_recv = l2_packet_init(params->bridge[0], NULL,
						ETH_P_EAPOL, handle_read, drv,
						1);
		if (drv->sock_recv == NULL)
			goto bad;
	} else if (linux_br_get(brname, drv->iface) == 0) {
		wpa_printf(MSG_DEBUG, "Interface in bridge %s; configure for "
			   "EAPOL receive", brname);
		drv->sock_recv = l2_packet_init(brname, NULL, ETH_P_EAPOL,
						handle_read, drv, 1);
		if (drv->sock_recv == NULL)
			goto bad;
        } else
		    drv->sock_recv = drv->sock_xmit;

	if (marvell_wireless_event_init(drv))
		goto bad;
            
	/* for wps with open security and ieee8021x=0 mode */
	if (drv->hapd->conf->wps_state && !drv->hapd->conf->wpa)
	{
   		if ( set80211param(drv, WL_PARAM_WPAWPA2MODE, 0x10, TRUE) )
		{
			wpa_printf(MSG_DEBUG,"%s: WPS Extended Open mode setting failed\n", __func__);
			return NULL;
		}
	}

	return drv;
bad:
	if (drv->sock_xmit != NULL)
		l2_packet_deinit(drv->sock_xmit);
	if (drv->ioctl_sock >= 0)
		close(drv->ioctl_sock);
	if (drv != NULL)
		free(drv);
	return NULL;
}


static void
marvell_deinit(void* priv)
{
	struct marvell_driver_data *drv = priv;

	netlink_deinit(drv->netlink);
	(void) linux_set_iface_flags(drv->ioctl_sock, drv->iface, 0);
	if (drv->ioctl_sock >= 0)
		close(drv->ioctl_sock);
	if (drv->sock_recv != NULL && drv->sock_recv != drv->sock_xmit)
		l2_packet_deinit(drv->sock_recv);
	if (drv->sock_xmit != NULL)
		l2_packet_deinit(drv->sock_xmit);
	if (drv->sock_raw)
		l2_packet_deinit(drv->sock_raw);
	free(drv);

}

static int
marvell_set_ssid(void *priv, const u8 *buf, int len)
{
	struct marvell_driver_data *drv = priv;
	struct iwreq iwr;

	memset(&iwr, 0, sizeof(iwr));
	strncpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
	iwr.u.essid.flags = 1; /* SSID active */
	iwr.u.essid.pointer = (caddr_t) buf;
	iwr.u.essid.length = len + 1;

	if (ioctl(drv->ioctl_sock, SIOCSIWESSID, &iwr) < 0) {
		perror("ioctl[SIOCSIWESSID]");
		printf("len=%d\n", len);
		return -1;
	}
	if (drv->hapd->conf->wps_state )
	{
    	if (ioctl(drv->ioctl_sock, SIOCSIWCOMMIT, &iwr) < 0) {
        	printf("ioctl[SIOCSIWCOMMIT]");
        	return -1;
    	}
	}
	return 0;
}

static int
marvell_get_ssid(void *priv, u8 *buf, int len)
{
	struct marvell_driver_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->iface, IFNAMSIZ);
	iwr.u.essid.pointer = (caddr_t) buf;
	iwr.u.essid.length = len;

	if (ioctl(drv->ioctl_sock, SIOCGIWESSID, &iwr) < 0) {
		perror("ioctl[SIOCGIWESSID]");
		ret = -1;
	} else
		ret = iwr.u.essid.length;// -1; /*remove the '\0' */

	return ret;
}

static int
marvell_set_countermeasures(void *priv, int enabled)
{
	struct marvell_driver_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s: enabled=%d", __FUNCTION__, enabled);

	return set80211param(drv, WL_PARAM_COUNTERMEASURES, enabled,FALSE);
}

static int
marvell_commit (void *priv)
{
	struct marvell_driver_data *drv = priv;
	struct iwreq iwr;

	memset(&iwr, 0, sizeof(iwr));
	strncpy(iwr.ifr_name, drv->iface, IFNAMSIZ);

    if (ioctl(drv->ioctl_sock, SIOCSIWCOMMIT, &iwr) < 0) {
		printf("ioctl[SIOCSIWCOMMIT]");
		return -1;
	}

	return 0;
}

const struct wpa_driver_ops wpa_driver_marvell_ops = {
	.name			        = "marvell",
	.hapd_init			    = marvell_init,
	.deinit			        = marvell_deinit,
	.set_ieee8021x		    = marvell_set_ieee8021x,
	.set_key		        = marvell_set_key,
	.flush			        = marvell_flush,
	.set_generic_elem	    = marvell_set_opt_ie,
	.read_sta_data		    = marvell_read_sta_driver_data,
	.hapd_send_eapol	    = marvell_send_eapol,
	.sta_deauth		        = marvell_sta_deauth,
	.hapd_set_ssid		    = marvell_set_ssid,
	.hapd_get_ssid		    = marvell_get_ssid,
	.set_countermeasures    = marvell_set_countermeasures,
    .send_ether             = marvell_send_ether,
	.set_ap_wps_ie		    = marvell_set_ap_wps_ie,
	.commit                 = marvell_commit,
};
