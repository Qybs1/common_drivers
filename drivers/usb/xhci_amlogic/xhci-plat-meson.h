/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xhci-plat.h - xHCI host controller driver platform Bus Glue.
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 */

#ifndef _XHCI_PLAT_MESON_H
#define _XHCI_PLAT_MESON_H

#include "xhci-meson.h"	/* for hcd_to_xhci() */

struct aml_xhci_plat_priv {
	const char *firmware_name;
	unsigned long long quirks;
	int (*plat_setup)(struct usb_hcd *);
	void (*plat_start)(struct usb_hcd *);
	int (*init_quirk)(struct usb_hcd *);
	int (*suspend_quirk)(struct usb_hcd *);
	int (*resume_quirk)(struct usb_hcd *);
};

#define aml_hcd_to_xhci_priv(h) ((struct aml_xhci_plat_priv *)hcd_to_xhci(h)->priv)
#define aml_xhci_to_priv(x) ((struct aml_xhci_plat_priv *)(x)->priv)
#endif	/* _XHCI_PLAT_H */
