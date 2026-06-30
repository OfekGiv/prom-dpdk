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
 * @file doca_verbs_trap.h
 * @page DOCA_VERBS_TRAP
 * @defgroup DOCA_VERBS_TRAP DOCA Verbs Trap
 * @ingroup DOCA_VERBS
 * DOCA Verbs library. For more details please refer to the user guide on DOCA devzone.
 *
 * @{
 */
#ifndef DOCA_VERBS_TRAP_H_
#define DOCA_VERBS_TRAP_H_

#include <doca_error.h>
#include <doca_types.h>

#include "doca_verbs_mp.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************************************************************
 * DOCA Verbs Trap opaque types
 *********************************************************************************************************************/
/**
 * Opaque structure representing a DOCA Verbs trap attributes
 */
struct doca_verbs_trap_attr;
/**
 * Opaque structure representing a DOCA Verbs Trap instance
 */
struct doca_verbs_trap;
/**
 * Opaque structure representing a DOCA Verbs Command instance
 */
struct doca_verbs_command;
/**
 * Opaque structure representing a DOCA Verbs RCX profile Attributes instance.
 */
struct doca_verbs_rcx_profile_attr;
/**
 * Opaque structure representing a DOCA Verbs RCX profile instance.
 */
struct doca_verbs_rcx_profile;

/**
 * @brief Asynchronous event type - Trap Error.
 *
 * @note This event type extends the Asynchronous event types defined in doca_verbs.h
 */
#define DOCA_VERBS_EVENT_TRAP_ERR 0x4

/**
 * @brief Asynchronous event type - Trap flushed.
 *
 * @note This event type extends the Asynchronous event types defined in doca_verbs.h
 * The event is fired when a trap instance has completed flushing. The trap can be destroyed after the event is
 * received.
 */
#define DOCA_VERBS_EVENT_TRAP_FLUSHED 0x5

/**
 * @brief Asynchronous event type - Trap flushing.
 *
 * @note This event type extends the Asynchronous event types defined in doca_verbs.h
 * The event is fired when a new trap has finished the upgrade process.
 */
#define DOCA_VERBS_EVENT_TRAP_READY 0x6

/**
 * @brief Use this flag to configure a doca_verbs_context with trap during creation.
 *
 * @details This flag is only valid with the doca_verbs_bridge_verbs_context_create() function and not with
 * the doca_verbs_bridge_verbs_context_import() function. If a verbs_context object is already opened with
 * this flag, an DOCA_ERROR_IN_USE error code will be returned (only one instance per device is permitted).
 */
#define DOCA_VERBS_CONTEXT_CREATE_FLAGS_ENABLE_TRAP (1 << 0)

/**
 * @brief Verbs based 1 device SACK format
 */
#define DOCA_VERBS_SACK_FORMAT_DEVICE_BASED_1 0x0

/**
 * @brief Verbs based 1 device NACK format
 */
#define DOCA_VERBS_NACK_FORMAT_DEVICE_BASED_1 0x0

/**
 * @brief Verbs RCX profile attributes
 */
enum doca_verbs_rcx_profile_attr_mask {
	DOCA_VERBS_RCX_PROFILE_ATTR_RCX_TYPE = (1 << 0),
	DOCA_VERBS_RCX_PROFILE_ATTR_SACK_FORMAT = (1 << 1),
	DOCA_VERBS_RCX_PROFILE_ATTR_NACK_FORMAT = (1 << 2),
	DOCA_VERBS_RCX_PROFILE_ATTR_RNR_RETRY = (1 << 3),
	DOCA_VERBS_RCX_PROFILE_ATTR_WRITE_EN = (1 << 4),
	DOCA_VERBS_RCX_PROFILE_ATTR_READ_EN = (1 << 5),
	DOCA_VERBS_RCX_PROFILE_ATTR_SEND_EN = (1 << 6),
	DOCA_VERBS_RCX_PROFILE_ATTR_ATOMIC_EN = (1 << 7),
	DOCA_VERBS_RCX_PROFILE_ATTR_ORDERING_SEMANTIC = (1 << 8),
	DOCA_VERBS_RCX_PROFILE_ATTR_PROFILE_ID = (1 << 9),
	DOCA_VERBS_RCX_PROFILE_ATTR_SACK_FREQ_MIN = (1 << 10),
	DOCA_VERBS_RCX_PROFILE_ATTR_CC_WINDOW_BASED = (1 << 11),
	DOCA_VERBS_RCX_PROFILE_ATTR_SACK_ENTROPY_SOURCE = (1 << 12),
};

/**
 * @brief Verbs trap profile
 */
enum doca_verbs_trap_profile {
	DOCA_VERBS_TRAP_PROFILE_MULTIPATH,
};

/**
 * @brief Verbs command state
 */
enum doca_verbs_command_state {
	DOCA_VERBS_COMMAND_STATE_POSTED,  /* Command posted by a tracked device. Trap application is expected to
						  continue it using doca_verbs_command_continue or return it using
						  doca_verbs_command_return. */
	DOCA_VERBS_COMMAND_STATE_SUCCESS, /* Command executed successfully by the device. Trap application is
						  expected to ack it using doca_verbs_command_ack. */
	DOCA_VERBS_COMMAND_STATE_ERROR,	  /* Device failed to execute the device. Trap application is expected to ack
						  it using doca_verbs_command_ack.*/
};

/**
 * @brief Verbs command op type
 */
enum doca_verbs_command_op_type {
	DOCA_VERBS_COMMAND_OP_TYPE_UNKNOWN = 0,
	DOCA_VERBS_COMMAND_OP_TYPE_HCA_INIT,
	DOCA_VERBS_COMMAND_OP_TYPE_HCA_TEAR_DOWN,
	DOCA_VERBS_COMMAND_OP_TYPE_CREATE_QP,
	DOCA_VERBS_COMMAND_OP_TYPE_MODIFY_QP,
	DOCA_VERBS_COMMAND_OP_TYPE_QUERY_QP,
	DOCA_VERBS_COMMAND_OP_TYPE_DESTROY_QP,
	DOCA_VERBS_COMMAND_OP_TYPE_QUERY_HCA_CAPS_ADVANCED_TRANSPORT_PROV,
	DOCA_VERBS_COMMAND_OP_TYPE_CREATE_CQ,
	DOCA_VERBS_COMMAND_OP_TYPE_DESTROY_CQ,
	DOCA_VERBS_COMMAND_OP_TYPE_QUERY_CQ,
	DOCA_VERBS_COMMAND_OP_TYPE_MODIFY_CQ,
};

/**
 * @brief Verbs command error types
 */
enum doca_verbs_cmd_error_types {
	DOCA_VERBS_CMD_ERROR_TYPE_OK,
	DOCA_VERBS_CMD_ERROR_TYPE_BAD_INPUT_LEN_ERR,
	DOCA_VERBS_CMD_ERROR_TYPE_BAD_OUTPUT_LEN_ERR,
	DOCA_VERBS_CMD_ERROR_TYPE_BAD_PARAM_ERR,
	DOCA_VERBS_CMD_ERROR_TYPE_BAD_OP_ERR,
	DOCA_VERBS_CMD_ERROR_TYPE_RESOURCE_BUSY_ERR,
	DOCA_VERBS_CMD_ERROR_TYPE_NOT_READY_ERR,
};

/**
 * @brief Verbs RCX config SACK entropy source
 */
enum doca_verbs_rcx_config_sack_entropy_source {
	DOCA_VERBS_RCX_CONFIG_SACK_ENTROPY_SOURCE_UDP_SRC_IP_FLOW_LABEL = 0X0,
	DOCA_VERBS_RCX_CONFIG_SACK_ENTROPY_SOURCE_IP_FLOW_LABEL = 0X1,
	DOCA_VERBS_RCX_CONFIG_SACK_ENTROPY_SOURCE_UDP_SRC = 0X2
};

/**
 * @brief Verbs RCX config SACK frequency min
 */
enum doca_verbs_rcx_config_sack_freq_min {
	DOCA_VERBS_RCX_CONFIG_SACK_FREQ_MIN_DISABLED = 0x0,
	DOCA_VERBS_RCX_CONFIG_SACK_FREQ_MIN_PROFILE_DEFAULT = 0x1,
	DOCA_VERBS_RCX_CONFIG_SACK_FREQ_MIN_FREQ_MILD = 0x2,
	DOCA_VERBS_RCX_CONFIG_SACK_FREQ_MIN_FREQ_MEDIUM = 0x3,
	DOCA_VERBS_RCX_CONFIG_SACK_FREQ_MIN_FREQ_AGGRESIVE = 0x4,
};

/**
 * @brief Verbs RCX profile IDs
 */
enum doca_verbs_rcx_config_profile_id {
	DOCA_VERBS_RCX_CONFIG_PROFILE_ID_0 = 0x0,
	DOCA_VERBS_RCX_CONFIG_PROFILE_ID_1 = 0x1,
	DOCA_VERBS_RCX_CONFIG_PROFILE_ID_2 = 0x2,
};

/**
 * @brief Defines usage of transmission window based Congestion Control for the protocol
 */
enum doca_verbs_rcx_config_cc_window_based {
	DOCA_VERBS_RCX_CONFIG_CC_WINDOW_BASED_PROFILE_DEFAULT = 0x0,
	DOCA_VERBS_RCX_CONFIG_CC_WINDOW_BASED_ENABLED = 0x1,
	DOCA_VERBS_RCX_CONFIG_CC_WINDOW_BASED_DISABLED = 0x2
};

/**
 * @brief Create a DOCA Verbs trap attributes instance.
 *
 * @param [out] trap_attr
 * Pointer to pointer to be set to point to the created doca_verbs_trap_attr instance.
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NO_MEMORY - failed to allocate resources.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_trap_attr_create(struct doca_verbs_trap_attr **trap_attr);

/**
 * @brief Destroy a DOCA Verbs trap attributes instance.
 *
 * @param [in] trap_attr
 * Pointer to doca_verbs_trap_attr instance.
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_trap_attr_destroy(struct doca_verbs_trap_attr *trap_attr);

/**
 * @brief Set profile attribute for doca_verbs_trap_attr
 *
 * @param [in] trap_attr
 * Pointer to doca_verbs_trap_attr instance.
 * @param [in] profile
 * doca_verbs_trap_profile attribute.
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_trap_attr_set_profile(struct doca_verbs_trap_attr *trap_attr,
					      enum doca_verbs_trap_profile profile);

/**
 * @brief Get profile attribute from doca_verbs_trap_attr
 *
 * @param [in] trap_attr
 * Pointer to doca_verbs_trap_attr.
 *
 * @return
 * profile attribute.
 */
DOCA_EXPERIMENTAL
enum doca_verbs_trap_profile doca_verbs_trap_attr_get_profile(const struct doca_verbs_trap_attr *trap_attr);

/**
 * @brief Set command response timeout attribute for doca_verbs_trap_attr
 *
 * @param [in] trap_attr
 * Pointer to doca_verbs_trap_attr instance.
 * @param [in] cmd_resp_timeout
 * command response timeout attribute (milliseconds).
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_trap_attr_set_cmd_resp_timeout(struct doca_verbs_trap_attr *trap_attr,
						       uint32_t cmd_resp_timeout);

/**
 * @brief Get command response timeout attribute from doca_verbs_trap_attr
 *
 * @param [in] trap_attr
 * Pointer to doca_verbs_trap_attr.
 *
 * @return
 * command response timeout attribute (milliseconds).
 */
DOCA_EXPERIMENTAL
uint32_t doca_verbs_trap_attr_get_cmd_resp_timeout(const struct doca_verbs_trap_attr *trap_attr);

/**
 * @brief Set representor attribute for doca_verbs_trap_attr
 *
 * @param [in] trap_attr
 * Pointer to doca_verbs_trap_attr instance.
 * @param [in] rep
 * representor attribute. Null implies self trap.
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_trap_attr_set_rep(struct doca_verbs_trap_attr *trap_attr, struct doca_dev_rep *rep);

/**
 * @brief Get representor attribute from doca_verbs_trap_attr
 *
 * @param [in] trap_attr
 * Pointer to doca_verbs_trap_attr.
 *
 * @return
 * representor attribute.
 */
DOCA_EXPERIMENTAL
struct doca_dev_rep *doca_verbs_trap_attr_get_rep(const struct doca_verbs_trap_attr *trap_attr);

/**
 * @brief Set upgrade attribute for doca_verbs_trap_attr
 *
 * @details A device can only run one trap for a representor. Setting trap attribute to true will overtake the previous
 * trap (that in turn will be destroyed). Use this attribute when upgrading an application that uses trap.
 *
 * @param [in] trap_attr
 * Pointer to doca_verbs_trap_attr instance.
 * @param [in] upgrade
 * True if upgrade, false if not.
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_trap_attr_set_upgrade(struct doca_verbs_trap_attr *trap_attr, uint8_t upgrade);

/**
 * @brief Get upgrade attribute from doca_verbs_trap_attr
 *
 * @param [in] trap_attr
 * Pointer to doca_verbs_trap_attr.
 *
 * @return
 * upgrade attribute.
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_trap_attr_get_upgrade(const struct doca_verbs_trap_attr *trap_attr);

/**
 * @brief Create DOCA Verbs command trap
 *
 * @param [in] ctx
 * DOCA verbs context to create the trap from
 * @param [in] trap_attr
 * Trap attributes
 * @param [out] trap
 * The created trap
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NO_MEMORY - failed to allocate resources.
 * - DOCA_ERROR_NOT_SUPPORTED - trap is not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_trap_create(struct doca_verbs_context *ctx,
				    const struct doca_verbs_trap_attr *trap_attr,
				    struct doca_verbs_trap **trap);

/**
 * @brief destroy a doca verbs trap
 *
 * @param [in] trap
 * Trap to destroy.
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NOT_PERMITTED - some commands are not released (ack will release them, even if the trap is in error
 * state).
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_trap_destroy(struct doca_verbs_trap *trap);

/**
 * @brief Allow handover to another trap
 *
 * @details Call this function to allow another trap to take over this trap (as a part of an upgrade flow).
 * The new trap will need to set upgrade attribute to true when created (@see doca_verbs_trap_attr_set_upgrade)
 *
 * @param [in] trap
 * Trap to allow / disallow handover from
 * @param [in] allow
 * True to allow, false to disallow
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NOT_SUPPORTED - handover capability is not supported (@see
 * doca_verbs_device_attr_is_trap_handover_supported)
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_trap_set_allow_handover(struct doca_verbs_trap *trap, uint8_t allow);

/**
 * @brief Get trap handle
 *
 * @details. Trap handle can be used to wait for trap event.
 *
 * @param [in] trap
 * Trap to get the handle from.
 *
 * @return
 * trap handle.
 */
DOCA_EXPERIMENTAL
doca_event_handle_t doca_verbs_trap_get_handle(const struct doca_verbs_trap *trap);

/**
 * @brief get event from a trap
 *
 * @details retrieves an event from the trap (@see return). Event (e.g. epoll) will continue to be signaled until this
 * method is called.
 *
 * @param [in] trap
 * Trap to get the event from.
 *
 * @return
 * DOCA_SUCCESS - in case of one or more commands waiting to be polled (poll should be called for every command).
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_AGAIN - no commands are waiting to be polled.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_trap_get_event(struct doca_verbs_trap *trap);

/**
 * @brief Arm a verbs trap to get notification when a command is ready to be polled.
 *
 * @param [in] trap
 * Trap to request the notification from.
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_trap_request_notification(struct doca_verbs_trap *trap);

/**
 * @brief Poll trap for a command
 *
 * @param [in] trap
 * Trap to poll
 * @param [out] command
 * Polled command
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_AGAIN - No command to poll.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_trap_poll(struct doca_verbs_trap *trap, struct doca_verbs_command **command);

/**
 * @brief Continue a posted command
 * @note Applies to all commands in posted state that the user didn't call doca_verbs_command_set_output()
 * variation on.
 * @details The device will continue executing the command. command must not be used after this method is
 * invoked.
 *
 * @param [in] command
 * Command to continue
 * @param [in] cmd_user_data
 * cmd_user_data can be used to bind between a posted command and a success/error command (@see
 * doca_verbs_command_get_user_data)
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_BAD_STATE - invalid command state.
 * - DOCA_ERROR_NOT_PERMITTED - user called doca_verbs_command_set_output() variation on the command.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_command_continue(struct doca_verbs_command *command, union doca_data cmd_user_data);

/**
 * @brief Ack a command
 * @note Applies to all commands in success/error state.
 * @details Ack a success/error command.
 *
 * @param [in] command
 * Command to ack
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_BAD_STATE - invalid command state.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_command_ack(struct doca_verbs_command *command);

/**
 * @brief Return a command
 * @note Applies to all commands in posted state.
 * @details Return a verbs command directly without continue / ack. @see doca_verbs_command_set_output variations
 * for setting the output before returning the command.
 *
 * @param [in] command
 * Command to return
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_BAD_STATE - invalid command state.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_command_return(struct doca_verbs_command *command);

/**
 * @brief Get command state
 * @note Applies to all commands in all states
 *
 * @param [in] command
 * Command to get the state from
 *
 * @return
 * Command state (@see enum doca_verbs_command_state).
 */
DOCA_EXPERIMENTAL
enum doca_verbs_command_state doca_verbs_command_get_state(const struct doca_verbs_command *command);

/**
 * @brief Get command op type
 * @note Applies to all commands in all states.
 *
 * @param [in] command
 * Command to get the op type from
 *
 * @return
 * Command type (@see enum doca_verbs_command_op_type).
 */
DOCA_EXPERIMENTAL
enum doca_verbs_command_op_type doca_verbs_command_get_op_type(const struct doca_verbs_command *command);

/**
 * @brief Get command user data
 * @note Applies to all commands in success/error state.
 *
 * @param [in] command
 * Command to get the user data from
 *
 * @return
 * cmd_user_data supplied in doca_verbs_command_continue.
 */
DOCA_EXPERIMENTAL
union doca_data doca_verbs_command_get_user_data(const struct doca_verbs_command *command);

/**
 * @brief Get command id
 *
 * @details command id can be used to correlate a retransmitted command on a new trap instance with a retransmitted
 * command from an old instance that crashed.
 * Trap application should store the command id (and the input command data) so that the new instance will be able to
 * correlate it with a retransmitted command.
 * An input command will be retransmitted if it wasn't continued / returned by the crashed trap application. And output
 * command will be retransmitted if the input command was continued by the crashed trap application.
 *
 * @param [in] command
 * command to get the command id from.
 * @param [out] command_id
 * Queried command id.
 *
 * @return
 * DOCA_SUCCESS - command sent successfully
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - invalid param
 * - DOCA_ERROR_BAD_STATE - command id was not set.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_command_get_command_id(const struct doca_verbs_command *command, uint32_t *command_id);

/**
 * @brief Get command input as is for PRM experts
 * @note User can't modify the command input but only view it.
 * @note Applies to all commands in all states.
 *
 * @param [in] command
 * Command to get the input from
 * @param [out] input
 * Pointer to the input data
 * @param [out] size
 * input size
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_command_get_input(const struct doca_verbs_command *command,
					  const uint8_t **input,
					  size_t *size);

/**
 * @brief Get command output as is for PRM experts
 * @note User can't modify the command output but only view it.
 * @note Applies to all commands in success/error state.
 *
 * @param [in] command
 * Command to get the output from
 * @param [out] output
 * Pointer to the output data
 * @param [out] size
 * output size
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_BAD_STATE - invalid command state.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_command_get_output(const struct doca_verbs_command *command,
					   const uint8_t **output,
					   size_t *size);

/**
 * @brief Get resource id from a command
 * @note Applies to the following:
 * - DOCA_VERBS_COMMAND_OP_TYPE_CREATE_QP in success state (returns QP number)
 * - DOCA_VERBS_COMMAND_OP_TYPE_MODIFY_QP in all states (returns QP number)
 * - DOCA_VERBS_COMMAND_OP_TYPE_QUERY_QP in all states (returns QP number)
 * - DOCA_VERBS_COMMAND_OP_TYPE_DESTROY_QP in all states (returns QP number)
 * - DOCA_VERBS_COMMAND_OP_TYPE_CREATE_CQ in success state (returns CQ number)
 * - DOCA_VERBS_COMMAND_OP_TYPE_MODIFY_CQ in all states (returns CQ number)
 * - DOCA_VERBS_COMMAND_OP_TYPE_QUERY_CQ in all states (returns CQ number)
 * - DOCA_VERBS_COMMAND_OP_TYPE_DESTROY_CQ in all states (returns CQ number)
 *
 * @param [in] command
 * Command to get the resource id from
 *
 * @return
 * resource id
 */
DOCA_EXPERIMENTAL
uint32_t doca_verbs_command_get_resource_id(const struct doca_verbs_command *command);

/**
 * @brief Get command UID
 * @note Applies to all commands in all states
 *
 * @param [in] command
 * Command to get UID from
 *
 * @return
 * Command UID
 */
DOCA_EXPERIMENTAL
uint16_t doca_verbs_command_get_uid(const struct doca_verbs_command *command);

/**
 * @brief Check if command input is truncated
 *
 * @details Command size is limited (see trap caps). Most commands shall be discarded if too large. Some commands shall
 * be trapped and truncated.
 *
 * @param [in] command
 * Command to check if truncated
 *
 * @return
 * 1 if truncated, 0 if not.
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_command_is_input_truncated(const struct doca_verbs_command *command);

/**
 * @brief Check if command output is truncated
 *
 * @details Command size is limited (see trap caps). Most commands shall be discarded if too large. Some commands shall
 * be trapped and truncated.
 *
 * @param [in] command
 * Command to check if truncated
 *
 * @return
 * 1 if truncated, 0 if not.
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_command_is_output_truncated(const struct doca_verbs_command *command);

/**
 * @brief Check if command is retransmitted
 *
 * @details A command may be retransmitted if a trap application has crashed and a new trap application instance runs.
 *
 * @param [in] command
 * Command to check if retransmitted
 *
 * @return
 * 1 if retransmitted, 0 if not.
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_command_is_retransmitted(const struct doca_verbs_command *command);

/**
 * @brief Set input data from crashed trap application
 *
 * @details This command sets input data from shared memory that was set by a crashed trap application instance.
 * The new trap application is expected to save the input data, size and command id. Output command id should be matched
 * to the saved input command id for correlation.
 * Input data is stored in the command.
 *
 * @param [in] command
 * Command to set input to
 * @param [in] input_data
 * Restored input data
 * @param [in] input_size
 * Restored input size
 *
 * * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NOT_PERMITTED - command is not output or command is not retransmitted.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_command_retransmitted_output_set_restored_input_data(struct doca_verbs_command *command,
									     const uint8_t *input_data,
									     size_t input_size);

/**
 * @brief Extract QP attributes from command
 * @note Extracted QP attributes are (others are invalid):
 * - current state
 * - next state
 * - destination QP num (in INIT2RTR)
 * - MTU (in INIT2RTR)
 * - GID (in INIT2RTR)
 * - PS hints (in INIT2RTR)
 * @note Applies to DOCA_VERBS_COMMAND_OP_TYPE_MODIFY_QP in all states.
 *
 * @param [in] command
 * Command to extract from
 * @param [out] verbs_qp_attr
 * QP attribute to extract into
 * @param [out] attr_mask
 * If not NULL, mask for QP attributes that were extracted. see define for DOCA_VERBS_QP_ATTR_*
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NOT_PERMITTED - op_type isn't DOCA_VERBS_COMMAND_OP_TYPE_MODIFY_QP.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_command_extract_qp_attr(const struct doca_verbs_command *command,
						struct doca_verbs_qp_attr *verbs_qp_attr,
						int *attr_mask);

/**
 * @brief Extract QP init attributes from command
 * @note Extracted QP init attributes are (others are invalid):
 * - DBR UMEM ID
 * - DBR UMEM offset
 * - WQ UMEM ID
 * - WQ UMEM offset
 * - QP type
 * - RCX type (in case QP is of type RCX)
 * - UAR ID
 * - PDN
 * - SQ WR
 * - RQ WR
 * - send CQ number
 * - receive CQ number
 * - user index
 * @note Applies to DOCA_VERBS_COMMAND_OP_TYPE_CREATE_QP in all states.
 *
 * @param [in] command
 * Command to extract from
 * @param [in] verbs_qp_init_attr
 * QP init attribute to extract into
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NOT_PERMITTED - op_type isn't DOCA_VERBS_COMMAND_OP_TYPE_CREATE_QP.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_command_extract_qp_init_attr(const struct doca_verbs_command *command,
						     struct doca_verbs_qp_init_attr *verbs_qp_init_attr);

/**
 * @brief Get flow tag from a QP command
 * @note Applies to the following:
 * - DOCA_VERBS_COMMAND_OP_TYPE_MODIFY_QP in INIT2RTR, RTR2RTS or RTS2RTS state transitions
 * - QP type is RCX or force RCX is set.
 *
 * @param [in] command
 * Command to get the flow tag from
 *
 * @return
 * flow tag
 */
DOCA_EXPERIMENTAL
uint32_t doca_verbs_command_get_qp_flow_tag(const struct doca_verbs_command *command);

/**
 * @brief Extract CQ attributes from command
 * @note Extracted CQ attributes are (others are invalid):
 * - cq_size
 * - cq_entry_size
 * - dpa_thread_id
 * - cq_overrun
 * @note Applies to DOCA_VERBS_COMMAND_OP_TYPE_CREATE_CQ
 *
 * @param [in] command
 * Command to extract from
 * @param [out] verbs_cq_attr
 * QP attribute to extract into
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NOT_PERMITTED - op_type isn't DOCA_VERBS_COMMAND_OP_TYPE_CREATE_CQ
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_command_extract_cq_attr(const struct doca_verbs_command *command,
						struct doca_verbs_cq_attr *verbs_cq_attr);

/**
 * @brief Set command output as is for PRM experts
 * @note Library won't take ownership of the output provided by the user but only copy it internally.
 * @note Applies to all commands in posted state.
 *
 * @param [in] command
 * Command to set the output to
 * @param [in] output
 * Pointer to the output data
 * @param [in] size
 * output size
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_BAD_STATE - invalid command state.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_command_set_output(struct doca_verbs_command *command, const uint8_t *output, size_t size);

/**
 * @brief Set HCA_CAPS_ADVANCED_TRANSPORT_PROV command output using doca_verbs_device_advanced_transport_attr.
 * @note Library won't take ownership of device_advanced_transport_attr provided by the user but only copy it
 * internally.
 * @note Applies to DOCA_VERBS_COMMAND_OP_TYPE_QUERY_HCA_CAPS_ADVANCED_TRANSPORT_PROV command in posted state.
 *
 * @param [in] command
 * Command to set the output to
 * @param [in] verbs_device_advanced_transport_attr
 * Device advanced transport attributes to use as output
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_BAD_STATE - invalid command state.
 * - DOCA_ERROR_NOT_PERMITTED - invalid command op_type.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_command_set_query_hca_caps_advanced_transport_output(
	struct doca_verbs_command *command,
	const struct doca_verbs_device_advanced_transport_attr *verbs_device_advanced_transport_attr);

/**
 * @brief Set command output to be error with specific status and syndrome.
 * @note Applies to all commands (other than HCA_TEAR_DOWN) in posted state.
 *
 * @param [in] command
 * Command to set the output to
 * @param [in] status
 * error status to set
 * @param [in] syndrome
 * error syndrome to set
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_BAD_STATE - invalid command state.
 * - DOCA_ERROR_NOT_PERMITTED - invalid command op_type.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_command_set_error_output(struct doca_verbs_command *command,
						 enum doca_verbs_cmd_error_types status,
						 uint32_t syndrome);

/**
 * @brief Create a DOCA Verbs RCX profile attributes instance
 *
 * @param [out] attr
 * The created RCX profile attributes instance
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NO_MEMORY - failed to allocate resources.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_create(struct doca_verbs_rcx_profile_attr **attr);

/**
 * @brief Destroy a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes instance
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_destroy(struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Set RCX type to a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 * @param [in] rcx_type
 * RCX type
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_set_rcx_type(struct doca_verbs_rcx_profile_attr *attr, uint8_t rcx_type);

/**
 * @brief Get RCX type from a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 *
 * @return
 * The RCX type
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_rcx_profile_attr_get_rcx_type(const struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Set SACK format to a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 * @param [in] sack_format
 * SACK format to set
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_set_sack_format(struct doca_verbs_rcx_profile_attr *attr, uint8_t sack_format);

/**
 * @brief Get SACK format from a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 *
 * @return
 * SACK format
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_rcx_profile_attr_get_sack_format(const struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Set NACK format to a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 * @param [in] nack_format
 * NACK format to set
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_set_nack_format(struct doca_verbs_rcx_profile_attr *attr, uint8_t nack_format);

/**
 * @brief Get NACK format from a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 *
 * @return
 * NACK format
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_rcx_profile_attr_get_nack_format(const struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Set RNR retry to a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 * @param [in] rnr_retry
 * RNR retry to set
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_set_rnr_retry(struct doca_verbs_rcx_profile_attr *attr, uint16_t rnr_retry);

/**
 * @brief Get RNR retry from a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 *
 * @return
 * RNR retry
 */
DOCA_EXPERIMENTAL
uint16_t doca_verbs_rcx_profile_attr_get_rnr_retry(const struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Set write enable to a DOCA Verbs RCX profile attributes instance
 * @note This is an indicator for the RCX type to support RDMA WRITE / WRITE WITH IMMEDIATE
 *
 * @param [in] attr
 * RCX profile attributes
 * @param [in] write_en
 * write enable to set
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_set_write_en(struct doca_verbs_rcx_profile_attr *attr, uint8_t write_en);

/**
 * @brief Get write enable from a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 *
 * @return
 * write enable
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_rcx_profile_attr_get_write_en(const struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Set read enable to a DOCA Verbs RCX profile attributes instance
 * @note This is an indicator for the RCX type to support RDMA READ
 *
 * @param [in] attr
 * RCX profile attributes
 * @param [in] read_en
 * read enable to set
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_set_read_en(struct doca_verbs_rcx_profile_attr *attr, uint8_t read_en);

/**
 * @brief Get read enable from a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 *
 * @return
 * read enable
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_rcx_profile_attr_get_read_en(const struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Set send enable to a DOCA Verbs RCX profile attributes instance
 * @note This is an indicator for the RCX type to support RDMA SEND
 *
 * @param [in] attr
 * RCX profile attributes
 * @param [in] send_en
 * send enable to set
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_set_send_en(struct doca_verbs_rcx_profile_attr *attr, uint8_t send_en);

/**
 * @brief Get send enable from a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 *
 * @return
 * send enable
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_rcx_profile_attr_get_send_en(const struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Set atomic enable to a DOCA Verbs RCX profile attributes instance
 * @note This is an indicator for the RCX type to support RDMA ATOMIC
 *
 * @param [in] attr
 * RCX profile attributes
 * @param [in] atomic_en
 * atomic enable to set
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_set_atomic_en(struct doca_verbs_rcx_profile_attr *attr, uint8_t atomic_en);

/**
 * @brief Get atomic enable from a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 *
 * @return
 * atomic enable
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_rcx_profile_attr_get_atomic_en(const struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Set ordering semantic to a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 * @param [in] ordering_semantic
 * ordering semantic to set
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_set_ordering_semantic(struct doca_verbs_rcx_profile_attr *attr,
							       enum doca_verbs_qp_ordering_semantic ordering_semantic);

/**
 * @brief Get ordering semantic from a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 *
 * @return
 * ordering semantic
 */
DOCA_EXPERIMENTAL
enum doca_verbs_qp_ordering_semantic doca_verbs_rcx_profile_attr_get_ordering_semantic(
	const struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Set profile to a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 * @param [in] profile
 * RCX profile
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_set_profile_id(struct doca_verbs_rcx_profile_attr *attr,
							enum doca_verbs_rcx_config_profile_id profile);

/**
 * @brief Get profile from a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 *
 * @return
 * profile
 */
DOCA_EXPERIMENTAL
enum doca_verbs_rcx_config_profile_id doca_verbs_rcx_profile_attr_get_profile_id(
	const struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Set sack frequency min to a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 * @param [in] sack_freq_min
 * SACK frequency min
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_set_sack_freq_min(struct doca_verbs_rcx_profile_attr *attr,
							   enum doca_verbs_rcx_config_sack_freq_min sack_freq_min);

/**
 * @brief Get SACK frequency min from a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 *
 * @return
 * SACK frequency min
 */
DOCA_EXPERIMENTAL
enum doca_verbs_rcx_config_sack_freq_min doca_verbs_rcx_profile_attr_get_sack_freq_min(
	const struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Set CC window based to a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 * @param [in] cc_window_based
 * CC window based
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_set_cc_window_based(struct doca_verbs_rcx_profile_attr *attr,
							     enum doca_verbs_rcx_config_cc_window_based cc_window_based);

/**
 * @brief Get CC window based from a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 *
 * @return
 * CC window based
 */
DOCA_EXPERIMENTAL
enum doca_verbs_rcx_config_cc_window_based doca_verbs_rcx_profile_attr_get_cc_window_based(
	const struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Set sack entropy source to a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 * @param [in] sack_entropy_source
 * sack entropy source
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_attr_set_sack_entropy_source(
	struct doca_verbs_rcx_profile_attr *attr,
	enum doca_verbs_rcx_config_sack_entropy_source sack_entropy_source);

/**
 * @brief Get sack entropy source from a DOCA Verbs RCX profile attributes instance
 *
 * @param [in] attr
 * RCX profile attributes
 *
 * @return
 * sack entropy source
 */
DOCA_EXPERIMENTAL
enum doca_verbs_rcx_config_sack_entropy_source doca_verbs_rcx_profile_attr_get_sack_entropy_source(
	const struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Create a DOCA Verbs RCX profile instance
 *
 * @param [in] verbs_context
 * verbs_context instance
 * @param [in] attr
 * RCX profile attributes
 * @param [in] attr_mask
 * Mask for initialized attributes
 * @param [out] rcx_profile
 * The created RCX profile
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NO_MEMORY - failed to allocate resources.
 * - DOCA_ERROR_NOT_SUPPORTED- RCX profile is not supported.
 * - DOCA_ERROR_IN_USE- RCX profile with the same rcx_tpe exists.
 * - DOCA_ERROR_FULL- max RCX profiles exists.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_create(struct doca_verbs_context *verbs_context,
					   struct doca_verbs_rcx_profile_attr *attr,
					   uint64_t attr_mask,
					   struct doca_verbs_rcx_profile **rcx_profile);

/**
 * @brief Destroy a DOCA Verbs RCX profile instance
 *
 * @param [in] rcx_profile
 * RCX profile instance
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_destroy(struct doca_verbs_rcx_profile *rcx_profile);

/**
 * @brief Query the attributes of a DOCA Verbs RCX profile instance
 *
 * @param [in] rcx_profile
 * RCX profile instance
 * @param [out] attr_mask
 * Mask for valid attributes
 * @param [out] attr
 * The queried RCX profile attributes
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_DRIVER - low level layer failure.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_rcx_profile_query(struct doca_verbs_rcx_profile *rcx_profile,
					  uint64_t *attr_mask,
					  struct doca_verbs_rcx_profile_attr *attr);

/**
 * @brief Force RCX type (used to create rcx_profile with) for a specific representor. This means that if an application
 * running on the representor creates an RC, it will be changed forcefully to RCX with the specific rcx_type by FW.
 * @note If this function is called for a specific representor after calling "doca_verbs_trap_create()" for the same
 * representor, then it will fail. A trap that is created on the same representor MUST be destroyed before the
 * corresponding RCX profile is destroyed.
 *
 * @param [in] verbs_context
 * verbs_context instance
 * @param [in] rep
 * representor to force RCX type for (can be NULL for self)
 * @param [in] rcx_type
 * RCX type
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - received invalid input.
 * - DOCA_ERROR_NOT_SUPPORTED - RCX profile is not supported.
 * - DOCA_ERROR_DRIVER - low level layer failure.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_force_rcx_type(struct doca_verbs_context *verbs_context,
				       struct doca_dev_rep *rep,
				       uint8_t rcx_type);

/**********************************************************************************************************************
 * Capabilities functions
 *********************************************************************************************************************/

/**
 * @brief Get the maximum number of traps supported by the device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 *
 * @return
 * The max number of traps supported by the device.
 */
DOCA_EXPERIMENTAL
uint16_t doca_verbs_device_attr_get_max_trap(const struct doca_verbs_device_attr *verbs_device_attr);

/**
 * @brief Check if a given trap profile is supported by the device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 * @param [in] trap_profile
 * The trap profile to check its support.
 *
 * @return
 * DOCA_SUCCESS - in case trap profile is supported.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 * - DOCA_ERROR_NOT_SUPPORTED - if trap profile is not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_attr_get_is_trap_profile_supported(const struct doca_verbs_device_attr *verbs_device_attr,
								  enum doca_verbs_trap_profile trap_profile);

/**
 * @brief Get the maximum number of representors per trap supported by the device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 *
 * @return
 * The max number of representors per trap supported by the device.
 */
DOCA_EXPERIMENTAL
uint32_t doca_verbs_device_attr_get_max_trap_num_rep(const struct doca_verbs_device_attr *verbs_device_attr);

/**
 * @brief Get the maximum number of interceptions per trap supported by the device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 *
 * @return
 * The max number of interceptions per trap supported by the device.
 */
DOCA_EXPERIMENTAL
uint16_t doca_verbs_device_attr_get_max_trap_num_interceptions(const struct doca_verbs_device_attr *verbs_device_attr);

/**
 * @brief Get the maximum trap command input size
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 *
 * @return Max output input size
 */
DOCA_EXPERIMENTAL
uint32_t doca_verbs_device_attr_get_verbs_command_max_input_size(const struct doca_verbs_device_attr *verbs_device_attr);

/**
 * @brief Get the maximum trap command output size
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 *
 * @return Max output command size
 */
DOCA_EXPERIMENTAL
uint32_t doca_verbs_device_attr_get_verbs_command_max_output_size(
	const struct doca_verbs_device_attr *verbs_device_attr);

/**
 * @brief Check if trap is supported on this device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 *
 * @return
 * DOCA_SUCCESS - in case trap is supported.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 * - DOCA_ERROR_NOT_SUPPORTED - if trap is not supported.
 * - DOCA_ERROR_IN_USE - if trap is already enabled on another instance of the device.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_attr_is_trap_supported(const struct doca_verbs_device_attr *verbs_device_attr);

/**
 * @brief Check if trap handover is supported
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 *
 * @return
 * DOCA_SUCCESS - in case trap is supported.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 * - DOCA_ERROR_NOT_SUPPORTED - if trap is not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_attr_is_trap_handover_supported(const struct doca_verbs_device_attr *verbs_device_attr);

/**
 * @brief Get the maximum number of RCX profiles supported by the device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 *
 * @return
 * The max number of RCX profiles supported by the device.
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_device_attr_get_max_rcx_profile(const struct doca_verbs_device_attr *verbs_device_attr);

/**
 * @brief Get the maximum RCX type supported by the device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 *
 * @return
 * The max maximum RCX type supported by the device.
 */
DOCA_EXPERIMENTAL
uint8_t doca_verbs_device_attr_get_max_rcx_type(const struct doca_verbs_device_attr *verbs_device_attr);

/**
 * @brief Check if RCX force is supported by the device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 *
 * @return
 * DOCA_SUCCESS - in case RCX force is supported.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 * - DOCA_ERROR_NOT_SUPPORTED - if RCX force is not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_attr_get_is_force_rcx_type_supported(
	const struct doca_verbs_device_attr *verbs_device_attr);

/**
 * @brief Check if RCX window based is supported on this device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 *
 * @return
 * DOCA_SUCCESS - RCX window based is supported.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 * - DOCA_ERROR_NOT_SUPPORTED - if RCX window based is not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_attr_get_is_rcx_cc_window_based_supported(
	const struct doca_verbs_device_attr *verbs_device_attr);

/**
 * @brief Check if RCX sack entropy source is supported on this device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 * @param [in] sack_entropy_source
 * Sack entropy source to check
 *
 * @return
 * DOCA_SUCCESS - Sack entropy source is supported.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 * - DOCA_ERROR_NOT_SUPPORTED - if RCX sack entropy source is not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_attr_get_is_rcx_sack_entropy_source_supported(
	const struct doca_verbs_device_attr *verbs_device_attr,
	enum doca_verbs_rcx_config_sack_entropy_source sack_entropy_source);

/**
 * @brief Check if RCX sack frequency min type is supported on this device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 * @param [in] sack_freq_min
 * Sack frequency min to check
 *
 * @return
 * DOCA_SUCCESS - Sack frequency min is supported.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 * - DOCA_ERROR_NOT_SUPPORTED - if RCX config sack frequency min is not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_attr_get_is_rcx_sack_freq_min_type_supported(
	const struct doca_verbs_device_attr *verbs_device_attr,
	enum doca_verbs_rcx_config_sack_freq_min sack_freq_min);

/**
 * @brief Check if RCX connection profile is supported on this device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 * @param [in] profile
 * RCX profile
 *
 * @return
 * DOCA_SUCCESS - connection profile is supported.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 * - DOCA_ERROR_NOT_SUPPORTED - if connection profile is not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_attr_get_is_rcx_profile_id_supported(
	const struct doca_verbs_device_attr *verbs_device_attr,
	enum doca_verbs_rcx_config_profile_id profile);

/**
 * @brief Set if PS hints are supported on this device.
 *
 * @param [in] verbs_device_advanced_transport_attr
 * Pointer to doca_verbs_device_advanced_transport_attr instance.
 * @param [in] is_ps_hints_supported
 * Indicator for PS hints support to set.
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_advanced_transport_attr_set_is_ps_hints_supported(
	struct doca_verbs_device_advanced_transport_attr *verbs_device_advanced_transport_attr,
	uint8_t is_ps_hints_supported);

/**
 * @brief Set if a given RCX type is supported on this device.
 *
 * @param [in] verbs_device_advanced_transport_attr
 * Pointer to doca_verbs_device_advanced_transport_attr instance.
 * @param [in] rcx_type
 * The RCX type to set its support.
 * @param [in] is_rcx_supported
 * Indicator for RCX type support to set.
 *
 * @return
 * DOCA_SUCCESS - in case of success.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_advanced_transport_attr_set_is_rcx_supported(
	struct doca_verbs_device_advanced_transport_attr *verbs_device_advanced_transport_attr,
	uint8_t rcx_type,
	uint8_t is_rcx_supported);

/**
 * @brief Check if QP CC flow tag is supported by the device.
 *
 * @param [in] verbs_device_attr
 * Pointer to doca_verbs_device_attr instance.
 *
 * @return
 * DOCA_SUCCESS - in case CC flow tag is supported.
 * doca_error code - in case of failure:
 * - DOCA_ERROR_INVALID_VALUE - if an invalid parameter was given.
 * - DOCA_ERROR_NOT_SUPPORTED - if CC flow tag is not supported.
 */
DOCA_EXPERIMENTAL
doca_error_t doca_verbs_device_attr_get_is_qp_cc_flow_tag_supported(
	const struct doca_verbs_device_attr *verbs_device_attr);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOCA_VERBS_TRAP_H_ */

/** @} */
