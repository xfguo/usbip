/*
 * $Id$
 *
 * Copyright (C) 2003-2007 Takahiro Hirofuchi
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

#include "usbip_common.h"
#include "stub.h"


static int is_clear_halt_cmd(struct urb *urb)
{
	struct usb_ctrlrequest *req;

	req = (struct usb_ctrlrequest *) urb->setup_packet;

	 return (req->bRequest == USB_REQ_CLEAR_FEATURE) &&
		 (req->bRequestType == USB_RECIP_ENDPOINT) &&
		 (req->wValue == USB_ENDPOINT_HALT);
}

static int is_set_interface_cmd(struct urb *urb)
{
	struct usb_ctrlrequest *req;

	req = (struct usb_ctrlrequest *) urb->setup_packet;

	return (req->bRequest == USB_REQ_SET_INTERFACE) &&
		   (req->bRequestType == USB_RECIP_INTERFACE);
}

static int is_set_configuration_cmd(struct urb *urb)
{
	struct usb_ctrlrequest *req;

	req = (struct usb_ctrlrequest *) urb->setup_packet;

	return (req->bRequest == USB_REQ_SET_CONFIGURATION) &&
		   (req->bRequestType == USB_RECIP_DEVICE);
}

static int tweak_clear_halt_cmd(struct urb *urb)
{
	struct usb_ctrlrequest *req;
	int target_endp;
	int target_dir;
	int target_pipe;
	int ret;

	req = (struct usb_ctrlrequest *) urb->setup_packet;

	/*
	 * The stalled endpoint is specified in the wIndex value. The endpoint
	 * of the urb is the target of this clear_halt request (i.e., control
	 * endpoint).
	 */
	target_endp = le16_to_cpu(req->wIndex) & 0x000f;

	/* the stalled endpoint direction is IN or OUT?. USB_DIR_IN is 0x80. */
	target_dir = le16_to_cpu(req->wIndex) & 0x0080;

	if (target_dir)
		target_pipe = usb_rcvctrlpipe(urb->dev, target_endp);
	else
		target_pipe = usb_sndctrlpipe(urb->dev, target_endp);

	ret = usb_clear_halt(urb->dev, target_pipe);
	if (ret < 0)
		uinfo("clear_halt error: devnum %d endp %d, %d\n",
				urb->dev->devnum, target_endp, ret);
	else
		uinfo("clear_halt done: devnum %d endp %d\n",
				urb->dev->devnum, target_endp);

	return ret;
}

static int tweak_set_interface_cmd(struct urb *urb)
{
	struct usb_ctrlrequest *req;
	__u16 alternate;
	__u16 interface;
	int ret;

	req = (struct usb_ctrlrequest *) urb->setup_packet;
	alternate = le16_to_cpu(req->wValue);
	interface = le16_to_cpu(req->wIndex);

	dbg_stub_rx("set_interface: inf %u alt %u\n", interface, alternate);

	ret = usb_set_interface(urb->dev, interface, alternate);
	if (ret < 0)
		uinfo("set_interface error: inf %u alt %u, %d\n",
				interface, alternate, ret);
	else
		uinfo("set_interface done: inf %u alt %u\n", interface, alternate);

	return ret;
}

static int tweak_set_configuration_cmd(struct urb *urb)
{
	struct usb_ctrlrequest *req;
	__u16 config;

	info("set_configuration is not fully supported yet\n");

	req = (struct usb_ctrlrequest *) urb->setup_packet;
	config = le16_to_cpu(req->wValue);

	uinfo("set_configuration: devnum %d %d\n", urb->dev->devnum, config);

#if 0
	return usb_set_configuration(urb->dev, config);
#endif
	return 0;
}

/*
 * clear_halt, set_interface, and set_configuration require special tricks.
 * TODO: set_configuration. but I have never seen a multi-config device.
 */
static void tweak_special_requests(struct urb *urb)
{
	if (!urb || !urb->setup_packet)
		return;

	if (usb_pipetype(urb->pipe) != PIPE_CONTROL)
		return;

	if (is_clear_halt_cmd(urb))
		/* tweak clear_halt */
		 tweak_clear_halt_cmd(urb);

	else if (is_set_interface_cmd(urb))
		/* tweak set_interface */
		tweak_set_interface_cmd(urb);

	else if (is_set_configuration_cmd(urb))
		/* tweak set_configuration */
		tweak_set_configuration_cmd(urb);

	else
		dbg_stub_rx("no need to tweak\n");
}

/*
 * stub_recv_unlink() unlinks the URB by a call to usb_unlink_urb().
 * By unlinking the urb asynchronously, stub_rx can continuously
 * process coming urbs.  Even if the urb is unlinked, its completion
 * handler will be called and stub_tx will send a return pdu.
 *
 * See also comments about unlinking strategy in vhci_hcd.c.
 */
static int stub_recv_cmd_unlink(struct stub_device *sdev, struct usbip_header *pdu)
{
	struct list_head *listhead = &sdev->priv_init;
	struct list_head *ptr;
	unsigned long flags;

	struct stub_priv *priv;


	spin_lock_irqsave(&sdev->priv_lock, flags);

	for (ptr = listhead->next; ptr != listhead; ptr = ptr->next) {
		priv = list_entry(ptr, struct stub_priv, list);
		if (priv->seqnum == pdu->u.cmd_unlink.seqnum) {
			int ret;

			uinfo("unlink urb %p\n", priv->urb);

			/*
			 * This matched urb is not completed yet (i.e., be in
			 * flight in usb hcd hardware/driver). Now we are
			 * cancelling it. The unlinking flag means that we are
			 * now not going to return the normal result pdu of a
			 * submission request, but going to return a result pdu
			 * of the unlink request.
			 */
			priv->unlinking = 1;

			/*
			 * In the case that unlinking flag is on, prev->seqnum
			 * is changed from the seqnum of the cancelling urb to
			 * the seqnum of the unlink request. This will be used
			 * to make the result pdu of the unlink request.
			 */
			priv->seqnum = pdu->base.seqnum;

			spin_unlock_irqrestore(&sdev->priv_lock, flags);

			/*
			 * usb_unlink_urb() is now out of spinlocking to avoid
			 * spinlock recursion since stub_complete() is
			 * sometimes called in this context but not in the
			 * interrupt context.  If stub_complete() is executed
			 * before we call usb_unlink_urb(), usb_unlink_urb()
			 * will return an error value. In this case, stub_tx
			 * will return the result pdu of this unlink request
			 * though submission is completed and actual unlinking
			 * is not executed. OK?
			 */
			/* In the above case, urb->status is not -ECONNRESET,
			 * so a driver in a client host will know the failure
			 * of the unlink request ?
			 */
			ret = usb_unlink_urb(priv->urb);
			if (ret != -EINPROGRESS)
				uerr("faild to unlink a urb %p, ret %d\n", priv->urb, ret);

			return 0;
		}
	}

	dbg_stub_rx("seqnum %d is not pending\n", pdu->u.cmd_unlink.seqnum);

	/*
	 * The urb of the unlink target is not found in priv_init queue. It was
	 * already completed and its results is/was going to be sent by a
	 * CMD_RET pdu. In this case, usb_unlink_urb() is not needed. We only
	 * return the completeness of this unlink request to vhci_hcd.
	 */
	stub_enqueue_ret_unlink(sdev, pdu->base.seqnum, 0);

	spin_unlock_irqrestore(&sdev->priv_lock, flags);


	return 0;
}

static int valid_request(struct stub_device *sdev, struct usbip_header *pdu)
{
	struct usbip_device *ud = &sdev->ud;

	int bus = interface_to_busnum(sdev->interface);
	int dev = interface_to_devnum(sdev->interface);

	if (pdu->base.busnum == bus && pdu->base.devnum == dev) {
		spin_lock(&ud->lock);
		if (ud->status == SDEV_ST_USED) {
			/* A request is valid. */
			spin_unlock(&ud->lock);
			return 1;
		}
		spin_unlock(&ud->lock);
	}

	return 0;
}

static struct stub_priv *stub_priv_alloc(struct stub_device *sdev,
		struct usbip_header *pdu)
{
	struct stub_priv *priv;
	struct usbip_device *ud = &sdev->ud;
	unsigned long flags;

	spin_lock_irqsave(&sdev->priv_lock, flags);

	priv = kmem_cache_alloc(stub_priv_cache, GFP_ATOMIC);
	if (!priv) {
		uerr("alloc stub_priv\n");
		spin_unlock_irqrestore(&sdev->priv_lock, flags);
		usbip_event_add(ud, SDEV_EVENT_ERROR_MALLOC);
		return NULL;
	}

	memset(priv, 0, sizeof(struct stub_priv));

	priv->seqnum = pdu->base.seqnum;
	priv->sdev = sdev;

	/*
	 * After a stub_priv is linked to a list_head,
	 * our error handler can free allocated data.
	 */
	list_add_tail(&priv->list, &sdev->priv_init);

	spin_unlock_irqrestore(&sdev->priv_lock, flags);

	return priv;
}

static void stub_recv_cmd_submit(struct stub_device *sdev, struct usbip_header *pdu)
{
	int ret;
	struct stub_priv *priv;
	struct usbip_device *ud = &sdev->ud;


	priv = stub_priv_alloc(sdev, pdu);
	if (!priv)
		return;

	/* setup a urb */
	if (usb_pipeisoc(pdu->base.pipe))
		priv->urb = usb_alloc_urb(pdu->u.cmd_submit.number_of_packets, GFP_KERNEL);
	else
		priv->urb = usb_alloc_urb(0, GFP_KERNEL);

	if (!priv->urb) {
		uerr("malloc urb\n");
		usbip_event_add(ud, SDEV_EVENT_ERROR_MALLOC);
		return;
	}

	/* set priv->urb->transfer_buffer */
	if (pdu->u.cmd_submit.transfer_buffer_length > 0) {
		priv->urb->transfer_buffer =
			kzalloc(pdu->u.cmd_submit.transfer_buffer_length, GFP_KERNEL);
		if (!priv->urb->transfer_buffer) {
			uerr("malloc x_buff\n");
			usbip_event_add(ud, SDEV_EVENT_ERROR_MALLOC);
			return;
		}
	}

	/* set priv->urb->setup_packet */
	priv->urb->setup_packet = kzalloc(8, GFP_KERNEL);
	if (!priv->urb->setup_packet) {
		uerr("allocate setup_packet\n");
		usbip_event_add(ud, SDEV_EVENT_ERROR_MALLOC);
		return;
	}
	memcpy(priv->urb->setup_packet, &pdu->u.cmd_submit.setup, 8);

	/* set other members from the base header of pdu */
	priv->urb->context                = (void *) priv;
	priv->urb->dev                    = interface_to_usbdev(sdev->interface);
	priv->urb->pipe                   = pdu->base.pipe;
	priv->urb->complete               = stub_complete;

	usbip_pack_pdu(pdu, priv->urb, USBIP_CMD_SUBMIT, 0);


	if (usbip_recv_xbuff(ud, priv->urb) < 0)
		return;

	if (usbip_recv_iso(ud, priv->urb) < 0)
		return;

	/* no need to submit an intercepted request, but harmless? */
	tweak_special_requests(priv->urb);

	/* urb is now ready to submit */
	ret = usb_submit_urb(priv->urb, GFP_KERNEL);

	if (ret == 0)
		dbg_stub_rx("submit urb ok, seqnum %u\n", pdu->base.seqnum);
	else {
		uerr("submit_urb error, %d\n", ret);

		/*
		 * Pessimistic.
		 * This connection will be discarded.
		 */
		usbip_event_add(ud, SDEV_EVENT_ERROR_SUBMIT);
	}

	dbg_stub_rx("Leave\n");
	return;
}

/* recv a pdu */
static void stub_rx_pdu(struct usbip_device *ud)
{
	int ret;
	struct usbip_header pdu;
	struct stub_device *sdev = container_of(ud, struct stub_device, ud);


	dbg_stub_rx("Enter\n");

	memset(&pdu, 0, sizeof(pdu));


	/* 1. receive a pdu header */
	ret = usbip_xmit(0, ud->tcp_socket, (char *) &pdu, sizeof(pdu),0);
	if (ret != sizeof(pdu)) {
		uerr("recv a header, %d\n", ret);
		usbip_event_add(ud, SDEV_EVENT_ERROR_TCP);
		return;
	}

	usbip_header_correct_endian(&pdu, 0);

	if (dbg_flag_stub_rx)
		usbip_dump_header(&pdu);

	if (!valid_request(sdev, &pdu)) {
		uerr("recv invalid request\n");
		usbip_event_add(ud, SDEV_EVENT_ERROR_TCP);
		return;
	}

	switch (pdu.base.command) {
		case USBIP_CMD_UNLINK:
			stub_recv_cmd_unlink(sdev, &pdu);
			break;

		case USBIP_CMD_SUBMIT:
			stub_recv_cmd_submit(sdev, &pdu);
			break;

		default:
			/* NOTREACHED */
			uerr("unknown pdu\n");
			usbip_event_add(ud, SDEV_EVENT_ERROR_TCP);
			return;
	}

}

void stub_rx_loop(struct usbip_task *ut)
{
	struct usbip_device *ud = container_of(ut, struct usbip_device, tcp_rx);

	while (1) {
		if (signal_pending(current)) {
			dbg_stub_rx("signal caught!\n");
			break;
		}

		if (usbip_event_happend(ud))
			break;

		stub_rx_pdu(ud);
	}
}

