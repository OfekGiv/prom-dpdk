/*
 * Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of NVIDIA CORPORATION &
 * AFFILIATES (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 *
 */

/**
 * @file doca_verbs_mp.h
 * @page DOCA_VERBS_MP
 * @defgroup DOCA_VERBS_MP DOCA Verbs MP
 * @ingroup DOCA_VERBS
 * DOCA Verbs library. For more details please refer to the user guide on DOCA devzone.
 *
 * @{
 */
#ifndef DOCA_VERBS_MP_H_
#define DOCA_VERBS_MP_H_

#include <doca_error.h>
#include <doca_types.h>

#include "doca_verbs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************************************************************
 * DOCA Verbs MP opaque types
 *********************************************************************************************************************/
/**
 * Opaque structure representing a DOCA Verbs Device Advanced Transport Attributes instance.
 */
struct doca_verbs_device_advanced_transport_attr;

/**
 * @brief Verbs QP ordering semantic.
 */
enum doca_verbs_qp_ordering_semantic {
	DOCA_VERBS_QP_ORDERING_SEMANTIC_IBTA = 0x0,
	DOCA_VERBS_QP_ORDERING_SEMANTIC_OOO_RW = 0x1,
	DOCA_VERBS_QP_ORDERING_SEMANTIC_OOO_ALL = 0x2,
};

/**
 * @brief Verbs Rx out of order PSN Window sizes
 */
enum doca_verbs_rx_ooo_psn_win_size {
	DOCA_VERBS_RX_OOO_PSN_WIN_SIZE_512 = 0,
	DOCA_VERBS_RX_OOO_PSN_WIN_SIZE_1K = 1,
	DOCA_VERBS_RX_OOO_PSN_WIN_SIZE_2K = 2,
	DOCA_VERBS_RX_OOO_PSN_WIN_SIZE_4K = 3,
	DOCA_VERBS_RX_OOO_PSN_WIN_SIZE_8K = 4,
	DOCA_VERBS_RX_OOO_PSN_WIN_SIZE_16K = 5,
	DOCA_VERBS_RX_OOO_PSN_WIN_SIZE_32K = 6,
	DOCA_VERBS_RX_OOO_PSN_WIN_SIZE_64K = 7,
};

/**
 * @brief Verbs RCX QP type define.
 *
 * @note These attributes extend the QP type defined in doca_verbs.h
 *
 */
#define DOCA_VERBS_QP_TYPE_RCX 0x10000

/**
 * @brief Set rcx_type attribute for verbs_qp_init_attr
 * @note This is an extension to doca_verbs_qp_init_attr_set_* functions that's relevant only to RCX QP
 *
 * @param [in] verbs_qp_init_attr
 * Pointer to verbs_qp_init_attr instance.
 * @param [in] rcx_type
 * rcx_type attribute.
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_qp_init_attr_set_rcx_type(struct doca_verbs_qp_init_attr *verbs_qp_init_attr, uint8_t rcx_type);

/**
 * @brief Get rcx_type attribute from verbs_qp_init_attr
 * @note This is an extension to doca_verbs_qp_init_attr_get_* functions that's relevant only to RCX QP
 *
 * @param [in] verbs_qp_init_attr
 * Pointer to verbs_qp_init_attr instance.
 *
 * @return
 * rcx_type attribute.
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_qp_init_attr_get_rcx_type(const struct doca_verbs_qp_init_attr *verbs_qp_init_attr);

/**
 * @brief Set ordering semantic attribute for verbs_qp_init_attr
 * @note Not setting ordering semantic doesn't guarantee ordering semantic didn't change (setting ECE may change it)
 *
 * @param [in] verbs_qp_init_attr
 * Pointer to verbs_qp_init_attr instance.
 * @param [in] ordering_semantic
 * ordering semantic attribute.
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_qp_init_attr_set_ordering_semantic(struct doca_verbs_qp_init_attr *verbs_qp_init_attr,
							   enum doca_verbs_qp_ordering_semantic ordering_semantic);

/**
 * @brief Get ordering semantic attribute from verbs_qp_init_attr
 *
 * @param [in] verbs_qp_init_attr
 * Pointer to verbs_qp_init_attr instance.
 *
 * @return
 * ordering semantic attribute.
 */
DOCA_EXPERIMENTAL
enum doca_verbs_qp_ordering_semantic doca_verbs_qp_init_attr_get_ordering_semantic(
	const struct doca_verbs_qp_init_attr *verbs_qp_init_attr);

/**
 * @brief Verbs QP MP attributes - PS hints attribute
 *
 * @note These attributes extend the QP attributes defined in doca_verbs.h
 *
 * @details Can be used with doca_verbs_qp_modify() to set PS hints attribute in init->rtr command.
 *
 */
#define DOCA_VERBS_QP_ATTR_PS_HINTS (1 << 31)

/**
 * @brief Verbs QP MP attributes - Rx OOO PSN window size
 *
 * @note These attributes extend the QP attributes defined in doca_verbs.h
 *
 * @details Can be used with doca_verbs_qp_modify() to set Rx OOO PSN window size attribute in init->rtr command.
 *
 */
#define DOCA_VERBS_QP_ATTR_RX_OOO_PSN_WIN_SIZE (1 << 30)

/**
 * @brief Set PS hints attribute for verbs_qp_attr
 *
 * @param [in] verbs_qp_attr
 * Pointer to verbs_qp_attr instance.
 * @param [in] ps_hints
 * Pointer to a buffer that contains the PS hints.
 * @param [in] len
 * The size of the PS hints buffer. Must not exceed the size provided by
 * doca_verbs_device_advanced_transport_attr_get_ps_hints_max_size()
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_qp_attr_set_ps_hints(struct doca_verbs_qp_attr *verbs_qp_attr, void *ps_hints, size_t len);

/**
 * @brief Get PS hints from verbs_qp_attr
 *
 * @param [in] verbs_qp_attr
 * Pointer to verbs_qp_attr instance.
 * @param [out] ps_hints
 * Buffer for the PS hints.
 * @param [in] len
 * The size of ps_hints buffer. Must not exceed the size provided by
 * doca_verbs_device_advanced_transport_attr_get_ps_hints_max_size()
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_qp_attr_get_ps_hints(const struct doca_verbs_qp_attr *verbs_qp_attr,
					     void *ps_hints,
					     size_t len);

/**
 * @brief Set Rx out of order PSN win size
 *
 * @details Rx out of order win size requires out of order ordering semantics.
 *
 * @param [in] verbs_qp_attr
 * Pointer to verbs_qp_attr instance.
 * @param [in] rx_ooo_psn_win_size
 * RX out of order PSN windows size
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_qp_attr_set_rx_ooo_psn_win_size(struct doca_verbs_qp_attr *verbs_qp_attr,
							enum doca_verbs_rx_ooo_psn_win_size rx_ooo_psn_win_size);

/**
 * @brief Get Rx out of order PSN win size
 *
 * @param [in] verbs_qp_attr
 * Pointer to verbs_qp_attr instance.
 *
 * @return
 * Rx out of order PSN win size
 */
DOCA_EXPERIMENTAL
enum doca_verbs_rx_ooo_psn_win_size doca_verbs_qp_attr_get_rx_ooo_psn_win_size(
	const struct doca_verbs_qp_attr *verbs_qp_attr);

/**********************************************************************************************************************
 * Capabilities functions
 *********************************************************************************************************************/

/**
 * @brief Check if ordering semantic is supported for a specific QP type on this device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 * @param [in] qp_ordering_semantic
 * The QP ordering semantic.
 * @param [in] qp_type
 * The QP type.
 *
 * @return
 * DOCA_SUCCESS - in case ordering semantic is supported.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 * - DOCA_ERROR_NOT_SUPPORTED - if ordering semantic is not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_attr_get_is_ordering_semantic_supported(
	const struct doca_verbs_device_attr *verbs_device_attr,
	enum doca_verbs_qp_ordering_semantic qp_ordering_semantic,
	uint32_t qp_type);

/**
 * @brief Check if RX out of order PSN window size is supported
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 * @param [in] rx_ooo_psn_win_size
 * Rx out of order PSN win size
 *
 * @return
 * DOCA_SUCCESS - Rx out of order PSN window size is supported.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 * - DOCA_ERROR_NOT_SUPPORTED - if connection profile is not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_attr_get_is_rx_ooo_psn_win_size_supported(
	const struct doca_verbs_device_attr *verbs_device_attr,
	enum doca_verbs_rx_ooo_psn_win_size rx_ooo_psn_win_size);

/**
 * @brief Create DOCA Verbs device advanced transport attributes.
 * @note This object can be used to:
 * - Get advanced transport capabilities using "doca_verbs_device_attr_extract_device_advanced_transport_attr()"
 * - Set QUERY_HCA_CAPS_ADVANCED_TRANSPORT_PROV command output using
 * "doca_verbs_command_set_query_hca_caps_advanced_transport_output()".
 *
 * @param [out] verbs_device_advanced_transport_attr
 * Pointer to pointer to be set to point to the created doca_verbs_device_advanced_transport_attr instance.
 * User is expected to free this object with "doca_verbs_device_advanced_transport_attr_free()".
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NO_MEMORY - failed to allocate resources.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_advanced_transport_attr_create(
	struct doca_verbs_device_advanced_transport_attr **verbs_device_advanced_transport_attr);

/**
 * @brief Destroy a DOCA Verbs device advanced transport attributes instance.
 *
 * @param [in] verbs_device_advanced_transport_attr
 * Pointer to verbs_device_advanced_transport_attr instance.
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_advanced_transport_attr_destroy(
	struct doca_verbs_device_advanced_transport_attr *verbs_device_advanced_transport_attr);

/**
 * @brief Extract advanced transport device attributes into doca_verbs_device_advanced_transport_attr instance.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance to extract from.
 * @param [in] verbs_device_advanced_transport_attr
 * Pointer to doca_verbs_device_advanced_transport_attr instance to extract to.
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_attr_extract_device_advanced_transport_attr(
	const struct doca_verbs_device_attr *verbs_device_attr,
	struct doca_verbs_device_advanced_transport_attr *verbs_device_advanced_transport_attr);

/**
 * @brief Check if PS hints capability is supported on this device.
 * @note This value is uninitialized unless "doca_verbs_device_attr_extract_device_advanced_transport_attr()" is
 * called
 *
 * @param [in] verbs_device_advanced_transport_attr
 * Pointer to doca_verbs_device_advanced_transport_attr instance.
 *
 * @return
 * DOCA_SUCCESS - in case PS hints are supported.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 * - DOCA_ERROR_NOT_SUPPORTED - if PS hints capability are not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_advanced_transport_attr_get_is_ps_hints_supported(
	const struct doca_verbs_device_advanced_transport_attr *verbs_device_advanced_transport_attr);

/**
 * @brief Check if a given RCX type is supported on this device.
 * @note This value is uninitialized unless "doca_verbs_device_attr_extract_device_advanced_transport_attr()" is
 * called
 *
 * @param [in] verbs_device_advanced_transport_attr
 * Pointer to doca_verbs_device_advanced_transport_attr instance.
 * @param [in] rcx_type
 * The RCX type to check its support.
 *
 * @return
 * DOCA_SUCCESS - in case RCX type is supported.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 * - DOCA_ERROR_NOT_SUPPORTED - if RCX type is not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_advanced_transport_attr_get_is_rcx_supported(
	const struct doca_verbs_device_advanced_transport_attr *verbs_device_advanced_transport_attr,
	uint8_t rcx_type);

/**
 * @brief Get the maximum size of a PS hints buffer supported by the device.
 *
 * @param [in] verbs_device_advanced_transport_attr
 * Pointer to doca_verbs_device_advanced_transport_attr instance.
 *
 * @return
 * The maximum size of a PS hints buffer. 0 if PS hints are not supported.
 */
DOCA_EXPERIMENTAL
size_t doca_verbs_device_advanced_transport_attr_get_ps_hints_max_size(
	const struct doca_verbs_device_advanced_transport_attr *verbs_device_advanced_transport_attr);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOCA_VERBS_MP_H_ */

/** @} */
