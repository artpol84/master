/*
 * Copyright (c) 2004-2010 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2013 Los Alamos National Security, LLC.
 *                         All rights reserved.
 * Copyright (c) 2009-2012 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2011      Oak Ridge National Labs.  All rights reserved.
 * Copyright (c) 2013-2015 Intel, Inc.  All rights reserved.
 * Copyright (c) 2015      Mellanox Technologies, Inc.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "src/api/pmix.h"
#include "src/class/pmix_object.h"
#include "src/buffer_ops/types.h"
#include "test_common.h"

static int get_local_peers(int **_peers, int *count)
{
    pmix_value_t *val;
    int *peers = NULL;
    char *sptr, *token, *eptr, *str;
    int npeers;
    char nspace[PMIX_MAX_VALLEN];
    int rank, rc;

    /* To get namespace and rank */
    if (PMIX_SUCCESS != (rc = PMIx_Init(nspace, &rank))) {
        TEST_ERROR(("rank %d: PMIx_Init failed: %d", rank, rc));
        return rc;
    }
    /* to keep reference counter consistent */
    PMIx_Finalize();

    /* get number of neighbours on this node */
    if (PMIX_SUCCESS != (rc = PMIx_Get(nspace, rank, PMIX_LOCAL_SIZE, &val))) {
        TEST_ERROR(("rank %d: PMIx_Get local peer # failed: %d", rank, rc));
        return rc;
    }
    if (NULL == val) {
        TEST_ERROR(("rank %d: PMIx_Get local peer # returned NULL value", rank));
        return PMIX_ERROR;
    }

    if (val->type != PMIX_UINT32  ) {
        TEST_ERROR(("rank %d: local peer # attribute value type mismatch,"
                " want %d get %d(%d)",
                rank, PMIX_UINT32, val->type));
        return PMIX_ERROR;
    }
    npeers = val->data.uint32;
    peers = malloc(sizeof(int) * npeers);

    /* get ranks of neighbours on this node */
    if (PMIX_SUCCESS != (rc = PMIx_Get(nspace, rank, PMIX_LOCAL_PEERS, &val))) {
        TEST_ERROR(("rank %d: PMIx_Get local peers failed: %d", rank, rc));
        return rc;
    }
    if (NULL == val) {
        TEST_ERROR(("rank %d: PMIx_Get local peers returned NULL value", rank));
        return PMIX_ERROR;
    }

    if (val->type != PMIX_STRING  ) {
        TEST_ERROR(("rank %d: local peers attribute value type mismatch,"
                " want %d get %d(%d)",
                rank, PMIX_UINT32, val->type));
        return PMIX_ERROR;
    }

    *count = 0;
    sptr = NULL;
    str = val->data.string;
    do{
        if( *count > npeers ){
            TEST_ERROR(("rank %d: Bad peer ranks number: should be %d, actual %d (%s)",
                rank, npeers, *count, val->data.string));
            return PMIX_ERROR;
        }
        token = strtok_r(str, ",", &sptr);
        str = NULL;
        if( NULL != token ){
            peers[(*count)++] = strtol(token,&eptr,10);
            if( *eptr != '\0' ){
                TEST_ERROR(("rank %d: Bad peer ranks string", rank));
                return PMIX_ERROR;
            }
        }

    } while( NULL != token );

    if( *count != npeers ){
        TEST_ERROR(("rank %d: Bad peer ranks number: should be %d, actual %d (%s)",
                rank, npeers, *count, val->data.string));
        return PMIX_ERROR;
    }

    *_peers = peers;
    return PMIX_SUCCESS;
}

static void release_cb(pmix_status_t status, void *cbdata)
{
    int *ptr = (int*)cbdata;
    *ptr = 0;
}

int main(int argc, char **argv)
{
    char nspace[PMIX_MAX_VALLEN];
    int rank;
    int rc, i, j;
    pmix_value_t value;
    char key[50], sval[50];
    int collect = 0;
    int nonblocking = 0;
    int *peers, npeers;
    pmix_value_t *val = &value;

    TEST_OUTPUT(("rank X: Start", rank));

    parse_cmd(argc, argv, NULL, NULL, NULL);
    TEST_OUTPUT(("rank X: parsed command line", rank));

    /* init us */
    if (PMIX_SUCCESS != (rc = PMIx_Init(nspace, &rank))) {
        TEST_ERROR(("rank %d: PMIx_Init failed: %d", rank, rc));
        goto error_out;
    }

    TEST_OUTPUT(("rank %d: PMIx_Init success", rank));

    if (PMIX_SUCCESS != (rc = PMIx_Get(nspace, rank,PMIX_UNIV_SIZE,&val))) {
        TEST_ERROR(("rank %d: PMIx_Get universe size failed: %d", rank, rc));
        goto error_out;
    }
    if (NULL == val) {
        TEST_ERROR(("rank %d: PMIx_Get universe size returned NULL value", rank));
        goto error_out;
    }
    if (val->type != PMIX_UINT32 || val->data.uint32 != nprocs ) {
        TEST_ERROR(("rank %d: Universe size value or type mismatch,"
                    " want %d(%d) get %d(%d)",
                    rank, nprocs, PMIX_UINT32,
                    val->data.integer, val->type));
        goto error_out;
    }

    TEST_OUTPUT(("rank %d: Universe size check: PASSED", rank));

    if( 0 != strcmp(nspace, TEST_NAMESPACE) ) {
        TEST_ERROR(("rank %d: Bad nspace!", rank));
        goto error_out;
    }

    for (i=0; i < 3; i++) {
        (void)snprintf(key, 50, "local-key-%d", i);
        PMIX_VAL_SET(&value, int, 12340 + i);
        if (PMIX_SUCCESS != (rc = PMIx_Put(PMIX_LOCAL, key, &value))) {
            TEST_ERROR(("rank %d: PMIx_Put failed: %d", rank, rc));
            goto error_out;
        }

        (void)snprintf(key, 50, "remote-key-%d", i);
        (void)snprintf(sval, 50, "Test string #%d", i);
        PMIX_VAL_SET(&value, string, sval);
        if (PMIX_SUCCESS != (rc = PMIx_Put(PMIX_REMOTE, key, &value))) {
            TEST_ERROR(("rank %d: PMIx_Put failed: %d", rank, rc));
            goto error_out;
        }
        PMIX_VALUE_DESTRUCT(&value);

        (void)snprintf(key, 50, "global-key-%d", i);
        PMIX_VAL_SET(&value, float, 12.15 + i);
        if (PMIX_SUCCESS != (rc = PMIx_Put(PMIX_GLOBAL, key, &value))) {
            TEST_ERROR(("rank %d: PMIx_Put failed: %d", rank, rc));
            goto error_out;
        }
    }

    /* Submit the data */
    if (PMIX_SUCCESS != (rc = PMIx_Commit())) {
        TEST_ERROR(("rank %d: PMIx_Commit failed: %d", rank, rc));
        goto error_out;
    }

    /* Perform a fence if was requested */
    if( !nonblocking ){
        if (PMIX_SUCCESS != (rc = PMIx_Fence(NULL, 0, 1))) {
            TEST_ERROR(("rank %d: PMIx_Fence failed: %d", rank, rc));
            goto error_out;
        }
    } else {
        int in_progress = 1, count;
        if ( PMIX_SUCCESS != (rc = PMIx_Fence_nb(NULL, 0, collect, release_cb, &in_progress))) {
            TEST_ERROR(("rank %d: PMIx_Fence failed: %d", rank, rc));
            goto error_out;
        }

        count = 0;
        while( in_progress ){
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 100;
            nanosleep(&ts,NULL);
            count++;

        }
        TEST_OUTPUT(("PMIx_Fence_nb(barrier,collect): free time: %lfs", count*100*1E-9));
    }
    TEST_OUTPUT(("rank %d: Fence successfully completed", rank));

    if (PMIX_SUCCESS != (rc = get_local_peers(&peers, &npeers))) {
        goto error_out;
    }

    /* Check the predefined output */
    for (i=0; i < (int)nprocs; i++) {

        for (j=0; j < 3; j++) {

            int local = 0, k;
            for(k=0; k<npeers; k++){
                if( peers[k] == i){
                    local = 1;
                }
            }
            if( local ){
                sprintf(key,"local-key-%d",j);
                if (PMIX_SUCCESS != (rc = PMIx_Get(nspace, i, key, &val))) {
                    TEST_ERROR(("rank %d: PMIx_Get failed: %d", rank, rc));
                    goto error_out;
                }
                if (NULL == val) {
                    TEST_ERROR(("rank %d: PMIx_Get returned NULL value", rank));
                    goto error_out;
                }
                if (val->type != PMIX_INT || val->data.integer != (12340+j)) {
                    TEST_ERROR(("rank %d: Key %s value or type mismatch,"
                            " want %d(%d) get %d(%d)",
                            rank, key, (12340+j), PMIX_INT,
                            val->data.integer, val->type));
                    goto error_out;
                }
                TEST_VERBOSE(("rank %d: GET OF %s SUCCEEDED", rank, key));
                PMIX_VALUE_RELEASE(val);
            }

            sprintf(key,"remote-key-%d",j);
            sprintf(sval,"Test string #%d",j);
            if (PMIX_SUCCESS != (rc = PMIx_Get(nspace, i, key, &val))) {
                TEST_ERROR(("rank %d: PMIx_Get failed (%d)", rank, rc));
                goto error_out;
            }
            if (val->type != PMIX_STRING || strcmp(val->data.string, sval)) {
                TEST_ERROR(("rank %d:  Key %s value or type mismatch, wait %s(%d) get %s(%d)",
                            rank, key, sval, PMIX_STRING, val->data.string, val->type));
                goto error_out;
            }
            TEST_VERBOSE(("rank %d: GET OF %s SUCCEEDED", rank, key));
            PMIX_VALUE_RELEASE(val);

            sprintf(key, "global-key-%d", j);
            if (PMIX_SUCCESS != (rc = PMIx_Get(nspace, i, key, &val))) {
                TEST_ERROR(("rank %d: PMIx_Get failed (%d)", rank, rc))
                goto error_out;
            }
            if (val->type != PMIX_FLOAT || val->data.fval != (float)12.15 + j) {
                TEST_ERROR(("rank %d [ERROR]: Key %s value or type mismatch,"
                            " wait %f(%d) get %f(%d)",
                            rank, key, ((float)10.15 + i), PMIX_FLOAT,
                            val->data.fval, val->type));
                goto error_out;
            }
            PMIX_VALUE_RELEASE(val);
            TEST_VERBOSE(("rank %d: GET OF %s SUCCEEDED", rank, key));
        }

        /* ask for a non-existent key */
        if (PMIX_SUCCESS == (rc = PMIx_Get(nspace, i, "foobar", &val))) {
            TEST_ERROR(("rank %d: PMIx_Get returned success instead of failure",
                        rank));
            goto error_out;
        }
        if (PMIX_ERR_NOT_FOUND != rc) {
            TEST_ERROR(("rank %d [ERROR]: PMIx_Get returned %d instead of not_found",
                        rank, rc));
        }
        if (NULL != val) {
            TEST_ERROR(("rank %d [ERROR]: PMIx_Get did not return NULL value", rank));
            goto error_out;
        }
        TEST_VERBOSE(("rank %d: rank %d is OK", rank, i));
    }

    TEST_OUTPUT(("rank %d: test PASSED", rank));

 error_out:
    /* finalize us */
    TEST_OUTPUT(("rank %d: Finalizing", rank));
    fflush(stderr);
    if (PMIX_SUCCESS != (rc = PMIx_Finalize())) {
        TEST_ERROR(("rank %d:PMIx_Finalize failed: %d", rank, rc));
    } else {
        TEST_OUTPUT(("rank %d:PMIx_Finalize successfully completed", rank));
    }

    exit(0);
}
