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
 * tpm_self_check.h
 * DESCRIPTION:
 *               Traffic Processor Manager = TPM
 * DEPENDENCIES:
 *               None
 * CREATED BY:   Evan
 * DATE CREATED:
 * FILE REVISION NUMBER:
 ******************************************************************************/
#ifndef _TPM_SELF_CHECK_H_
#define _TPM_SELF_CHECK_H_

typedef int32_t (*tpm_check_rebuild_pnc_t)(tpm_db_pon_type_t pon_type,
    tpm_rule_entry_t *tpm_rule, tpm_db_mod_conn_t *mod_con,
    tpm_db_api_entry_t *api_ent_mem_area, tpm_pncl_pnc_full_t *pnc_data,
    uint32_t *mod_entry, tpm_pkt_mod_t *pkt_mod, tpm_pkt_mod_bm_t *pkt_mod_bm,
    tpm_pkt_mod_int_bm_t *int_mod_bm, tpm_trg_port_type_t *trg_port,
    tpm_rule_action_t *rule_action);

typedef enum {
    TPMCHECK_CHAIN_TYPE_OK,
    TPMCHECK_DB_CHAIN_TYPE_MISMATCH,
    TPMCHECK_HW_CHAIN_TYPE_MISMATCH,
    TPMCHECK_BOTH_CHAIN_TYPE_MISMATCH
} tpm_main_chain_check_type_t;

typedef enum {
    TPM_CHECK_DISABLE,/*is not TPM check, just normal usage*/
    TPM_CHECK_MAIN_CHAIN_IDX_DB,/*is tpm check, and main chain index from DB*/	
    TPM_CHECK_MAIN_CHAIN_IDX_HW,/*is tpm check, and main chain index from HW*/
    TPM_CHECK_WITH_DB,/*is tpm check, and check with TPM DB*/
    TPM_CHECK_WITH_HW,/*is tpm check, and check with HW*/
    TPM_CHECK_ENABLE,/*is TPM check*/
} tpm_self_check_enable_t;

tpm_error_code_t tpm_self_check(uint32_t owner_id,
    tpm_self_check_level_enum_t check_level);

#endif
