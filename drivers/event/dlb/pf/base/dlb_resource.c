/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2016-2020 Intel Corporation
 */

#include "dlb_hw_types.h"
#include "../../dlb_user.h"
#include "dlb_resource.h"
#include "dlb_osdep.h"
#include "dlb_osdep_bitmap.h"
#include "dlb_osdep_types.h"
#include "dlb_regs.h"
#include "../../dlb_priv.h"
#include "../../dlb_inline_fns.h"

#define DLB_DOM_LIST_HEAD(head, type) \
	DLB_LIST_HEAD((head), type, domain_list)

#define DLB_FUNC_LIST_HEAD(head, type) \
	DLB_LIST_HEAD((head), type, func_list)

#define DLB_DOM_LIST_FOR(head, ptr, iter) \
	DLB_LIST_FOR_EACH(head, ptr, domain_list, iter)

#define DLB_FUNC_LIST_FOR(head, ptr, iter) \
	DLB_LIST_FOR_EACH(head, ptr, func_list, iter)

#define DLB_DOM_LIST_FOR_SAFE(head, ptr, ptr_tmp, it, it_tmp) \
	DLB_LIST_FOR_EACH_SAFE((head), ptr, ptr_tmp, domain_list, it, it_tmp)

#define DLB_FUNC_LIST_FOR_SAFE(head, ptr, ptr_tmp, it, it_tmp) \
	DLB_LIST_FOR_EACH_SAFE((head), ptr, ptr_tmp, func_list, it, it_tmp)

static inline void dlb_flush_csr(struct dlb_hw *hw)
{
	DLB_CSR_RD(hw, DLB_SYS_TOTAL_VAS);
}

static void dlb_init_fn_rsrc_lists(struct dlb_function_resources *rsrc)
{
	dlb_list_init_head(&rsrc->avail_domains);
	dlb_list_init_head(&rsrc->used_domains);
	dlb_list_init_head(&rsrc->avail_ldb_queues);
	dlb_list_init_head(&rsrc->avail_ldb_ports);
	dlb_list_init_head(&rsrc->avail_dir_pq_pairs);
	dlb_list_init_head(&rsrc->avail_ldb_credit_pools);
	dlb_list_init_head(&rsrc->avail_dir_credit_pools);
}

static void dlb_init_domain_rsrc_lists(struct dlb_domain *domain)
{
	dlb_list_init_head(&domain->used_ldb_queues);
	dlb_list_init_head(&domain->used_ldb_ports);
	dlb_list_init_head(&domain->used_dir_pq_pairs);
	dlb_list_init_head(&domain->used_ldb_credit_pools);
	dlb_list_init_head(&domain->used_dir_credit_pools);
	dlb_list_init_head(&domain->avail_ldb_queues);
	dlb_list_init_head(&domain->avail_ldb_ports);
	dlb_list_init_head(&domain->avail_dir_pq_pairs);
	dlb_list_init_head(&domain->avail_ldb_credit_pools);
	dlb_list_init_head(&domain->avail_dir_credit_pools);
}

int dlb_resource_init(struct dlb_hw *hw)
{
	struct dlb_list_entry *list;
	unsigned int i;

	/* For optimal load-balancing, ports that map to one or more QIDs in
	 * common should not be in numerical sequence. This is application
	 * dependent, but the driver interleaves port IDs as much as possible
	 * to reduce the likelihood of this. This initial allocation maximizes
	 * the average distance between an ID and its immediate neighbors (i.e.
	 * the distance from 1 to 0 and to 2, the distance from 2 to 1 and to
	 * 3, etc.).
	 */
	u32 init_ldb_port_allocation[DLB_MAX_NUM_LDB_PORTS] = {
		0,  31, 62, 29, 60, 27, 58, 25, 56, 23, 54, 21, 52, 19, 50, 17,
		48, 15, 46, 13, 44, 11, 42,  9, 40,  7, 38,  5, 36,  3, 34, 1,
		32, 63, 30, 61, 28, 59, 26, 57, 24, 55, 22, 53, 20, 51, 18, 49,
		16, 47, 14, 45, 12, 43, 10, 41,  8, 39,  6, 37,  4, 35,  2, 33
	};

	/* Zero-out resource tracking data structures */
	memset(&hw->rsrcs, 0, sizeof(hw->rsrcs));
	memset(&hw->pf, 0, sizeof(hw->pf));

	dlb_init_fn_rsrc_lists(&hw->pf);

	for (i = 0; i < DLB_MAX_NUM_DOMAINS; i++) {
		memset(&hw->domains[i], 0, sizeof(hw->domains[i]));
		dlb_init_domain_rsrc_lists(&hw->domains[i]);
		hw->domains[i].parent_func = &hw->pf;
	}

	/* Give all resources to the PF driver */
	hw->pf.num_avail_domains = DLB_MAX_NUM_DOMAINS;
	for (i = 0; i < hw->pf.num_avail_domains; i++) {
		list = &hw->domains[i].func_list;

		dlb_list_add(&hw->pf.avail_domains, list);
	}

	hw->pf.num_avail_ldb_queues = DLB_MAX_NUM_LDB_QUEUES;
	for (i = 0; i < hw->pf.num_avail_ldb_queues; i++) {
		list = &hw->rsrcs.ldb_queues[i].func_list;

		dlb_list_add(&hw->pf.avail_ldb_queues, list);
	}

	hw->pf.num_avail_ldb_ports = DLB_MAX_NUM_LDB_PORTS;
	for (i = 0; i < hw->pf.num_avail_ldb_ports; i++) {
		struct dlb_ldb_port *port;

		port = &hw->rsrcs.ldb_ports[init_ldb_port_allocation[i]];

		dlb_list_add(&hw->pf.avail_ldb_ports, &port->func_list);
	}

	hw->pf.num_avail_dir_pq_pairs = DLB_MAX_NUM_DIR_PORTS;
	for (i = 0; i < hw->pf.num_avail_dir_pq_pairs; i++) {
		list = &hw->rsrcs.dir_pq_pairs[i].func_list;

		dlb_list_add(&hw->pf.avail_dir_pq_pairs, list);
	}

	hw->pf.num_avail_ldb_credit_pools = DLB_MAX_NUM_LDB_CREDIT_POOLS;
	for (i = 0; i < hw->pf.num_avail_ldb_credit_pools; i++) {
		list = &hw->rsrcs.ldb_credit_pools[i].func_list;

		dlb_list_add(&hw->pf.avail_ldb_credit_pools, list);
	}

	hw->pf.num_avail_dir_credit_pools = DLB_MAX_NUM_DIR_CREDIT_POOLS;
	for (i = 0; i < hw->pf.num_avail_dir_credit_pools; i++) {
		list = &hw->rsrcs.dir_credit_pools[i].func_list;

		dlb_list_add(&hw->pf.avail_dir_credit_pools, list);
	}

	/* There are 5120 history list entries, which allows us to overprovision
	 * the inflight limit (4096) by 1k.
	 */
	if (dlb_bitmap_alloc(hw,
			     &hw->pf.avail_hist_list_entries,
			     DLB_MAX_NUM_HIST_LIST_ENTRIES))
		return -1;

	if (dlb_bitmap_fill(hw->pf.avail_hist_list_entries))
		return -1;

	if (dlb_bitmap_alloc(hw,
			     &hw->pf.avail_qed_freelist_entries,
			     DLB_MAX_NUM_LDB_CREDITS))
		return -1;

	if (dlb_bitmap_fill(hw->pf.avail_qed_freelist_entries))
		return -1;

	if (dlb_bitmap_alloc(hw,
			     &hw->pf.avail_dqed_freelist_entries,
			     DLB_MAX_NUM_DIR_CREDITS))
		return -1;

	if (dlb_bitmap_fill(hw->pf.avail_dqed_freelist_entries))
		return -1;

	if (dlb_bitmap_alloc(hw,
			     &hw->pf.avail_aqed_freelist_entries,
			     DLB_MAX_NUM_AQOS_ENTRIES))
		return -1;

	if (dlb_bitmap_fill(hw->pf.avail_aqed_freelist_entries))
		return -1;

	/* Initialize the hardware resource IDs */
	for (i = 0; i < DLB_MAX_NUM_DOMAINS; i++)
		hw->domains[i].id = i;

	for (i = 0; i < DLB_MAX_NUM_LDB_QUEUES; i++)
		hw->rsrcs.ldb_queues[i].id = i;

	for (i = 0; i < DLB_MAX_NUM_LDB_PORTS; i++)
		hw->rsrcs.ldb_ports[i].id = i;

	for (i = 0; i < DLB_MAX_NUM_DIR_PORTS; i++)
		hw->rsrcs.dir_pq_pairs[i].id = i;

	for (i = 0; i < DLB_MAX_NUM_LDB_CREDIT_POOLS; i++)
		hw->rsrcs.ldb_credit_pools[i].id = i;

	for (i = 0; i < DLB_MAX_NUM_DIR_CREDIT_POOLS; i++)
		hw->rsrcs.dir_credit_pools[i].id = i;

	for (i = 0; i < DLB_MAX_NUM_SEQUENCE_NUMBER_GROUPS; i++) {
		hw->rsrcs.sn_groups[i].id = i;
		/* Default mode (0) is 32 sequence numbers per queue */
		hw->rsrcs.sn_groups[i].mode = 0;
		hw->rsrcs.sn_groups[i].sequence_numbers_per_queue = 32;
		hw->rsrcs.sn_groups[i].slot_use_bitmap = 0;
	}

	return 0;
}

void dlb_resource_free(struct dlb_hw *hw)
{
	dlb_bitmap_free(hw->pf.avail_hist_list_entries);

	dlb_bitmap_free(hw->pf.avail_qed_freelist_entries);

	dlb_bitmap_free(hw->pf.avail_dqed_freelist_entries);

	dlb_bitmap_free(hw->pf.avail_aqed_freelist_entries);
}

static struct dlb_domain *dlb_get_domain_from_id(struct dlb_hw *hw, u32 id)
{
	if (id >= DLB_MAX_NUM_DOMAINS)
		return NULL;

	return &hw->domains[id];
}

static int dlb_attach_ldb_queues(struct dlb_hw *hw,
				 struct dlb_function_resources *rsrcs,
				 struct dlb_domain *domain,
				 u32 num_queues,
				 struct dlb_cmd_response *resp)
{
	unsigned int i, j;

	if (rsrcs->num_avail_ldb_queues < num_queues) {
		resp->status = DLB_ST_LDB_QUEUES_UNAVAILABLE;
		return -1;
	}

	for (i = 0; i < num_queues; i++) {
		struct dlb_ldb_queue *queue;

		queue = DLB_FUNC_LIST_HEAD(rsrcs->avail_ldb_queues,
					   typeof(*queue));
		if (queue == NULL) {
			DLB_HW_ERR(hw,
				   "[%s()] Internal error: domain validation failed\n",
				   __func__);
			goto cleanup;
		}

		dlb_list_del(&rsrcs->avail_ldb_queues, &queue->func_list);

		queue->domain_id = domain->id;
		queue->owned = true;

		dlb_list_add(&domain->avail_ldb_queues, &queue->domain_list);
	}

	rsrcs->num_avail_ldb_queues -= num_queues;

	return 0;

cleanup:

	/* Return the assigned queues */
	for (j = 0; j < i; j++) {
		struct dlb_ldb_queue *queue;

		queue = DLB_FUNC_LIST_HEAD(domain->avail_ldb_queues,
					   typeof(*queue));
		/* Unrecoverable internal error */
		if (queue == NULL)
			break;

		queue->owned = false;

		dlb_list_del(&domain->avail_ldb_queues, &queue->domain_list);

		dlb_list_add(&rsrcs->avail_ldb_queues, &queue->func_list);
	}

	return -EFAULT;
}

static struct dlb_ldb_port *
dlb_get_next_ldb_port(struct dlb_hw *hw,
		      struct dlb_function_resources *rsrcs,
		      u32 domain_id)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_ldb_port *port;

	/* To reduce the odds of consecutive load-balanced ports mapping to the
	 * same queue(s), the driver attempts to allocate ports whose neighbors
	 * are owned by a different domain.
	 */
	DLB_FUNC_LIST_FOR(rsrcs->avail_ldb_ports, port, iter) {
		u32 next, prev;
		u32 phys_id;

		phys_id = port->id;
		next = phys_id + 1;
		prev = phys_id - 1;

		if (phys_id == DLB_MAX_NUM_LDB_PORTS - 1)
			next = 0;
		if (phys_id == 0)
			prev = DLB_MAX_NUM_LDB_PORTS - 1;

		if (!hw->rsrcs.ldb_ports[next].owned ||
		    hw->rsrcs.ldb_ports[next].domain_id == domain_id)
			continue;

		if (!hw->rsrcs.ldb_ports[prev].owned ||
		    hw->rsrcs.ldb_ports[prev].domain_id == domain_id)
			continue;

		return port;
	}

	/* Failing that, the driver looks for a port with one neighbor owned by
	 * a different domain and the other unallocated.
	 */
	DLB_FUNC_LIST_FOR(rsrcs->avail_ldb_ports, port, iter) {
		u32 next, prev;
		u32 phys_id;

		phys_id = port->id;
		next = phys_id + 1;
		prev = phys_id - 1;

		if (phys_id == DLB_MAX_NUM_LDB_PORTS - 1)
			next = 0;
		if (phys_id == 0)
			prev = DLB_MAX_NUM_LDB_PORTS - 1;

		if (!hw->rsrcs.ldb_ports[prev].owned &&
		    hw->rsrcs.ldb_ports[next].owned &&
		    hw->rsrcs.ldb_ports[next].domain_id != domain_id)
			return port;

		if (!hw->rsrcs.ldb_ports[next].owned &&
		    hw->rsrcs.ldb_ports[prev].owned &&
		    hw->rsrcs.ldb_ports[prev].domain_id != domain_id)
			return port;
	}

	/* Failing that, the driver looks for a port with both neighbors
	 * unallocated.
	 */
	DLB_FUNC_LIST_FOR(rsrcs->avail_ldb_ports, port, iter) {
		u32 next, prev;
		u32 phys_id;

		phys_id = port->id;
		next = phys_id + 1;
		prev = phys_id - 1;

		if (phys_id == DLB_MAX_NUM_LDB_PORTS - 1)
			next = 0;
		if (phys_id == 0)
			prev = DLB_MAX_NUM_LDB_PORTS - 1;

		if (!hw->rsrcs.ldb_ports[prev].owned &&
		    !hw->rsrcs.ldb_ports[next].owned)
			return port;
	}

	/* If all else fails, the driver returns the next available port. */
	return DLB_FUNC_LIST_HEAD(rsrcs->avail_ldb_ports, typeof(*port));
}

static int dlb_attach_ldb_ports(struct dlb_hw *hw,
				struct dlb_function_resources *rsrcs,
				struct dlb_domain *domain,
				u32 num_ports,
				struct dlb_cmd_response *resp)
{
	unsigned int i, j;

	if (rsrcs->num_avail_ldb_ports < num_ports) {
		resp->status = DLB_ST_LDB_PORTS_UNAVAILABLE;
		return -1;
	}

	for (i = 0; i < num_ports; i++) {
		struct dlb_ldb_port *port;

		port = dlb_get_next_ldb_port(hw, rsrcs, domain->id);

		if (port == NULL) {
			DLB_HW_ERR(hw,
				   "[%s()] Internal error: domain validation failed\n",
				   __func__);
			goto cleanup;
		}

		dlb_list_del(&rsrcs->avail_ldb_ports, &port->func_list);

		port->domain_id = domain->id;
		port->owned = true;

		dlb_list_add(&domain->avail_ldb_ports, &port->domain_list);
	}

	rsrcs->num_avail_ldb_ports -= num_ports;

	return 0;

cleanup:

	/* Return the assigned ports */
	for (j = 0; j < i; j++) {
		struct dlb_ldb_port *port;

		port = DLB_FUNC_LIST_HEAD(domain->avail_ldb_ports,
					  typeof(*port));
		/* Unrecoverable internal error */
		if (port == NULL)
			break;

		port->owned = false;

		dlb_list_del(&domain->avail_ldb_ports, &port->domain_list);

		dlb_list_add(&rsrcs->avail_ldb_ports, &port->func_list);
	}

	return -EFAULT;
}

static int dlb_attach_dir_ports(struct dlb_hw *hw,
				struct dlb_function_resources *rsrcs,
				struct dlb_domain *domain,
				u32 num_ports,
				struct dlb_cmd_response *resp)
{
	unsigned int i, j;

	if (rsrcs->num_avail_dir_pq_pairs < num_ports) {
		resp->status = DLB_ST_DIR_PORTS_UNAVAILABLE;
		return -1;
	}

	for (i = 0; i < num_ports; i++) {
		struct dlb_dir_pq_pair *port;

		port = DLB_FUNC_LIST_HEAD(rsrcs->avail_dir_pq_pairs,
					  typeof(*port));
		if (port == NULL) {
			DLB_HW_ERR(hw,
				   "[%s()] Internal error: domain validation failed\n",
				   __func__);
			goto cleanup;
		}

		dlb_list_del(&rsrcs->avail_dir_pq_pairs, &port->func_list);

		port->domain_id = domain->id;
		port->owned = true;

		dlb_list_add(&domain->avail_dir_pq_pairs, &port->domain_list);
	}

	rsrcs->num_avail_dir_pq_pairs -= num_ports;

	return 0;

cleanup:

	/* Return the assigned ports */
	for (j = 0; j < i; j++) {
		struct dlb_dir_pq_pair *port;

		port = DLB_FUNC_LIST_HEAD(domain->avail_dir_pq_pairs,
					  typeof(*port));
		/* Unrecoverable internal error */
		if (port == NULL)
			break;

		port->owned = false;

		dlb_list_del(&domain->avail_dir_pq_pairs, &port->domain_list);

		dlb_list_add(&rsrcs->avail_dir_pq_pairs, &port->func_list);
	}

	return -EFAULT;
}

static int dlb_attach_ldb_credits(struct dlb_function_resources *rsrcs,
				  struct dlb_domain *domain,
				  u32 num_credits,
				  struct dlb_cmd_response *resp)
{
	struct dlb_bitmap *bitmap = rsrcs->avail_qed_freelist_entries;

	if (dlb_bitmap_count(bitmap) < (int)num_credits) {
		resp->status = DLB_ST_LDB_CREDITS_UNAVAILABLE;
		return -1;
	}

	if (num_credits) {
		int base;

		base = dlb_bitmap_find_set_bit_range(bitmap, num_credits);
		if (base < 0)
			goto error;

		domain->qed_freelist.base = base;
		domain->qed_freelist.bound = base + num_credits;
		domain->qed_freelist.offset = 0;

		dlb_bitmap_clear_range(bitmap, base, num_credits);
	}

	return 0;

error:
	resp->status = DLB_ST_QED_FREELIST_ENTRIES_UNAVAILABLE;
	return -1;
}

static int dlb_attach_dir_credits(struct dlb_function_resources *rsrcs,
				  struct dlb_domain *domain,
				  u32 num_credits,
				  struct dlb_cmd_response *resp)
{
	struct dlb_bitmap *bitmap = rsrcs->avail_dqed_freelist_entries;

	if (dlb_bitmap_count(bitmap) < (int)num_credits) {
		resp->status = DLB_ST_DIR_CREDITS_UNAVAILABLE;
		return -1;
	}

	if (num_credits) {
		int base;

		base = dlb_bitmap_find_set_bit_range(bitmap, num_credits);
		if (base < 0)
			goto error;

		domain->dqed_freelist.base = base;
		domain->dqed_freelist.bound = base + num_credits;
		domain->dqed_freelist.offset = 0;

		dlb_bitmap_clear_range(bitmap, base, num_credits);
	}

	return 0;

error:
	resp->status = DLB_ST_DQED_FREELIST_ENTRIES_UNAVAILABLE;
	return -1;
}

static int dlb_attach_ldb_credit_pools(struct dlb_hw *hw,
				       struct dlb_function_resources *rsrcs,
				       struct dlb_domain *domain,
				       u32 num_credit_pools,
				       struct dlb_cmd_response *resp)
{
	unsigned int i, j;

	if (rsrcs->num_avail_ldb_credit_pools < num_credit_pools) {
		resp->status = DLB_ST_LDB_CREDIT_POOLS_UNAVAILABLE;
		return -1;
	}

	for (i = 0; i < num_credit_pools; i++) {
		struct dlb_credit_pool *pool;

		pool = DLB_FUNC_LIST_HEAD(rsrcs->avail_ldb_credit_pools,
					  typeof(*pool));
		if (pool == NULL) {
			DLB_HW_ERR(hw,
				   "[%s()] Internal error: domain validation failed\n",
				   __func__);
			goto cleanup;
		}

		dlb_list_del(&rsrcs->avail_ldb_credit_pools,
			     &pool->func_list);

		pool->domain_id = domain->id;
		pool->owned = true;

		dlb_list_add(&domain->avail_ldb_credit_pools,
			     &pool->domain_list);
	}

	rsrcs->num_avail_ldb_credit_pools -= num_credit_pools;

	return 0;

cleanup:

	/* Return the assigned credit pools */
	for (j = 0; j < i; j++) {
		struct dlb_credit_pool *pool;

		pool = DLB_FUNC_LIST_HEAD(domain->avail_ldb_credit_pools,
					  typeof(*pool));
		/* Unrecoverable internal error */
		if (pool == NULL)
			break;

		pool->owned = false;

		dlb_list_del(&domain->avail_ldb_credit_pools,
			     &pool->domain_list);

		dlb_list_add(&rsrcs->avail_ldb_credit_pools,
			     &pool->func_list);
	}

	return -EFAULT;
}

static int dlb_attach_dir_credit_pools(struct dlb_hw *hw,
				       struct dlb_function_resources *rsrcs,
				       struct dlb_domain *domain,
				       u32 num_credit_pools,
				       struct dlb_cmd_response *resp)
{
	unsigned int i, j;

	if (rsrcs->num_avail_dir_credit_pools < num_credit_pools) {
		resp->status = DLB_ST_DIR_CREDIT_POOLS_UNAVAILABLE;
		return -1;
	}

	for (i = 0; i < num_credit_pools; i++) {
		struct dlb_credit_pool *pool;

		pool = DLB_FUNC_LIST_HEAD(rsrcs->avail_dir_credit_pools,
					  typeof(*pool));
		if (pool == NULL) {
			DLB_HW_ERR(hw,
				   "[%s()] Internal error: domain validation failed\n",
				   __func__);
			goto cleanup;
		}

		dlb_list_del(&rsrcs->avail_dir_credit_pools,
			     &pool->func_list);

		pool->domain_id = domain->id;
		pool->owned = true;

		dlb_list_add(&domain->avail_dir_credit_pools,
			     &pool->domain_list);
	}

	rsrcs->num_avail_dir_credit_pools -= num_credit_pools;

	return 0;

cleanup:

	/* Return the assigned credit pools */
	for (j = 0; j < i; j++) {
		struct dlb_credit_pool *pool;

		pool = DLB_FUNC_LIST_HEAD(domain->avail_dir_credit_pools,
					  typeof(*pool));
		/* Unrecoverable internal error */
		if (pool == NULL)
			break;

		pool->owned = false;

		dlb_list_del(&domain->avail_dir_credit_pools,
			     &pool->domain_list);

		dlb_list_add(&rsrcs->avail_dir_credit_pools,
			     &pool->func_list);
	}

	return -EFAULT;
}

static int
dlb_attach_domain_hist_list_entries(struct dlb_function_resources *rsrcs,
				    struct dlb_domain *domain,
				    u32 num_hist_list_entries,
				    struct dlb_cmd_response *resp)
{
	struct dlb_bitmap *bitmap;
	int base;

	if (num_hist_list_entries) {
		bitmap = rsrcs->avail_hist_list_entries;

		base = dlb_bitmap_find_set_bit_range(bitmap,
						     num_hist_list_entries);
		if (base < 0)
			goto error;

		domain->total_hist_list_entries = num_hist_list_entries;
		domain->avail_hist_list_entries = num_hist_list_entries;
		domain->hist_list_entry_base = base;
		domain->hist_list_entry_offset = 0;

		dlb_bitmap_clear_range(bitmap, base, num_hist_list_entries);
	}
	return 0;

error:
	resp->status = DLB_ST_HIST_LIST_ENTRIES_UNAVAILABLE;
	return -1;
}

static int dlb_attach_atomic_inflights(struct dlb_function_resources *rsrcs,
				       struct dlb_domain *domain,
				       u32 num_atomic_inflights,
				       struct dlb_cmd_response *resp)
{
	if (num_atomic_inflights) {
		struct dlb_bitmap *bitmap =
			rsrcs->avail_aqed_freelist_entries;
		int base;

		base = dlb_bitmap_find_set_bit_range(bitmap,
						     num_atomic_inflights);
		if (base < 0)
			goto error;

		domain->aqed_freelist.base = base;
		domain->aqed_freelist.bound = base + num_atomic_inflights;
		domain->aqed_freelist.offset = 0;

		dlb_bitmap_clear_range(bitmap, base, num_atomic_inflights);
	}

	return 0;

error:
	resp->status = DLB_ST_ATOMIC_INFLIGHTS_UNAVAILABLE;
	return -1;
}


static int
dlb_domain_attach_resources(struct dlb_hw *hw,
			    struct dlb_function_resources *rsrcs,
			    struct dlb_domain *domain,
			    struct dlb_create_sched_domain_args *args,
			    struct dlb_cmd_response *resp)
{
	int ret;

	ret = dlb_attach_ldb_queues(hw,
				    rsrcs,
				    domain,
				    args->num_ldb_queues,
				    resp);
	if (ret < 0)
		return ret;

	ret = dlb_attach_ldb_ports(hw,
				   rsrcs,
				   domain,
				   args->num_ldb_ports,
				   resp);
	if (ret < 0)
		return ret;

	ret = dlb_attach_dir_ports(hw,
				   rsrcs,
				   domain,
				   args->num_dir_ports,
				   resp);
	if (ret < 0)
		return ret;

	ret = dlb_attach_ldb_credits(rsrcs,
				     domain,
				     args->num_ldb_credits,
				     resp);
	if (ret < 0)
		return ret;

	ret = dlb_attach_dir_credits(rsrcs,
				     domain,
				     args->num_dir_credits,
				     resp);
	if (ret < 0)
		return ret;

	ret = dlb_attach_ldb_credit_pools(hw,
					  rsrcs,
					  domain,
					  args->num_ldb_credit_pools,
					  resp);
	if (ret < 0)
		return ret;

	ret = dlb_attach_dir_credit_pools(hw,
					  rsrcs,
					  domain,
					  args->num_dir_credit_pools,
					  resp);
	if (ret < 0)
		return ret;

	ret = dlb_attach_domain_hist_list_entries(rsrcs,
						  domain,
						  args->num_hist_list_entries,
						  resp);
	if (ret < 0)
		return ret;

	ret = dlb_attach_atomic_inflights(rsrcs,
					  domain,
					  args->num_atomic_inflights,
					  resp);
	if (ret < 0)
		return ret;

	domain->configured = true;

	domain->started = false;

	rsrcs->num_avail_domains--;

	return 0;
}

static void dlb_ldb_port_cq_enable(struct dlb_hw *hw,
				   struct dlb_ldb_port *port)
{
	union dlb_lsp_cq_ldb_dsbl reg;

	/* Don't re-enable the port if a removal is pending. The caller should
	 * mark this port as enabled (if it isn't already), and when the
	 * removal completes the port will be enabled.
	 */
	if (port->num_pending_removals)
		return;

	reg.field.disabled = 0;

	DLB_CSR_WR(hw, DLB_LSP_CQ_LDB_DSBL(port->id), reg.val);

	dlb_flush_csr(hw);
}

static void dlb_dir_port_cq_enable(struct dlb_hw *hw,
				   struct dlb_dir_pq_pair *port)
{
	union dlb_lsp_cq_dir_dsbl reg;

	reg.field.disabled = 0;

	DLB_CSR_WR(hw, DLB_LSP_CQ_DIR_DSBL(port->id), reg.val);

	dlb_flush_csr(hw);
}


static void dlb_ldb_port_cq_disable(struct dlb_hw *hw,
				    struct dlb_ldb_port *port)
{
	union dlb_lsp_cq_ldb_dsbl reg;

	reg.field.disabled = 1;

	DLB_CSR_WR(hw, DLB_LSP_CQ_LDB_DSBL(port->id), reg.val);

	dlb_flush_csr(hw);
}

static void dlb_dir_port_cq_disable(struct dlb_hw *hw,
				    struct dlb_dir_pq_pair *port)
{
	union dlb_lsp_cq_dir_dsbl reg;

	reg.field.disabled = 1;

	DLB_CSR_WR(hw, DLB_LSP_CQ_DIR_DSBL(port->id), reg.val);

	dlb_flush_csr(hw);
}



void dlb_disable_dp_vasr_feature(struct dlb_hw *hw)
{
	union dlb_dp_dir_csr_ctrl r0;

	r0.val = DLB_CSR_RD(hw, DLB_DP_DIR_CSR_CTRL);

	r0.field.cfg_vasr_dis = 1;

	DLB_CSR_WR(hw, DLB_DP_DIR_CSR_CTRL, r0.val);
}

void dlb_enable_excess_tokens_alarm(struct dlb_hw *hw)
{
	union dlb_chp_cfg_chp_csr_ctrl r0;

	r0.val = DLB_CSR_RD(hw, DLB_CHP_CFG_CHP_CSR_CTRL);

	r0.val |= 1 << DLB_CHP_CFG_EXCESS_TOKENS_SHIFT;

	DLB_CSR_WR(hw, DLB_CHP_CFG_CHP_CSR_CTRL, r0.val);
}

void dlb_hw_enable_sparse_ldb_cq_mode(struct dlb_hw *hw)
{
	union dlb_sys_cq_mode r0;

	r0.val = DLB_CSR_RD(hw, DLB_SYS_CQ_MODE);

	r0.field.ldb_cq64 = 1;

	DLB_CSR_WR(hw, DLB_SYS_CQ_MODE, r0.val);
}

void dlb_hw_enable_sparse_dir_cq_mode(struct dlb_hw *hw)
{
	union dlb_sys_cq_mode r0;

	r0.val = DLB_CSR_RD(hw, DLB_SYS_CQ_MODE);

	r0.field.dir_cq64 = 1;

	DLB_CSR_WR(hw, DLB_SYS_CQ_MODE, r0.val);
}

void dlb_hw_disable_pf_to_vf_isr_pend_err(struct dlb_hw *hw)
{
	union dlb_sys_sys_alarm_int_enable r0;

	r0.val = DLB_CSR_RD(hw, DLB_SYS_SYS_ALARM_INT_ENABLE);

	r0.field.pf_to_vf_isr_pend_error = 0;

	DLB_CSR_WR(hw, DLB_SYS_SYS_ALARM_INT_ENABLE, r0.val);
}

static unsigned int
dlb_get_num_ports_in_use(struct dlb_hw *hw)
{
	unsigned int i, n = 0;

	for (i = 0; i < DLB_MAX_NUM_LDB_PORTS; i++)
		if (hw->rsrcs.ldb_ports[i].owned)
			n++;

	for (i = 0; i < DLB_MAX_NUM_DIR_PORTS; i++)
		if (hw->rsrcs.dir_pq_pairs[i].owned)
			n++;

	return n;
}

static bool dlb_port_find_slot(struct dlb_ldb_port *port,
			       enum dlb_qid_map_state state,
			       int *slot)
{
	int i;

	for (i = 0; i < DLB_MAX_NUM_QIDS_PER_LDB_CQ; i++) {
		if (port->qid_map[i].state == state)
			break;
	}

	*slot = i;

	return (i < DLB_MAX_NUM_QIDS_PER_LDB_CQ);
}

static bool dlb_port_find_slot_queue(struct dlb_ldb_port *port,
				     enum dlb_qid_map_state state,
				     struct dlb_ldb_queue *queue,
				     int *slot)
{
	int i;

	for (i = 0; i < DLB_MAX_NUM_QIDS_PER_LDB_CQ; i++) {
		if (port->qid_map[i].state == state &&
		    port->qid_map[i].qid == queue->id)
			break;
	}

	*slot = i;

	return (i < DLB_MAX_NUM_QIDS_PER_LDB_CQ);
}

static int dlb_port_slot_state_transition(struct dlb_hw *hw,
					  struct dlb_ldb_port *port,
					  struct dlb_ldb_queue *queue,
					  int slot,
					  enum dlb_qid_map_state new_state)
{
	enum dlb_qid_map_state curr_state = port->qid_map[slot].state;
	struct dlb_domain *domain;

	domain = dlb_get_domain_from_id(hw, port->domain_id);
	if (domain == NULL) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: unable to find domain %d\n",
			   __func__, port->domain_id);
		return -EFAULT;
	}

	switch (curr_state) {
	case DLB_QUEUE_UNMAPPED:
		switch (new_state) {
		case DLB_QUEUE_MAPPED:
			queue->num_mappings++;
			port->num_mappings++;
			break;
		case DLB_QUEUE_MAP_IN_PROGRESS:
			queue->num_pending_additions++;
			domain->num_pending_additions++;
			break;
		default:
			goto error;
		}
		break;
	case DLB_QUEUE_MAPPED:
		switch (new_state) {
		case DLB_QUEUE_UNMAPPED:
			queue->num_mappings--;
			port->num_mappings--;
			break;
		case DLB_QUEUE_UNMAP_IN_PROGRESS:
			port->num_pending_removals++;
			domain->num_pending_removals++;
			break;
		case DLB_QUEUE_MAPPED:
			/* Priority change, nothing to update */
			break;
		default:
			goto error;
		}
		break;
	case DLB_QUEUE_MAP_IN_PROGRESS:
		switch (new_state) {
		case DLB_QUEUE_UNMAPPED:
			queue->num_pending_additions--;
			domain->num_pending_additions--;
			break;
		case DLB_QUEUE_MAPPED:
			queue->num_mappings++;
			port->num_mappings++;
			queue->num_pending_additions--;
			domain->num_pending_additions--;
			break;
		default:
			goto error;
		}
		break;
	case DLB_QUEUE_UNMAP_IN_PROGRESS:
		switch (new_state) {
		case DLB_QUEUE_UNMAPPED:
			port->num_pending_removals--;
			domain->num_pending_removals--;
			queue->num_mappings--;
			port->num_mappings--;
			break;
		case DLB_QUEUE_MAPPED:
			port->num_pending_removals--;
			domain->num_pending_removals--;
			break;
		case DLB_QUEUE_UNMAP_IN_PROGRESS_PENDING_MAP:
			/* Nothing to update */
			break;
		default:
			goto error;
		}
		break;
	case DLB_QUEUE_UNMAP_IN_PROGRESS_PENDING_MAP:
		switch (new_state) {
		case DLB_QUEUE_UNMAP_IN_PROGRESS:
			/* Nothing to update */
			break;
		case DLB_QUEUE_UNMAPPED:
			/* An UNMAP_IN_PROGRESS_PENDING_MAP slot briefly
			 * becomes UNMAPPED before it transitions to
			 * MAP_IN_PROGRESS.
			 */
			queue->num_mappings--;
			port->num_mappings--;
			port->num_pending_removals--;
			domain->num_pending_removals--;
			break;
		default:
			goto error;
		}
		break;
	default:
		goto error;
	}

	port->qid_map[slot].state = new_state;

	DLB_HW_INFO(hw,
		    "[%s()] queue %d -> port %d state transition (%d -> %d)\n",
		    __func__, queue->id, port->id, curr_state,
		    new_state);
	return 0;

error:
	DLB_HW_ERR(hw,
		   "[%s()] Internal error: invalid queue %d -> port %d state transition (%d -> %d)\n",
		   __func__, queue->id, port->id, curr_state,
		   new_state);
	return -EFAULT;
}

/* dlb_ldb_queue_{enable, disable}_mapped_cqs() don't operate exactly as their
 * function names imply, and should only be called by the dynamic CQ mapping
 * code.
 */
static void dlb_ldb_queue_disable_mapped_cqs(struct dlb_hw *hw,
					     struct dlb_domain *domain,
					     struct dlb_ldb_queue *queue)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_ldb_port *port;
	int slot;

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter) {
		enum dlb_qid_map_state state = DLB_QUEUE_MAPPED;

		if (!dlb_port_find_slot_queue(port, state, queue, &slot))
			continue;

		if (port->enabled)
			dlb_ldb_port_cq_disable(hw, port);
	}
}

static void dlb_ldb_queue_enable_mapped_cqs(struct dlb_hw *hw,
					    struct dlb_domain *domain,
					    struct dlb_ldb_queue *queue)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_ldb_port *port;
	int slot;

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter) {
		enum dlb_qid_map_state state = DLB_QUEUE_MAPPED;

		if (!dlb_port_find_slot_queue(port, state, queue, &slot))
			continue;

		if (port->enabled)
			dlb_ldb_port_cq_enable(hw, port);
	}
}

static int dlb_ldb_port_map_qid_static(struct dlb_hw *hw,
				       struct dlb_ldb_port *p,
				       struct dlb_ldb_queue *q,
				       u8 priority)
{
	union dlb_lsp_cq2priov r0;
	union dlb_lsp_cq2qid r1;
	union dlb_atm_pipe_qid_ldb_qid2cqidx r2;
	union dlb_lsp_qid_ldb_qid2cqidx r3;
	union dlb_lsp_qid_ldb_qid2cqidx2 r4;
	enum dlb_qid_map_state state;
	int i;

	/* Look for a pending or already mapped slot, else an unused slot */
	if (!dlb_port_find_slot_queue(p, DLB_QUEUE_MAP_IN_PROGRESS, q, &i) &&
	    !dlb_port_find_slot_queue(p, DLB_QUEUE_MAPPED, q, &i) &&
	    !dlb_port_find_slot(p, DLB_QUEUE_UNMAPPED, &i)) {
		DLB_HW_ERR(hw,
			   "[%s():%d] Internal error: CQ has no available QID mapping slots\n",
			   __func__, __LINE__);
		return -EFAULT;
	}

	if (i >= DLB_MAX_NUM_QIDS_PER_LDB_CQ) {
		DLB_HW_ERR(hw,
			   "[%s():%d] Internal error: port slot tracking failed\n",
			   __func__, __LINE__);
		return -EFAULT;
	}

	/* Read-modify-write the priority and valid bit register */
	r0.val = DLB_CSR_RD(hw, DLB_LSP_CQ2PRIOV(p->id));

	r0.field.v |= 1 << i;
	r0.field.prio |= (priority & 0x7) << i * 3;

	DLB_CSR_WR(hw, DLB_LSP_CQ2PRIOV(p->id), r0.val);

	/* Read-modify-write the QID map register */
	r1.val = DLB_CSR_RD(hw, DLB_LSP_CQ2QID(p->id, i / 4));

	if (i == 0 || i == 4)
		r1.field.qid_p0 = q->id;
	if (i == 1 || i == 5)
		r1.field.qid_p1 = q->id;
	if (i == 2 || i == 6)
		r1.field.qid_p2 = q->id;
	if (i == 3 || i == 7)
		r1.field.qid_p3 = q->id;

	DLB_CSR_WR(hw, DLB_LSP_CQ2QID(p->id, i / 4), r1.val);

	r2.val = DLB_CSR_RD(hw,
			    DLB_ATM_PIPE_QID_LDB_QID2CQIDX(q->id,
							   p->id / 4));

	r3.val = DLB_CSR_RD(hw,
			    DLB_LSP_QID_LDB_QID2CQIDX(q->id,
						      p->id / 4));

	r4.val = DLB_CSR_RD(hw,
			    DLB_LSP_QID_LDB_QID2CQIDX2(q->id,
						       p->id / 4));

	switch (p->id % 4) {
	case 0:
		r2.field.cq_p0 |= 1 << i;
		r3.field.cq_p0 |= 1 << i;
		r4.field.cq_p0 |= 1 << i;
		break;

	case 1:
		r2.field.cq_p1 |= 1 << i;
		r3.field.cq_p1 |= 1 << i;
		r4.field.cq_p1 |= 1 << i;
		break;

	case 2:
		r2.field.cq_p2 |= 1 << i;
		r3.field.cq_p2 |= 1 << i;
		r4.field.cq_p2 |= 1 << i;
		break;

	case 3:
		r2.field.cq_p3 |= 1 << i;
		r3.field.cq_p3 |= 1 << i;
		r4.field.cq_p3 |= 1 << i;
		break;
	}

	DLB_CSR_WR(hw,
		   DLB_ATM_PIPE_QID_LDB_QID2CQIDX(q->id,
						  p->id / 4),
		   r2.val);

	DLB_CSR_WR(hw,
		   DLB_LSP_QID_LDB_QID2CQIDX(q->id,
					     p->id / 4),
		   r3.val);

	DLB_CSR_WR(hw,
		   DLB_LSP_QID_LDB_QID2CQIDX2(q->id,
					      p->id / 4),
		   r4.val);

	dlb_flush_csr(hw);

	p->qid_map[i].qid = q->id;
	p->qid_map[i].priority = priority;

	state = DLB_QUEUE_MAPPED;

	return dlb_port_slot_state_transition(hw, p, q, i, state);
}

static int dlb_ldb_port_set_has_work_bits(struct dlb_hw *hw,
					  struct dlb_ldb_port *port,
					  struct dlb_ldb_queue *queue,
					  int slot)
{
	union dlb_lsp_qid_aqed_active_cnt r0;
	union dlb_lsp_qid_ldb_enqueue_cnt r1;
	union dlb_lsp_ldb_sched_ctrl r2 = { {0} };

	/* Set the atomic scheduling haswork bit */
	r0.val = DLB_CSR_RD(hw, DLB_LSP_QID_AQED_ACTIVE_CNT(queue->id));

	r2.field.cq = port->id;
	r2.field.qidix = slot;
	r2.field.value = 1;
	r2.field.rlist_haswork_v = r0.field.count > 0;

	/* Set the non-atomic scheduling haswork bit */
	DLB_CSR_WR(hw, DLB_LSP_LDB_SCHED_CTRL, r2.val);

	r1.val = DLB_CSR_RD(hw, DLB_LSP_QID_LDB_ENQUEUE_CNT(queue->id));

	memset(&r2, 0, sizeof(r2));

	r2.field.cq = port->id;
	r2.field.qidix = slot;
	r2.field.value = 1;
	r2.field.nalb_haswork_v = (r1.field.count > 0);

	DLB_CSR_WR(hw, DLB_LSP_LDB_SCHED_CTRL, r2.val);

	dlb_flush_csr(hw);

	return 0;
}

static void dlb_ldb_port_clear_queue_if_status(struct dlb_hw *hw,
					       struct dlb_ldb_port *port,
					       int slot)
{
	union dlb_lsp_ldb_sched_ctrl r0 = { {0} };

	r0.field.cq = port->id;
	r0.field.qidix = slot;
	r0.field.value = 0;
	r0.field.inflight_ok_v = 1;

	DLB_CSR_WR(hw, DLB_LSP_LDB_SCHED_CTRL, r0.val);

	dlb_flush_csr(hw);
}

static void dlb_ldb_port_set_queue_if_status(struct dlb_hw *hw,
					     struct dlb_ldb_port *port,
					     int slot)
{
	union dlb_lsp_ldb_sched_ctrl r0 = { {0} };

	r0.field.cq = port->id;
	r0.field.qidix = slot;
	r0.field.value = 1;
	r0.field.inflight_ok_v = 1;

	DLB_CSR_WR(hw, DLB_LSP_LDB_SCHED_CTRL, r0.val);

	dlb_flush_csr(hw);
}

static void dlb_ldb_queue_set_inflight_limit(struct dlb_hw *hw,
					     struct dlb_ldb_queue *queue)
{
	union dlb_lsp_qid_ldb_infl_lim r0 = { {0} };

	r0.field.limit = queue->num_qid_inflights;

	DLB_CSR_WR(hw, DLB_LSP_QID_LDB_INFL_LIM(queue->id), r0.val);
}

static void dlb_ldb_queue_clear_inflight_limit(struct dlb_hw *hw,
					       struct dlb_ldb_queue *queue)
{
	DLB_CSR_WR(hw,
		   DLB_LSP_QID_LDB_INFL_LIM(queue->id),
		   DLB_LSP_QID_LDB_INFL_LIM_RST);
}

static int dlb_ldb_port_finish_map_qid_dynamic(struct dlb_hw *hw,
					       struct dlb_domain *domain,
					       struct dlb_ldb_port *port,
					       struct dlb_ldb_queue *queue)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	union dlb_lsp_qid_ldb_infl_cnt r0;
	enum dlb_qid_map_state state;
	int slot, ret;
	u8 prio;

	r0.val = DLB_CSR_RD(hw, DLB_LSP_QID_LDB_INFL_CNT(queue->id));

	if (r0.field.count) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: non-zero QID inflight count\n",
			   __func__);
		return -EFAULT;
	}

	/* For each port with a pending mapping to this queue, perform the
	 * static mapping and set the corresponding has_work bits.
	 */
	state = DLB_QUEUE_MAP_IN_PROGRESS;
	if (!dlb_port_find_slot_queue(port, state, queue, &slot))
		return -EINVAL;

	if (slot >= DLB_MAX_NUM_QIDS_PER_LDB_CQ) {
		DLB_HW_ERR(hw,
			   "[%s():%d] Internal error: port slot tracking failed\n",
			   __func__, __LINE__);
		return -EFAULT;
	}

	prio = port->qid_map[slot].priority;

	/* Update the CQ2QID, CQ2PRIOV, and QID2CQIDX registers, and
	 * the port's qid_map state.
	 */
	ret = dlb_ldb_port_map_qid_static(hw, port, queue, prio);
	if (ret)
		return ret;

	ret = dlb_ldb_port_set_has_work_bits(hw, port, queue, slot);
	if (ret)
		return ret;

	/* Ensure IF_status(cq,qid) is 0 before enabling the port to
	 * prevent spurious schedules to cause the queue's inflight
	 * count to increase.
	 */
	dlb_ldb_port_clear_queue_if_status(hw, port, slot);

	/* Reset the queue's inflight status */
	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter) {
		state = DLB_QUEUE_MAPPED;
		if (!dlb_port_find_slot_queue(port, state, queue, &slot))
			continue;

		dlb_ldb_port_set_queue_if_status(hw, port, slot);
	}

	dlb_ldb_queue_set_inflight_limit(hw, queue);

	/* Re-enable CQs mapped to this queue */
	dlb_ldb_queue_enable_mapped_cqs(hw, domain, queue);

	/* If this queue has other mappings pending, clear its inflight limit */
	if (queue->num_pending_additions > 0)
		dlb_ldb_queue_clear_inflight_limit(hw, queue);

	return 0;
}

/**
 * dlb_ldb_port_map_qid_dynamic() - perform a "dynamic" QID->CQ mapping
 * @hw: dlb_hw handle for a particular device.
 * @port: load-balanced port
 * @queue: load-balanced queue
 * @priority: queue servicing priority
 *
 * Returns 0 if the queue was mapped, 1 if the mapping is scheduled to occur
 * at a later point, and <0 if an error occurred.
 */
static int dlb_ldb_port_map_qid_dynamic(struct dlb_hw *hw,
					struct dlb_ldb_port *port,
					struct dlb_ldb_queue *queue,
					u8 priority)
{
	union dlb_lsp_qid_ldb_infl_cnt r0 = { {0} };
	enum dlb_qid_map_state state;
	struct dlb_domain *domain;
	int slot, ret;

	domain = dlb_get_domain_from_id(hw, port->domain_id);
	if (domain == NULL) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: unable to find domain %d\n",
			   __func__, port->domain_id);
		return -EFAULT;
	}

	/* Set the QID inflight limit to 0 to prevent further scheduling of the
	 * queue.
	 */
	DLB_CSR_WR(hw, DLB_LSP_QID_LDB_INFL_LIM(queue->id), 0);

	if (!dlb_port_find_slot(port, DLB_QUEUE_UNMAPPED, &slot)) {
		DLB_HW_ERR(hw,
			   "Internal error: No available unmapped slots\n");
		return -EFAULT;
	}

	if (slot >= DLB_MAX_NUM_QIDS_PER_LDB_CQ) {
		DLB_HW_ERR(hw,
			   "[%s():%d] Internal error: port slot tracking failed\n",
			   __func__, __LINE__);
		return -EFAULT;
	}

	port->qid_map[slot].qid = queue->id;
	port->qid_map[slot].priority = priority;

	state = DLB_QUEUE_MAP_IN_PROGRESS;
	ret = dlb_port_slot_state_transition(hw, port, queue, slot, state);
	if (ret)
		return ret;

	r0.val = DLB_CSR_RD(hw, DLB_LSP_QID_LDB_INFL_CNT(queue->id));

	if (r0.field.count) {
		/* The queue is owed completions so it's not safe to map it
		 * yet. Schedule a kernel thread to complete the mapping later,
		 * once software has completed all the queue's inflight events.
		 */
		if (!os_worker_active(hw))
			os_schedule_work(hw);

		return 1;
	}

	/* Disable the affected CQ, and the CQs already mapped to the QID,
	 * before reading the QID's inflight count a second time. There is an
	 * unlikely race in which the QID may schedule one more QE after we
	 * read an inflight count of 0, and disabling the CQs guarantees that
	 * the race will not occur after a re-read of the inflight count
	 * register.
	 */
	if (port->enabled)
		dlb_ldb_port_cq_disable(hw, port);

	dlb_ldb_queue_disable_mapped_cqs(hw, domain, queue);

	r0.val = DLB_CSR_RD(hw, DLB_LSP_QID_LDB_INFL_CNT(queue->id));

	if (r0.field.count) {
		if (port->enabled)
			dlb_ldb_port_cq_enable(hw, port);

		dlb_ldb_queue_enable_mapped_cqs(hw, domain, queue);

		/* The queue is owed completions so it's not safe to map it
		 * yet. Schedule a kernel thread to complete the mapping later,
		 * once software has completed all the queue's inflight events.
		 */
		if (!os_worker_active(hw))
			os_schedule_work(hw);

		return 1;
	}

	return dlb_ldb_port_finish_map_qid_dynamic(hw, domain, port, queue);
}


static int dlb_ldb_port_map_qid(struct dlb_hw *hw,
				struct dlb_domain *domain,
				struct dlb_ldb_port *port,
				struct dlb_ldb_queue *queue,
				u8 prio)
{
	if (domain->started)
		return dlb_ldb_port_map_qid_dynamic(hw, port, queue, prio);
	else
		return dlb_ldb_port_map_qid_static(hw, port, queue, prio);
}

static int dlb_ldb_port_unmap_qid(struct dlb_hw *hw,
				  struct dlb_ldb_port *port,
				  struct dlb_ldb_queue *queue)
{
	enum dlb_qid_map_state mapped, in_progress, pending_map, unmapped;
	union dlb_lsp_cq2priov r0;
	union dlb_atm_pipe_qid_ldb_qid2cqidx r1;
	union dlb_lsp_qid_ldb_qid2cqidx r2;
	union dlb_lsp_qid_ldb_qid2cqidx2 r3;
	u32 queue_id;
	u32 port_id;
	int i;

	/* Find the queue's slot */
	mapped = DLB_QUEUE_MAPPED;
	in_progress = DLB_QUEUE_UNMAP_IN_PROGRESS;
	pending_map = DLB_QUEUE_UNMAP_IN_PROGRESS_PENDING_MAP;

	if (!dlb_port_find_slot_queue(port, mapped, queue, &i) &&
	    !dlb_port_find_slot_queue(port, in_progress, queue, &i) &&
	    !dlb_port_find_slot_queue(port, pending_map, queue, &i)) {
		DLB_HW_ERR(hw,
			   "[%s():%d] Internal error: QID %d isn't mapped\n",
			   __func__, __LINE__, queue->id);
		return -EFAULT;
	}

	if (i >= DLB_MAX_NUM_QIDS_PER_LDB_CQ) {
		DLB_HW_ERR(hw,
			   "[%s():%d] Internal error: port slot tracking failed\n",
			   __func__, __LINE__);
		return -EFAULT;
	}

	port_id = port->id;
	queue_id = queue->id;

	/* Read-modify-write the priority and valid bit register */
	r0.val = DLB_CSR_RD(hw, DLB_LSP_CQ2PRIOV(port_id));

	r0.field.v &= ~(1 << i);

	DLB_CSR_WR(hw, DLB_LSP_CQ2PRIOV(port_id), r0.val);

	r1.val = DLB_CSR_RD(hw,
			    DLB_ATM_PIPE_QID_LDB_QID2CQIDX(queue_id,
							   port_id / 4));

	r2.val = DLB_CSR_RD(hw,
			    DLB_LSP_QID_LDB_QID2CQIDX(queue_id,
						      port_id / 4));

	r3.val = DLB_CSR_RD(hw,
			    DLB_LSP_QID_LDB_QID2CQIDX2(queue_id,
						       port_id / 4));

	switch (port_id % 4) {
	case 0:
		r1.field.cq_p0 &= ~(1 << i);
		r2.field.cq_p0 &= ~(1 << i);
		r3.field.cq_p0 &= ~(1 << i);
		break;

	case 1:
		r1.field.cq_p1 &= ~(1 << i);
		r2.field.cq_p1 &= ~(1 << i);
		r3.field.cq_p1 &= ~(1 << i);
		break;

	case 2:
		r1.field.cq_p2 &= ~(1 << i);
		r2.field.cq_p2 &= ~(1 << i);
		r3.field.cq_p2 &= ~(1 << i);
		break;

	case 3:
		r1.field.cq_p3 &= ~(1 << i);
		r2.field.cq_p3 &= ~(1 << i);
		r3.field.cq_p3 &= ~(1 << i);
		break;
	}

	DLB_CSR_WR(hw,
		   DLB_ATM_PIPE_QID_LDB_QID2CQIDX(queue_id, port_id / 4),
		   r1.val);

	DLB_CSR_WR(hw,
		   DLB_LSP_QID_LDB_QID2CQIDX(queue_id, port_id / 4),
		   r2.val);

	DLB_CSR_WR(hw,
		   DLB_LSP_QID_LDB_QID2CQIDX2(queue_id, port_id / 4),
		   r3.val);

	dlb_flush_csr(hw);

	unmapped = DLB_QUEUE_UNMAPPED;

	return dlb_port_slot_state_transition(hw, port, queue, i, unmapped);
}

static int
dlb_verify_create_sched_domain_args(struct dlb_hw *hw,
				    struct dlb_function_resources *rsrcs,
				    struct dlb_create_sched_domain_args *args,
				    struct dlb_cmd_response *resp)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_bitmap *ldb_credit_freelist;
	struct dlb_bitmap *dir_credit_freelist;
	unsigned int ldb_credit_freelist_count;
	unsigned int dir_credit_freelist_count;
	unsigned int max_contig_aqed_entries;
	unsigned int max_contig_dqed_entries;
	unsigned int max_contig_qed_entries;
	unsigned int max_contig_hl_entries;
	struct dlb_bitmap *aqed_freelist;
	enum dlb_dev_revision revision;

	ldb_credit_freelist = rsrcs->avail_qed_freelist_entries;
	dir_credit_freelist = rsrcs->avail_dqed_freelist_entries;
	aqed_freelist = rsrcs->avail_aqed_freelist_entries;

	ldb_credit_freelist_count = dlb_bitmap_count(ldb_credit_freelist);
	dir_credit_freelist_count = dlb_bitmap_count(dir_credit_freelist);

	max_contig_hl_entries =
		dlb_bitmap_longest_set_range(rsrcs->avail_hist_list_entries);
	max_contig_aqed_entries =
		dlb_bitmap_longest_set_range(aqed_freelist);
	max_contig_qed_entries =
		dlb_bitmap_longest_set_range(ldb_credit_freelist);
	max_contig_dqed_entries =
		dlb_bitmap_longest_set_range(dir_credit_freelist);

	if (rsrcs->num_avail_domains < 1)
		resp->status = DLB_ST_DOMAIN_UNAVAILABLE;
	else if (rsrcs->num_avail_ldb_queues < args->num_ldb_queues)
		resp->status = DLB_ST_LDB_QUEUES_UNAVAILABLE;
	else if (rsrcs->num_avail_ldb_ports < args->num_ldb_ports)
		resp->status = DLB_ST_LDB_PORTS_UNAVAILABLE;
	else if (args->num_ldb_queues > 0 && args->num_ldb_ports == 0)
		resp->status = DLB_ST_LDB_PORT_REQUIRED_FOR_LDB_QUEUES;
	else if (rsrcs->num_avail_dir_pq_pairs < args->num_dir_ports)
		resp->status = DLB_ST_DIR_PORTS_UNAVAILABLE;
	else if (ldb_credit_freelist_count < args->num_ldb_credits)
		resp->status = DLB_ST_LDB_CREDITS_UNAVAILABLE;
	else if (dir_credit_freelist_count < args->num_dir_credits)
		resp->status = DLB_ST_DIR_CREDITS_UNAVAILABLE;
	else if (rsrcs->num_avail_ldb_credit_pools < args->num_ldb_credit_pools)
		resp->status = DLB_ST_LDB_CREDIT_POOLS_UNAVAILABLE;
	else if (rsrcs->num_avail_dir_credit_pools < args->num_dir_credit_pools)
		resp->status = DLB_ST_DIR_CREDIT_POOLS_UNAVAILABLE;
	else if (max_contig_hl_entries < args->num_hist_list_entries)
		resp->status = DLB_ST_HIST_LIST_ENTRIES_UNAVAILABLE;
	else if (max_contig_aqed_entries < args->num_atomic_inflights)
		resp->status = DLB_ST_ATOMIC_INFLIGHTS_UNAVAILABLE;
	else if (max_contig_qed_entries < args->num_ldb_credits)
		resp->status = DLB_ST_QED_FREELIST_ENTRIES_UNAVAILABLE;
	else if (max_contig_dqed_entries < args->num_dir_credits)
		resp->status = DLB_ST_DQED_FREELIST_ENTRIES_UNAVAILABLE;

	/* DLB A-stepping workaround for hardware write buffer lock up issue:
	 * limit the maximum configured ports to less than 128 and disable CQ
	 * occupancy interrupts.
	 */
	revision = os_get_dev_revision(hw);

	if (revision < DLB_B0) {
		u32 n = dlb_get_num_ports_in_use(hw);

		n += args->num_ldb_ports + args->num_dir_ports;

		if (n >= DLB_A_STEP_MAX_PORTS)
			resp->status = args->num_ldb_ports ?
				DLB_ST_LDB_PORTS_UNAVAILABLE :
				DLB_ST_DIR_PORTS_UNAVAILABLE;
	}

	if (resp->status)
		return -1;

	return 0;
}


static void
dlb_log_create_sched_domain_args(struct dlb_hw *hw,
				 struct dlb_create_sched_domain_args *args)
{
	DLB_HW_INFO(hw, "DLB create sched domain arguments:\n");
	DLB_HW_INFO(hw, "\tNumber of LDB queues:        %d\n",
		    args->num_ldb_queues);
	DLB_HW_INFO(hw, "\tNumber of LDB ports:         %d\n",
		    args->num_ldb_ports);
	DLB_HW_INFO(hw, "\tNumber of DIR ports:         %d\n",
		    args->num_dir_ports);
	DLB_HW_INFO(hw, "\tNumber of ATM inflights:     %d\n",
		    args->num_atomic_inflights);
	DLB_HW_INFO(hw, "\tNumber of hist list entries: %d\n",
		    args->num_hist_list_entries);
	DLB_HW_INFO(hw, "\tNumber of LDB credits:       %d\n",
		    args->num_ldb_credits);
	DLB_HW_INFO(hw, "\tNumber of DIR credits:       %d\n",
		    args->num_dir_credits);
	DLB_HW_INFO(hw, "\tNumber of LDB credit pools:  %d\n",
		    args->num_ldb_credit_pools);
	DLB_HW_INFO(hw, "\tNumber of DIR credit pools:  %d\n",
		    args->num_dir_credit_pools);
}

/**
 * dlb_hw_create_sched_domain() - Allocate and initialize a DLB scheduling
 *	domain and its resources.
 * @hw:	  Contains the current state of the DLB hardware.
 * @args: User-provided arguments.
 * @resp: Response to user.
 *
 * Return: returns < 0 on error, 0 otherwise. If the driver is unable to
 * satisfy a request, resp->status will be set accordingly.
 */
int dlb_hw_create_sched_domain(struct dlb_hw *hw,
			       struct dlb_create_sched_domain_args *args,
			       struct dlb_cmd_response *resp)
{
	struct dlb_domain *domain;
	struct dlb_function_resources *rsrcs;
	int ret;

	rsrcs = &hw->pf;

	dlb_log_create_sched_domain_args(hw, args);

	/* Verify that hardware resources are available before attempting to
	 * satisfy the request. This simplifies the error unwinding code.
	 */
	if (dlb_verify_create_sched_domain_args(hw, rsrcs, args, resp))
		return -EINVAL;

	domain = DLB_FUNC_LIST_HEAD(rsrcs->avail_domains, typeof(*domain));

	/* Verification should catch this. */
	if (domain == NULL) {
		DLB_HW_ERR(hw,
			   "[%s():%d] Internal error: no available domains\n",
			   __func__, __LINE__);
		return -EFAULT;
	}

	if (domain->configured) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: avail_domains contains configured domains.\n",
			   __func__);
		return -EFAULT;
	}

	dlb_init_domain_rsrc_lists(domain);

	/* Verification should catch this too. */
	ret = dlb_domain_attach_resources(hw, rsrcs, domain, args, resp);
	if (ret < 0) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: failed to verify args.\n",
			   __func__);

		return -EFAULT;
	}

	dlb_list_del(&rsrcs->avail_domains, &domain->func_list);

	dlb_list_add(&rsrcs->used_domains, &domain->func_list);

	resp->id = domain->id;
	resp->status = 0;

	return 0;
}

static void
dlb_configure_ldb_credit_pool(struct dlb_hw *hw,
			      struct dlb_domain *domain,
			      struct dlb_create_ldb_pool_args *args,
			      struct dlb_credit_pool *pool)
{
	union dlb_sys_ldb_pool_enbld r0 = { {0} };
	union dlb_chp_ldb_pool_crd_lim r1 = { {0} };
	union dlb_chp_ldb_pool_crd_cnt r2 = { {0} };
	union dlb_chp_qed_fl_base  r3 = { {0} };
	union dlb_chp_qed_fl_lim r4 = { {0} };
	union dlb_chp_qed_fl_push_ptr r5 = { {0} };
	union dlb_chp_qed_fl_pop_ptr  r6 = { {0} };

	r1.field.limit = args->num_ldb_credits;

	DLB_CSR_WR(hw, DLB_CHP_LDB_POOL_CRD_LIM(pool->id), r1.val);

	r2.field.count = args->num_ldb_credits;

	DLB_CSR_WR(hw, DLB_CHP_LDB_POOL_CRD_CNT(pool->id), r2.val);

	r3.field.base = domain->qed_freelist.base + domain->qed_freelist.offset;

	DLB_CSR_WR(hw, DLB_CHP_QED_FL_BASE(pool->id), r3.val);

	r4.field.freelist_disable = 0;
	r4.field.limit = r3.field.base + args->num_ldb_credits - 1;

	DLB_CSR_WR(hw, DLB_CHP_QED_FL_LIM(pool->id), r4.val);

	r5.field.push_ptr = r3.field.base;
	r5.field.generation = 1;

	DLB_CSR_WR(hw, DLB_CHP_QED_FL_PUSH_PTR(pool->id), r5.val);

	r6.field.pop_ptr = r3.field.base;
	r6.field.generation = 0;

	DLB_CSR_WR(hw, DLB_CHP_QED_FL_POP_PTR(pool->id), r6.val);

	r0.field.pool_enabled = 1;

	DLB_CSR_WR(hw, DLB_SYS_LDB_POOL_ENBLD(pool->id), r0.val);

	pool->avail_credits = args->num_ldb_credits;
	pool->total_credits = args->num_ldb_credits;
	domain->qed_freelist.offset += args->num_ldb_credits;

	pool->configured = true;
}

static int
dlb_verify_create_ldb_pool_args(struct dlb_hw *hw,
				u32 domain_id,
				struct dlb_create_ldb_pool_args *args,
				struct dlb_cmd_response *resp)
{
	struct dlb_freelist *qed_freelist;
	struct dlb_domain *domain;

	domain = dlb_get_domain_from_id(hw, domain_id);

	if (domain == NULL) {
		resp->status = DLB_ST_INVALID_DOMAIN_ID;
		return -1;
	}

	if (!domain->configured) {
		resp->status = DLB_ST_DOMAIN_NOT_CONFIGURED;
		return -1;
	}

	qed_freelist = &domain->qed_freelist;

	if (dlb_freelist_count(qed_freelist) < args->num_ldb_credits) {
		resp->status = DLB_ST_LDB_CREDITS_UNAVAILABLE;
		return -1;
	}

	if (dlb_list_empty(&domain->avail_ldb_credit_pools)) {
		resp->status = DLB_ST_LDB_CREDIT_POOLS_UNAVAILABLE;
		return -1;
	}

	if (domain->started) {
		resp->status = DLB_ST_DOMAIN_STARTED;
		return -1;
	}

	return 0;
}

static void
dlb_log_create_ldb_pool_args(struct dlb_hw *hw,
			     u32 domain_id,
			     struct dlb_create_ldb_pool_args *args)
{
	DLB_HW_INFO(hw, "DLB create load-balanced credit pool arguments:\n");
	DLB_HW_INFO(hw, "\tDomain ID:             %d\n", domain_id);
	DLB_HW_INFO(hw, "\tNumber of LDB credits: %d\n",
		    args->num_ldb_credits);
}

/**
 * dlb_hw_create_ldb_pool() - Allocate and initialize a DLB credit pool.
 * @hw:	  Contains the current state of the DLB hardware.
 * @args: User-provided arguments.
 * @resp: Response to user.
 *
 * Return: returns < 0 on error, 0 otherwise. If the driver is unable to
 * satisfy a request, resp->status will be set accordingly.
 */
int dlb_hw_create_ldb_pool(struct dlb_hw *hw,
			   u32 domain_id,
			   struct dlb_create_ldb_pool_args *args,
			   struct dlb_cmd_response *resp)
{
	struct dlb_credit_pool *pool;
	struct dlb_domain *domain;

	dlb_log_create_ldb_pool_args(hw, domain_id, args);

	/* Verify that hardware resources are available before attempting to
	 * satisfy the request. This simplifies the error unwinding code.
	 */
	if (dlb_verify_create_ldb_pool_args(hw, domain_id, args, resp))
		return -EINVAL;

	domain = dlb_get_domain_from_id(hw, domain_id);
	if (domain == NULL) {
		DLB_HW_ERR(hw,
			   "[%s():%d] Internal error: domain not found\n",
			   __func__, __LINE__);
		return -EFAULT;
	}

	pool = DLB_DOM_LIST_HEAD(domain->avail_ldb_credit_pools, typeof(*pool));

	/* Verification should catch this. */
	if (pool == NULL) {
		DLB_HW_ERR(hw,
			   "[%s():%d] Internal error: no available ldb credit pools\n",
			   __func__, __LINE__);
		return -EFAULT;
	}

	dlb_configure_ldb_credit_pool(hw, domain, args, pool);

	/* Configuration succeeded, so move the resource from the 'avail' to
	 * the 'used' list.
	 */
	dlb_list_del(&domain->avail_ldb_credit_pools, &pool->domain_list);

	dlb_list_add(&domain->used_ldb_credit_pools, &pool->domain_list);

	resp->status = 0;
	resp->id = pool->id;

	return 0;
}

static void
dlb_configure_dir_credit_pool(struct dlb_hw *hw,
			      struct dlb_domain *domain,
			      struct dlb_create_dir_pool_args *args,
			      struct dlb_credit_pool *pool)
{
	union dlb_sys_dir_pool_enbld r0 = { {0} };
	union dlb_chp_dir_pool_crd_lim r1 = { {0} };
	union dlb_chp_dir_pool_crd_cnt r2 = { {0} };
	union dlb_chp_dqed_fl_base  r3 = { {0} };
	union dlb_chp_dqed_fl_lim r4 = { {0} };
	union dlb_chp_dqed_fl_push_ptr r5 = { {0} };
	union dlb_chp_dqed_fl_pop_ptr  r6 = { {0} };

	r1.field.limit = args->num_dir_credits;

	DLB_CSR_WR(hw, DLB_CHP_DIR_POOL_CRD_LIM(pool->id), r1.val);

	r2.field.count = args->num_dir_credits;

	DLB_CSR_WR(hw, DLB_CHP_DIR_POOL_CRD_CNT(pool->id), r2.val);

	r3.field.base = domain->dqed_freelist.base +
			domain->dqed_freelist.offset;

	DLB_CSR_WR(hw, DLB_CHP_DQED_FL_BASE(pool->id), r3.val);

	r4.field.freelist_disable = 0;
	r4.field.limit = r3.field.base + args->num_dir_credits - 1;

	DLB_CSR_WR(hw, DLB_CHP_DQED_FL_LIM(pool->id), r4.val);

	r5.field.push_ptr = r3.field.base;
	r5.field.generation = 1;

	DLB_CSR_WR(hw, DLB_CHP_DQED_FL_PUSH_PTR(pool->id), r5.val);

	r6.field.pop_ptr = r3.field.base;
	r6.field.generation = 0;

	DLB_CSR_WR(hw, DLB_CHP_DQED_FL_POP_PTR(pool->id), r6.val);

	r0.field.pool_enabled = 1;

	DLB_CSR_WR(hw, DLB_SYS_DIR_POOL_ENBLD(pool->id), r0.val);

	pool->avail_credits = args->num_dir_credits;
	pool->total_credits = args->num_dir_credits;
	domain->dqed_freelist.offset += args->num_dir_credits;

	pool->configured = true;
}

static int
dlb_verify_create_dir_pool_args(struct dlb_hw *hw,
				u32 domain_id,
				struct dlb_create_dir_pool_args *args,
				struct dlb_cmd_response *resp)
{
	struct dlb_freelist *dqed_freelist;
	struct dlb_domain *domain;

	domain = dlb_get_domain_from_id(hw, domain_id);

	if (domain == NULL) {
		resp->status = DLB_ST_INVALID_DOMAIN_ID;
		return -1;
	}

	if (!domain->configured) {
		resp->status = DLB_ST_DOMAIN_NOT_CONFIGURED;
		return -1;
	}

	dqed_freelist = &domain->dqed_freelist;

	if (dlb_freelist_count(dqed_freelist) < args->num_dir_credits) {
		resp->status = DLB_ST_DIR_CREDITS_UNAVAILABLE;
		return -1;
	}

	if (dlb_list_empty(&domain->avail_dir_credit_pools)) {
		resp->status = DLB_ST_DIR_CREDIT_POOLS_UNAVAILABLE;
		return -1;
	}

	if (domain->started) {
		resp->status = DLB_ST_DOMAIN_STARTED;
		return -1;
	}

	return 0;
}

static void
dlb_log_create_dir_pool_args(struct dlb_hw *hw,
			     u32 domain_id,
			     struct dlb_create_dir_pool_args *args)
{
	DLB_HW_INFO(hw, "DLB create directed credit pool arguments:\n");
	DLB_HW_INFO(hw, "\tDomain ID:             %d\n", domain_id);
	DLB_HW_INFO(hw, "\tNumber of DIR credits: %d\n",
		    args->num_dir_credits);
}

/**
 * dlb_hw_create_dir_pool() - Allocate and initialize a DLB credit pool.
 * @hw:	  Contains the current state of the DLB hardware.
 * @args: User-provided arguments.
 * @resp: Response to user.
 *
 * Return: returns < 0 on error, 0 otherwise. If the driver is unable to
 * satisfy a request, resp->status will be set accordingly.
 */
int dlb_hw_create_dir_pool(struct dlb_hw *hw,
			   u32 domain_id,
			   struct dlb_create_dir_pool_args *args,
			   struct dlb_cmd_response *resp)
{
	struct dlb_credit_pool *pool;
	struct dlb_domain *domain;

	dlb_log_create_dir_pool_args(hw, domain_id, args);

	/* Verify that hardware resources are available before attempting to
	 * satisfy the request. This simplifies the error unwinding code.
	 */
	/* At least one available pool */
	if (dlb_verify_create_dir_pool_args(hw, domain_id, args, resp))
		return -EINVAL;

	domain = dlb_get_domain_from_id(hw, domain_id);
	if (domain == NULL) {
		DLB_HW_ERR(hw,
			   "[%s():%d] Internal error: domain not found\n",
			   __func__, __LINE__);
		return -EFAULT;
	}

	pool = DLB_DOM_LIST_HEAD(domain->avail_dir_credit_pools, typeof(*pool));

	/* Verification should catch this. */
	if (pool == NULL) {
		DLB_HW_ERR(hw,
			   "[%s():%d] Internal error: no available dir credit pools\n",
			   __func__, __LINE__);
		return -EFAULT;
	}

	dlb_configure_dir_credit_pool(hw, domain, args, pool);

	/* Configuration succeeded, so move the resource from the 'avail' to
	 * the 'used' list.
	 */
	dlb_list_del(&domain->avail_dir_credit_pools, &pool->domain_list);

	dlb_list_add(&domain->used_dir_credit_pools, &pool->domain_list);

	resp->status = 0;
	resp->id = pool->id;

	return 0;
}

static u32 dlb_ldb_cq_inflight_count(struct dlb_hw *hw,
				     struct dlb_ldb_port *port)
{
	union dlb_lsp_cq_ldb_infl_cnt r0;

	r0.val = DLB_CSR_RD(hw, DLB_LSP_CQ_LDB_INFL_CNT(port->id));

	return r0.field.count;
}

static u32 dlb_ldb_cq_token_count(struct dlb_hw *hw,
				  struct dlb_ldb_port *port)
{
	union dlb_lsp_cq_ldb_tkn_cnt r0;

	r0.val = DLB_CSR_RD(hw, DLB_LSP_CQ_LDB_TKN_CNT(port->id));

	return r0.field.token_count;
}

static int dlb_drain_ldb_cq(struct dlb_hw *hw, struct dlb_ldb_port *port)
{
	u32 infl_cnt, tkn_cnt;
	unsigned int i;

	infl_cnt = dlb_ldb_cq_inflight_count(hw, port);

	/* Account for the initial token count, which is used in order to
	 * provide a CQ with depth less than 8.
	 */
	tkn_cnt = dlb_ldb_cq_token_count(hw, port) - port->init_tkn_cnt;

	if (infl_cnt || tkn_cnt) {
		struct dlb_hcw hcw_mem[8], *hcw;
		void  *pp_addr;

		pp_addr = os_map_producer_port(hw, port->id, true);

		/* Point hcw to a 64B-aligned location */
		hcw = (struct dlb_hcw *)((uintptr_t)&hcw_mem[4] & ~0x3F);

		/* Program the first HCW for a completion and token return and
		 * the other HCWs as NOOPS
		 */

		memset(hcw, 0, 4 * sizeof(*hcw));
		hcw->qe_comp = (infl_cnt > 0);
		hcw->cq_token = (tkn_cnt > 0);
		hcw->lock_id = tkn_cnt - 1;

		/* Return tokens in the first HCW */
		dlb_movdir64b(pp_addr, hcw);

		hcw->cq_token = 0;

		/* Issue remaining completions (if any) */
		for (i = 1; i < infl_cnt; i++)
			dlb_movdir64b(pp_addr, hcw);

		os_fence_hcw(hw, pp_addr);

		os_unmap_producer_port(hw, pp_addr);
	}

	return 0;
}

static int dlb_domain_drain_ldb_cqs(struct dlb_hw *hw,
				    struct dlb_domain *domain,
				    bool toggle_port)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_ldb_port *port;
	int ret;

	/* If the domain hasn't been started, there's no traffic to drain */
	if (!domain->started)
		return 0;

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter) {
		if (toggle_port)
			dlb_ldb_port_cq_disable(hw, port);

		ret = dlb_drain_ldb_cq(hw, port);
		if (ret < 0)
			return ret;

		if (toggle_port)
			dlb_ldb_port_cq_enable(hw, port);
	}

	return 0;
}

static void dlb_domain_disable_ldb_queue_write_perms(struct dlb_hw *hw,
						     struct dlb_domain *domain)
{
	int domain_offset = domain->id * DLB_MAX_NUM_LDB_QUEUES;
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	union dlb_sys_ldb_vasqid_v r0;
	struct dlb_ldb_queue *queue;

	r0.field.vasqid_v = 0;

	DLB_DOM_LIST_FOR(domain->used_ldb_queues, queue, iter) {
		int idx = domain_offset + queue->id;

		DLB_CSR_WR(hw, DLB_SYS_LDB_VASQID_V(idx), r0.val);
	}
}

static void dlb_domain_disable_ldb_seq_checks(struct dlb_hw *hw,
					      struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	union dlb_chp_sn_chk_enbl r1;
	struct dlb_ldb_port *port;

	r1.field.en = 0;

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter)
		DLB_CSR_WR(hw,
			   DLB_CHP_SN_CHK_ENBL(port->id),
			   r1.val);
}

static void dlb_domain_disable_ldb_port_crd_updates(struct dlb_hw *hw,
						    struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	union dlb_chp_ldb_pp_crd_req_state r0;
	struct dlb_ldb_port *port;

	r0.field.no_pp_credit_update = 1;

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter)
		DLB_CSR_WR(hw,
			   DLB_CHP_LDB_PP_CRD_REQ_STATE(port->id),
			   r0.val);
}

static void dlb_domain_disable_ldb_port_interrupts(struct dlb_hw *hw,
						   struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	union dlb_chp_ldb_cq_int_enb r0 = { {0} };
	union dlb_chp_ldb_cq_wd_enb r1 = { {0} };
	struct dlb_ldb_port *port;

	r0.field.en_tim = 0;
	r0.field.en_depth = 0;

	r1.field.wd_enable = 0;

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter) {
		DLB_CSR_WR(hw,
			   DLB_CHP_LDB_CQ_INT_ENB(port->id),
			   r0.val);

		DLB_CSR_WR(hw,
			   DLB_CHP_LDB_CQ_WD_ENB(port->id),
			   r1.val);
	}
}

static void dlb_domain_disable_dir_queue_write_perms(struct dlb_hw *hw,
						     struct dlb_domain *domain)
{
	int domain_offset = domain->id * DLB_MAX_NUM_DIR_PORTS;
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	union dlb_sys_dir_vasqid_v r0;
	struct dlb_dir_pq_pair *port;

	r0.field.vasqid_v = 0;

	DLB_DOM_LIST_FOR(domain->used_dir_pq_pairs, port, iter) {
		int idx = domain_offset + port->id;

		DLB_CSR_WR(hw, DLB_SYS_DIR_VASQID_V(idx), r0.val);
	}
}

static void dlb_domain_disable_dir_port_interrupts(struct dlb_hw *hw,
						   struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	union dlb_chp_dir_cq_int_enb r0 = { {0} };
	union dlb_chp_dir_cq_wd_enb r1 = { {0} };
	struct dlb_dir_pq_pair *port;

	r0.field.en_tim = 0;
	r0.field.en_depth = 0;

	r1.field.wd_enable = 0;

	DLB_DOM_LIST_FOR(domain->used_dir_pq_pairs, port, iter) {
		DLB_CSR_WR(hw,
			   DLB_CHP_DIR_CQ_INT_ENB(port->id),
			   r0.val);

		DLB_CSR_WR(hw,
			   DLB_CHP_DIR_CQ_WD_ENB(port->id),
			   r1.val);
	}
}

static void dlb_domain_disable_dir_port_crd_updates(struct dlb_hw *hw,
						    struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	union dlb_chp_dir_pp_crd_req_state r0;
	struct dlb_dir_pq_pair *port;

	r0.field.no_pp_credit_update = 1;

	DLB_DOM_LIST_FOR(domain->used_dir_pq_pairs, port, iter)
		DLB_CSR_WR(hw,
			   DLB_CHP_DIR_PP_CRD_REQ_STATE(port->id),
			   r0.val);
}

static void dlb_domain_disable_dir_cqs(struct dlb_hw *hw,
				       struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_dir_pq_pair *port;

	DLB_DOM_LIST_FOR(domain->used_dir_pq_pairs, port, iter) {
		port->enabled = false;

		dlb_dir_port_cq_disable(hw, port);
	}
}

static void dlb_domain_disable_ldb_cqs(struct dlb_hw *hw,
				       struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_ldb_port *port;

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter) {
		port->enabled = false;

		dlb_ldb_port_cq_disable(hw, port);
	}
}

static void dlb_domain_enable_ldb_cqs(struct dlb_hw *hw,
				      struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_ldb_port *port;

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter) {
		port->enabled = true;

		dlb_ldb_port_cq_enable(hw, port);
	}
}

static struct dlb_ldb_queue *dlb_get_ldb_queue_from_id(struct dlb_hw *hw,
						       u32 id)
{
	if (id >= DLB_MAX_NUM_LDB_QUEUES)
		return NULL;

	return &hw->rsrcs.ldb_queues[id];
}

static void dlb_ldb_port_clear_has_work_bits(struct dlb_hw *hw,
					     struct dlb_ldb_port *port,
					     u8 slot)
{
	union dlb_lsp_ldb_sched_ctrl r2 = { {0} };

	r2.field.cq = port->id;
	r2.field.qidix = slot;
	r2.field.value = 0;
	r2.field.rlist_haswork_v = 1;

	DLB_CSR_WR(hw, DLB_LSP_LDB_SCHED_CTRL, r2.val);

	memset(&r2, 0, sizeof(r2));

	r2.field.cq = port->id;
	r2.field.qidix = slot;
	r2.field.value = 0;
	r2.field.nalb_haswork_v = 1;

	DLB_CSR_WR(hw, DLB_LSP_LDB_SCHED_CTRL, r2.val);

	dlb_flush_csr(hw);
}

static void dlb_domain_finish_map_port(struct dlb_hw *hw,
				       struct dlb_domain *domain,
				       struct dlb_ldb_port *port)
{
	int i;

	for (i = 0; i < DLB_MAX_NUM_QIDS_PER_LDB_CQ; i++) {
		union dlb_lsp_qid_ldb_infl_cnt r0;
		struct dlb_ldb_queue *queue;
		int qid;

		if (port->qid_map[i].state != DLB_QUEUE_MAP_IN_PROGRESS)
			continue;

		qid = port->qid_map[i].qid;

		queue = dlb_get_ldb_queue_from_id(hw, qid);

		if (queue == NULL) {
			DLB_HW_ERR(hw,
				   "[%s()] Internal error: unable to find queue %d\n",
				   __func__, qid);
			continue;
		}

		r0.val = DLB_CSR_RD(hw, DLB_LSP_QID_LDB_INFL_CNT(qid));

		if (r0.field.count)
			continue;

		/* Disable the affected CQ, and the CQs already mapped to the
		 * QID, before reading the QID's inflight count a second time.
		 * There is an unlikely race in which the QID may schedule one
		 * more QE after we read an inflight count of 0, and disabling
		 * the CQs guarantees that the race will not occur after a
		 * re-read of the inflight count register.
		 */
		if (port->enabled)
			dlb_ldb_port_cq_disable(hw, port);

		dlb_ldb_queue_disable_mapped_cqs(hw, domain, queue);

		r0.val = DLB_CSR_RD(hw, DLB_LSP_QID_LDB_INFL_CNT(qid));

		if (r0.field.count) {
			if (port->enabled)
				dlb_ldb_port_cq_enable(hw, port);

			dlb_ldb_queue_enable_mapped_cqs(hw, domain, queue);

			continue;
		}

		dlb_ldb_port_finish_map_qid_dynamic(hw, domain, port, queue);
	}
}

static unsigned int
dlb_domain_finish_map_qid_procedures(struct dlb_hw *hw,
				     struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_ldb_port *port;

	if (!domain->configured || domain->num_pending_additions == 0)
		return 0;

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter)
		dlb_domain_finish_map_port(hw, domain, port);

	return domain->num_pending_additions;
}

unsigned int dlb_finish_map_qid_procedures(struct dlb_hw *hw)
{
	int i, num = 0;

	/* Finish queue map jobs for any domain that needs it */
	for (i = 0; i < DLB_MAX_NUM_DOMAINS; i++) {
		struct dlb_domain *domain = &hw->domains[i];

		num += dlb_domain_finish_map_qid_procedures(hw, domain);
	}

	return num;
}


static int dlb_domain_wait_for_ldb_cqs_to_empty(struct dlb_hw *hw,
						struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_ldb_port *port;

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter) {
		int i;

		for (i = 0; i < DLB_MAX_CQ_COMP_CHECK_LOOPS; i++) {
			if (dlb_ldb_cq_inflight_count(hw, port) == 0)
				break;
		}

		if (i == DLB_MAX_CQ_COMP_CHECK_LOOPS) {
			DLB_HW_ERR(hw,
				   "[%s()] Internal error: failed to flush load-balanced port %d's completions.\n",
				   __func__, port->id);
			return -EFAULT;
		}
	}

	return 0;
}


static void dlb_domain_finish_unmap_port_slot(struct dlb_hw *hw,
					      struct dlb_domain *domain,
					      struct dlb_ldb_port *port,
					      int slot)
{
	enum dlb_qid_map_state state;
	struct dlb_ldb_queue *queue;

	queue = &hw->rsrcs.ldb_queues[port->qid_map[slot].qid];

	state = port->qid_map[slot].state;

	/* Update the QID2CQIDX and CQ2QID vectors */
	dlb_ldb_port_unmap_qid(hw, port, queue);

	/* Ensure the QID will not be serviced by this {CQ, slot} by clearing
	 * the has_work bits
	 */
	dlb_ldb_port_clear_has_work_bits(hw, port, slot);

	/* Reset the {CQ, slot} to its default state */
	dlb_ldb_port_set_queue_if_status(hw, port, slot);

	/* Re-enable the CQ if it was not manually disabled by the user */
	if (port->enabled)
		dlb_ldb_port_cq_enable(hw, port);

	/* If there is a mapping that is pending this slot's removal, perform
	 * the mapping now.
	 */
	if (state == DLB_QUEUE_UNMAP_IN_PROGRESS_PENDING_MAP) {
		struct dlb_ldb_port_qid_map *map;
		struct dlb_ldb_queue *map_queue;
		u8 prio;

		map = &port->qid_map[slot];

		map->qid = map->pending_qid;
		map->priority = map->pending_priority;

		map_queue = &hw->rsrcs.ldb_queues[map->qid];
		prio = map->priority;

		dlb_ldb_port_map_qid(hw, domain, port, map_queue, prio);
	}
}

static bool dlb_domain_finish_unmap_port(struct dlb_hw *hw,
					 struct dlb_domain *domain,
					 struct dlb_ldb_port *port)
{
	union dlb_lsp_cq_ldb_infl_cnt r0;
	int i;

	if (port->num_pending_removals == 0)
		return false;

	/* The unmap requires all the CQ's outstanding inflights to be
	 * completed.
	 */
	r0.val = DLB_CSR_RD(hw, DLB_LSP_CQ_LDB_INFL_CNT(port->id));
	if (r0.field.count > 0)
		return false;

	for (i = 0; i < DLB_MAX_NUM_QIDS_PER_LDB_CQ; i++) {
		struct dlb_ldb_port_qid_map *map;

		map = &port->qid_map[i];

		if (map->state != DLB_QUEUE_UNMAP_IN_PROGRESS &&
		    map->state != DLB_QUEUE_UNMAP_IN_PROGRESS_PENDING_MAP)
			continue;

		dlb_domain_finish_unmap_port_slot(hw, domain, port, i);
	}

	return true;
}

static unsigned int
dlb_domain_finish_unmap_qid_procedures(struct dlb_hw *hw,
				       struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_ldb_port *port;

	if (!domain->configured || domain->num_pending_removals == 0)
		return 0;

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter)
		dlb_domain_finish_unmap_port(hw, domain, port);

	return domain->num_pending_removals;
}

unsigned int dlb_finish_unmap_qid_procedures(struct dlb_hw *hw)
{
	int i, num = 0;

	/* Finish queue unmap jobs for any domain that needs it */
	for (i = 0; i < DLB_MAX_NUM_DOMAINS; i++) {
		struct dlb_domain *domain = &hw->domains[i];

		num += dlb_domain_finish_unmap_qid_procedures(hw, domain);
	}

	return num;
}

/* Returns whether the queue is empty, including its inflight and replay
 * counts.
 */
static bool dlb_ldb_queue_is_empty(struct dlb_hw *hw,
				   struct dlb_ldb_queue *queue)
{
	union dlb_lsp_qid_ldb_replay_cnt r0;
	union dlb_lsp_qid_aqed_active_cnt r1;
	union dlb_lsp_qid_atq_enqueue_cnt r2;
	union dlb_lsp_qid_ldb_enqueue_cnt r3;
	union dlb_lsp_qid_ldb_infl_cnt r4;

	r0.val = DLB_CSR_RD(hw,
			    DLB_LSP_QID_LDB_REPLAY_CNT(queue->id));
	if (r0.val)
		return false;

	r1.val = DLB_CSR_RD(hw,
			    DLB_LSP_QID_AQED_ACTIVE_CNT(queue->id));
	if (r1.val)
		return false;

	r2.val = DLB_CSR_RD(hw,
			    DLB_LSP_QID_ATQ_ENQUEUE_CNT(queue->id));
	if (r2.val)
		return false;

	r3.val = DLB_CSR_RD(hw,
			    DLB_LSP_QID_LDB_ENQUEUE_CNT(queue->id));
	if (r3.val)
		return false;

	r4.val = DLB_CSR_RD(hw,
			    DLB_LSP_QID_LDB_INFL_CNT(queue->id));
	if (r4.val)
		return false;

	return true;
}

static bool dlb_domain_mapped_queues_empty(struct dlb_hw *hw,
					   struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_ldb_queue *queue;

	DLB_DOM_LIST_FOR(domain->used_ldb_queues, queue, iter) {
		if (queue->num_mappings == 0)
			continue;

		if (!dlb_ldb_queue_is_empty(hw, queue))
			return false;
	}

	return true;
}

static int dlb_domain_drain_mapped_queues(struct dlb_hw *hw,
					  struct dlb_domain *domain)
{
	int i, ret;

	/* If the domain hasn't been started, there's no traffic to drain */
	if (!domain->started)
		return 0;

	if (domain->num_pending_removals > 0) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: failed to unmap domain queues\n",
			   __func__);
		return -EFAULT;
	}

	for (i = 0; i < DLB_MAX_QID_EMPTY_CHECK_LOOPS; i++) {
		ret = dlb_domain_drain_ldb_cqs(hw, domain, true);
		if (ret < 0)
			return ret;

		if (dlb_domain_mapped_queues_empty(hw, domain))
			break;
	}

	if (i == DLB_MAX_QID_EMPTY_CHECK_LOOPS) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: failed to empty queues\n",
			   __func__);
		return -EFAULT;
	}

	/* Drain the CQs one more time. For the queues to go empty, they would
	 * have scheduled one or more QEs.
	 */
	ret = dlb_domain_drain_ldb_cqs(hw, domain, true);
	if (ret < 0)
		return ret;

	return 0;
}

static int dlb_domain_drain_unmapped_queue(struct dlb_hw *hw,
					   struct dlb_domain *domain,
					   struct dlb_ldb_queue *queue)
{
	struct dlb_ldb_port *port;
	int ret;

	/* If a domain has LDB queues, it must have LDB ports */
	if (dlb_list_empty(&domain->used_ldb_ports)) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: No configured LDB ports\n",
			   __func__);
		return -EFAULT;
	}

	port = DLB_DOM_LIST_HEAD(domain->used_ldb_ports, typeof(*port));

	/* If necessary, free up a QID slot in this CQ */
	if (port->num_mappings == DLB_MAX_NUM_QIDS_PER_LDB_CQ) {
		struct dlb_ldb_queue *mapped_queue;

		mapped_queue = &hw->rsrcs.ldb_queues[port->qid_map[0].qid];

		ret = dlb_ldb_port_unmap_qid(hw, port, mapped_queue);
		if (ret)
			return ret;
	}

	ret = dlb_ldb_port_map_qid_dynamic(hw, port, queue, 0);
	if (ret)
		return ret;

	return dlb_domain_drain_mapped_queues(hw, domain);
}

static int dlb_domain_drain_unmapped_queues(struct dlb_hw *hw,
					    struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_ldb_queue *queue;
	int ret;

	/* If the domain hasn't been started, there's no traffic to drain */
	if (!domain->started)
		return 0;

	DLB_DOM_LIST_FOR(domain->used_ldb_queues, queue, iter) {
		if (queue->num_mappings != 0 ||
		    dlb_ldb_queue_is_empty(hw, queue))
			continue;

		ret = dlb_domain_drain_unmapped_queue(hw, domain, queue);
		if (ret)
			return ret;
	}

	return 0;
}

static int dlb_domain_wait_for_ldb_pool_refill(struct dlb_hw *hw,
					       struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_credit_pool *pool;

	/* Confirm that all credits are returned to the domain's credit pools */
	DLB_DOM_LIST_FOR(domain->used_ldb_credit_pools, pool, iter) {
		union dlb_chp_qed_fl_push_ptr r0;
		union dlb_chp_qed_fl_pop_ptr r1;
		unsigned long pop_offs, push_offs;
		int i;

		push_offs = DLB_CHP_QED_FL_PUSH_PTR(pool->id);
		pop_offs = DLB_CHP_QED_FL_POP_PTR(pool->id);

		for (i = 0; i < DLB_MAX_QID_EMPTY_CHECK_LOOPS; i++) {
			r0.val = DLB_CSR_RD(hw, push_offs);

			r1.val = DLB_CSR_RD(hw, pop_offs);

			/* Break early if the freelist is replenished */
			if (r1.field.pop_ptr == r0.field.push_ptr &&
			    r1.field.generation != r0.field.generation) {
				break;
			}
		}

		/* Error if the freelist is not full */
		if (r1.field.pop_ptr != r0.field.push_ptr ||
		    r1.field.generation == r0.field.generation) {
			return -EFAULT;
		}
	}

	return 0;
}

static int dlb_domain_wait_for_dir_pool_refill(struct dlb_hw *hw,
					       struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_credit_pool *pool;

	/* Confirm that all credits are returned to the domain's credit pools */
	DLB_DOM_LIST_FOR(domain->used_dir_credit_pools, pool, iter) {
		union dlb_chp_dqed_fl_push_ptr r0;
		union dlb_chp_dqed_fl_pop_ptr r1;
		unsigned long pop_offs, push_offs;
		int i;

		push_offs = DLB_CHP_DQED_FL_PUSH_PTR(pool->id);
		pop_offs = DLB_CHP_DQED_FL_POP_PTR(pool->id);

		for (i = 0; i < DLB_MAX_QID_EMPTY_CHECK_LOOPS; i++) {
			r0.val = DLB_CSR_RD(hw, push_offs);

			r1.val = DLB_CSR_RD(hw, pop_offs);

			/* Break early if the freelist is replenished */
			if (r1.field.pop_ptr == r0.field.push_ptr &&
			    r1.field.generation != r0.field.generation) {
				break;
			}
		}

		/* Error if the freelist is not full */
		if (r1.field.pop_ptr != r0.field.push_ptr ||
		    r1.field.generation == r0.field.generation) {
			return -EFAULT;
		}
	}

	return 0;
}

static u32 dlb_dir_queue_depth(struct dlb_hw *hw,
			       struct dlb_dir_pq_pair *queue)
{
	union dlb_lsp_qid_dir_enqueue_cnt r0;

	r0.val = DLB_CSR_RD(hw, DLB_LSP_QID_DIR_ENQUEUE_CNT(queue->id));

	return r0.field.count;
}

static bool dlb_dir_queue_is_empty(struct dlb_hw *hw,
				   struct dlb_dir_pq_pair *queue)
{
	return dlb_dir_queue_depth(hw, queue) == 0;
}

static bool dlb_domain_dir_queues_empty(struct dlb_hw *hw,
					struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_dir_pq_pair *queue;

	DLB_DOM_LIST_FOR(domain->used_dir_pq_pairs, queue, iter) {
		if (!dlb_dir_queue_is_empty(hw, queue))
			return false;
	}

	return true;
}

static u32 dlb_dir_cq_token_count(struct dlb_hw *hw,
				  struct dlb_dir_pq_pair *port)
{
	union dlb_lsp_cq_dir_tkn_cnt r0;

	r0.val = DLB_CSR_RD(hw, DLB_LSP_CQ_DIR_TKN_CNT(port->id));

	return r0.field.count;
}

static void dlb_drain_dir_cq(struct dlb_hw *hw, struct dlb_dir_pq_pair *port)
{
	unsigned int port_id = port->id;
	u32 cnt;

	/* Return any outstanding tokens */
	cnt = dlb_dir_cq_token_count(hw, port);

	if (cnt != 0) {
		struct dlb_hcw hcw_mem[8], *hcw;
		void  *pp_addr;

		pp_addr = os_map_producer_port(hw, port_id, false);

		/* Point hcw to a 64B-aligned location */
		hcw = (struct dlb_hcw *)((uintptr_t)&hcw_mem[4] & ~0x3F);

		/* Program the first HCW for a batch token return and
		 * the rest as NOOPS
		 */
		memset(hcw, 0, 4 * sizeof(*hcw));
		hcw->cq_token = 1;
		hcw->lock_id = cnt - 1;

		dlb_movdir64b(pp_addr, hcw);

		os_fence_hcw(hw, pp_addr);

		os_unmap_producer_port(hw, pp_addr);
	}
}

static int dlb_domain_drain_dir_cqs(struct dlb_hw *hw,
				    struct dlb_domain *domain,
				    bool toggle_port)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_dir_pq_pair *port;

	DLB_DOM_LIST_FOR(domain->used_dir_pq_pairs, port, iter) {
		/* Can't drain a port if it's not configured, and there's
		 * nothing to drain if its queue is unconfigured.
		 */
		if (!port->port_configured || !port->queue_configured)
			continue;

		if (toggle_port)
			dlb_dir_port_cq_disable(hw, port);

		dlb_drain_dir_cq(hw, port);

		if (toggle_port)
			dlb_dir_port_cq_enable(hw, port);
	}

	return 0;
}

static int dlb_domain_drain_dir_queues(struct dlb_hw *hw,
				       struct dlb_domain *domain)
{
	int i;

	/* If the domain hasn't been started, there's no traffic to drain */
	if (!domain->started)
		return 0;

	for (i = 0; i < DLB_MAX_QID_EMPTY_CHECK_LOOPS; i++) {
		dlb_domain_drain_dir_cqs(hw, domain, true);

		if (dlb_domain_dir_queues_empty(hw, domain))
			break;
	}

	if (i == DLB_MAX_QID_EMPTY_CHECK_LOOPS) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: failed to empty queues\n",
			   __func__);
		return -EFAULT;
	}

	/* Drain the CQs one more time. For the queues to go empty, they would
	 * have scheduled one or more QEs.
	 */
	dlb_domain_drain_dir_cqs(hw, domain, true);

	return 0;
}

static void dlb_domain_disable_dir_producer_ports(struct dlb_hw *hw,
						  struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_dir_pq_pair *port;
	union dlb_sys_dir_pp_v r1;

	r1.field.pp_v = 0;

	DLB_DOM_LIST_FOR(domain->used_dir_pq_pairs, port, iter)
		DLB_CSR_WR(hw,
			   DLB_SYS_DIR_PP_V(port->id),
			   r1.val);
}

static void dlb_domain_disable_ldb_producer_ports(struct dlb_hw *hw,
						  struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	union dlb_sys_ldb_pp_v r1;
	struct dlb_ldb_port *port;

	r1.field.pp_v = 0;

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter) {
		DLB_CSR_WR(hw,
			   DLB_SYS_LDB_PP_V(port->id),
			   r1.val);

		hw->pf.num_enabled_ldb_ports--;
	}
}

static void dlb_domain_disable_dir_pools(struct dlb_hw *hw,
					 struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	union dlb_sys_dir_pool_enbld r0 = { {0} };
	struct dlb_credit_pool *pool;

	DLB_DOM_LIST_FOR(domain->used_dir_credit_pools, pool, iter)
		DLB_CSR_WR(hw,
			   DLB_SYS_DIR_POOL_ENBLD(pool->id),
			   r0.val);
}

static void dlb_domain_disable_ldb_pools(struct dlb_hw *hw,
					 struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	union dlb_sys_ldb_pool_enbld r0 = { {0} };
	struct dlb_credit_pool *pool;

	DLB_DOM_LIST_FOR(domain->used_ldb_credit_pools, pool, iter)
		DLB_CSR_WR(hw,
			   DLB_SYS_LDB_POOL_ENBLD(pool->id),
			   r0.val);
}

static int dlb_reset_hw_resource(struct dlb_hw *hw, int type, int id)
{
	union dlb_cfg_mstr_diag_reset_sts r0 = { {0} };
	union dlb_cfg_mstr_bcast_reset_vf_start r1 = { {0} };
	int i;

	r1.field.vf_reset_start = 1;

	r1.field.vf_reset_type = type;
	r1.field.vf_reset_id = id;

	DLB_CSR_WR(hw, DLB_CFG_MSTR_BCAST_RESET_VF_START, r1.val);

	/* Wait for hardware to complete. This is a finite time operation,
	 * but wait set a loop bound just in case.
	 */
	for (i = 0; i < 1024 * 1024; i++) {
		r0.val = DLB_CSR_RD(hw, DLB_CFG_MSTR_DIAG_RESET_STS);

		if (r0.field.chp_vf_reset_done &&
		    r0.field.rop_vf_reset_done &&
		    r0.field.lsp_vf_reset_done &&
		    r0.field.nalb_vf_reset_done &&
		    r0.field.ap_vf_reset_done &&
		    r0.field.dp_vf_reset_done &&
		    r0.field.qed_vf_reset_done &&
		    r0.field.dqed_vf_reset_done &&
		    r0.field.aqed_vf_reset_done)
			return 0;

		os_udelay(1);
	}

	return -ETIMEDOUT;
}

static int dlb_domain_reset_hw_resources(struct dlb_hw *hw,
					 struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_dir_pq_pair *dir_port;
	struct dlb_ldb_queue *ldb_queue;
	struct dlb_ldb_port *ldb_port;
	struct dlb_credit_pool *pool;
	int ret;

	DLB_DOM_LIST_FOR(domain->used_ldb_credit_pools, pool, iter) {
		ret = dlb_reset_hw_resource(hw,
					    VF_RST_TYPE_POOL_LDB,
					    pool->id);
		if (ret)
			return ret;
	}

	DLB_DOM_LIST_FOR(domain->used_dir_credit_pools, pool, iter) {
		ret = dlb_reset_hw_resource(hw,
					    VF_RST_TYPE_POOL_DIR,
					    pool->id);
		if (ret)
			return ret;
	}

	DLB_DOM_LIST_FOR(domain->used_ldb_queues, ldb_queue, iter) {
		ret = dlb_reset_hw_resource(hw,
					    VF_RST_TYPE_QID_LDB,
					    ldb_queue->id);
		if (ret)
			return ret;
	}

	DLB_DOM_LIST_FOR(domain->used_dir_pq_pairs, dir_port, iter) {
		ret = dlb_reset_hw_resource(hw,
					    VF_RST_TYPE_QID_DIR,
					    dir_port->id);
		if (ret)
			return ret;
	}

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, ldb_port, iter) {
		ret = dlb_reset_hw_resource(hw,
					    VF_RST_TYPE_CQ_LDB,
					    ldb_port->id);
		if (ret)
			return ret;
	}

	DLB_DOM_LIST_FOR(domain->used_dir_pq_pairs, dir_port, iter) {
		ret = dlb_reset_hw_resource(hw,
					    VF_RST_TYPE_CQ_DIR,
					    dir_port->id);
		if (ret)
			return ret;
	}

	return 0;
}

static int dlb_domain_verify_reset_success(struct dlb_hw *hw,
					   struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_dir_pq_pair *dir_port;
	struct dlb_ldb_port *ldb_port;
	struct dlb_credit_pool *pool;
	struct dlb_ldb_queue *queue;

	/* Confirm that all credits are returned to the domain's credit pools */
	DLB_DOM_LIST_FOR(domain->used_dir_credit_pools, pool, iter) {
		union dlb_chp_dqed_fl_pop_ptr r0;
		union dlb_chp_dqed_fl_push_ptr r1;

		r0.val = DLB_CSR_RD(hw,
				    DLB_CHP_DQED_FL_POP_PTR(pool->id));

		r1.val = DLB_CSR_RD(hw,
				    DLB_CHP_DQED_FL_PUSH_PTR(pool->id));

		if (r0.field.pop_ptr != r1.field.push_ptr ||
		    r0.field.generation == r1.field.generation) {
			DLB_HW_ERR(hw,
				   "[%s()] Internal error: failed to refill directed pool %d's credits.\n",
				   __func__, pool->id);
			return -EFAULT;
		}
	}

	/* Confirm that all the domain's queue's inflight counts and AQED
	 * active counts are 0.
	 */
	DLB_DOM_LIST_FOR(domain->used_ldb_queues, queue, iter) {
		if (!dlb_ldb_queue_is_empty(hw, queue)) {
			DLB_HW_ERR(hw,
				   "[%s()] Internal error: failed to empty ldb queue %d\n",
				   __func__, queue->id);
			return -EFAULT;
		}
	}

	/* Confirm that all the domain's CQs inflight and token counts are 0. */
	DLB_DOM_LIST_FOR(domain->used_ldb_ports, ldb_port, iter) {
		if (dlb_ldb_cq_inflight_count(hw, ldb_port) ||
		    dlb_ldb_cq_token_count(hw, ldb_port)) {
			DLB_HW_ERR(hw,
				   "[%s()] Internal error: failed to empty ldb port %d\n",
				   __func__, ldb_port->id);
			return -EFAULT;
		}
	}

	DLB_DOM_LIST_FOR(domain->used_dir_pq_pairs, dir_port, iter) {
		if (!dlb_dir_queue_is_empty(hw, dir_port)) {
			DLB_HW_ERR(hw,
				   "[%s()] Internal error: failed to empty dir queue %d\n",
				   __func__, dir_port->id);
			return -EFAULT;
		}

		if (dlb_dir_cq_token_count(hw, dir_port)) {
			DLB_HW_ERR(hw,
				   "[%s()] Internal error: failed to empty dir port %d\n",
				   __func__, dir_port->id);
			return -EFAULT;
		}
	}

	return 0;
}

static void __dlb_domain_reset_ldb_port_registers(struct dlb_hw *hw,
						  struct dlb_ldb_port *port)
{
	union dlb_chp_ldb_pp_state_reset r0 = { {0} };

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_PP_CRD_REQ_STATE(port->id),
		   DLB_CHP_LDB_PP_CRD_REQ_STATE_RST);

	/* Reset the port's load-balanced and directed credit state */
	r0.field.dir_type = 0;
	r0.field.reset_pp_state = 1;

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_PP_STATE_RESET(port->id),
		   r0.val);

	r0.field.dir_type = 1;
	r0.field.reset_pp_state = 1;

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_PP_STATE_RESET(port->id),
		   r0.val);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_PP_DIR_PUSH_PTR(port->id),
		   DLB_CHP_LDB_PP_DIR_PUSH_PTR_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_PP_LDB_PUSH_PTR(port->id),
		   DLB_CHP_LDB_PP_LDB_PUSH_PTR_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_PP_LDB_MIN_CRD_QNT(port->id),
		   DLB_CHP_LDB_PP_LDB_MIN_CRD_QNT_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_PP_LDB_CRD_LWM(port->id),
		   DLB_CHP_LDB_PP_LDB_CRD_LWM_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_PP_LDB_CRD_HWM(port->id),
		   DLB_CHP_LDB_PP_LDB_CRD_HWM_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_LDB_PP2POOL(port->id),
		   DLB_CHP_LDB_LDB_PP2POOL_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_PP_DIR_MIN_CRD_QNT(port->id),
		   DLB_CHP_LDB_PP_DIR_MIN_CRD_QNT_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_PP_DIR_CRD_LWM(port->id),
		   DLB_CHP_LDB_PP_DIR_CRD_LWM_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_PP_DIR_CRD_HWM(port->id),
		   DLB_CHP_LDB_PP_DIR_CRD_HWM_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_DIR_PP2POOL(port->id),
		   DLB_CHP_LDB_DIR_PP2POOL_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_LDB_PP2LDBPOOL(port->id),
		   DLB_SYS_LDB_PP2LDBPOOL_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_LDB_PP2DIRPOOL(port->id),
		   DLB_SYS_LDB_PP2DIRPOOL_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_HIST_LIST_LIM(port->id),
		   DLB_CHP_HIST_LIST_LIM_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_HIST_LIST_BASE(port->id),
		   DLB_CHP_HIST_LIST_BASE_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_HIST_LIST_POP_PTR(port->id),
		   DLB_CHP_HIST_LIST_POP_PTR_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_HIST_LIST_PUSH_PTR(port->id),
		   DLB_CHP_HIST_LIST_PUSH_PTR_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_CQ_WPTR(port->id),
		   DLB_CHP_LDB_CQ_WPTR_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_CQ_INT_DEPTH_THRSH(port->id),
		   DLB_CHP_LDB_CQ_INT_DEPTH_THRSH_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_CQ_TMR_THRESHOLD(port->id),
		   DLB_CHP_LDB_CQ_TMR_THRESHOLD_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_CQ_INT_ENB(port->id),
		   DLB_CHP_LDB_CQ_INT_ENB_RST);

	DLB_CSR_WR(hw,
		   DLB_LSP_CQ_LDB_INFL_LIM(port->id),
		   DLB_LSP_CQ_LDB_INFL_LIM_RST);

	DLB_CSR_WR(hw,
		   DLB_LSP_CQ2PRIOV(port->id),
		   DLB_LSP_CQ2PRIOV_RST);

	DLB_CSR_WR(hw,
		   DLB_LSP_CQ_LDB_TOT_SCH_CNT_CTRL(port->id),
		   DLB_LSP_CQ_LDB_TOT_SCH_CNT_CTRL_RST);

	DLB_CSR_WR(hw,
		   DLB_LSP_CQ_LDB_TKN_DEPTH_SEL(port->id),
		   DLB_LSP_CQ_LDB_TKN_DEPTH_SEL_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_LDB_CQ_TKN_DEPTH_SEL(port->id),
		   DLB_CHP_LDB_CQ_TKN_DEPTH_SEL_RST);

	DLB_CSR_WR(hw,
		   DLB_LSP_CQ_LDB_DSBL(port->id),
		   DLB_LSP_CQ_LDB_DSBL_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_LDB_CQ2VF_PF(port->id),
		   DLB_SYS_LDB_CQ2VF_PF_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_LDB_PP2VF_PF(port->id),
		   DLB_SYS_LDB_PP2VF_PF_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_LDB_CQ_ADDR_L(port->id),
		   DLB_SYS_LDB_CQ_ADDR_L_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_LDB_CQ_ADDR_U(port->id),
		   DLB_SYS_LDB_CQ_ADDR_U_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_LDB_PP_ADDR_L(port->id),
		   DLB_SYS_LDB_PP_ADDR_L_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_LDB_PP_ADDR_U(port->id),
		   DLB_SYS_LDB_PP_ADDR_U_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_LDB_PP_V(port->id),
		   DLB_SYS_LDB_PP_V_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_LDB_PP2VAS(port->id),
		   DLB_SYS_LDB_PP2VAS_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_LDB_CQ_ISR(port->id),
		   DLB_SYS_LDB_CQ_ISR_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_WBUF_LDB_FLAGS(port->id),
		   DLB_SYS_WBUF_LDB_FLAGS_RST);
}

static void __dlb_domain_reset_dir_port_registers(struct dlb_hw *hw,
						  struct dlb_dir_pq_pair *port)
{
	union dlb_chp_dir_pp_state_reset r0 = { {0} };

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_PP_CRD_REQ_STATE(port->id),
		   DLB_CHP_DIR_PP_CRD_REQ_STATE_RST);

	/* Reset the port's load-balanced and directed credit state */
	r0.field.dir_type = 0;
	r0.field.reset_pp_state = 1;

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_PP_STATE_RESET(port->id),
		   r0.val);

	r0.field.dir_type = 1;
	r0.field.reset_pp_state = 1;

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_PP_STATE_RESET(port->id),
		   r0.val);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_PP_DIR_PUSH_PTR(port->id),
		   DLB_CHP_DIR_PP_DIR_PUSH_PTR_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_PP_LDB_PUSH_PTR(port->id),
		   DLB_CHP_DIR_PP_LDB_PUSH_PTR_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_PP_LDB_MIN_CRD_QNT(port->id),
		   DLB_CHP_DIR_PP_LDB_MIN_CRD_QNT_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_PP_LDB_CRD_LWM(port->id),
		   DLB_CHP_DIR_PP_LDB_CRD_LWM_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_PP_LDB_CRD_HWM(port->id),
		   DLB_CHP_DIR_PP_LDB_CRD_HWM_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_LDB_PP2POOL(port->id),
		   DLB_CHP_DIR_LDB_PP2POOL_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_PP_DIR_MIN_CRD_QNT(port->id),
		   DLB_CHP_DIR_PP_DIR_MIN_CRD_QNT_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_PP_DIR_CRD_LWM(port->id),
		   DLB_CHP_DIR_PP_DIR_CRD_LWM_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_PP_DIR_CRD_HWM(port->id),
		   DLB_CHP_DIR_PP_DIR_CRD_HWM_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_DIR_PP2POOL(port->id),
		   DLB_CHP_DIR_DIR_PP2POOL_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_DIR_PP2LDBPOOL(port->id),
		   DLB_SYS_DIR_PP2LDBPOOL_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_DIR_PP2DIRPOOL(port->id),
		   DLB_SYS_DIR_PP2DIRPOOL_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_CQ_WPTR(port->id),
		   DLB_CHP_DIR_CQ_WPTR_RST);

	DLB_CSR_WR(hw,
		   DLB_LSP_CQ_DIR_TKN_DEPTH_SEL_DSI(port->id),
		   DLB_LSP_CQ_DIR_TKN_DEPTH_SEL_DSI_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_CQ_TKN_DEPTH_SEL(port->id),
		   DLB_CHP_DIR_CQ_TKN_DEPTH_SEL_RST);

	DLB_CSR_WR(hw,
		   DLB_LSP_CQ_DIR_DSBL(port->id),
		   DLB_LSP_CQ_DIR_DSBL_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_CQ_WPTR(port->id),
		   DLB_CHP_DIR_CQ_WPTR_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_CQ_INT_DEPTH_THRSH(port->id),
		   DLB_CHP_DIR_CQ_INT_DEPTH_THRSH_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_CQ_TMR_THRESHOLD(port->id),
		   DLB_CHP_DIR_CQ_TMR_THRESHOLD_RST);

	DLB_CSR_WR(hw,
		   DLB_CHP_DIR_CQ_INT_ENB(port->id),
		   DLB_CHP_DIR_CQ_INT_ENB_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_DIR_CQ2VF_PF(port->id),
		   DLB_SYS_DIR_CQ2VF_PF_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_DIR_PP2VF_PF(port->id),
		   DLB_SYS_DIR_PP2VF_PF_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_DIR_CQ_ADDR_L(port->id),
		   DLB_SYS_DIR_CQ_ADDR_L_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_DIR_CQ_ADDR_U(port->id),
		   DLB_SYS_DIR_CQ_ADDR_U_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_DIR_PP_ADDR_L(port->id),
		   DLB_SYS_DIR_PP_ADDR_L_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_DIR_PP_ADDR_U(port->id),
		   DLB_SYS_DIR_PP_ADDR_U_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_DIR_PP_V(port->id),
		   DLB_SYS_DIR_PP_V_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_DIR_PP2VAS(port->id),
		   DLB_SYS_DIR_PP2VAS_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_DIR_CQ_ISR(port->id),
		   DLB_SYS_DIR_CQ_ISR_RST);

	DLB_CSR_WR(hw,
		   DLB_SYS_WBUF_DIR_FLAGS(port->id),
		   DLB_SYS_WBUF_DIR_FLAGS_RST);
}

static void dlb_domain_reset_dir_port_registers(struct dlb_hw *hw,
						struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_dir_pq_pair *port;

	DLB_DOM_LIST_FOR(domain->used_dir_pq_pairs, port, iter)
		__dlb_domain_reset_dir_port_registers(hw, port);
}

static void dlb_domain_reset_ldb_queue_registers(struct dlb_hw *hw,
						 struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_ldb_queue *queue;

	DLB_DOM_LIST_FOR(domain->used_ldb_queues, queue, iter) {
		DLB_CSR_WR(hw,
			   DLB_AQED_PIPE_FL_LIM(queue->id),
			   DLB_AQED_PIPE_FL_LIM_RST);

		DLB_CSR_WR(hw,
			   DLB_AQED_PIPE_FL_BASE(queue->id),
			   DLB_AQED_PIPE_FL_BASE_RST);

		DLB_CSR_WR(hw,
			   DLB_AQED_PIPE_FL_POP_PTR(queue->id),
			   DLB_AQED_PIPE_FL_POP_PTR_RST);

		DLB_CSR_WR(hw,
			   DLB_AQED_PIPE_FL_PUSH_PTR(queue->id),
			   DLB_AQED_PIPE_FL_PUSH_PTR_RST);

		DLB_CSR_WR(hw,
			   DLB_AQED_PIPE_QID_FID_LIM(queue->id),
			   DLB_AQED_PIPE_QID_FID_LIM_RST);

		DLB_CSR_WR(hw,
			   DLB_LSP_QID_AQED_ACTIVE_LIM(queue->id),
			   DLB_LSP_QID_AQED_ACTIVE_LIM_RST);

		DLB_CSR_WR(hw,
			   DLB_LSP_QID_LDB_INFL_LIM(queue->id),
			   DLB_LSP_QID_LDB_INFL_LIM_RST);

		DLB_CSR_WR(hw,
			   DLB_SYS_LDB_QID_V(queue->id),
			   DLB_SYS_LDB_QID_V_RST);

		DLB_CSR_WR(hw,
			   DLB_SYS_LDB_QID_V(queue->id),
			   DLB_SYS_LDB_QID_V_RST);

		DLB_CSR_WR(hw,
			   DLB_CHP_ORD_QID_SN(queue->id),
			   DLB_CHP_ORD_QID_SN_RST);

		DLB_CSR_WR(hw,
			   DLB_CHP_ORD_QID_SN_MAP(queue->id),
			   DLB_CHP_ORD_QID_SN_MAP_RST);

		DLB_CSR_WR(hw,
			   DLB_RO_PIPE_QID2GRPSLT(queue->id),
			   DLB_RO_PIPE_QID2GRPSLT_RST);
	}
}

static void dlb_domain_reset_dir_queue_registers(struct dlb_hw *hw,
						 struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_dir_pq_pair *queue;

	DLB_DOM_LIST_FOR(domain->used_dir_pq_pairs, queue, iter) {
		DLB_CSR_WR(hw,
			   DLB_SYS_DIR_QID_V(queue->id),
			   DLB_SYS_DIR_QID_V_RST);
	}
}

static void dlb_domain_reset_ldb_pool_registers(struct dlb_hw *hw,
						struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_credit_pool *pool;

	DLB_DOM_LIST_FOR(domain->used_ldb_credit_pools, pool, iter) {
		DLB_CSR_WR(hw,
			   DLB_CHP_LDB_POOL_CRD_LIM(pool->id),
			   DLB_CHP_LDB_POOL_CRD_LIM_RST);

		DLB_CSR_WR(hw,
			   DLB_CHP_LDB_POOL_CRD_CNT(pool->id),
			   DLB_CHP_LDB_POOL_CRD_CNT_RST);

		DLB_CSR_WR(hw,
			   DLB_CHP_QED_FL_BASE(pool->id),
			   DLB_CHP_QED_FL_BASE_RST);

		DLB_CSR_WR(hw,
			   DLB_CHP_QED_FL_LIM(pool->id),
			   DLB_CHP_QED_FL_LIM_RST);

		DLB_CSR_WR(hw,
			   DLB_CHP_QED_FL_PUSH_PTR(pool->id),
			   DLB_CHP_QED_FL_PUSH_PTR_RST);

		DLB_CSR_WR(hw,
			   DLB_CHP_QED_FL_POP_PTR(pool->id),
			   DLB_CHP_QED_FL_POP_PTR_RST);
	}
}

static void dlb_domain_reset_dir_pool_registers(struct dlb_hw *hw,
						struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_credit_pool *pool;

	DLB_DOM_LIST_FOR(domain->used_dir_credit_pools, pool, iter) {
		DLB_CSR_WR(hw,
			   DLB_CHP_DIR_POOL_CRD_LIM(pool->id),
			   DLB_CHP_DIR_POOL_CRD_LIM_RST);

		DLB_CSR_WR(hw,
			   DLB_CHP_DIR_POOL_CRD_CNT(pool->id),
			   DLB_CHP_DIR_POOL_CRD_CNT_RST);

		DLB_CSR_WR(hw,
			   DLB_CHP_DQED_FL_BASE(pool->id),
			   DLB_CHP_DQED_FL_BASE_RST);

		DLB_CSR_WR(hw,
			   DLB_CHP_DQED_FL_LIM(pool->id),
			   DLB_CHP_DQED_FL_LIM_RST);

		DLB_CSR_WR(hw,
			   DLB_CHP_DQED_FL_PUSH_PTR(pool->id),
			   DLB_CHP_DQED_FL_PUSH_PTR_RST);

		DLB_CSR_WR(hw,
			   DLB_CHP_DQED_FL_POP_PTR(pool->id),
			   DLB_CHP_DQED_FL_POP_PTR_RST);
	}
}

static void dlb_domain_reset_ldb_port_registers(struct dlb_hw *hw,
						struct dlb_domain *domain)
{
	struct dlb_list_entry *iter;
	RTE_SET_USED(iter);
	struct dlb_ldb_port *port;

	DLB_DOM_LIST_FOR(domain->used_ldb_ports, port, iter)
		__dlb_domain_reset_ldb_port_registers(hw, port);
}

static void dlb_domain_reset_registers(struct dlb_hw *hw,
				       struct dlb_domain *domain)
{
	dlb_domain_reset_ldb_port_registers(hw, domain);

	dlb_domain_reset_dir_port_registers(hw, domain);

	dlb_domain_reset_ldb_queue_registers(hw, domain);

	dlb_domain_reset_dir_queue_registers(hw, domain);

	dlb_domain_reset_ldb_pool_registers(hw, domain);

	dlb_domain_reset_dir_pool_registers(hw, domain);
}

static int dlb_domain_reset_software_state(struct dlb_hw *hw,
					   struct dlb_domain *domain)
{
	struct dlb_ldb_queue *tmp_ldb_queue;
	RTE_SET_USED(tmp_ldb_queue);
	struct dlb_dir_pq_pair *tmp_dir_port;
	RTE_SET_USED(tmp_dir_port);
	struct dlb_ldb_port *tmp_ldb_port;
	RTE_SET_USED(tmp_ldb_port);
	struct dlb_credit_pool *tmp_pool;
	RTE_SET_USED(tmp_pool);
	struct dlb_list_entry *iter1;
	RTE_SET_USED(iter1);
	struct dlb_list_entry *iter2;
	RTE_SET_USED(iter2);
	struct dlb_ldb_queue *ldb_queue;
	struct dlb_dir_pq_pair *dir_port;
	struct dlb_ldb_port *ldb_port;
	struct dlb_credit_pool *pool;

	struct dlb_function_resources *rsrcs;
	struct dlb_list_head *list;
	int ret;

	rsrcs = domain->parent_func;

	/* Move the domain's ldb queues to the function's avail list */
	list = &domain->used_ldb_queues;
	DLB_DOM_LIST_FOR_SAFE(*list, ldb_queue, tmp_ldb_queue, iter1, iter2) {
		if (ldb_queue->sn_cfg_valid) {
			struct dlb_sn_group *grp;

			grp = &hw->rsrcs.sn_groups[ldb_queue->sn_group];

			dlb_sn_group_free_slot(grp, ldb_queue->sn_slot);
			ldb_queue->sn_cfg_valid = false;
		}

		ldb_queue->owned = false;
		ldb_queue->num_mappings = 0;
		ldb_queue->num_pending_additions = 0;

		dlb_list_del(&domain->used_ldb_queues, &ldb_queue->domain_list);
		dlb_list_add(&rsrcs->avail_ldb_queues, &ldb_queue->func_list);
		rsrcs->num_avail_ldb_queues++;
	}

	list = &domain->avail_ldb_queues;
	DLB_DOM_LIST_FOR_SAFE(*list, ldb_queue, tmp_ldb_queue, iter1, iter2) {
		ldb_queue->owned = false;

		dlb_list_del(&domain->avail_ldb_queues,
			     &ldb_queue->domain_list);
		dlb_list_add(&rsrcs->avail_ldb_queues,
			     &ldb_queue->func_list);
		rsrcs->num_avail_ldb_queues++;
	}

	/* Move the domain's ldb ports to the function's avail list */
	list = &domain->used_ldb_ports;
	DLB_DOM_LIST_FOR_SAFE(*list, ldb_port, tmp_ldb_port, iter1, iter2) {
		int i;

		ldb_port->owned = false;
		ldb_port->configured = false;
		ldb_port->num_pending_removals = 0;
		ldb_port->num_mappings = 0;
		for (i = 0; i < DLB_MAX_NUM_QIDS_PER_LDB_CQ; i++)
			ldb_port->qid_map[i].state = DLB_QUEUE_UNMAPPED;

		dlb_list_del(&domain->used_ldb_ports, &ldb_port->domain_list);
		dlb_list_add(&rsrcs->avail_ldb_ports, &ldb_port->func_list);
		rsrcs->num_avail_ldb_ports++;
	}

	list = &domain->avail_ldb_ports;
	DLB_DOM_LIST_FOR_SAFE(*list, ldb_port, tmp_ldb_port, iter1, iter2) {
		ldb_port->owned = false;

		dlb_list_del(&domain->avail_ldb_ports, &ldb_port->domain_list);
		dlb_list_add(&rsrcs->avail_ldb_ports, &ldb_port->func_list);
		rsrcs->num_avail_ldb_ports++;
	}

	/* Move the domain's dir ports to the function's avail list */
	list = &domain->used_dir_pq_pairs;
	DLB_DOM_LIST_FOR_SAFE(*list, dir_port, tmp_dir_port, iter1, iter2) {
		dir_port->owned = false;
		dir_port->port_configured = false;

		dlb_list_del(&domain->used_dir_pq_pairs,
			     &dir_port->domain_list);

		dlb_list_add(&rsrcs->avail_dir_pq_pairs,
			     &dir_port->func_list);
		rsrcs->num_avail_dir_pq_pairs++;
	}

	list = &domain->avail_dir_pq_pairs;
	DLB_DOM_LIST_FOR_SAFE(*list, dir_port, tmp_dir_port, iter1, iter2) {
		dir_port->owned = false;

		dlb_list_del(&domain->avail_dir_pq_pairs,
			     &dir_port->domain_list);

		dlb_list_add(&rsrcs->avail_dir_pq_pairs,
			     &dir_port->func_list);
		rsrcs->num_avail_dir_pq_pairs++;
	}

	/* Return hist list entries to the function */
	ret = dlb_bitmap_set_range(rsrcs->avail_hist_list_entries,
				   domain->hist_list_entry_base,
				   domain->total_hist_list_entries);
	if (ret) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: domain hist list base does not match the function's bitmap.\n",
			   __func__);
		return -EFAULT;
	}

	domain->total_hist_list_entries = 0;
	domain->avail_hist_list_entries = 0;
	domain->hist_list_entry_base = 0;
	domain->hist_list_entry_offset = 0;

	/* Return QED entries to the function */
	ret = dlb_bitmap_set_range(rsrcs->avail_qed_freelist_entries,
				   domain->qed_freelist.base,
				   (domain->qed_freelist.bound -
					domain->qed_freelist.base));
	if (ret) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: domain QED base does not match the function's bitmap.\n",
			   __func__);
		return -EFAULT;
	}

	domain->qed_freelist.base = 0;
	domain->qed_freelist.bound = 0;
	domain->qed_freelist.offset = 0;

	/* Return DQED entries back to the function */
	ret = dlb_bitmap_set_range(rsrcs->avail_dqed_freelist_entries,
				   domain->dqed_freelist.base,
				   (domain->dqed_freelist.bound -
					domain->dqed_freelist.base));
	if (ret) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: domain DQED base does not match the function's bitmap.\n",
			   __func__);
		return -EFAULT;
	}

	domain->dqed_freelist.base = 0;
	domain->dqed_freelist.bound = 0;
	domain->dqed_freelist.offset = 0;

	/* Return AQED entries back to the function */
	ret = dlb_bitmap_set_range(rsrcs->avail_aqed_freelist_entries,
				   domain->aqed_freelist.base,
				   (domain->aqed_freelist.bound -
					domain->aqed_freelist.base));
	if (ret) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: domain AQED base does not match the function's bitmap.\n",
			   __func__);
		return -EFAULT;
	}

	domain->aqed_freelist.base = 0;
	domain->aqed_freelist.bound = 0;
	domain->aqed_freelist.offset = 0;

	/* Return ldb credit pools back to the function's avail list */
	list = &domain->used_ldb_credit_pools;
	DLB_DOM_LIST_FOR_SAFE(*list, pool, tmp_pool, iter1, iter2) {
		pool->owned = false;
		pool->configured = false;

		dlb_list_del(&domain->used_ldb_credit_pools,
			     &pool->domain_list);
		dlb_list_add(&rsrcs->avail_ldb_credit_pools,
			     &pool->func_list);
		rsrcs->num_avail_ldb_credit_pools++;
	}

	list = &domain->avail_ldb_credit_pools;
	DLB_DOM_LIST_FOR_SAFE(*list, pool, tmp_pool, iter1, iter2) {
		pool->owned = false;

		dlb_list_del(&domain->avail_ldb_credit_pools,
			     &pool->domain_list);
		dlb_list_add(&rsrcs->avail_ldb_credit_pools,
			     &pool->func_list);
		rsrcs->num_avail_ldb_credit_pools++;
	}

	/* Move dir credit pools back to the function */
	list = &domain->used_dir_credit_pools;
	DLB_DOM_LIST_FOR_SAFE(*list, pool, tmp_pool, iter1, iter2) {
		pool->owned = false;
		pool->configured = false;

		dlb_list_del(&domain->used_dir_credit_pools,
			     &pool->domain_list);
		dlb_list_add(&rsrcs->avail_dir_credit_pools,
			     &pool->func_list);
		rsrcs->num_avail_dir_credit_pools++;
	}

	list = &domain->avail_dir_credit_pools;
	DLB_DOM_LIST_FOR_SAFE(*list, pool, tmp_pool, iter1, iter2) {
		pool->owned = false;

		dlb_list_del(&domain->avail_dir_credit_pools,
			     &pool->domain_list);
		dlb_list_add(&rsrcs->avail_dir_credit_pools,
			     &pool->func_list);
		rsrcs->num_avail_dir_credit_pools++;
	}

	domain->num_pending_removals = 0;
	domain->num_pending_additions = 0;
	domain->configured = false;
	domain->started = false;

	/* Move the domain out of the used_domains list and back to the
	 * function's avail_domains list.
	 */
	dlb_list_del(&rsrcs->used_domains, &domain->func_list);
	dlb_list_add(&rsrcs->avail_domains, &domain->func_list);
	rsrcs->num_avail_domains++;

	return 0;
}

static void dlb_log_reset_domain(struct dlb_hw *hw, u32 domain_id)
{
	DLB_HW_INFO(hw, "DLB reset domain:\n");
	DLB_HW_INFO(hw, "\tDomain ID: %d\n", domain_id);
}

/**
 * dlb_reset_domain() - Reset a DLB scheduling domain and its associated
 *	hardware resources.
 * @hw:	  Contains the current state of the DLB hardware.
 * @args: User-provided arguments.
 * @resp: Response to user.
 *
 * Note: User software *must* stop sending to this domain's producer ports
 * before invoking this function, otherwise undefined behavior will result.
 *
 * Return: returns < 0 on error, 0 otherwise.
 */
int dlb_reset_domain(struct dlb_hw *hw, u32 domain_id)
{
	struct dlb_domain *domain;
	int ret;

	dlb_log_reset_domain(hw, domain_id);

	domain = dlb_get_domain_from_id(hw, domain_id);

	if (domain  == NULL || !domain->configured)
		return -EINVAL;

	/* For each queue owned by this domain, disable its write permissions to
	 * cause any traffic sent to it to be dropped. Well-behaved software
	 * should not be sending QEs at this point.
	 */
	dlb_domain_disable_dir_queue_write_perms(hw, domain);

	dlb_domain_disable_ldb_queue_write_perms(hw, domain);

	/* Disable credit updates and turn off completion tracking on all the
	 * domain's PPs.
	 */
	dlb_domain_disable_dir_port_crd_updates(hw, domain);

	dlb_domain_disable_ldb_port_crd_updates(hw, domain);

	dlb_domain_disable_dir_port_interrupts(hw, domain);

	dlb_domain_disable_ldb_port_interrupts(hw, domain);

	dlb_domain_disable_ldb_seq_checks(hw, domain);

	/* Disable the LDB CQs and drain them in order to complete the map and
	 * unmap procedures, which require zero CQ inflights and zero QID
	 * inflights respectively.
	 */
	dlb_domain_disable_ldb_cqs(hw, domain);

	ret = dlb_domain_drain_ldb_cqs(hw, domain, false);
	if (ret < 0)
		return ret;

	ret = dlb_domain_wait_for_ldb_cqs_to_empty(hw, domain);
	if (ret < 0)
		return ret;

	ret = dlb_domain_finish_unmap_qid_procedures(hw, domain);
	if (ret < 0)
		return ret;

	ret = dlb_domain_finish_map_qid_procedures(hw, domain);
	if (ret < 0)
		return ret;

	/* Re-enable the CQs in order to drain the mapped queues. */
	dlb_domain_enable_ldb_cqs(hw, domain);

	ret = dlb_domain_drain_mapped_queues(hw, domain);
	if (ret < 0)
		return ret;

	ret = dlb_domain_drain_unmapped_queues(hw, domain);
	if (ret < 0)
		return ret;

	ret = dlb_domain_wait_for_ldb_pool_refill(hw, domain);
	if (ret) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: LDB credits failed to refill\n",
			   __func__);
		return ret;
	}

	/* Done draining LDB QEs, so disable the CQs. */
	dlb_domain_disable_ldb_cqs(hw, domain);

	/* Directed queues are reset in dlb_domain_reset_hw_resources(), but
	 * that process does not decrement the directed queue size counters used
	 * by SMON for its average DQED depth measurement. So, we manually drain
	 * the directed queues here.
	 */
	dlb_domain_drain_dir_queues(hw, domain);

	ret = dlb_domain_wait_for_dir_pool_refill(hw, domain);
	if (ret) {
		DLB_HW_ERR(hw,
			   "[%s()] Internal error: DIR credits failed to refill\n",
			   __func__);
		return ret;
	}

	/* Done draining DIR QEs, so disable the CQs. */
	dlb_domain_disable_dir_cqs(hw, domain);

	dlb_domain_disable_dir_producer_ports(hw, domain);

	dlb_domain_disable_ldb_producer_ports(hw, domain);

	dlb_domain_disable_dir_pools(hw, domain);

	dlb_domain_disable_ldb_pools(hw, domain);

	/* Reset the QID, credit pool, and CQ hardware.
	 *
	 * Note: DLB 1.0 A0 h/w does not disarm CQ interrupts during sched
	 * domain reset.
	 * A spurious interrupt can occur on subsequent use of a reset CQ.
	 */
	ret = dlb_domain_reset_hw_resources(hw, domain);
	if (ret)
		return ret;

	ret = dlb_domain_verify_reset_success(hw, domain);
	if (ret)
		return ret;

	dlb_domain_reset_registers(hw, domain);

	/* Hardware reset complete. Reset the domain's software state */
	ret = dlb_domain_reset_software_state(hw, domain);
	if (ret)
		return ret;

	return 0;
}

void dlb_hw_get_num_resources(struct dlb_hw *hw,
			      struct dlb_get_num_resources_args *arg)
{
	struct dlb_function_resources *rsrcs;
	struct dlb_bitmap *map;

	rsrcs = &hw->pf;

	arg->num_sched_domains = rsrcs->num_avail_domains;

	arg->num_ldb_queues = rsrcs->num_avail_ldb_queues;

	arg->num_ldb_ports = rsrcs->num_avail_ldb_ports;

	arg->num_dir_ports = rsrcs->num_avail_dir_pq_pairs;

	map = rsrcs->avail_aqed_freelist_entries;

	arg->num_atomic_inflights = dlb_bitmap_count(map);

	arg->max_contiguous_atomic_inflights =
		dlb_bitmap_longest_set_range(map);

	map = rsrcs->avail_hist_list_entries;

	arg->num_hist_list_entries = dlb_bitmap_count(map);

	arg->max_contiguous_hist_list_entries =
		dlb_bitmap_longest_set_range(map);

	map = rsrcs->avail_qed_freelist_entries;

	arg->num_ldb_credits = dlb_bitmap_count(map);

	arg->max_contiguous_ldb_credits = dlb_bitmap_longest_set_range(map);

	map = rsrcs->avail_dqed_freelist_entries;

	arg->num_dir_credits = dlb_bitmap_count(map);

	arg->max_contiguous_dir_credits = dlb_bitmap_longest_set_range(map);

	arg->num_ldb_credit_pools = rsrcs->num_avail_ldb_credit_pools;

	arg->num_dir_credit_pools = rsrcs->num_avail_dir_credit_pools;
}

void dlb_hw_disable_vf_to_pf_isr_pend_err(struct dlb_hw *hw)
{
	union dlb_sys_sys_alarm_int_enable r0;

	r0.val = DLB_CSR_RD(hw, DLB_SYS_SYS_ALARM_INT_ENABLE);

	r0.field.vf_to_pf_isr_pend_error = 0;

	DLB_CSR_WR(hw, DLB_SYS_SYS_ALARM_INT_ENABLE, r0.val);
}
