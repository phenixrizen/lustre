/* -*- MODE: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *  lustre/fld/fld_request.c
 *  FLD (Fids Location Database)
 *
 *  Copyright (C) 2006 Cluster File Systems, Inc.
 *   Author: Yury Umanets <umka@clusterfs.com>
 *
 *   This file is part of the Lustre file system, http://www.lustre.org
 *   Lustre is a trademark of Cluster File Systems, Inc.
 *
 *   You may have signed or agreed to another license before downloading
 *   this software.  If so, you are bound by the terms and conditions
 *   of that agreement, and the following does not apply to you.  See the
 *   LICENSE file included with this distribution for more information.
 *
 *   If you did not agree to a different license, then this copy of Lustre
 *   is open source software; you can redistribute it and/or modify it
 *   under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   In either case, Lustre is distributed in the hope that it will be
 *   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   license text for more details.
 */
#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#define DEBUG_SUBSYSTEM S_FLD

#ifdef __KERNEL__
# include <libcfs/libcfs.h>
# include <linux/module.h>
# include <linux/jbd.h>
# include <asm/div64.h>
#else /* __KERNEL__ */
# include <liblustre.h>
# include <libcfs/list.h>
#endif

#include <obd.h>
#include <obd_class.h>
#include <lustre_ver.h>
#include <obd_support.h>
#include <lprocfs_status.h>

#include <dt_object.h>
#include <md_object.h>
#include <lustre_req_layout.h>
#include <lustre_fld.h>
#include "fld_internal.h"

static int fld_rrb_hash(struct lu_client_fld *fld,
                        seqno_t seq)
{
        LASSERT(fld->fld_count > 0);
        return do_div(seq, fld->fld_count);
}

static struct fld_target *
fld_rrb_scan(struct lu_client_fld *fld, seqno_t seq)
{
        struct fld_target *target;
        int hash;
        ENTRY;

        hash = fld_rrb_hash(fld, seq);

        list_for_each_entry(target, &fld->fld_targets, fldt_chain) {
                if (target->fldt_idx == hash)
                        RETURN(target);
        }

        /* if target is not found, there is logical error anyway, so here is
         * LBUG() to catch this situation. */
        LBUG();
        RETURN(NULL);
}

static int fld_dht_hash(struct lu_client_fld *fld,
                        seqno_t seq)
{
        /* XXX: here should be DHT hash */
        return fld_rrb_hash(fld, seq);
}

static struct fld_target *
fld_dht_scan(struct lu_client_fld *fld, seqno_t seq)
{
        /* XXX: here should be DHT scan code */
        return fld_dht_scan(fld, seq);
}

struct lu_fld_hash fld_hash[3] = {
        {
                .fh_name = "DHT",
                .fh_hash_func = fld_dht_hash,
                .fh_scan_func = fld_dht_scan
        },
        {
                .fh_name = "RRB",
                .fh_hash_func = fld_rrb_hash,
                .fh_scan_func = fld_rrb_scan
        },
        {
                0,
        }
};

static struct fld_target *
fld_client_get_target(struct lu_client_fld *fld,
                      seqno_t seq)
{
        struct fld_target *target;
        ENTRY;

        LASSERT(fld->fld_hash != NULL);

        spin_lock(&fld->fld_lock);
        target = fld->fld_hash->fh_scan_func(fld, seq);
        spin_unlock(&fld->fld_lock);

        RETURN(target);
}

/*
 * Add export to FLD. This is usually done by CMM and LMV as they are main users
 * of FLD module.
 */
int fld_client_add_target(struct lu_client_fld *fld,
                          struct obd_export *exp)
{
        struct client_obd *cli = &exp->exp_obd->u.cli;
        struct fld_target *target, *tmp;
        ENTRY;

        LASSERT(exp != NULL);

        CDEBUG(D_INFO|D_WARNING, "%s: adding export %s\n",
	       fld->fld_name, cli->cl_target_uuid.uuid);

        OBD_ALLOC_PTR(target);
        if (target == NULL)
                RETURN(-ENOMEM);

        spin_lock(&fld->fld_lock);
        list_for_each_entry(tmp, &fld->fld_targets, fldt_chain) {
                if (obd_uuid_equals(&tmp->fldt_exp->exp_client_uuid,
                                    &exp->exp_client_uuid))
                {
                        spin_unlock(&fld->fld_lock);
                        OBD_FREE_PTR(target);
                        RETURN(-EEXIST);
                }
        }

        target->fldt_exp = class_export_get(exp);
        target->fldt_idx = fld->fld_count;

        list_add_tail(&target->fldt_chain,
                      &fld->fld_targets);
        fld->fld_count++;
        spin_unlock(&fld->fld_lock);

        RETURN(0);
}
EXPORT_SYMBOL(fld_client_add_target);

/* remove export from FLD */
int fld_client_del_target(struct lu_client_fld *fld,
                          struct obd_export *exp)
{
        struct fld_target *target, *tmp;
        ENTRY;

        spin_lock(&fld->fld_lock);
        list_for_each_entry_safe(target, tmp,
                                 &fld->fld_targets, fldt_chain) {
                if (obd_uuid_equals(&target->fldt_exp->exp_client_uuid,
                                    &exp->exp_client_uuid))
                {
                        fld->fld_count--;
                        list_del(&target->fldt_chain);
                        spin_unlock(&fld->fld_lock);
                        class_export_put(target->fldt_exp);
                        OBD_FREE_PTR(target);
                        RETURN(0);
                }
        }
        spin_unlock(&fld->fld_lock);
        RETURN(-ENOENT);
}
EXPORT_SYMBOL(fld_client_del_target);

static void fld_client_proc_fini(struct lu_client_fld *fld);

#ifdef LPROCFS
static int fld_client_proc_init(struct lu_client_fld *fld)
{
        int rc;
        ENTRY;

        fld->fld_proc_dir = lprocfs_register(fld->fld_name,
                                             proc_lustre_root,
                                             NULL, NULL);

        if (IS_ERR(fld->fld_proc_dir)) {
                CERROR("LProcFS failed in fld-init\n");
                rc = PTR_ERR(fld->fld_proc_dir);
                RETURN(rc);
        }

        rc = lprocfs_add_vars(fld->fld_proc_dir,
                              fld_client_proc_list, fld);
        if (rc) {
                CERROR("can't init FLD "
                       "proc, rc %d\n", rc);
                GOTO(out_cleanup, rc);
        }

        RETURN(0);

out_cleanup:
        fld_client_proc_fini(fld);
        return rc;
}

static void fld_client_proc_fini(struct lu_client_fld *fld)
{
        ENTRY;
        if (fld->fld_proc_dir) {
                if (!IS_ERR(fld->fld_proc_dir))
                        lprocfs_remove(fld->fld_proc_dir);
                fld->fld_proc_dir = NULL;
        }
        EXIT;
}
#else
static int fld_client_proc_init(struct lu_client_fld *fld)
{
        return 0;
}

static void fld_client_proc_fini(struct lu_client_fld *fld)
{
        return;
}
#endif

static inline int hash_is_sane(int hash)
{
        return (hash >= 0 && hash < ARRAY_SIZE(fld_hash));
}

/* 1M of FLD cache will not hurt client a lot */
#define FLD_CACHE_SIZE 1024000

/* cache threshold is 10 percent of size */
#define FLD_CACHE_THRESHOLD 10

int fld_client_init(struct lu_client_fld *fld,
                    const char *uuid, int hash)
{
#ifdef __KERNEL__
        int cache_size, cache_threshold;
#endif
        int rc;
        ENTRY;

        LASSERT(fld != NULL);

        if (!hash_is_sane(hash)) {
                CERROR("wrong hash function %#x\n", hash);
                RETURN(-EINVAL);
        }

        INIT_LIST_HEAD(&fld->fld_targets);
        spin_lock_init(&fld->fld_lock);
        fld->fld_hash = &fld_hash[hash];
        fld->fld_count = 0;

        snprintf(fld->fld_name, sizeof(fld->fld_name),
                 "%s-cli-%s", LUSTRE_FLD_NAME, uuid);

#ifdef __KERNEL__
        cache_size = FLD_CACHE_SIZE /
                sizeof(struct fld_cache_entry);

        cache_threshold = cache_size *
                FLD_CACHE_THRESHOLD / 100;

        fld->fld_cache = fld_cache_init(FLD_HTABLE_SIZE,
                                        cache_size,
                                        cache_threshold);
        if (IS_ERR(fld->fld_cache)) {
                rc = PTR_ERR(fld->fld_cache);
                fld->fld_cache = NULL;
                GOTO(out, rc);
        }
#endif

        rc = fld_client_proc_init(fld);
        if (rc)
                GOTO(out, rc);
        EXIT;
out:
        if (rc)
                fld_client_fini(fld);
        else
                CDEBUG(D_INFO|D_WARNING,
                       "Client FLD, using \"%s\" hash\n",
                       fld->fld_hash->fh_name);
        return rc;
}
EXPORT_SYMBOL(fld_client_init);

void fld_client_fini(struct lu_client_fld *fld)
{
        struct fld_target *target, *tmp;
        ENTRY;

        fld_client_proc_fini(fld);

        spin_lock(&fld->fld_lock);
        list_for_each_entry_safe(target, tmp,
                                 &fld->fld_targets, fldt_chain) {
                fld->fld_count--;
                list_del(&target->fldt_chain);
                class_export_put(target->fldt_exp);
                OBD_FREE_PTR(target);
        }
        spin_unlock(&fld->fld_lock);

#ifdef __KERNEL__
        if (fld->fld_cache != NULL) {
                if (!IS_ERR(fld->fld_cache))
                        fld_cache_fini(fld->fld_cache);
                fld->fld_cache = NULL;
        }
#endif

        CDEBUG(D_INFO|D_WARNING, "Client FLD finalized\n");
        EXIT;
}
EXPORT_SYMBOL(fld_client_fini);

static int fld_client_rpc(struct obd_export *exp,
                          struct md_fld *mf, __u32 fld_op)
{
        int size[2] = {sizeof(__u32), sizeof(struct md_fld)}, rc;
        int mf_size = sizeof(struct md_fld);
        struct ptlrpc_request *req;
        struct req_capsule pill;
        struct md_fld *pmf;
        __u32 *op;
        ENTRY;

        LASSERT(exp != NULL);

        req = ptlrpc_prep_req(class_exp2cliimp(exp),
                              LUSTRE_MDS_VERSION, FLD_QUERY,
                              2, size, NULL);
        if (req == NULL)
                RETURN(-ENOMEM);

        req_capsule_init(&pill, req, RCL_CLIENT, NULL);

        req_capsule_set(&pill, &RQF_FLD_QUERY);

        op = req_capsule_client_get(&pill, &RMF_FLD_OPC);
        *op = fld_op;

        pmf = req_capsule_client_get(&pill, &RMF_FLD_MDFLD);
        *pmf = *mf;

        req->rq_replen = lustre_msg_size(1, &mf_size);
        req->rq_request_portal = FLD_REQUEST_PORTAL;

        rc = ptlrpc_queue_wait(req);
        if (rc)
                GOTO(out_req, rc);

        pmf = req_capsule_server_get(&pill, &RMF_FLD_MDFLD);
        if (pmf == NULL)
                GOTO(out_req, rc = -EFAULT);
        *mf = *pmf;
        EXIT;
out_req:
        req_capsule_fini(&pill);
        ptlrpc_req_finished(req);
        return rc;
}

int fld_client_create(struct lu_client_fld *fld,
                      seqno_t seq, mdsno_t mds)
{
        struct md_fld md_fld = { .mf_seq = seq, .mf_mds = mds };
        struct fld_target *target;
        int rc;
        ENTRY;

        target = fld_client_get_target(fld, seq);
        LASSERT(target != NULL);

        rc = fld_client_rpc(target->fldt_exp, &md_fld, FLD_CREATE);

        if (rc == 0) {
                /*
                 * Do not return result of calling fld_cache_insert()
                 * here. First of all because it may return -EEXISTS. Another
                 * reason is that, we do not want to stop proceeding because of
                 * cache errors. --umka
                 */
                fld_cache_insert(fld->fld_cache, seq, mds);
        }
        RETURN(rc);
}
EXPORT_SYMBOL(fld_client_create);

int fld_client_delete(struct lu_client_fld *fld,
                      seqno_t seq)
{
        struct md_fld md_fld = { .mf_seq = seq, .mf_mds = 0 };
        struct fld_target *target;
        int rc;
        ENTRY;

        fld_cache_delete(fld->fld_cache, seq);

        target = fld_client_get_target(fld, seq);
        LASSERT(target != NULL);

        rc = fld_client_rpc(target->fldt_exp,
                            &md_fld, FLD_DELETE);

        RETURN(rc);
}
EXPORT_SYMBOL(fld_client_delete);

int fld_client_lookup(struct lu_client_fld *fld,
                      seqno_t seq, mdsno_t *mds)
{
        struct md_fld md_fld = { .mf_seq = seq, .mf_mds = 0 };
        struct fld_target *target;
        int rc;
        ENTRY;

        /* lookup it in the cache */
        rc = fld_cache_lookup(fld->fld_cache, seq, mds);
        if (rc == 0)
                RETURN(0);

        /* can not find it in the cache */
        target = fld_client_get_target(fld, seq);
        LASSERT(target != NULL);

        rc = fld_client_rpc(target->fldt_exp,
                            &md_fld, FLD_LOOKUP);
        if (rc == 0) {
                *mds = md_fld.mf_mds;

                /*
                 * Do not return error here as well. See previous comment in
                 * same situation in function fld_client_create(). --umka
                 */
                fld_cache_insert(fld->fld_cache, seq, *mds);
        }
        RETURN(rc);
}
EXPORT_SYMBOL(fld_client_lookup);
