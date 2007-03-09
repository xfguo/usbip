/*
 * $Id: usbip_common.h 197 2006-10-24 10:20:25Z taka-hir $
 *
 * Copyright (C) 2003-2006 Takahiro Hirofuchi <taka-hir@is.naist.jp>
 *
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 */

#ifndef  __VHCI_COMMON_H
#define  __VHCI_COMMON_H


#include <linux/usb.h>
#include <net/sock.h>
#include <asm/byteorder.h>

/*-------------------------------------------------------------------------*/

/*
 * define macros to print messages
 */

/**
 * udbg - print debug messages if CONFIG_USB_DEBUG is defined
 * @fmt:
 * @args:
 */

#ifdef CONFIG_USB_DEBUG

#define udbg(fmt, args...)						\
	do{								\
		printk(KERN_DEBUG "%-10s:(%s,%d) %s: " fmt,		\
			(in_interrupt() ? "interrupt" : (current)->comm),\
			__FILE__, __LINE__, __FUNCTION__ , ##args);	\
	}while(0)

#else  /* CONFIG_USB_DEBUG */

#define udbg(fmt, args...)		do{ }while(0)

#endif /* CONFIG_USB_DEBUG */


enum {
	usbip_debug_xmit	= (1 << 0),
	usbip_debug_sysfs	= (1 << 1),
	usbip_debug_urb		= (1 << 2),
	usbip_debug_eh		= (1 << 3),

	usbip_debug_stub_cmp	= (1 << 8),
	usbip_debug_stub_dev	= (1 << 9),
	usbip_debug_stub_rx	= (1 << 10),
	usbip_debug_stub_tx	= (1 << 11),

	usbip_debug_vhci_rh	= (1 << 8),
	usbip_debug_vhci_hc	= (1 << 9),
	usbip_debug_vhci_rx	= (1 << 10),
	usbip_debug_vhci_tx	= (1 << 11),
	usbip_debug_vhci_sysfs  = (1 << 12)
};

#define dbg_flag_xmit		(usbip_debug_flag & usbip_debug_xmit)
#define dbg_flag_vhci_rh	(usbip_debug_flag & usbip_debug_vhci_rh)
#define dbg_flag_vhci_hc	(usbip_debug_flag & usbip_debug_vhci_hc)
#define dbg_flag_vhci_rx	(usbip_debug_flag & usbip_debug_vhci_rx)
#define dbg_flag_vhci_tx	(usbip_debug_flag & usbip_debug_vhci_tx)
#define dbg_flag_vhci_sysfs	(usbip_debug_flag & usbip_debug_vhci_sysfs)
#define dbg_flag_stub_rx	(usbip_debug_flag & usbip_debug_stub_rx)
#define dbg_flag_stub_tx	(usbip_debug_flag & usbip_debug_stub_tx)

extern unsigned long usbip_debug_flag;
extern struct device_attribute dev_attr_usbip_debug;

#define dbg_with_flag(flag, fmt, args...)		\
	do {						\
		if(flag & usbip_debug_flag) 		\
			udbg(fmt , ##args);		\
	} while(0)

#define dbg_sysfs(fmt, args...)	dbg_with_flag(usbip_debug_sysfs, fmt , ##args)
#define dbg_xmit(fmt, args...)	dbg_with_flag(usbip_debug_xmit,  fmt , ##args)
#define dbg_urb(fmt, args...)	dbg_with_flag(usbip_debug_urb,   fmt , ##args)
#define dbg_eh(fmt, args...)	dbg_with_flag(usbip_debug_eh,   fmt , ##args)

#define dbg_vhci_rh(fmt, args...) dbg_with_flag(usbip_debug_vhci_rh, fmt , ##args)
#define dbg_vhci_hc(fmt, args...) dbg_with_flag(usbip_debug_vhci_hc, fmt , ##args)
#define dbg_vhci_rx(fmt, args...) dbg_with_flag(usbip_debug_vhci_rx, fmt , ##args)
#define dbg_vhci_tx(fmt, args...) dbg_with_flag(usbip_debug_vhci_tx, fmt , ##args)
#define dbg_vhci_sysfs(fmt, args...) dbg_with_flag(usbip_debug_vhci_sysfs, fmt , ##args)

#define dbg_stub_cmp(fmt, args...) dbg_with_flag(usbip_debug_stub_cmp, fmt , ##args)
#define dbg_stub_rx(fmt, args...) dbg_with_flag(usbip_debug_stub_rx, fmt , ##args)
#define dbg_stub_tx(fmt, args...) dbg_with_flag(usbip_debug_stub_tx, fmt , ##args)


/**
 * uerr - print error messages
 * @fmt:
 * @args:
 */
#define uerr(fmt, args...)						\
	do {								\
		printk(KERN_ERR "%-10s: ***ERROR*** (%s,%d) %s: " fmt,	\
			(in_interrupt() ? "interrupt" : (current)->comm),\
			__FILE__, __LINE__, __FUNCTION__ , ##args);	\
	} while(0)

/**
 * uinfo - print information messages
 * @fmt:
 * @args:
 */
#define uinfo(fmt, args...)					\
	do {							\
		printk(KERN_INFO "usbip: " fmt , ## args);	\
	} while(0)


/*-------------------------------------------------------------------------*/

/*
 * USB/IP packet headers.
 * At now, we define 4 packet types:
 *
 *  - CMD_SUBMIT transfers a USB request. This is corresponding to usb_submit_urb().
 *  - RET_RETURN transfers a result of a USB request.
 *  - CMD_UNLINK transfers an unlink request of a pending USB request.
 *  - RET_UNLINK transfers an unlink request of a pending USB request.
 *
 * TODO:
 *
 *  - inter-operability between other OSs
 */

/*
 * A basic header followed by other additional headers.
 */
struct usbip_header_basic {
#define USBIP_CMD_SUBMIT	0x0001
#define USBIP_CMD_UNLINK	0x0002
#define USBIP_RET_SUBMIT	0x0003
#define USBIP_RET_UNLINK	0x0004
	__u32 command;

	__u32 busnum;
	__u32 devnum;
	__u32 seqnum; /* seaquencial number which identifies URBs */
	__u32 pipe;
} __attribute__ ((packed));

/*
 * An additional header for a CMD_SUBMIT packet.
 */
struct usbip_header_cmd_submit {
	__u32 transfer_flags;
	__s32 transfer_buffer_length;
	__s32 bandwidth;
	__s32 start_frame;
	__s32 number_of_packets;
	__s32 interval;
	unsigned char setup[8]; /* CTRL only */
}__attribute__ ((packed));

/*
 * An additional header for a RET_SUBMIT packet.
 */
struct usbip_header_ret_submit {
	__s32 status;
	__s32 actual_length; /* returned data length */
	__s32 bandwidth;
	__s32 start_frame; /* ISO and INT */
	__s32 number_of_packets;  /* ISO only */
	__s32 error_count; /* ISO only */
}__attribute__ ((packed));

/*
 * An additional header for a CMD_UNLINK packet.
 */
struct usbip_header_cmd_unlink {
	__u32 seqnum; /* URB's seqnum which will be unlinked */
}__attribute__ ((packed));


/*
 * An additional header for a RET_UNLINK packet.
 */
struct usbip_header_ret_unlink {
	__s32 status;
}__attribute__ ((packed));


/* the same as usb_iso_packet_descriptor but packed for pdu */
struct usbip_iso_packet_descriptor {
	__u32 offset;
	__u32 length;            /* expected length */
	__u32 actual_length;
	__u32 status;
}__attribute__ ((packed));


/*
 * All usbip packets use a common header to keep code simple.
 */
struct usbip_header {
	struct usbip_header_basic base;

	union {
		struct usbip_header_cmd_submit	cmd_submit;
		struct usbip_header_ret_submit	ret_submit;
		struct usbip_header_cmd_unlink	cmd_unlink;
		struct usbip_header_ret_unlink	ret_unlink;
	} u;
}__attribute__ ((packed));




/*-------------------------------------------------------------------------*/


int usbip_xmit(int , struct socket *, char *, int , int );
int usbip_sendmsg(struct socket *, struct msghdr *, int );


static inline int interface_to_busnum(struct usb_interface *interface)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	return udev->bus->busnum;
}

static inline int interface_to_devnum(struct usb_interface *interface)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	return udev->devnum;
}

static inline int interface_to_infnum(struct usb_interface *interface)
{
	return interface->cur_altsetting->desc.bInterfaceNumber;
}

int setnodelay(struct socket *);
int setquickack(struct socket *);
int setkeepalive(struct socket *socket);
void setreuse(struct socket *);
struct socket *sockfd_to_socket(unsigned int);
int set_sockaddr(struct socket *socket, struct sockaddr_storage *ss);


void usbip_dump_urb (struct urb *purb);
void usbip_dump_header(struct usbip_header *pdu);


struct usbip_device;

struct usbip_task {
	struct task_struct *thread;
	struct completion thread_done;
	char *name;
	void (*loop_ops)(struct usbip_task *);
};

enum usbip_side {
	USBIP_VHCI,
	USBIP_STUB,
};

enum usbip_status {
	/* sdev is available. */
	SDEV_ST_AVAILABLE = 0x01,
	/* sdev is now used. */
	SDEV_ST_USED,
	/* sdev is unusable because of a fatal error. */
	SDEV_ST_ERROR,

	/* vdev does not connect a remote device. */
	VDEV_ST_NULL,
	/* vdev is used, but the USB address is not assigned yet */
	VDEV_ST_NOTASSIGNED,
	VDEV_ST_USED,
	VDEV_ST_ERROR
};

/* a common structure for stub_device and vhci_device */
struct usbip_device{
	enum usbip_side side;

	enum usbip_status status;

	/* lock for status */
	spinlock_t lock;

	struct socket *tcp_socket;

	struct usbip_task tcp_rx;
	struct usbip_task tcp_tx;

	/* event handler */
#define USBIP_EH_SHUTDOWN	(1 << 0)
#define USBIP_EH_BYE		(1 << 1)
#define USBIP_EH_RESET		(1 << 2)
#define USBIP_EH_UNUSABLE	(1 << 3)

#define SDEV_EVENT_REMOVED     		( USBIP_EH_SHUTDOWN | USBIP_EH_RESET | USBIP_EH_BYE )
#define	SDEV_EVENT_DOWN	       		( USBIP_EH_SHUTDOWN | USBIP_EH_RESET )
#define	SDEV_EVENT_ERROR_TCP   		( USBIP_EH_SHUTDOWN | USBIP_EH_RESET )
#define	SDEV_EVENT_ERROR_SUBMIT		( USBIP_EH_SHUTDOWN | USBIP_EH_RESET )
#define	SDEV_EVENT_ERROR_MALLOC		( USBIP_EH_SHUTDOWN | USBIP_EH_UNUSABLE )

#define	VDEV_EVENT_REMOVED     		( USBIP_EH_SHUTDOWN | USBIP_EH_BYE )
#define	VDEV_EVENT_DOWN	       		( USBIP_EH_SHUTDOWN | USBIP_EH_RESET )
#define	VDEV_EVENT_ERROR_TCP   		( USBIP_EH_SHUTDOWN | USBIP_EH_RESET )
#define	VDEV_EVENT_ERROR_MALLOC		( USBIP_EH_SHUTDOWN | USBIP_EH_UNUSABLE)

	unsigned long event;
	struct usbip_task eh;
	wait_queue_head_t eh_waitq;

	struct eh_ops {
		void (*shutdown)(struct usbip_device *);
		void (*reset)(struct usbip_device *);
		void (*unusable)(struct usbip_device *);
	} eh_ops;
};


void usbip_task_init(struct usbip_task *ut, char *, void (*loop_ops)(struct usbip_task *));

void usbip_start_threads(struct usbip_device *ud);
void usbip_stop_threads(struct usbip_device *ud);
int usbip_thread(void *param);

void usbip_pack_pdu(struct usbip_header *pdu, struct urb *urb, int cmd, int pack);
void usbip_header_correct_endian(struct usbip_header *pdu, int send);
/* some members of urb must be substituted before. */
int usbip_recv_xbuff(struct usbip_device *ud, struct urb *urb);
/* some members of urb must be substituted before. */
int usbip_recv_iso(struct usbip_device *ud, struct urb *urb);
void *usbip_alloc_iso_desc_pdu(struct urb *urb, ssize_t *bufflen);


/* usbip_event.c */
void usbip_start_eh(struct usbip_device *ud);
void usbip_stop_eh(struct usbip_device *ud);
void usbip_event_add(struct usbip_device *ud, unsigned long event);
int usbip_event_happend(struct usbip_device *ud);


#endif
