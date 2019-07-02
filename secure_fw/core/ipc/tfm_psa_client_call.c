/*
 * Copyright (c) 2019, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdlib.h>
#include "tfm_psa_client_call.h"
#include "psa_service.h"
#include "tfm_utils.h"
#include "tfm_wait.h"
#include "tfm_internal_defines.h"
#include "tfm_message_queue.h"
#include "tfm_spm.h"
#include "tfm_memory_utils.h"

uint32_t tfm_psa_framework_version(void)
{
    return PSA_FRAMEWORK_VERSION;
}

uint32_t tfm_psa_version(uint32_t sid, int32_t ns_caller)
{
    struct tfm_spm_service_t *service;

    /*
     * It should return PSA_VERSION_NONE if the RoT Service is not
     * implemented.
     */
    service = tfm_spm_get_service_by_sid(sid);
    if (!service) {
        return PSA_VERSION_NONE;
    }

    /*
     * It should return PSA_VERSION_NONE if the caller is not authorized
     * to access the RoT Service.
     */
    if (ns_caller && !service->service_db->non_secure_client) {
        return PSA_VERSION_NONE;
    }

    return service->service_db->minor_version;
}

psa_status_t tfm_psa_connect(uint32_t sid, uint32_t minor_version,
                             int32_t ns_caller)
{
    struct tfm_spm_service_t *service;
    struct tfm_msg_body_t *msg;

    /* It is a fatal error if the RoT Service does not exist on the platform */
    service = tfm_spm_get_service_by_sid(sid);
    if (!service) {
        tfm_panic();
    }

    /*
     * It is a fatal error if the caller is not authorized to access the RoT
     * Service.
     */
    if (ns_caller && !service->service_db->non_secure_client) {
        tfm_panic();
    }

    /*
     * It is a fatal error if the version of the RoT Service requested is not
     * supported on the platform.
     */
    if (tfm_spm_check_client_version(service, minor_version) != IPC_SUCCESS) {
        tfm_panic();
    }

    /* No input or output needed for connect message */
    msg = tfm_spm_create_msg(service, PSA_NULL_HANDLE, PSA_IPC_CONNECT,
                             ns_caller, NULL, 0, NULL, 0, NULL);
    if (!msg) {
        return PSA_CONNECTION_BUSY;
    }

    /*
     * Send message and wake up the SP who is waiting on message queue,
     * and scheduler triggered
     */
    tfm_spm_send_event(service, msg);

    return PSA_SUCCESS;
}

psa_status_t tfm_psa_call(psa_handle_t handle, const psa_invec *in_vec,
                          size_t in_len, psa_outvec *out_vec, size_t out_len,
                          int32_t ns_caller)
{
    psa_invec invecs[PSA_MAX_IOVEC];
    psa_outvec outvecs[PSA_MAX_IOVEC];
    struct tfm_spm_service_t *service;
    struct tfm_msg_body_t *msg;
    int i;

    /* It is a fatal error if in_len + out_len > PSA_MAX_IOVEC. */
    if (in_len + out_len > PSA_MAX_IOVEC) {
        tfm_panic();
    }

    /* It is a fatal error if an invalid handle was passed. */
    service = tfm_spm_get_service_by_handle(handle);
    if (!service) {
        /* FixMe: Need to implement one mechanism to resolve this failure. */
        tfm_panic();
    }

    /* It is a fatal error if an invalid memory reference was provide. */
    if (tfm_memory_check((void *)in_vec, in_len * sizeof(psa_invec),
        ns_caller) != IPC_SUCCESS) {
        tfm_panic();
    }
    if (tfm_memory_check(out_vec, out_len * sizeof(psa_outvec),
        ns_caller) != IPC_SUCCESS) {
        tfm_panic();
    }

    tfm_memset(invecs, 0, sizeof(invecs));
    tfm_memset(outvecs, 0, sizeof(outvecs));

    /* Copy the address out to avoid TOCTOU attacks. */
    tfm_memcpy(invecs, in_vec, in_len * sizeof(psa_invec));
    tfm_memcpy(outvecs, out_vec, out_len * sizeof(psa_outvec));

    /*
     * It is a fatal error if an invalid payload memory reference
     * was provided.
     */
    for (i = 0; i < in_len; i++) {
        if (tfm_memory_check((void *)invecs[i].base, invecs[i].len,
            ns_caller) != IPC_SUCCESS) {
            tfm_panic();
        }
    }
    for (i = 0; i < out_len; i++) {
        if (tfm_memory_check(outvecs[i].base, outvecs[i].len,
            ns_caller) != IPC_SUCCESS) {
            tfm_panic();
        }
    }

    /*
     * FixMe: Need to check if the message is unrecognized by the RoT
     * Service or incorrectly formatted.
     */
    msg = tfm_spm_create_msg(service, handle, PSA_IPC_CALL, ns_caller, invecs,
                             in_len, outvecs, out_len, out_vec);
    if (!msg) {
        /* FixMe: Need to implement one mechanism to resolve this failure. */
        tfm_panic();
    }

    /*
     * Send message and wake up the SP who is waiting on message queue,
     * and scheduler triggered
     */
    if (tfm_spm_send_event(service, msg) != IPC_SUCCESS) {
        /* FixMe: Need to refine failure process here. */
        tfm_panic();
    }

    return PSA_SUCCESS;
}

void tfm_psa_close(psa_handle_t handle, int32_t ns_caller)
{
    struct tfm_spm_service_t *service;
    struct tfm_msg_body_t *msg;

    /* It will have no effect if called with the NULL handle */
    if (handle == PSA_NULL_HANDLE) {
        return;
    }

    /*
     * It is a fatal error if an invalid handle was provided that is not the
     * null handle..
     */
    service = tfm_spm_get_service_by_handle(handle);
    if (!service) {
        /* FixMe: Need to implement one mechanism to resolve this failure. */
        tfm_panic();
    }

    /* No input or output needed for close message */
    msg = tfm_spm_create_msg(service, handle, PSA_IPC_DISCONNECT, ns_caller,
                             NULL, 0, NULL, 0, NULL);
    if (!msg) {
        /* FixMe: Need to implement one mechanism to resolve this failure. */
        return;
    }

    /*
     * Send message and wake up the SP who is waiting on message queue,
     * and scheduler triggered
     */
    tfm_spm_send_event(service, msg);
}