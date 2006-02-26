/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *  Copyright (C) 2001-2004 Cluster File Systems, Inc. <info@clusterfs.com>
 *
 *   This file is part of Lustre, http://www.lustre.org.
 *
 *   Lustre is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Lustre is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Lustre; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Filesystem interface helper.
 *
 */

#ifndef _LINUX_LUSTRE_FSFILT_H
#define _LINUX_LUSTRE_FSFILT_H

#ifndef _LUSTRE_FSFILT_H
#error Do not #include this file directly. #include <lustre_fsfilt.h> instead
#endif

#ifdef __KERNEL__

#include <obd.h>
#include <obd_class.h>

typedef void (*fsfilt_cb_t)(struct obd_device *obd, __u64 last_rcvd,
                            void *data, int error);

struct fsfilt_objinfo {
        struct dentry *fso_dentry;
        int fso_bufcnt;
};

#define XATTR_LUSTRE_MDS_LOV_EA         "lov"

struct lustre_dquot;
struct fsfilt_operations {
        struct list_head fs_list;
        struct module *fs_owner;
        char   *fs_type;
        char   *(* fs_label)(struct super_block *sb);
        char   *(* fs_uuid)(struct super_block *sb);
        void   *(* fs_start)(struct inode *inode, int op, void *desc_private,
                             int logs);
        void   *(* fs_brw_start)(int objcount, struct fsfilt_objinfo *fso,
                                 int niocount, struct niobuf_local *nb,
                                 void *desc_private, int logs);
        int     (* fs_commit)(struct inode *inode, void *handle,int force_sync);
        int     (* fs_commit_async)(struct inode *inode, void *handle,
                                        void **wait_handle);
        int     (* fs_commit_wait)(struct inode *inode, void *handle);
        int     (* fs_setattr)(struct dentry *dentry, void *handle,
                               struct iattr *iattr, int do_trunc);
        int     (* fs_iocontrol)(struct inode *inode, struct file *file,
                                 unsigned int cmd, unsigned long arg);
        int     (* fs_set_md)(struct inode *inode, void *handle, void *md,
                              int size, const char *name);
        int     (* fs_get_md)(struct inode *inode, void *md, int size,
                              const char *name);
        /*
         * this method is needed to make IO operation fsfilt nature depend.
         *
         * This operation maybe synchronous or asynchronous.
         *
         * Return convention: positive number of bytes written (synchronously)
         * on success. Negative errno value on failure. Zero if asynchronous
         * IO was submitted successfully.
         *
         */
        int     (* fs_send_bio)(int rw, struct inode *inode,struct kiobuf *bio);
        ssize_t (* fs_readpage)(struct file *file, char *buf, size_t count,
                                loff_t *offset);
        int     (* fs_add_journal_cb)(struct obd_device *obd, __u64 last_rcvd,
                                      void *handle, fsfilt_cb_t cb_func,
                                      void *cb_data);
        int     (* fs_statfs)(struct super_block *sb, struct obd_statfs *osfs);
        int     (* fs_sync)(struct super_block *sb);
        int     (* fs_map_inode_pages)(struct inode *inode, struct page **page,
                                       int pages, unsigned long *blocks,
                                       int *created, int create,
                                       struct semaphore *sem);
        int     (* fs_prep_san_write)(struct inode *inode, long *blocks,
                                      int nblocks, loff_t newsize);
        int     (* fs_write_record)(struct file *, void *, int size, loff_t *,
                                    int force_sync);
        int     (* fs_read_record)(struct file *, void *, int size, loff_t *);
        int     (* fs_setup)(struct super_block *sb);
        int     (* fs_get_op_len)(int, struct fsfilt_objinfo *, int);
        int     (* fs_quotacheck)(struct super_block *sb,
                                  struct obd_quotactl *oqctl);
        int     (* fs_quotactl)(struct super_block *sb,
                                struct obd_quotactl *oqctl);
        int     (* fs_quotainfo)(struct lustre_quota_info *lqi, int type,
                                 int cmd);
        int     (* fs_qids)(struct file *file, struct inode *inode, int type,
                            struct list_head *list);
        int     (* fs_dquot)(struct lustre_dquot *dquot, int cmd);
};

extern int fsfilt_register_ops(struct fsfilt_operations *fs_ops);
extern void fsfilt_unregister_ops(struct fsfilt_operations *fs_ops);
extern struct fsfilt_operations *fsfilt_get_ops(const char *type);
extern void fsfilt_put_ops(struct fsfilt_operations *fs_ops);

static inline char *fsfilt_label(struct obd_device *obd, struct super_block *sb)
{
        if (obd->obd_fsops->fs_label == NULL)
                return NULL;
        if (obd->obd_fsops->fs_label(sb)[0] == '\0')
                return NULL;

        return obd->obd_fsops->fs_label(sb);
}

static inline __u8 *fsfilt_uuid(struct obd_device *obd, struct super_block *sb)
{
        if (obd->obd_fsops->fs_uuid == NULL)
                return NULL;

        return obd->obd_fsops->fs_uuid(sb);
}

#define FSFILT_OP_UNLINK         1
#define FSFILT_OP_RMDIR          2
#define FSFILT_OP_RENAME         3
#define FSFILT_OP_CREATE         4
#define FSFILT_OP_MKDIR          5
#define FSFILT_OP_SYMLINK        6
#define FSFILT_OP_MKNOD          7
#define FSFILT_OP_SETATTR        8
#define FSFILT_OP_LINK           9
#define FSFILT_OP_CANCEL_UNLINK 10
#define FSFILT_OP_JOIN          11
#define FSFILT_OP_NOOP          15

#define fsfilt_check_slow(start, timeout, msg)                          \
do {                                                                    \
        if (time_before(jiffies, start + 15 * HZ))                      \
                break;                                                  \
        else if (time_before(jiffies, start + timeout / 2 * HZ))        \
                CWARN("slow %s %lus\n", msg, (jiffies - start) / HZ);   \
        else                                                            \
                CERROR("slow %s %lus\n", msg, (jiffies - start) / HZ);  \
} while (0)

static inline void *fsfilt_start_log(struct obd_device *obd,
                                     struct inode *inode, int op,
                                     struct obd_trans_info *oti, int logs)
{
        unsigned long now = jiffies;
        void *parent_handle = oti ? oti->oti_handle : NULL;
        void *handle;

        if (obd->obd_fail)
                return ERR_PTR(-EROFS);

        handle = obd->obd_fsops->fs_start(inode, op, parent_handle, logs);
        CDEBUG(D_INFO, "started handle %p (%p)\n", handle, parent_handle);

        if (oti != NULL) {
                if (parent_handle == NULL) {
                        oti->oti_handle = handle;
                } else if (handle != parent_handle) {
                        CERROR("mismatch: parent %p, handle %p, oti %p\n",
                               parent_handle, handle, oti);
                        LBUG();
                }
        }
        fsfilt_check_slow(now, obd_timeout, "journal start");
        return handle;
}

static inline void *fsfilt_start(struct obd_device *obd, struct inode *inode,
                                 int op, struct obd_trans_info *oti)
{
        return fsfilt_start_log(obd, inode, op, oti, 0);
}

static inline void *fsfilt_brw_start_log(struct obd_device *obd, int objcount,
                                         struct fsfilt_objinfo *fso,
                                         int niocount, struct niobuf_local *nb,
                                         struct obd_trans_info *oti, int logs)
{
        unsigned long now = jiffies;
        void *parent_handle = oti ? oti->oti_handle : NULL;
        void *handle;

        if (obd->obd_fail)
                return ERR_PTR(-EROFS);

        handle = obd->obd_fsops->fs_brw_start(objcount, fso, niocount, nb,
                                              parent_handle, logs);
        CDEBUG(D_INFO, "started handle %p (%p)\n", handle, parent_handle);

        if (oti != NULL) {
                if (parent_handle == NULL) {
                        oti->oti_handle = handle;
                } else if (handle != parent_handle) {
                        CERROR("mismatch: parent %p, handle %p, oti %p\n",
                               parent_handle, handle, oti);
                        LBUG();
                }
        }
        fsfilt_check_slow(now, obd_timeout, "journal start");

        return handle;
}

static inline void *fsfilt_brw_start(struct obd_device *obd, int objcount,
                                     struct fsfilt_objinfo *fso, int niocount,
                                     struct niobuf_local *nb,
                                     struct obd_trans_info *oti)
{
        return fsfilt_brw_start_log(obd, objcount, fso, niocount, nb, oti, 0);
}

static inline int fsfilt_commit(struct obd_device *obd, struct inode *inode,
                                void *handle, int force_sync)
{
        unsigned long now = jiffies;
        int rc = obd->obd_fsops->fs_commit(inode, handle, force_sync);
        CDEBUG(D_INFO, "committing handle %p\n", handle);

        fsfilt_check_slow(now, obd_timeout, "journal start");

        return rc;
}

static inline int fsfilt_commit_async(struct obd_device *obd,
                                      struct inode *inode, void *handle,
                                      void **wait_handle)
{
        unsigned long now = jiffies;
        int rc = obd->obd_fsops->fs_commit_async(inode, handle, wait_handle);

        CDEBUG(D_INFO, "committing handle %p (async)\n", *wait_handle);
        fsfilt_check_slow(now, obd_timeout, "journal start");

        return rc;
}

static inline int fsfilt_commit_wait(struct obd_device *obd,
                                     struct inode *inode, void *handle)
{
        unsigned long now = jiffies;
        int rc = obd->obd_fsops->fs_commit_wait(inode, handle);
        CDEBUG(D_INFO, "waiting for completion %p\n", handle);
        fsfilt_check_slow(now, obd_timeout, "journal start");
        return rc;
}

static inline int fsfilt_setattr(struct obd_device *obd, struct dentry *dentry,
                                 void *handle, struct iattr *iattr,int do_trunc)
{
        unsigned long now = jiffies;
        int rc;
        rc = obd->obd_fsops->fs_setattr(dentry, handle, iattr, do_trunc);
        fsfilt_check_slow(now, obd_timeout, "setattr");
        return rc;
}

static inline int fsfilt_iocontrol(struct obd_device *obd, struct inode *inode,
                                   struct file *file, unsigned int cmd,
                                   unsigned long arg)
{
        return obd->obd_fsops->fs_iocontrol(inode, file, cmd, arg);
}

static inline int fsfilt_set_md(struct obd_device *obd, struct inode *inode,
                                void *handle, void *md, int size,
                                const char *name)
{
        return obd->obd_fsops->fs_set_md(inode, handle, md, size, name);
}

static inline int fsfilt_get_md(struct obd_device *obd, struct inode *inode,
                                void *md, int size, const char *name)
{
        return obd->obd_fsops->fs_get_md(inode, md, size, name);
}

static inline int fsfilt_send_bio(int rw, struct obd_device *obd,
                                  struct inode *inode, void *bio)
{
        LASSERTF(rw == OBD_BRW_WRITE || rw == OBD_BRW_READ, "%x\n", rw);

        if (rw == OBD_BRW_READ)
                return obd->obd_fsops->fs_send_bio(READ, inode, bio);
        return obd->obd_fsops->fs_send_bio(WRITE, inode, bio);
}

static inline ssize_t fsfilt_readpage(struct obd_device *obd,
                                      struct file *file, char *buf,
                                      size_t count, loff_t *offset)
{
        return obd->obd_fsops->fs_readpage(file, buf, count, offset);
}

static inline int fsfilt_add_journal_cb(struct obd_device *obd, __u64 last_rcvd,
                                        void *handle, fsfilt_cb_t cb_func,
                                        void *cb_data)
{
        return obd->obd_fsops->fs_add_journal_cb(obd, last_rcvd,
                                                 handle, cb_func, cb_data);
}

/* very similar to obd_statfs(), but caller already holds obd_osfs_lock */
static inline int fsfilt_statfs(struct obd_device *obd, struct super_block *sb,
                                unsigned long max_age)
{
        int rc = 0;

        CDEBUG(D_SUPER, "osfs %lu, max_age %lu\n", obd->obd_osfs_age, max_age);
        if (time_before(obd->obd_osfs_age, max_age)) {
                rc = obd->obd_fsops->fs_statfs(sb, &obd->obd_osfs);
                if (rc == 0) /* N.B. statfs can't really fail */
                        obd->obd_osfs_age = jiffies;
        } else {
                CDEBUG(D_SUPER, "using cached obd_statfs data\n");
        }

        return rc;
}

static inline int fsfilt_sync(struct obd_device *obd, struct super_block *sb)
{
        return obd->obd_fsops->fs_sync(sb);
}

static inline int fsfilt_quotacheck(struct obd_device *obd,
                                    struct super_block *sb,
                                    struct obd_quotactl *oqctl)
{
        if (obd->obd_fsops->fs_quotacheck)
                return obd->obd_fsops->fs_quotacheck(sb, oqctl);
        return -ENOTSUPP;
}

static inline int fsfilt_quotactl(struct obd_device *obd,
                                  struct super_block *sb,
                                  struct obd_quotactl *oqctl)
{
        if (obd->obd_fsops->fs_quotactl)
                return obd->obd_fsops->fs_quotactl(sb, oqctl);
        return -ENOTSUPP;
}

static inline int fsfilt_quotainfo(struct obd_device *obd,
                                   struct lustre_quota_info *lqi,
                                   int type, int cmd)
{
        if (obd->obd_fsops->fs_quotainfo)
                return obd->obd_fsops->fs_quotainfo(lqi, type, cmd);
        return -ENOTSUPP;
}

static inline int fsfilt_qids(struct obd_device *obd, struct file *file,
                              struct inode *inode, int type, 
                              struct list_head *list)
{
        if (obd->obd_fsops->fs_qids)
                return obd->obd_fsops->fs_qids(file, inode, type, list);
        return -ENOTSUPP;
}

static inline int fsfilt_dquot(struct obd_device *obd,
                               struct lustre_dquot *dquot, int cmd)
{
        if (obd->obd_fsops->fs_dquot)
                return obd->obd_fsops->fs_dquot(dquot, cmd);
        return -ENOTSUPP;
}

static inline int fsfilt_map_inode_pages(struct obd_device *obd,
                                         struct inode *inode,
                                         struct page **page, int pages,
                                         unsigned long *blocks, int *created,
                                         int create, struct semaphore *sem)
{
        return obd->obd_fsops->fs_map_inode_pages(inode, page, pages, blocks,
                                                  created, create, sem);
}

static inline int fs_prep_san_write(struct obd_device *obd, struct inode *inode,
                                    long *blocks, int nblocks, loff_t newsize)
{
        return obd->obd_fsops->fs_prep_san_write(inode, blocks,
                                                 nblocks, newsize);
}

static inline int fsfilt_read_record(struct obd_device *obd, struct file *file,
                                     void *buf, loff_t size, loff_t *offs)
{
        return obd->obd_fsops->fs_read_record(file, buf, size, offs);
}

static inline int fsfilt_write_record(struct obd_device *obd, struct file *file,
                                      void *buf, loff_t size, loff_t *offs,
                                      int force_sync)
{
        return obd->obd_fsops->fs_write_record(file, buf, size,offs,force_sync);
}

static inline int fsfilt_setup(struct obd_device *obd, struct super_block *fs)
{
        if (obd->obd_fsops->fs_setup)
                return obd->obd_fsops->fs_setup(fs);
        return 0;
}

#endif /* __KERNEL__ */

#endif
