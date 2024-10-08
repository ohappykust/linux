// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2008, Christoph Hellwig
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_quota.h"
#include "xfs_trans.h"
#include "xfs_icache.h"
#include "xfs_qm.h"


static int
xfs_qm_fill_state(
	struct qc_type_state	*tstate,
	struct xfs_mount	*mp,
	xfs_dqtype_t		type)
{
	struct xfs_inode	*ip;
	struct xfs_def_quota	*defq;
	int			error;

	error = xfs_qm_qino_load(mp, type, &ip);
	if (error) {
		tstate->ino = NULLFSINO;
		return error != -ENOENT ? error : 0;
	}

	defq = xfs_get_defquota(mp->m_quotainfo, type);

	tstate->ino = ip->i_ino;
	tstate->flags |= QCI_SYSFILE;
	tstate->blocks = ip->i_nblocks;
	tstate->nextents = ip->i_df.if_nextents;
	tstate->spc_timelimit = (u32)defq->blk.time;
	tstate->ino_timelimit = (u32)defq->ino.time;
	tstate->rt_spc_timelimit = (u32)defq->rtb.time;
	tstate->spc_warnlimit = 0;
	tstate->ino_warnlimit = 0;
	tstate->rt_spc_warnlimit = 0;
	xfs_irele(ip);

	return 0;
}

/*
 * Return quota status information, such as enforcements, quota file inode
 * numbers etc.
 */
static int
xfs_fs_get_quota_state(
	struct super_block	*sb,
	struct qc_state		*state)
{
	struct xfs_mount	*mp = XFS_M(sb);
	struct xfs_quotainfo	*q = mp->m_quotainfo;
	int			error;

	memset(state, 0, sizeof(*state));
	if (!XFS_IS_QUOTA_ON(mp))
		return 0;
	state->s_incoredqs = q->qi_dquots;
	if (XFS_IS_UQUOTA_ON(mp))
		state->s_state[USRQUOTA].flags |= QCI_ACCT_ENABLED;
	if (XFS_IS_UQUOTA_ENFORCED(mp))
		state->s_state[USRQUOTA].flags |= QCI_LIMITS_ENFORCED;
	if (XFS_IS_GQUOTA_ON(mp))
		state->s_state[GRPQUOTA].flags |= QCI_ACCT_ENABLED;
	if (XFS_IS_GQUOTA_ENFORCED(mp))
		state->s_state[GRPQUOTA].flags |= QCI_LIMITS_ENFORCED;
	if (XFS_IS_PQUOTA_ON(mp))
		state->s_state[PRJQUOTA].flags |= QCI_ACCT_ENABLED;
	if (XFS_IS_PQUOTA_ENFORCED(mp))
		state->s_state[PRJQUOTA].flags |= QCI_LIMITS_ENFORCED;

	error = xfs_qm_fill_state(&state->s_state[USRQUOTA], mp,
			XFS_DQTYPE_USER);
	if (error)
		return error;
	error = xfs_qm_fill_state(&state->s_state[GRPQUOTA], mp,
			XFS_DQTYPE_GROUP);
	if (error)
		return error;
	error = xfs_qm_fill_state(&state->s_state[PRJQUOTA], mp,
			XFS_DQTYPE_PROJ);
	if (error)
		return error;
	return 0;
}

STATIC xfs_dqtype_t
xfs_quota_type(int type)
{
	switch (type) {
	case USRQUOTA:
		return XFS_DQTYPE_USER;
	case GRPQUOTA:
		return XFS_DQTYPE_GROUP;
	default:
		return XFS_DQTYPE_PROJ;
	}
}

#define XFS_QC_SETINFO_MASK (QC_TIMER_MASK)

/*
 * Adjust quota timers & warnings
 */
static int
xfs_fs_set_info(
	struct super_block	*sb,
	int			type,
	struct qc_info		*info)
{
	struct xfs_mount	*mp = XFS_M(sb);
	struct qc_dqblk		newlim;

	if (sb_rdonly(sb))
		return -EROFS;
	if (!XFS_IS_QUOTA_ON(mp))
		return -ENOSYS;
	if (info->i_fieldmask & ~XFS_QC_SETINFO_MASK)
		return -EINVAL;
	if ((info->i_fieldmask & XFS_QC_SETINFO_MASK) == 0)
		return 0;

	newlim.d_fieldmask = info->i_fieldmask;
	newlim.d_spc_timer = info->i_spc_timelimit;
	newlim.d_ino_timer = info->i_ino_timelimit;
	newlim.d_rt_spc_timer = info->i_rt_spc_timelimit;
	newlim.d_ino_warns = info->i_ino_warnlimit;
	newlim.d_spc_warns = info->i_spc_warnlimit;
	newlim.d_rt_spc_warns = info->i_rt_spc_warnlimit;

	return xfs_qm_scall_setqlim(mp, 0, xfs_quota_type(type), &newlim);
}

static unsigned int
xfs_quota_flags(unsigned int uflags)
{
	unsigned int flags = 0;

	if (uflags & FS_QUOTA_UDQ_ACCT)
		flags |= XFS_UQUOTA_ACCT;
	if (uflags & FS_QUOTA_PDQ_ACCT)
		flags |= XFS_PQUOTA_ACCT;
	if (uflags & FS_QUOTA_GDQ_ACCT)
		flags |= XFS_GQUOTA_ACCT;
	if (uflags & FS_QUOTA_UDQ_ENFD)
		flags |= XFS_UQUOTA_ENFD;
	if (uflags & FS_QUOTA_GDQ_ENFD)
		flags |= XFS_GQUOTA_ENFD;
	if (uflags & FS_QUOTA_PDQ_ENFD)
		flags |= XFS_PQUOTA_ENFD;

	return flags;
}

STATIC int
xfs_quota_enable(
	struct super_block	*sb,
	unsigned int		uflags)
{
	struct xfs_mount	*mp = XFS_M(sb);

	if (sb_rdonly(sb))
		return -EROFS;
	if (!XFS_IS_QUOTA_ON(mp))
		return -ENOSYS;

	return xfs_qm_scall_quotaon(mp, xfs_quota_flags(uflags));
}

STATIC int
xfs_quota_disable(
	struct super_block	*sb,
	unsigned int		uflags)
{
	struct xfs_mount	*mp = XFS_M(sb);

	if (sb_rdonly(sb))
		return -EROFS;
	if (!XFS_IS_QUOTA_ON(mp))
		return -ENOSYS;

	return xfs_qm_scall_quotaoff(mp, xfs_quota_flags(uflags));
}

STATIC int
xfs_fs_rm_xquota(
	struct super_block	*sb,
	unsigned int		uflags)
{
	struct xfs_mount	*mp = XFS_M(sb);
	unsigned int		flags = 0;

	if (sb_rdonly(sb))
		return -EROFS;

	if (XFS_IS_QUOTA_ON(mp))
		return -EINVAL;

	if (uflags & ~(FS_USER_QUOTA | FS_GROUP_QUOTA | FS_PROJ_QUOTA))
		return -EINVAL;

	if (uflags & FS_USER_QUOTA)
		flags |= XFS_QMOPT_UQUOTA;
	if (uflags & FS_GROUP_QUOTA)
		flags |= XFS_QMOPT_GQUOTA;
	if (uflags & FS_PROJ_QUOTA)
		flags |= XFS_QMOPT_PQUOTA;

	return xfs_qm_scall_trunc_qfiles(mp, flags);
}

STATIC int
xfs_fs_get_dqblk(
	struct super_block	*sb,
	struct kqid		qid,
	struct qc_dqblk		*qdq)
{
	struct xfs_mount	*mp = XFS_M(sb);
	xfs_dqid_t		id;

	if (!XFS_IS_QUOTA_ON(mp))
		return -ENOSYS;

	id = from_kqid(&init_user_ns, qid);
	return xfs_qm_scall_getquota(mp, id, xfs_quota_type(qid.type), qdq);
}

/* Return quota info for active quota >= this qid */
STATIC int
xfs_fs_get_nextdqblk(
	struct super_block	*sb,
	struct kqid		*qid,
	struct qc_dqblk		*qdq)
{
	int			ret;
	struct xfs_mount	*mp = XFS_M(sb);
	xfs_dqid_t		id;

	if (!XFS_IS_QUOTA_ON(mp))
		return -ENOSYS;

	id = from_kqid(&init_user_ns, *qid);
	ret = xfs_qm_scall_getquota_next(mp, &id, xfs_quota_type(qid->type),
			qdq);
	if (ret)
		return ret;

	/* ID may be different, so convert back what we got */
	*qid = make_kqid(current_user_ns(), qid->type, id);
	return 0;
}

STATIC int
xfs_fs_set_dqblk(
	struct super_block	*sb,
	struct kqid		qid,
	struct qc_dqblk		*qdq)
{
	struct xfs_mount	*mp = XFS_M(sb);

	if (sb_rdonly(sb))
		return -EROFS;
	if (!XFS_IS_QUOTA_ON(mp))
		return -ENOSYS;

	return xfs_qm_scall_setqlim(mp, from_kqid(&init_user_ns, qid),
				     xfs_quota_type(qid.type), qdq);
}

const struct quotactl_ops xfs_quotactl_operations = {
	.get_state		= xfs_fs_get_quota_state,
	.set_info		= xfs_fs_set_info,
	.quota_enable		= xfs_quota_enable,
	.quota_disable		= xfs_quota_disable,
	.rm_xquota		= xfs_fs_rm_xquota,
	.get_dqblk		= xfs_fs_get_dqblk,
	.get_nextdqblk		= xfs_fs_get_nextdqblk,
	.set_dqblk		= xfs_fs_set_dqblk,
};
