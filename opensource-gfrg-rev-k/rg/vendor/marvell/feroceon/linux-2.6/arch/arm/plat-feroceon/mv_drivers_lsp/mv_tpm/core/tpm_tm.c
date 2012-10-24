/*******************************************************************************
Copyright (C) Marvell International Ltd. and its affiliates

This software file (the "File") is owned and distributed by Marvell
International Ltd. and/or its affiliates ("Marvell") under the following
alternative licensing terms.  Once you have made an election to distribute the
File under one of the following license alternatives, please (i) delete this
introductory statement regarding license alternatives, (ii) delete the two
license alternatives that you have not elected to use and (iii) preserve the
Marvell copyright notice above.

********************************************************************************
Marvell Commercial License Option

If you received this File from Marvell and you have entered into a commercial
license agreement (a "Commercial License") with Marvell, the File is licensed
to you under the terms of the applicable Commercial License.

********************************************************************************
Marvell GPL License Option

If you received this File from Marvell, you may opt to use, redistribute and/or
modify this File in accordance with the terms and conditions of the General
Public License Version 2, June 1991 (the "GPL License"), a copy of which is
available along with the File in the license.txt file or by writing to the Free
Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 or
on the worldwide web at http://www.gnu.org/licenses/gpl.txt.

THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE IMPLIED
WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE ARE EXPRESSLY
DISCLAIMED.  The GPL License provides additional details about this warranty
disclaimer.
********************************************************************************
Marvell BSD License Option

If you received this File from Marvell, you may opt to use, redistribute and/or
modify this File under the following licensing terms.
Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    *   Redistributions of source code must retain the above copyright notice,
	    this list of conditions and the following disclaimer.

    *   Redistributions in binary form must reproduce the above copyright
	notice, this list of conditions and the following disclaimer in the
	documentation and/or other materials provided with the distribution.

    *   Neither the name of Marvell nor the names of its contributors may be
	used to endorse or promote products derived from this software without
	specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/

/*******************************************************************************
* tpm_tm.c
*
*
*  MODULE : TPM
*
*  DESCRIPTION : This file config tpm traffic mng
*
*  MODIFICATION HISTORY:
*           15Aug10   Orenbh - initial version created.
*
* FILE REVISION NUMBER:
*       Revision: 1.1.1.1
*******************************************************************************/
#include "tpm_common.h"
#include "tpm_header.h"

/*******************************************************************************
* tpm_tm_set_wan_egr_queue_sched()
*
* DESCRIPTION:      Configures the scheduling mode per WAN queue.
*
* INPUTS:
*       owner_id          - APP owner id  should be used for all API calls.
*       sched_ent         - entity for setting the scheduling mode: ex:TPM_TRG_PORT_WAN
*       sched_mode        - scheduler mode per port: strict(0) / wrr(1)
*       queue_id          - queue number
*       wrr_weight        - weight value when WRR scheduling (1-256)
*
* OUTPUTS:
*       None.
*
* RETURNS:
* On success, the function returns TPM_RC_OK. On error different types are returned
* according to the case - see tpm_error_code_t.
*
*******************************************************************************/
tpm_error_code_t tpm_tm_set_wan_egr_queue_sched(IN uint32_t owner_id,
						IN tpm_trg_port_type_t sched_ent,
						IN tpm_pp_sched_type_t sched_mode,
						IN uint8_t queue_id,
						IN uint16_t wrr_weight)
{
	tpm_error_code_t retVal = TPM_RC_OK;
	uint32_t i = 0;
	uint32_t num_tcont_llid = 0;

	printk(KERN_INFO "==ENTER==%s: owner_id[%d],sched_ent[%d],sched_mode[%d],"
		"             queue_id[%d], wrr_weight[%d]\n\r",
		__func__, owner_id, sched_ent, sched_mode, queue_id, wrr_weight);

	if (queue_id >= TPM_MAX_NUM_TX_QUEUE) {
		printk(KERN_INFO "==ERROE==%s: Invalid queue number: owner_id[%d],sched_ent[%d],sched_mode[%d],"
			"                                    queue_id[%d], wrr_weight[%d]\n\r",
			__func__, owner_id, sched_ent, sched_mode, queue_id, wrr_weight);
		return ERR_SW_TM_QUEUE_INVALID;
	}

	if ((sched_mode == TPM_PP_SCHED_WRR) && (wrr_weight > TPM_MAX_WRR_WEIGHT)) {

		printk(KERN_INFO "==ERROE==%s: WRR weight is out of rage: owner_id[%d],sched_mode[%d],"
			"                                   queue_id[%d], wrr_weight[%d]\n\r",
			__func__, owner_id, sched_mode, queue_id, wrr_weight);
		return ERR_SW_TM_QUEUE_INVALID;
	}

	tpm_db_num_tcont_llid_get(&num_tcont_llid);
	for (i = 0; i < num_tcont_llid; i++) {
		if (sched_ent & 0x1) {
			switch (sched_mode) {
			case TPM_PP_SCHED_STRICT:
				mvNetaTxqFixPrioSet(PON_PORT, i, queue_id);
				break;

			case TPM_PP_SCHED_WRR:
				mvNetaTxqWrrPrioSet(PON_PORT, i, queue_id, wrr_weight);
				break;

			default:
				printk(KERN_ERR " Unknown TXQ command \n");
				return ERR_GENERAL;
			}
		}
		sched_ent = sched_ent >> 1;
		if (sched_ent == 0)
			break;
	}
	if ((sched_ent != 0) && (i == num_tcont_llid - 1))
		printk(KERN_INFO "TCONT doesn't not exist\n");

	printk(KERN_INFO "==EXIT== %s:\n\r", __func__);

	return retVal;
}
EXPORT_SYMBOL(tpm_tm_set_wan_egr_queue_sched);

/*******************************************************************************
* tpm_tm_set_wan_sched_egr_rate_lim()
*
* DESCRIPTION:      Configures the egress rate limit of upstream traffic.
*
* INPUTS:
*       owner_id          - APP owner id  should be used for all API calls.
*       sched_ent         - entity for setting the rate limit: ex:TPM_TRG_PORT_WAN
*       rate_limit_val    - egress rate limit value
*       bucket_size       - bucket size value
*
* OUTPUTS:
*       None.
*
* RETURNS:
*       On success, the function returns TPM_RC_OK. On error different types are returned
*       according to the case - see tpm_error_code_t.
*
*******************************************************************************/
tpm_error_code_t tpm_tm_set_wan_sched_egr_rate_lim(IN uint32_t owner_id,
						IN tpm_trg_port_type_t sched_ent,
						IN uint32_t rate_limit_val,
						IN uint32_t bucket_size)
{
	uint32_t i = 0;
	uint32_t num_tcont_llid = 0;

	printk(KERN_INFO "==ENTER==%s: owner_id[%d],sched_ent[%d],rate_limit_val[%d], bucket_size[%d]\n\r",
		__func__, owner_id, sched_ent, rate_limit_val, bucket_size);

	tpm_db_num_tcont_llid_get(&num_tcont_llid);
	for (i = 0; i < num_tcont_llid; i++) {
		if (sched_ent & 0x1) {

			if (mvNetaTxpBurstSet(PON_PORT, i, bucket_size) != MV_OK) {
				printk(KERN_INFO
					"==ERROE==%s: Invalid bucket size value: "
					"owner_id[%d],sched_ent[%d] bucket_size[%d]\n\r",
					__func__, owner_id, i, bucket_size);
				return ERR_SW_TM_BUCKET_SIZE_INVALID;
			}
			mvNetaTxpRateSet(PON_PORT, i, rate_limit_val);
		}
		sched_ent = sched_ent >> 1;
		if (sched_ent == 0)
			break;
	}
	if ((sched_ent != 0) && (i == num_tcont_llid - 1))
		printk(KERN_INFO "TCONT doesn't not exist\n");

	printk(KERN_INFO "==EXIT== %s: \n\r", __func__);

	return TPM_RC_OK;

}
EXPORT_SYMBOL(tpm_tm_set_wan_sched_egr_rate_lim);

/*******************************************************************************
* tpm_tm_set_wan_queue_egr_rate_lim()
*
* DESCRIPTION:      Configures the upstream traffic egress rate limit for a specific
*                   queue of an upstream scheduling entity.
*
* INPUTS:
*       owner_id          - APP owner id  should be used for all API calls.
*       sched_ent         - entity for setting the rate limit: ex:TPM_TRG_PORT_WAN
*       queue_id          - queue number
*       rate_limit_val    - egress rate limit value
*       bucket_size       - bucket size value
*
* OUTPUTS:
*       None.
*
* RETURNS:
*       On success, the function returns TPM_RC_OK. On error different types are returned
*       according to the case - see tpm_error_code_t.
*
*******************************************************************************/
tpm_error_code_t tpm_tm_set_wan_queue_egr_rate_lim(IN uint32_t owner_id,
						IN tpm_trg_port_type_t sched_ent,
						IN uint32_t queue_id,
						IN uint32_t rate_limit_val,
						IN uint32_t bucket_size)
{
	uint32_t i = 0;
	uint32_t num_tcont_llid = 0;

	printk(KERN_INFO "==ENTER==%s: owner_id[%d],sched_ent[%d],queue_id[%d]"
		"             rate_limit_val[%d], bucket_size[%d] \n\r",
		__func__, owner_id, sched_ent, queue_id, rate_limit_val, bucket_size);

	tpm_db_num_tcont_llid_get(&num_tcont_llid);

	if (queue_id >= TPM_MAX_NUM_TX_QUEUE) {

		printk(KERN_INFO "==ERROE==%s: Invalid queue number: owner_id[%d],sched_ent[%d],queue_id[%d]"
			"                                   rate_limit_val[%d], bucket_size[%d] \n\r",
			__func__, owner_id, sched_ent, queue_id, rate_limit_val, bucket_size);
		return ERR_SW_TM_QUEUE_INVALID;
	}

	for (i = 0; i < num_tcont_llid; i++) {
		if (sched_ent & 0x1) {

			if (mvNetaTxqBurstSet(PON_PORT, i, queue_id, bucket_size) != MV_OK) {
				printk(KERN_INFO
					"==ERROE==%s: Invalid bucket size value: owner_id[%d], sched_ent[%d], queue_id[%d]"
					"             rate_limit_val[%d], bucket_size[%d] \n\r", __func__, owner_id,
					i, queue_id, rate_limit_val, bucket_size);
				return ERR_SW_TM_BUCKET_SIZE_INVALID;
			}
			mvNetaTxqRateSet(PON_PORT, i, queue_id, rate_limit_val);
		}
		sched_ent = sched_ent >> 1;
		if (sched_ent == 0)
			break;
	}
	if ((sched_ent != 0) && (i == num_tcont_llid - 1))
		printk(KERN_INFO "TCONT doesn't not exist\n");

	printk(KERN_INFO "==EXIT== %s: \n\r", __func__);

	return TPM_RC_OK;
}
EXPORT_SYMBOL(tpm_tm_set_wan_queue_egr_rate_lim);

/*******************************************************************************
* tpm_tm_set_wan_ingr_queue_sched()
*
* DESCRIPTION:      Configures the scheduling mode per all downstream traffic from the WAN.
*
* INPUTS:
*       owner_id          - APP owner id  should be used for all API calls.
*       sched_mode        - scheduler mode per port: strict(0) / wrr(1)
*       queue_id          - queue number
*       wrr_weight        - weight value when WRR scheduling (1-256)
*
* OUTPUTS:
*       None.
*
* RETURNS:
* On success, the function returns TPM_RC_OK. On error different types are returned
* according to the case - see tpm_error_code_t.
*
*******************************************************************************/
tpm_error_code_t tpm_tm_set_wan_ingr_queue_sched(IN uint32_t owner_id,
						IN tpm_pp_sched_type_t sched_mode,
						IN uint8_t queue_id,
						IN uint16_t wrr_weight)
{
	uint32_t tmp_tcont = 0;

	printk(KERN_INFO "==ENTER==%s: owner_id[%d],sched_mode[%d],"
		"             queue_id[%d], wrr_weight[%d]\n\r",
		__func__, owner_id, sched_mode, queue_id, wrr_weight);

	if (queue_id >= TPM_MAX_NUM_TX_QUEUE) {

		printk(KERN_INFO "==ERROE==%s: Invalid queue number: owner_id[%d],sched_mode[%d],"
			"                                   queue_id[%d], wrr_weight[%d]\n\r",
			__func__, owner_id, sched_mode, queue_id, wrr_weight);
		return ERR_SW_TM_QUEUE_INVALID;
	}

	if ((sched_mode == TPM_PP_SCHED_WRR) && (wrr_weight > TPM_MAX_WRR_WEIGHT)) {

		printk(KERN_INFO "==ERROE==%s: WRR weight is out of rage: owner_id[%d],sched_mode[%d],"
			"                                   queue_id[%d], wrr_weight[%d]\n\r",
			__func__, owner_id, sched_mode, queue_id, wrr_weight);
		return ERR_SW_TM_QUEUE_INVALID;
	}

	switch (sched_mode) {
	case TPM_PP_SCHED_STRICT:
		mvNetaTxqFixPrioSet(SW_GMAC_0, tmp_tcont, queue_id);
		break;

	case TPM_PP_SCHED_WRR:
		mvNetaTxqWrrPrioSet(SW_GMAC_0, tmp_tcont, queue_id, wrr_weight);
		break;

	default:
		printk(KERN_ERR " Unknown TXQ command \n");
		return ERR_GENERAL;

	}

	printk(KERN_INFO "==EXIT== %s:\n\r", __func__);

	return TPM_RC_OK;

}
EXPORT_SYMBOL(tpm_tm_set_wan_ingr_queue_sched);

/*******************************************************************************
* tpm_tm_set_wan_ingr_rate_lim()
*
* DESCRIPTION:      Configures the rate limit of all downstream traffic from the WAN.
*
* INPUTS:
*       owner_id          - APP owner id  should be used for all API calls.
*       rate_limit_val    - ingress rate limit value
*       bucket_size       - bucket size value
*
* OUTPUTS:
*       None.
*
* RETURNS:
*       On success, the function returns TPM_RC_OK. On error different types are returned
*       according to the case - see tpm_error_code_t.
*
*******************************************************************************/
tpm_error_code_t tpm_tm_set_wan_ingr_rate_lim(IN uint32_t owner_id,
						IN uint32_t rate_limit_val,
						IN uint32_t bucket_size)
{
	uint32_t tmp_tcont = 0;

	printk(KERN_INFO "==ENTER==%s: owner_id[%d],rate_limit_val[%d] bucket_size[%d]\n\r",
		__func__, owner_id, rate_limit_val, bucket_size);

	if (mvNetaTxpBurstSet(SW_GMAC_0, tmp_tcont, bucket_size) != MV_OK) {
		printk(KERN_INFO
			"==ERROE==%s: Invalid bucket size value: owner_id[%d],rate_limit_val[%d] bucket_size[%d]\n\r",
			__func__, owner_id, rate_limit_val, bucket_size);
		return ERR_SW_TM_BUCKET_SIZE_INVALID;
	}

	mvNetaTxpRateSet(SW_GMAC_0, tmp_tcont, rate_limit_val);

	printk(KERN_INFO "==EXIT== %s: \n\r", __func__);

	return TPM_RC_OK;
}
EXPORT_SYMBOL(tpm_tm_set_wan_ingr_rate_lim);

/*******************************************************************************
* tpm_tm_set_wan_q_ingr_rate_lim()
*
* DESCRIPTION:      Configures the egress rate limit of a specific queue for
*                   downstream traffic.
*
* INPUTS:
*       owner_id          - APP owner id  should be used for all API calls.
*       queue_id          - queue number
*       rate_limit_val    - ingress rate limit value
*       bucket_size       - bucket size value
*
* OUTPUTS:
*       None.
*
* RETURNS:
*       On success, the function returns TPM_RC_OK. On error different types are returned
*       according to the case - see tpm_error_code_t.
*
*******************************************************************************/
tpm_error_code_t tpm_tm_set_wan_q_ingr_rate_lim(IN uint32_t owner_id,
						IN uint32_t queue_id,
						IN uint32_t rate_limit_val,
						IN uint32_t bucket_size)
{
	uint32_t tmp_tcont = 0;

	printk(KERN_INFO "==ENTER==%s: owner_id[%d], queue_id[%d]"
		"             rate_limit_val[%d], bucket_size[%d] \n\r",
		__func__, owner_id, queue_id, rate_limit_val, bucket_size);

	if (queue_id >= TPM_MAX_NUM_TX_QUEUE) {
		printk(KERN_INFO "==ERROE==%s: Invalid queue number: owner_id[%d], queue_id[%d]"
			"                                   rate_limit_val[%d], bucket_size[%d] \n\r",
			__func__, owner_id, queue_id, rate_limit_val, bucket_size);
		return ERR_SW_TM_QUEUE_INVALID;
	}

	if (mvNetaTxqBurstSet(SW_GMAC_0, tmp_tcont, queue_id, bucket_size) != MV_OK) {
		printk(KERN_INFO "==ERROE==%s: Invalid bucket size value: owner_id[%d], queue_id[%d]"
			"              rate_limit_val[%d], bucket_size[%d] \n\r",
			__func__, owner_id, queue_id, rate_limit_val, bucket_size);
		return ERR_SW_TM_BUCKET_SIZE_INVALID;
	}

	mvNetaTxqRateSet(SW_GMAC_0, tmp_tcont, queue_id, rate_limit_val);

	printk(KERN_INFO "==EXIT== %s: \n\r", __func__);

	return TPM_RC_OK;
}
EXPORT_SYMBOL(tpm_tm_set_wan_q_ingr_rate_lim);
