/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rtk_sb2_sem.h - Realtek SB2 HW semaphore API (bring-up stub)
 *
 * The real SB2 hardware semaphore coordinates register access between the
 * SCPU and the audio/video co-processors. During mainline bring-up those
 * co-processors are not running, so no cross-CPU locking is required: provide
 * no-op stubs and never hand out a lock (of_sb2_sem_get() returns an error,
 * leaving consumers' lock pointers NULL so all locking is skipped).
 *
 * Copyright (C) 2017 Realtek Semiconductor Corporation
 */

#ifndef __SOC_REALTEK_SB2_SEM_H
#define __SOC_REALTEK_SB2_SEM_H

#include <linux/err.h>
#include <linux/of.h>

struct sb2_sem;

#define SB2_SEM_NO_WARNING		0x2
#define SB2_SEM_TIMEOUT_INFINITY	0x4

static inline struct sb2_sem *sb2_sem_get(unsigned int index)
{
	return ERR_PTR(-ENODEV);
}

static inline int sb2_sem_try_lock(struct sb2_sem *sem, unsigned int flags)
{
	return 0;
}

static inline void sb2_sem_lock(struct sb2_sem *sem, unsigned int flags) { }

static inline void sb2_sem_unlock(struct sb2_sem *sem) { }

static inline struct sb2_sem *sb2_sem_node_to_lock(struct device_node *np)
{
	return ERR_PTR(-ENODEV);
}

static inline struct sb2_sem *of_sb2_sem_get(const struct device_node *np,
					     int index)
{
	return ERR_PTR(-ENODEV);
}

#endif /* __SOC_REALTEK_SB2_SEM_H */
