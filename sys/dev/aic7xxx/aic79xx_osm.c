/*
 * Bus independent FreeBSD shim for the aic79xx based Adaptec SCSI controllers
 *
 * Copyright (c) 1994-2002 Justin T. Gibbs.
 * Copyright (c) 2001-2002 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: //depot/aic7xxx/freebsd/dev/aic7xxx/aic79xx_osm.c#35 $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/sys/dev/aic7xxx/aic79xx_osm.c,v 1.3.2.5 2004/02/15 19:30:01 gibbs Exp $");

#include <dev/aic7xxx/aic79xx_osm.h>
#include <dev/aic7xxx/aic79xx_inline.h>

#include <sys/kthread.h>

#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>
#endif

#ifndef AHD_TMODE_ENABLE
#define AHD_TMODE_ENABLE 0
#endif

#include <dev/aic7xxx/aic_osm_lib.c>

#define ccb_scb_ptr spriv_ptr0

#if UNUSED
static void	ahd_dump_targcmd(struct target_cmd *cmd);
#endif
static int	ahd_modevent(module_t mod, int type, void *data);
static void	ahd_action(struct cam_sim *sim, union ccb *ccb);
static void	ahd_set_tran_settings(struct ahd_softc *ahd,
				      int our_id, char channel,
				      struct ccb_trans_settings *cts);
static void	ahd_get_tran_settings(struct ahd_softc *ahd,
				      int our_id, char channel,
				      struct ccb_trans_settings *cts);
static void	ahd_async(void *callback_arg, uint32_t code,
			  struct cam_path *path, void *arg);
static void	ahd_execute_scb(void *arg, bus_dma_segment_t *dm_segs,
				int nsegments, int error);
static void	ahd_poll(struct cam_sim *sim);
static void	ahd_setup_data(struct ahd_softc *ahd, struct cam_sim *sim,
			       struct ccb_scsiio *csio, struct scb *scb);
static void	ahd_abort_ccb(struct ahd_softc *ahd, struct cam_sim *sim,
			      union ccb *ccb);
static int	ahd_create_path(struct ahd_softc *ahd,
				char channel, u_int target, u_int lun,
				struct cam_path **path);

static int
ahd_create_path(struct ahd_softc *ahd, char channel, u_int target,
	        u_int lun, struct cam_path **path)
{
	path_id_t path_id;

	path_id = cam_sim_path(ahd->platform_data->sim);
	return (xpt_create_path(path, /*periph*/NULL,
				path_id, target, lun));
}

int
ahd_map_int(struct ahd_softc *ahd)
{
	int error;

	/* Hook up our interrupt handler */
	error = bus_setup_intr(ahd->dev_softc, ahd->platform_data->irq,
			       INTR_TYPE_CAM, ahd_platform_intr, ahd,
			       &ahd->platform_data->ih);
	if (error != 0)
		device_printf(ahd->dev_softc, "bus_setup_intr() failed: %d\n",
			      error);
	return (error);
}

/*
 * Attach all the sub-devices we can find
 */
int
ahd_attach(struct ahd_softc *ahd)
{
	char   ahd_info[256];
	struct ccb_setasync csa;
	struct cam_devq *devq;
	struct cam_sim *sim;
	struct cam_path *path;
	long s;
	int count;

	count = 0;
	devq = NULL;
	sim = NULL;

	/*
	 * Create a thread to perform all recovery.
	 */
	if (ahd_spawn_recovery_thread(ahd) != 0)
		goto fail;

	ahd_controller_info(ahd, ahd_info);
	printf("%s\n", ahd_info);
	ahd_lock(ahd, &s);

	/*
	 * Create the device queue for our SIM(s).
	 */
	devq = cam_simq_alloc(AHD_MAX_QUEUE);
	if (devq == NULL)
		goto fail;

	/*
	 * Construct our SIM entry
	 */
	sim = cam_sim_alloc(ahd_action, ahd_poll, "ahd", ahd,
			    device_get_unit(ahd->dev_softc),
			    1, /*XXX*/256, devq);
	if (sim == NULL) {
		cam_simq_free(devq);
		goto fail;
	}

	if (xpt_bus_register(sim, /*bus_id*/0) != CAM_SUCCESS) {
		cam_sim_free(sim, /*free_devq*/TRUE);
		sim = NULL;
		goto fail;
	}
	
	if (xpt_create_path(&path, /*periph*/NULL,
			    cam_sim_path(sim), CAM_TARGET_WILDCARD,
			    CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_bus_deregister(cam_sim_path(sim));
		cam_sim_free(sim, /*free_devq*/TRUE);
		sim = NULL;
		goto fail;
	}
		
	xpt_setup_ccb(&csa.ccb_h, path, /*priority*/5);
	csa.ccb_h.func_code = XPT_SASYNC_CB;
	csa.event_enable = AC_LOST_DEVICE;
	csa.callback = ahd_async;
	csa.callback_arg = sim;
	xpt_action((union ccb *)&csa);
	count++;

fail:
	ahd->platform_data->sim = sim;
	ahd->platform_data->path = path;
	if (count != 0) {
		/* We have to wait until after any system dumps... */
		ahd->platform_data->eh =
		    EVENTHANDLER_REGISTER(shutdown_final, ahd_shutdown,
					  ahd, SHUTDOWN_PRI_DEFAULT);
		ahd_intr_enable(ahd, TRUE);
	}

	ahd_unlock(ahd, &s);

	return (count);
}

/*
 * Catch an interrupt from the adapter
 */
void
ahd_platform_intr(void *arg)
{
	struct	ahd_softc *ahd;

	ahd = (struct ahd_softc *)arg; 
	ahd_intr(ahd);
}

/*
 * We have an scb which has been processed by the
 * adaptor, now we look to see how the operation
 * went.
 */
void
ahd_done(struct ahd_softc *ahd, struct scb *scb)
{
	union ccb *ccb;

	CAM_DEBUG(scb->io_ctx->ccb_h.path, CAM_DEBUG_TRACE,
		  ("ahd_done - scb %d\n", SCB_GET_TAG(scb)));

	ccb = scb->io_ctx;
	LIST_REMOVE(scb, pending_links);
	if ((scb->flags & SCB_TIMEDOUT) != 0)
		LIST_REMOVE(scb, timedout_links);

	untimeout(ahd_platform_timeout, (caddr_t)scb, ccb->ccb_h.timeout_ch);

	if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		bus_dmasync_op_t op;

		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN)
			op = BUS_DMASYNC_POSTREAD;
		else
			op = BUS_DMASYNC_POSTWRITE;
		bus_dmamap_sync(ahd->buffer_dmat, scb->dmamap, op);
		bus_dmamap_unload(ahd->buffer_dmat, scb->dmamap);
	}

#ifdef AHD_TARGET_MODE
	if (ccb->ccb_h.func_code == XPT_CONT_TARGET_IO) {
		struct cam_path *ccb_path;

		/*
		 * If we have finally disconnected, clean up our
		 * pending device state.
		 * XXX - There may be error states that cause where
		 *       we will remain connected.
		 */
		ccb_path = ccb->ccb_h.path;
		if (ahd->pending_device != NULL
		 && xpt_path_comp(ahd->pending_device->path, ccb_path) == 0) {

			if ((ccb->ccb_h.flags & CAM_SEND_STATUS) != 0) {
				ahd->pending_device = NULL;
			} else {
				xpt_print_path(ccb->ccb_h.path);
				printf("Still disconnected\n");
				ahd_freeze_ccb(ccb);
			}
		}

		if (aic_get_transaction_status(scb) == CAM_REQ_INPROG)
			ccb->ccb_h.status |= CAM_REQ_CMP;
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		ahd_free_scb(ahd, scb);
		xpt_done(ccb);
		return;
	}
#endif

	/*
	 * If the recovery SCB completes, we have to be
	 * out of our timeout.
	 */
	if ((scb->flags & SCB_RECOVERY_SCB) != 0) {
		struct	scb *list_scb;

		/*
		 * We were able to complete the command successfully,
		 * so reinstate the timeouts for all other pending
		 * commands.
		 */
		LIST_FOREACH(list_scb, &ahd->pending_scbs, pending_links) {
			union ccb *ccb;
			uint64_t time;

			ccb = list_scb->io_ctx;
			if (ccb->ccb_h.timeout == CAM_TIME_INFINITY)
				continue;

			time = ccb->ccb_h.timeout;
			time *= hz;
			time /= 1000;
			ccb->ccb_h.timeout_ch = 
			    timeout(ahd_platform_timeout, list_scb, time);
		}

		if (aic_get_transaction_status(scb) == CAM_BDR_SENT
		 || aic_get_transaction_status(scb) == CAM_REQ_ABORTED)
			aic_set_transaction_status(scb, CAM_CMD_TIMEOUT);

		ahd_print_path(ahd, scb);
		printf("no longer in timeout, status = %x\n",
		       ccb->ccb_h.status);
	}

	/* Don't clobber any existing error state */
	if (aic_get_transaction_status(scb) == CAM_REQ_INPROG) {
		ccb->ccb_h.status |= CAM_REQ_CMP;
	} else if ((scb->flags & SCB_SENSE) != 0) {
		/*
		 * We performed autosense retrieval.
		 *
		 * Zero any sense not transferred by the
		 * device.  The SCSI spec mandates that any
		 * untransfered data should be assumed to be
		 * zero.  Complete the 'bounce' of sense information
		 * through buffers accessible via bus-space by
		 * copying it into the clients csio.
		 */
		memset(&ccb->csio.sense_data, 0, sizeof(ccb->csio.sense_data));
		memcpy(&ccb->csio.sense_data,
		       ahd_get_sense_buf(ahd, scb),
/* XXX What size do we want to use??? */
			sizeof(ccb->csio.sense_data)
		       - ccb->csio.sense_resid);
		scb->io_ctx->ccb_h.status |= CAM_AUTOSNS_VALID;
	} else if ((scb->flags & SCB_PKT_SENSE) != 0) {
		struct scsi_status_iu_header *siu;
		u_int sense_len;
		int i;

		/*
		 * Copy only the sense data into the provided buffer.
		 */
		siu = (struct scsi_status_iu_header *)scb->sense_data;
		sense_len = MIN(scsi_4btoul(siu->sense_length),
				sizeof(ccb->csio.sense_data));
		memset(&ccb->csio.sense_data, 0, sizeof(ccb->csio.sense_data));
		memcpy(&ccb->csio.sense_data,
		       ahd_get_sense_buf(ahd, scb) + SIU_SENSE_OFFSET(siu),
		       sense_len);
		printf("Copied %d bytes of sense data offset %d:", sense_len,
		       SIU_SENSE_OFFSET(siu));
		for (i = 0; i < sense_len; i++)
			printf(" 0x%x", ((uint8_t *)&ccb->csio.sense_data)[i]);
		printf("\n");
		scb->io_ctx->ccb_h.status |= CAM_AUTOSNS_VALID;
	}
	ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
	ahd_free_scb(ahd, scb);
	xpt_done(ccb);
}

static void
ahd_action(struct cam_sim *sim, union ccb *ccb)
{
	struct	ahd_softc *ahd;
#ifdef AHD_TARGET_MODE
	struct	ahd_tmode_lstate *lstate;
#endif
	u_int	target_id;
	u_int	our_id;
	long	s;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE, ("ahd_action\n"));
	
	ahd = (struct ahd_softc *)cam_sim_softc(sim);

	target_id = ccb->ccb_h.target_id;
	our_id = SIM_SCSI_ID(ahd, sim);
	
	switch (ccb->ccb_h.func_code) {
	/* Common cases first */
#ifdef AHD_TARGET_MODE
	case XPT_ACCEPT_TARGET_IO:	/* Accept Host Target Mode CDB */
	case XPT_CONT_TARGET_IO:/* Continue Host Target I/O Connection*/
	{
		struct	   ahd_tmode_tstate *tstate;
		cam_status status;

		status = ahd_find_tmode_devs(ahd, sim, ccb, &tstate,
					     &lstate, TRUE);

		if (status != CAM_REQ_CMP) {
			if (ccb->ccb_h.func_code == XPT_CONT_TARGET_IO) {
				/* Response from the black hole device */
				tstate = NULL;
				lstate = ahd->black_hole;
			} else {
				ccb->ccb_h.status = status;
				xpt_done(ccb);
				break;
			}
		}
		if (ccb->ccb_h.func_code == XPT_ACCEPT_TARGET_IO) {

			ahd_lock(ahd, &s);
			SLIST_INSERT_HEAD(&lstate->accept_tios, &ccb->ccb_h,
					  sim_links.sle);
			ccb->ccb_h.status = CAM_REQ_INPROG;
			if ((ahd->flags & AHD_TQINFIFO_BLOCKED) != 0)
				ahd_run_tqinfifo(ahd, /*paused*/FALSE);
			ahd_unlock(ahd, &s);
			break;
		}

		/*
		 * The target_id represents the target we attempt to
		 * select.  In target mode, this is the initiator of
		 * the original command.
		 */
		our_id = target_id;
		target_id = ccb->csio.init_id;
		/* FALLTHROUGH */
	}
#endif
	case XPT_SCSI_IO:	/* Execute the requested I/O operation */
	case XPT_RESET_DEV:	/* Bus Device Reset the specified SCSI device */
	{
		struct	scb *scb;
		struct	hardware_scb *hscb;	
		struct	ahd_initiator_tinfo *tinfo;
		struct	ahd_tmode_tstate *tstate;
		u_int	col_idx;

		if ((ahd->flags & AHD_INITIATORROLE) == 0
		 && (ccb->ccb_h.func_code == XPT_SCSI_IO
		  || ccb->ccb_h.func_code == XPT_RESET_DEV)) {
			ccb->ccb_h.status = CAM_PROVIDE_FAIL;
			xpt_done(ccb);
			return;
		}

		/*
		 * get an scb to use.
		 */
		ahd_lock(ahd, &s);
		tinfo = ahd_fetch_transinfo(ahd, 'A', our_id,
					    target_id, &tstate);
		if ((ccb->ccb_h.flags & CAM_TAG_ACTION_VALID) == 0
		 || (tinfo->curr.ppr_options & MSG_EXT_PPR_IU_REQ) != 0
		 || ccb->ccb_h.func_code == XPT_CONT_TARGET_IO) {
			col_idx = AHD_NEVER_COL_IDX;
		} else {
			col_idx = AHD_BUILD_COL_IDX(target_id,
						    ccb->ccb_h.target_lun);
		}
		if ((scb = ahd_get_scb(ahd, col_idx)) == NULL) {
	
			xpt_freeze_simq(sim, /*count*/1);
			ahd->flags |= AHD_RESOURCE_SHORTAGE;
			ahd_unlock(ahd, &s);
			ccb->ccb_h.status = CAM_REQUEUE_REQ;
			xpt_done(ccb);
			return;
		}
		ahd_unlock(ahd, &s);
		
		hscb = scb->hscb;
		
		CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_SUBTRACE,
			  ("start scb(%p)\n", scb));
		scb->io_ctx = ccb;
		/*
		 * So we can find the SCB when an abort is requested
		 */
		ccb->ccb_h.ccb_scb_ptr = scb;

		/*
		 * Put all the arguments for the xfer in the scb
		 */
		hscb->control = 0;
		hscb->scsiid = BUILD_SCSIID(ahd, sim, target_id, our_id);
		hscb->lun = ccb->ccb_h.target_lun;
		if (ccb->ccb_h.func_code == XPT_RESET_DEV) {
			hscb->cdb_len = 0;
			scb->flags |= SCB_DEVICE_RESET;
			hscb->control |= MK_MESSAGE;
			hscb->task_management = SIU_TASKMGMT_LUN_RESET;
			ahd_execute_scb(scb, NULL, 0, 0);
		} else {
#ifdef AHD_TARGET_MODE
			if (ccb->ccb_h.func_code == XPT_CONT_TARGET_IO) {
				struct target_data *tdata;

				tdata = &hscb->shared_data.tdata;
				if (ahd->pending_device == lstate)
					scb->flags |= SCB_TARGET_IMMEDIATE;
				hscb->control |= TARGET_SCB;
				tdata->target_phases = 0;
				if ((ccb->ccb_h.flags & CAM_SEND_STATUS) != 0) {
					tdata->target_phases |= SPHASE_PENDING;
					tdata->scsi_status =
					    ccb->csio.scsi_status;
				}
	 			if (ccb->ccb_h.flags & CAM_DIS_DISCONNECT)
					tdata->target_phases |= NO_DISCONNECT;

				tdata->initiator_tag =
				    ahd_htole16(ccb->csio.tag_id);
			}
#endif
			hscb->task_management = 0;
			if (ccb->ccb_h.flags & CAM_TAG_ACTION_VALID)
				hscb->control |= ccb->csio.tag_action;
			
			ahd_setup_data(ahd, sim, &ccb->csio, scb);
		}
		break;
	}
#ifdef AHD_TARGET_MODE
	case XPT_NOTIFY_ACK:
	case XPT_IMMED_NOTIFY:
	{
		struct	   ahd_tmode_tstate *tstate;
		struct	   ahd_tmode_lstate *lstate;
		cam_status status;

		status = ahd_find_tmode_devs(ahd, sim, ccb, &tstate,
					     &lstate, TRUE);

		if (status != CAM_REQ_CMP) {
			ccb->ccb_h.status = status;
			xpt_done(ccb);
			break;
		}
		SLIST_INSERT_HEAD(&lstate->immed_notifies, &ccb->ccb_h,
				  sim_links.sle);
		ccb->ccb_h.status = CAM_REQ_INPROG;
		ahd_send_lstate_events(ahd, lstate);
		break;
	}
	case XPT_EN_LUN:		/* Enable LUN as a target */
		ahd_handle_en_lun(ahd, sim, ccb);
		xpt_done(ccb);
		break;
#endif
	case XPT_ABORT:			/* Abort the specified CCB */
	{
		ahd_abort_ccb(ahd, sim, ccb);
		break;
	}
	case XPT_SET_TRAN_SETTINGS:
	{
		ahd_lock(ahd, &s);
		ahd_set_tran_settings(ahd, SIM_SCSI_ID(ahd, sim),
				      SIM_CHANNEL(ahd, sim), &ccb->cts);
		ahd_unlock(ahd, &s);
		xpt_done(ccb);
		break;
	}
	case XPT_GET_TRAN_SETTINGS:
	/* Get default/user set transfer settings for the target */
	{
		ahd_lock(ahd, &s);
		ahd_get_tran_settings(ahd, SIM_SCSI_ID(ahd, sim),
				      SIM_CHANNEL(ahd, sim), &ccb->cts);
		ahd_unlock(ahd, &s);
		xpt_done(ccb);
		break;
	}
	case XPT_CALC_GEOMETRY:
	{
		aic_calc_geometry(&ccb->ccg, ahd->flags & AHD_EXTENDED_TRANS_A);
		xpt_done(ccb);
		break;
	}
	case XPT_RESET_BUS:		/* Reset the specified SCSI bus */
	{
		int  found;
		
		ahd_lock(ahd, &s);
		found = ahd_reset_channel(ahd, SIM_CHANNEL(ahd, sim),
					  /*initiate reset*/TRUE);
		ahd_unlock(ahd, &s);
		if (bootverbose) {
			xpt_print_path(SIM_PATH(ahd, sim));
			printf("SCSI bus reset delivered. "
			       "%d SCBs aborted.\n", found);
		}
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	}
	case XPT_TERM_IO:		/* Terminate the I/O process */
		/* XXX Implement */
		ccb->ccb_h.status = CAM_REQ_INVALID;
		xpt_done(ccb);
		break;
	case XPT_PATH_INQ:		/* Path routing inquiry */
	{
		struct ccb_pathinq *cpi = &ccb->cpi;
		
		cpi->version_num = 1; /* XXX??? */
		cpi->hba_inquiry = PI_SDTR_ABLE|PI_TAG_ABLE;
		if ((ahd->features & AHD_WIDE) != 0)
			cpi->hba_inquiry |= PI_WIDE_16;
		if ((ahd->features & AHD_TARGETMODE) != 0) {
			cpi->target_sprt = PIT_PROCESSOR
					 | PIT_DISCONNECT
					 | PIT_TERM_IO;
		} else {
			cpi->target_sprt = 0;
		}
		cpi->hba_misc = 0;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = (ahd->features & AHD_WIDE) ? 15 : 7;
		cpi->max_lun = AHD_NUM_LUNS - 1;
		cpi->initiator_id = ahd->our_id;
		if ((ahd->flags & AHD_RESET_BUS_A) == 0) {
			cpi->hba_misc |= PIM_NOBUSRESET;
		}
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 3300;
		strncpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strncpy(cpi->hba_vid, "Adaptec", HBA_IDLEN);
		strncpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
#ifdef AHD_NEW_TRAN_SETTINGS
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_2;
		cpi->transport = XPORT_SPI;
		cpi->transport_version = 2;
		cpi->xport_specific.spi.ppr_options = SID_SPI_CLOCK_ST;
		cpi->transport_version = 4;
		cpi->xport_specific.spi.ppr_options = SID_SPI_CLOCK_DT_ST;
#endif
		cpi->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	}
	default:
		ccb->ccb_h.status = CAM_PROVIDE_FAIL;
		xpt_done(ccb);
		break;
	}
}


static void
ahd_set_tran_settings(struct ahd_softc *ahd, int our_id, char channel,
		      struct ccb_trans_settings *cts)
{
#ifdef AHD_NEW_TRAN_SETTINGS
	struct	  ahd_devinfo devinfo;
	struct	  ccb_trans_settings_scsi *scsi;
	struct	  ccb_trans_settings_spi *spi;
	struct	  ahd_initiator_tinfo *tinfo;
	struct	  ahd_tmode_tstate *tstate;
	uint16_t *discenable;
	uint16_t *tagenable;
	u_int	  update_type;

	scsi = &cts->proto_specific.scsi;
	spi = &cts->xport_specific.spi;
	ahd_compile_devinfo(&devinfo, SIM_SCSI_ID(ahd, sim),
			    cts->ccb_h.target_id,
			    cts->ccb_h.target_lun,
			    SIM_CHANNEL(ahd, sim),
			    ROLE_UNKNOWN);
	tinfo = ahd_fetch_transinfo(ahd, devinfo.channel,
				    devinfo.our_scsiid,
				    devinfo.target, &tstate);
	update_type = 0;
	if (cts->type == CTS_TYPE_CURRENT_SETTINGS) {
		update_type |= AHD_TRANS_GOAL;
		discenable = &tstate->discenable;
		tagenable = &tstate->tagenable;
		tinfo->curr.protocol_version = cts->protocol_version;
		tinfo->curr.transport_version = cts->transport_version;
		tinfo->goal.protocol_version = cts->protocol_version;
		tinfo->goal.transport_version = cts->transport_version;
	} else if (cts->type == CTS_TYPE_USER_SETTINGS) {
		update_type |= AHD_TRANS_USER;
		discenable = &ahd->user_discenable;
		tagenable = &ahd->user_tagenable;
		tinfo->user.protocol_version = cts->protocol_version;
		tinfo->user.transport_version = cts->transport_version;
	} else {
		cts->ccb_h.status = CAM_REQ_INVALID;
		return;
	}
	
	if ((spi->valid & CTS_SPI_VALID_DISC) != 0) {
		if ((spi->flags & CTS_SPI_FLAGS_DISC_ENB) != 0)
			*discenable |= devinfo.target_mask;
		else
			*discenable &= ~devinfo.target_mask;
	}
	
	if ((scsi->valid & CTS_SCSI_VALID_TQ) != 0) {
		if ((scsi->flags & CTS_SCSI_FLAGS_TAG_ENB) != 0)
			*tagenable |= devinfo.target_mask;
		else
			*tagenable &= ~devinfo.target_mask;
	}	

	if ((spi->valid & CTS_SPI_VALID_BUS_WIDTH) != 0) {
		ahd_validate_width(ahd, /*tinfo limit*/NULL,
				   &spi->bus_width, ROLE_UNKNOWN);
		ahd_set_width(ahd, &devinfo, spi->bus_width,
			      update_type, /*paused*/FALSE);
	}

	if ((spi->valid & CTS_SPI_VALID_PPR_OPTIONS) == 0) {
		if (update_type == AHD_TRANS_USER)
			spi->ppr_options = tinfo->user.ppr_options;
		else
			spi->ppr_options = tinfo->goal.ppr_options;
	}

	if ((spi->valid & CTS_SPI_VALID_SYNC_OFFSET) == 0) {
		if (update_type == AHD_TRANS_USER)
			spi->sync_offset = tinfo->user.offset;
		else
			spi->sync_offset = tinfo->goal.offset;
	}

	if ((spi->valid & CTS_SPI_VALID_SYNC_RATE) == 0) {
		if (update_type == AHD_TRANS_USER)
			spi->sync_period = tinfo->user.period;
		else
			spi->sync_period = tinfo->goal.period;
	}

	if (((spi->valid & CTS_SPI_VALID_SYNC_RATE) != 0)
	 || ((spi->valid & CTS_SPI_VALID_SYNC_OFFSET) != 0)) {
		u_int	maxsync;

		maxsync = AHD_SYNCRATE_MAX;

		if (spi->bus_width != MSG_EXT_WDTR_BUS_16_BIT)
			spi->ppr_options &= ~MSG_EXT_PPR_DT_REQ;

		if ((*discenable & devinfo.target_mask) == 0)
			spi->ppr_options &= ~MSG_EXT_PPR_IU_REQ;

		ahd_find_syncrate(ahd, &spi->sync_period,
				  &spi->ppr_options, maxsync);
		ahd_validate_offset(ahd, /*tinfo limit*/NULL,
				    spi->sync_period, &spi->sync_offset,
				    spi->bus_width, ROLE_UNKNOWN);

		/* We use a period of 0 to represent async */
		if (spi->sync_offset == 0) {
			spi->sync_period = 0;
			spi->ppr_options = 0;
		}

		ahd_set_syncrate(ahd, &devinfo, spi->sync_period,
				 spi->sync_offset, spi->ppr_options,
				 update_type, /*paused*/FALSE);
	}
	cts->ccb_h.status = CAM_REQ_CMP;
#else
	struct	  ahd_devinfo devinfo;
	struct	  ahd_initiator_tinfo *tinfo;
	struct	  ahd_tmode_tstate *tstate;
	uint16_t *discenable;
	uint16_t *tagenable;
	u_int	  update_type;

	ahd_compile_devinfo(&devinfo, SIM_SCSI_ID(ahd, sim),
			    cts->ccb_h.target_id,
			    cts->ccb_h.target_lun,
			    SIM_CHANNEL(ahd, sim),
			    ROLE_UNKNOWN);
	tinfo = ahd_fetch_transinfo(ahd, devinfo.channel,
				    devinfo.our_scsiid,
				    devinfo.target, &tstate);
	update_type = 0;
	if ((cts->flags & CCB_TRANS_CURRENT_SETTINGS) != 0) {
		update_type |= AHD_TRANS_GOAL;
		discenable = &tstate->discenable;
		tagenable = &tstate->tagenable;
	} else if ((cts->flags & CCB_TRANS_USER_SETTINGS) != 0) {
		update_type |= AHD_TRANS_USER;
		discenable = &ahd->user_discenable;
		tagenable = &ahd->user_tagenable;
	} else {
		cts->ccb_h.status = CAM_REQ_INVALID;
		return;
	}
	
	if ((cts->valid & CCB_TRANS_DISC_VALID) != 0) {
		if ((cts->flags & CCB_TRANS_DISC_ENB) != 0)
			*discenable |= devinfo.target_mask;
		else
			*discenable &= ~devinfo.target_mask;
	}
	
	if ((cts->valid & CCB_TRANS_TQ_VALID) != 0) {
		if ((cts->flags & CCB_TRANS_TAG_ENB) != 0)
			*tagenable |= devinfo.target_mask;
		else
			*tagenable &= ~devinfo.target_mask;
	}	

	if ((cts->valid & CCB_TRANS_BUS_WIDTH_VALID) != 0) {
		ahd_validate_width(ahd, /*tinfo limit*/NULL,
				   &cts->bus_width, ROLE_UNKNOWN);
		ahd_set_width(ahd, &devinfo, cts->bus_width,
			      update_type, /*paused*/FALSE);
	}

	if ((cts->valid & CCB_TRANS_SYNC_OFFSET_VALID) == 0) {
		if (update_type == AHD_TRANS_USER)
			cts->sync_offset = tinfo->user.offset;
		else
			cts->sync_offset = tinfo->goal.offset;
	}

	if ((cts->valid & CCB_TRANS_SYNC_RATE_VALID) == 0) {
		if (update_type == AHD_TRANS_USER)
			cts->sync_period = tinfo->user.period;
		else
			cts->sync_period = tinfo->goal.period;
	}

	if (((cts->valid & CCB_TRANS_SYNC_RATE_VALID) != 0)
	 || ((cts->valid & CCB_TRANS_SYNC_OFFSET_VALID) != 0)
	 || ((cts->valid & CCB_TRANS_TQ_VALID) != 0)
	 || ((cts->valid & CCB_TRANS_DISC_VALID) != 0)) {
		u_int ppr_options;
		u_int maxsync;

		maxsync = AHD_SYNCRATE_MAX;
		ppr_options = 0;
		if (cts->sync_period <= AHD_SYNCRATE_DT
		 && cts->bus_width == MSG_EXT_WDTR_BUS_16_BIT) {
			ppr_options = tinfo->user.ppr_options
				    | MSG_EXT_PPR_DT_REQ;
		}

		if ((*tagenable & devinfo.target_mask) == 0
		 || (*discenable & devinfo.target_mask) == 0)
			ppr_options &= ~MSG_EXT_PPR_IU_REQ;

		ahd_find_syncrate(ahd, &cts->sync_period,
				  &ppr_options, maxsync);
		ahd_validate_offset(ahd, /*tinfo limit*/NULL,
				    cts->sync_period, &cts->sync_offset,
				    MSG_EXT_WDTR_BUS_8_BIT,
				    ROLE_UNKNOWN);

		/* We use a period of 0 to represent async */
		if (cts->sync_offset == 0) {
			cts->sync_period = 0;
			ppr_options = 0;
		}

		if (ppr_options != 0
		 && tinfo->user.transport_version >= 3) {
			tinfo->goal.transport_version =
			    tinfo->user.transport_version;
			tinfo->curr.transport_version =
			    tinfo->user.transport_version;
		}
		
		ahd_set_syncrate(ahd, &devinfo, cts->sync_period,
				 cts->sync_offset, ppr_options,
				 update_type, /*paused*/FALSE);
	}
	cts->ccb_h.status = CAM_REQ_CMP;
#endif
}

static void
ahd_get_tran_settings(struct ahd_softc *ahd, int our_id, char channel,
		      struct ccb_trans_settings *cts)
{
#ifdef AHD_NEW_TRAN_SETTINGS
	struct	ahd_devinfo devinfo;
	struct	ccb_trans_settings_scsi *scsi;
	struct	ccb_trans_settings_spi *spi;
	struct	ahd_initiator_tinfo *targ_info;
	struct	ahd_tmode_tstate *tstate;
	struct	ahd_transinfo *tinfo;

	scsi = &cts->proto_specific.scsi;
	spi = &cts->xport_specific.spi;
	ahd_compile_devinfo(&devinfo, our_id,
			    cts->ccb_h.target_id,
			    cts->ccb_h.target_lun,
			    channel, ROLE_UNKNOWN);
	targ_info = ahd_fetch_transinfo(ahd, devinfo.channel,
					devinfo.our_scsiid,
					devinfo.target, &tstate);
	
	if (cts->type == CTS_TYPE_CURRENT_SETTINGS)
		tinfo = &targ_info->curr;
	else
		tinfo = &targ_info->user;
	
	scsi->flags &= ~CTS_SCSI_FLAGS_TAG_ENB;
	spi->flags &= ~CTS_SPI_FLAGS_DISC_ENB;
	if (cts->type == CTS_TYPE_USER_SETTINGS) {
		if ((ahd->user_discenable & devinfo.target_mask) != 0)
			spi->flags |= CTS_SPI_FLAGS_DISC_ENB;

		if ((ahd->user_tagenable & devinfo.target_mask) != 0)
			scsi->flags |= CTS_SCSI_FLAGS_TAG_ENB;
	} else {
		if ((tstate->discenable & devinfo.target_mask) != 0)
			spi->flags |= CTS_SPI_FLAGS_DISC_ENB;

		if ((tstate->tagenable & devinfo.target_mask) != 0)
			scsi->flags |= CTS_SCSI_FLAGS_TAG_ENB;
	}
	cts->protocol_version = tinfo->protocol_version;
	cts->transport_version = tinfo->transport_version;

	spi->sync_period = tinfo->period;
	spi->sync_offset = tinfo->offset;
	spi->bus_width = tinfo->width;
	spi->ppr_options = tinfo->ppr_options;
	
	cts->protocol = PROTO_SCSI;
	cts->transport = XPORT_SPI;
	spi->valid = CTS_SPI_VALID_SYNC_RATE
		   | CTS_SPI_VALID_SYNC_OFFSET
		   | CTS_SPI_VALID_BUS_WIDTH
		   | CTS_SPI_VALID_PPR_OPTIONS;

	if (cts->ccb_h.target_lun != CAM_LUN_WILDCARD) {
		scsi->valid = CTS_SCSI_VALID_TQ;
		spi->valid |= CTS_SPI_VALID_DISC;
	} else {
		scsi->valid = 0;
	}

	cts->ccb_h.status = CAM_REQ_CMP;
#else
	struct	ahd_devinfo devinfo;
	struct	ahd_initiator_tinfo *targ_info;
	struct	ahd_tmode_tstate *tstate;
	struct	ahd_transinfo *tinfo;

	ahd_compile_devinfo(&devinfo, our_id,
			    cts->ccb_h.target_id,
			    cts->ccb_h.target_lun,
			    channel, ROLE_UNKNOWN);
	targ_info = ahd_fetch_transinfo(ahd, devinfo.channel,
					devinfo.our_scsiid,
					devinfo.target, &tstate);
	
	if ((cts->flags & CCB_TRANS_CURRENT_SETTINGS) != 0)
		tinfo = &targ_info->curr;
	else
		tinfo = &targ_info->user;
	
	cts->flags &= ~(CCB_TRANS_DISC_ENB|CCB_TRANS_TAG_ENB);
	if ((cts->flags & CCB_TRANS_CURRENT_SETTINGS) == 0) {
		if ((ahd->user_discenable & devinfo.target_mask) != 0)
			cts->flags |= CCB_TRANS_DISC_ENB;

		if ((ahd->user_tagenable & devinfo.target_mask) != 0)
			cts->flags |= CCB_TRANS_TAG_ENB;
	} else {
		if ((tstate->discenable & devinfo.target_mask) != 0)
			cts->flags |= CCB_TRANS_DISC_ENB;

		if ((tstate->tagenable & devinfo.target_mask) != 0)
			cts->flags |= CCB_TRANS_TAG_ENB;
	}
	cts->sync_period = tinfo->period;
	cts->sync_offset = tinfo->offset;
	cts->bus_width = tinfo->width;
	
	cts->valid = CCB_TRANS_SYNC_RATE_VALID
		   | CCB_TRANS_SYNC_OFFSET_VALID
		   | CCB_TRANS_BUS_WIDTH_VALID;

	if (cts->ccb_h.target_lun != CAM_LUN_WILDCARD)
		cts->valid |= CCB_TRANS_DISC_VALID|CCB_TRANS_TQ_VALID;

	cts->ccb_h.status = CAM_REQ_CMP;
#endif
}

static void
ahd_async(void *callback_arg, uint32_t code, struct cam_path *path, void *arg)
{
	struct ahd_softc *ahd;
	struct cam_sim *sim;

	sim = (struct cam_sim *)callback_arg;
	ahd = (struct ahd_softc *)cam_sim_softc(sim);
	switch (code) {
	case AC_LOST_DEVICE:
	{
		struct	ahd_devinfo devinfo;
		long	s;

		ahd_compile_devinfo(&devinfo, SIM_SCSI_ID(ahd, sim),
				    xpt_path_target_id(path),
				    xpt_path_lun_id(path),
				    SIM_CHANNEL(ahd, sim),
				    ROLE_UNKNOWN);

		/*
		 * Revert to async/narrow transfers
		 * for the next device.
		 */
		ahd_lock(ahd, &s);
		ahd_set_width(ahd, &devinfo, MSG_EXT_WDTR_BUS_8_BIT,
			      AHD_TRANS_GOAL|AHD_TRANS_CUR, /*paused*/FALSE);
		ahd_set_syncrate(ahd, &devinfo, /*period*/0, /*offset*/0,
				 /*ppr_options*/0, AHD_TRANS_GOAL|AHD_TRANS_CUR,
				 /*paused*/FALSE);
		ahd_unlock(ahd, &s);
		break;
	}
	default:
		break;
	}
}

static void
ahd_execute_scb(void *arg, bus_dma_segment_t *dm_segs, int nsegments,
		int error)
{
	struct	scb *scb;
	union	ccb *ccb;
	struct	ahd_softc *ahd;
	struct	ahd_initiator_tinfo *tinfo;
	struct	ahd_tmode_tstate *tstate;
	u_int	mask;
	u_long	s;

	scb = (struct scb *)arg;
	ccb = scb->io_ctx;
	ahd = scb->ahd_softc;

	if (error != 0) {
		if (error == EFBIG)
			aic_set_transaction_status(scb, CAM_REQ_TOO_BIG);
		else
			aic_set_transaction_status(scb, CAM_REQ_CMP_ERR);
		if (nsegments != 0)
			bus_dmamap_unload(ahd->buffer_dmat, scb->dmamap);
		ahd_lock(ahd, &s);
		ahd_free_scb(ahd, scb);
		ahd_unlock(ahd, &s);
		xpt_done(ccb);
		return;
	}
	scb->sg_count = 0;
	if (nsegments != 0) {
		void *sg;
		bus_dmasync_op_t op;
		u_int i;

		/* Copy the segments into our SG list */
		for (i = nsegments, sg = scb->sg_list; i > 0; i--) {

			sg = ahd_sg_setup(ahd, scb, sg, dm_segs->ds_addr,
					  dm_segs->ds_len,
					  /*last*/i == 1);
			dm_segs++;
		}
		
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN)
			op = BUS_DMASYNC_PREREAD;
		else
			op = BUS_DMASYNC_PREWRITE;

		bus_dmamap_sync(ahd->buffer_dmat, scb->dmamap, op);

		if (ccb->ccb_h.func_code == XPT_CONT_TARGET_IO) {
			struct target_data *tdata;

			tdata = &scb->hscb->shared_data.tdata;
			tdata->target_phases |= DPHASE_PENDING;
			if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT)
				tdata->data_phase = P_DATAOUT;
			else
				tdata->data_phase = P_DATAIN;
		}
	}

	ahd_lock(ahd, &s);

	/*
	 * Last time we need to check if this SCB needs to
	 * be aborted.
	 */
	if (aic_get_transaction_status(scb) != CAM_REQ_INPROG) {
		if (nsegments != 0)
			bus_dmamap_unload(ahd->buffer_dmat,
					  scb->dmamap);
		ahd_free_scb(ahd, scb);
		ahd_unlock(ahd, &s);
		xpt_done(ccb);
		return;
	}

	tinfo = ahd_fetch_transinfo(ahd, SCSIID_CHANNEL(ahd, scb->hscb->scsiid),
				    SCSIID_OUR_ID(scb->hscb->scsiid),
				    SCSIID_TARGET(ahd, scb->hscb->scsiid),
				    &tstate);

	mask = SCB_GET_TARGET_MASK(ahd, scb);

	if ((tstate->discenable & mask) != 0
	 && (ccb->ccb_h.flags & CAM_DIS_DISCONNECT) == 0)
		scb->hscb->control |= DISCENB;

	if ((tinfo->curr.ppr_options & MSG_EXT_PPR_IU_REQ) != 0) {
		scb->flags |= SCB_PACKETIZED;
		if (scb->hscb->task_management != 0)
			scb->hscb->control &= ~MK_MESSAGE;
	}

	if ((ccb->ccb_h.flags & CAM_NEGOTIATE) != 0
	 && (tinfo->goal.width != 0
	  || tinfo->goal.period != 0
	  || tinfo->goal.ppr_options != 0)) {
		scb->flags |= SCB_NEGOTIATE;
		scb->hscb->control |= MK_MESSAGE;
	} else if ((tstate->auto_negotiate & mask) != 0) {
		scb->flags |= SCB_AUTO_NEGOTIATE;
		scb->hscb->control |= MK_MESSAGE;
	}

	LIST_INSERT_HEAD(&ahd->pending_scbs, scb, pending_links);

	ccb->ccb_h.status |= CAM_SIM_QUEUED;

	if (ccb->ccb_h.timeout != CAM_TIME_INFINITY) {
		uint64_t time;

		if (ccb->ccb_h.timeout == CAM_TIME_DEFAULT)
			ccb->ccb_h.timeout = 5 * 1000;

		time = ccb->ccb_h.timeout;
		time *= hz;
		time /= 1000;
		ccb->ccb_h.timeout_ch =
		    timeout(ahd_platform_timeout, (caddr_t)scb, time);
	}

	if ((scb->flags & SCB_TARGET_IMMEDIATE) != 0) {
		/* Define a mapping from our tag to the SCB. */
		ahd->scb_data.scbindex[SCB_GET_TAG(scb)] = scb;
		ahd_pause(ahd);
		ahd_set_scbptr(ahd, SCB_GET_TAG(scb));
		ahd_outb(ahd, RETURN_1, CONT_MSG_LOOP_TARG);
		ahd_unpause(ahd);
	} else {
		ahd_queue_scb(ahd, scb);
	}

	ahd_unlock(ahd, &s);
}

static void
ahd_poll(struct cam_sim *sim)
{
	ahd_intr(cam_sim_softc(sim));
}

static void
ahd_setup_data(struct ahd_softc *ahd, struct cam_sim *sim,
	       struct ccb_scsiio *csio, struct scb *scb)
{
	struct hardware_scb *hscb;
	struct ccb_hdr *ccb_h;
	
	hscb = scb->hscb;
	ccb_h = &csio->ccb_h;
	
	csio->resid = 0;
	csio->sense_resid = 0;
	if (ccb_h->func_code == XPT_SCSI_IO) {
		hscb->cdb_len = csio->cdb_len;
		if ((ccb_h->flags & CAM_CDB_POINTER) != 0) {

			if (hscb->cdb_len > MAX_CDB_LEN
			 && (ccb_h->flags & CAM_CDB_PHYS) == 0) {
				u_long s;

				/*
				 * Should CAM start to support CDB sizes
				 * greater than 16 bytes, we could use
				 * the sense buffer to store the CDB.
				 */
				aic_set_transaction_status(scb,
							   CAM_REQ_INVALID);
				ahd_lock(ahd, &s);
				ahd_free_scb(ahd, scb);
				ahd_unlock(ahd, &s);
				xpt_done((union ccb *)csio);
				return;
			}
			if ((ccb_h->flags & CAM_CDB_PHYS) != 0) {
				hscb->shared_data.idata.cdb_from_host.cdbptr =
				   aic_htole64((uintptr_t)csio->cdb_io.cdb_ptr);
				hscb->shared_data.idata.cdb_from_host.cdblen =
				   csio->cdb_len;
				hscb->cdb_len |= SCB_CDB_LEN_PTR;
			} else {
				memcpy(hscb->shared_data.idata.cdb, 
				       csio->cdb_io.cdb_ptr,
				       hscb->cdb_len);
			}
		} else {
			if (hscb->cdb_len > MAX_CDB_LEN) {
				u_long s;

				aic_set_transaction_status(scb,
							   CAM_REQ_INVALID);
				ahd_lock(ahd, &s);
				ahd_free_scb(ahd, scb);
				ahd_unlock(ahd, &s);
				xpt_done((union ccb *)csio);
				return;
			}
			memcpy(hscb->shared_data.idata.cdb,
			       csio->cdb_io.cdb_bytes, hscb->cdb_len);
		}
	}
		
	/* Only use S/G if there is a transfer */
	if ((ccb_h->flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		if ((ccb_h->flags & CAM_SCATTER_VALID) == 0) {
			/* We've been given a pointer to a single buffer */
			if ((ccb_h->flags & CAM_DATA_PHYS) == 0) {
				int s;
				int error;

				s = splsoftvm();
				error = bus_dmamap_load(ahd->buffer_dmat,
							scb->dmamap,
							csio->data_ptr,
							csio->dxfer_len,
							ahd_execute_scb,
							scb, /*flags*/0);
				if (error == EINPROGRESS) {
					/*
					 * So as to maintain ordering,
					 * freeze the controller queue
					 * until our mapping is
					 * returned.
					 */
					xpt_freeze_simq(sim,
							/*count*/1);
					scb->io_ctx->ccb_h.status |=
					    CAM_RELEASE_SIMQ;
				}
				splx(s);
			} else {
				struct bus_dma_segment seg;

				/* Pointer to physical buffer */
				if (csio->dxfer_len > AHD_MAXTRANSFER_SIZE)
					panic("ahd_setup_data - Transfer size "
					      "larger than can device max");

				seg.ds_addr =
				    (bus_addr_t)(vm_offset_t)csio->data_ptr;
				seg.ds_len = csio->dxfer_len;
				ahd_execute_scb(scb, &seg, 1, 0);
			}
		} else {
			struct bus_dma_segment *segs;

			if ((ccb_h->flags & CAM_DATA_PHYS) != 0)
				panic("ahd_setup_data - Physical segment "
				      "pointers unsupported");

			if ((ccb_h->flags & CAM_SG_LIST_PHYS) == 0)
				panic("ahd_setup_data - Virtual segment "
				      "addresses unsupported");

			/* Just use the segments provided */
			segs = (struct bus_dma_segment *)csio->data_ptr;
			ahd_execute_scb(scb, segs, csio->sglist_cnt, 0);
		}
	} else {
		ahd_execute_scb(scb, NULL, 0, 0);
	}
}

static void
ahd_abort_ccb(struct ahd_softc *ahd, struct cam_sim *sim, union ccb *ccb)
{
	union ccb *abort_ccb;

	abort_ccb = ccb->cab.abort_ccb;
	switch (abort_ccb->ccb_h.func_code) {
#ifdef AHD_TARGET_MODE
	case XPT_ACCEPT_TARGET_IO:
	case XPT_IMMED_NOTIFY:
	case XPT_CONT_TARGET_IO:
	{
		struct ahd_tmode_tstate *tstate;
		struct ahd_tmode_lstate *lstate;
		struct ccb_hdr_slist *list;
		cam_status status;

		status = ahd_find_tmode_devs(ahd, sim, abort_ccb, &tstate,
					     &lstate, TRUE);

		if (status != CAM_REQ_CMP) {
			ccb->ccb_h.status = status;
			break;
		}

		if (abort_ccb->ccb_h.func_code == XPT_ACCEPT_TARGET_IO)
			list = &lstate->accept_tios;
		else if (abort_ccb->ccb_h.func_code == XPT_IMMED_NOTIFY)
			list = &lstate->immed_notifies;
		else
			list = NULL;

		if (list != NULL) {
			struct ccb_hdr *curelm;
			int found;

			curelm = SLIST_FIRST(list);
			found = 0;
			if (curelm == &abort_ccb->ccb_h) {
				found = 1;
				SLIST_REMOVE_HEAD(list, sim_links.sle);
			} else {
				while(curelm != NULL) {
					struct ccb_hdr *nextelm;

					nextelm =
					    SLIST_NEXT(curelm, sim_links.sle);

					if (nextelm == &abort_ccb->ccb_h) {
						found = 1;
						SLIST_NEXT(curelm,
							   sim_links.sle) =
						    SLIST_NEXT(nextelm,
							       sim_links.sle);
						break;
					}
					curelm = nextelm;
				}
			}

			if (found) {
				abort_ccb->ccb_h.status = CAM_REQ_ABORTED;
				xpt_done(abort_ccb);
				ccb->ccb_h.status = CAM_REQ_CMP;
			} else {
				xpt_print_path(abort_ccb->ccb_h.path);
				printf("Not found\n");
				ccb->ccb_h.status = CAM_PATH_INVALID;
			}
			break;
		}
		/* FALLTHROUGH */
	}
#endif
	case XPT_SCSI_IO:
		/* XXX Fully implement the hard ones */
		ccb->ccb_h.status = CAM_UA_ABORT;
		break;
	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}
	xpt_done(ccb);
}

void
ahd_send_async(struct ahd_softc *ahd, char channel, u_int target,
		u_int lun, ac_code code, void *opt_arg)
{
	struct	ccb_trans_settings cts;
	struct cam_path *path;
	void *arg;
	int error;

	arg = NULL;
	error = ahd_create_path(ahd, channel, target, lun, &path);

	if (error != CAM_REQ_CMP)
		return;

	switch (code) {
	case AC_TRANSFER_NEG:
	{
#ifdef AHD_NEW_TRAN_SETTINGS
		struct	ccb_trans_settings_scsi *scsi;
	
		cts.type = CTS_TYPE_CURRENT_SETTINGS;
		scsi = &cts.proto_specific.scsi;
#else
		cts.flags = CCB_TRANS_CURRENT_SETTINGS;
#endif
		cts.ccb_h.path = path;
		cts.ccb_h.target_id = target;
		cts.ccb_h.target_lun = lun;
		ahd_get_tran_settings(ahd, ahd->our_id, channel, &cts);
		arg = &cts;
#ifdef AHD_NEW_TRAN_SETTINGS
		scsi->valid &= ~CTS_SCSI_VALID_TQ;
		scsi->flags &= ~CTS_SCSI_FLAGS_TAG_ENB;
#else
		cts.valid &= ~CCB_TRANS_TQ_VALID;
		cts.flags &= ~CCB_TRANS_TAG_ENB;
#endif
		if (opt_arg == NULL)
			break;
		if (*((ahd_queue_alg *)opt_arg) == AHD_QUEUE_TAGGED)
#ifdef AHD_NEW_TRAN_SETTINGS
			scsi->flags |= ~CTS_SCSI_FLAGS_TAG_ENB;
		scsi->valid |= CTS_SCSI_VALID_TQ;
#else
			cts.flags |= CCB_TRANS_TAG_ENB;
		cts.valid |= CCB_TRANS_TQ_VALID;
#endif
		break;
	}
	case AC_SENT_BDR:
	case AC_BUS_RESET:
		break;
	default:
		panic("ahd_send_async: Unexpected async event");
	}
	xpt_async(code, path, arg);
	xpt_free_path(path);
}

void
ahd_platform_set_tags(struct ahd_softc *ahd,
		      struct ahd_devinfo *devinfo, int enable)
{
}

int
ahd_platform_alloc(struct ahd_softc *ahd, void *platform_arg)
{
	ahd->platform_data = malloc(sizeof(struct ahd_platform_data), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (ahd->platform_data == NULL)
		return (ENOMEM);
	return (0);
}

void
ahd_platform_free(struct ahd_softc *ahd)
{
	struct ahd_platform_data *pdata;

	pdata = ahd->platform_data;
	if (pdata != NULL) {
		if (pdata->regs[0] != NULL)
			bus_release_resource(ahd->dev_softc,
					     pdata->regs_res_type[0],
					     pdata->regs_res_id[0],
					     pdata->regs[0]);

		if (pdata->regs[1] != NULL)
			bus_release_resource(ahd->dev_softc,
					     pdata->regs_res_type[1],
					     pdata->regs_res_id[1],
					     pdata->regs[1]);

		if (pdata->irq != NULL)
			bus_release_resource(ahd->dev_softc,
					     pdata->irq_res_type,
					     0, pdata->irq);

		if (pdata->sim != NULL) {
			xpt_async(AC_LOST_DEVICE, pdata->path, NULL);
			xpt_free_path(pdata->path);
			xpt_bus_deregister(cam_sim_path(pdata->sim));
			cam_sim_free(pdata->sim, /*free_devq*/TRUE);
		}
		if (pdata->eh != NULL)
			EVENTHANDLER_DEREGISTER(shutdown_final, pdata->eh);
		free(ahd->platform_data, M_DEVBUF);
	}
}

int
ahd_softc_comp(struct ahd_softc *lahd, struct ahd_softc *rahd)
{
	/* We don't sort softcs under FreeBSD so report equal always */
	return (0);
}

int
ahd_detach(device_t dev)
{
	struct ahd_softc *ahd;
	u_long l;
	u_long s;

	ahd_list_lock(&l);
	device_printf(dev, "detaching device\n");
	ahd = device_get_softc(dev);
	ahd = ahd_find_softc(ahd);
	if (ahd == NULL) {
		device_printf(dev, "aic7xxx already detached\n");
		ahd_list_unlock(&l);
		return (ENOENT);
	}
	TAILQ_REMOVE(&ahd_tailq, ahd, links);
	ahd_list_unlock(&l);
	ahd_lock(ahd, &s);
	ahd_intr_enable(ahd, FALSE);
	bus_teardown_intr(dev, ahd->platform_data->irq, ahd->platform_data->ih);
	ahd_unlock(ahd, &s);
	ahd_free(ahd);
	return (0);
}

#if UNUSED
static void
ahd_dump_targcmd(struct target_cmd *cmd)
{
	uint8_t *byte;
	uint8_t *last_byte;
	int i;

	byte = &cmd->initiator_channel;
	/* Debugging info for received commands */
	last_byte = &cmd[1].initiator_channel;

	i = 0;
	while (byte < last_byte) {
		if (i == 0)
			printf("\t");
		printf("%#x", *byte++);
		i++;
		if (i == 8) {
			printf("\n");
			i = 0;
		} else {
			printf(", ");
		}
	}
}
#endif

static int
ahd_modevent(module_t mod, int type, void *data)
{
	/* XXX Deal with busy status on unload. */
	return 0;
}
  
static moduledata_t ahd_mod = {
	"ahd",
	ahd_modevent,
	NULL
};

/********************************** DDB Hooks *********************************/
#ifdef DDB
static struct ahd_softc *ahd_ddb_softc;
static int ahd_ddb_paused;
static int ahd_ddb_paused_on_entry;
DB_COMMAND(ahd_set_unit, ahd_ddb_set_unit)
{
	struct ahd_softc *list_ahd;

	ahd_ddb_softc = NULL;
	TAILQ_FOREACH(list_ahd, &ahd_tailq, links) {
		if (list_ahd->unit == addr)
			ahd_ddb_softc = list_ahd;
	}
	if (ahd_ddb_softc == NULL)
		db_error("No matching softc found!\n");
}

DB_COMMAND(ahd_pause, ahd_ddb_pause)
{
	if (ahd_ddb_softc == NULL) {
		db_error("Must set unit with ahd_set_unit first!\n");
		return;
	}
	if (ahd_ddb_paused == 0) {
		ahd_ddb_paused++;
		if (ahd_is_paused(ahd_ddb_softc)) {
			ahd_ddb_paused_on_entry++;
			return;
		}
		ahd_pause(ahd_ddb_softc);
	}
}

DB_COMMAND(ahd_unpause, ahd_ddb_unpause)
{
	if (ahd_ddb_softc == NULL) {
		db_error("Must set unit with ahd_set_unit first!\n");
		return;
	}
	if (ahd_ddb_paused != 0) {
		ahd_ddb_paused = 0;
		if (ahd_ddb_paused_on_entry)
			return;
		ahd_unpause(ahd_ddb_softc);
	} else if (ahd_ddb_paused_on_entry != 0) {
		/* Two unpauses to clear a paused on entry. */
		ahd_ddb_paused_on_entry = 0;
		ahd_unpause(ahd_ddb_softc);
	}
}

DB_COMMAND(ahd_in, ahd_ddb_in)
{
	int c;
	int size;
 
	if (ahd_ddb_softc == NULL) {
		db_error("Must set unit with ahd_set_unit first!\n");
		return;
	}
	if (have_addr == 0)
		return;

	size = 1;
	while ((c = *modif++) != '\0') {
		switch (c) {
		case 'b':
			size = 1;
			break;
		case 'w':
			size = 2;
			break;
		case 'l':
			size = 4;
		break;
		}
	}

	if (count <= 0)
		count = 1;
	while (--count >= 0) {
		db_printf("%04lx (M)%x: \t", (u_long)addr,
			  ahd_inb(ahd_ddb_softc, MODE_PTR));
		switch (size) {
		case 1:
			db_printf("%02x\n", ahd_inb(ahd_ddb_softc, addr));
			break;
		case 2:
			db_printf("%04x\n", ahd_inw(ahd_ddb_softc, addr));
			break;
		case 4:
			db_printf("%08x\n", ahd_inl(ahd_ddb_softc, addr));
			break;
		}
	}
}

DB_SET(ahd_out, ahd_ddb_out, db_cmd_set, CS_MORE, NULL)
{
	db_expr_t old_value;
	db_expr_t new_value;
	int	  size;
 
	if (ahd_ddb_softc == NULL) {
		db_error("Must set unit with ahd_set_unit first!\n");
		return;
	}

	switch (modif[0]) {
	case '\0':
	case 'b':
		size = 1;
		break;
	case 'h':
		size = 2;
		break;
	case 'l':
		size = 4;
		break;
	default:
		db_error("Unknown size\n");
		return;
	}
 
	while (db_expression(&new_value)) {
		switch (size) {
		default:
		case 1:
			old_value = ahd_inb(ahd_ddb_softc, addr);
			ahd_outb(ahd_ddb_softc, addr, new_value);
			break;
		case 2:
			old_value = ahd_inw(ahd_ddb_softc, addr);
			ahd_outw(ahd_ddb_softc, addr, new_value);
			break;
		case 4:
			old_value = ahd_inl(ahd_ddb_softc, addr);
			ahd_outl(ahd_ddb_softc, addr, new_value);
			break;
		}
		db_printf("%04lx (M)%x: \t0x%lx\t=\t0x%lx",
			  (u_long)addr, ahd_inb(ahd_ddb_softc, MODE_PTR),
			  (u_long)old_value, (u_long)new_value);
		addr += size;
	}
	db_skip_to_eol();
}

#endif


DECLARE_MODULE(ahd, ahd_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_DEPEND(ahd, cam, 1, 1, 1);
MODULE_VERSION(ahd, 1);
