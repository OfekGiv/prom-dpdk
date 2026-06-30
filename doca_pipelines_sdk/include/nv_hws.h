/* SPDX-FileCopyrightText: NVIDIA CORPORATION & AFFILIATES
 * Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef NV_HWS
#define NV_HWS

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <linux/types.h>
#include <infiniband/mlx5dv.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nv_hws_context;
struct nv_hws_table;
struct nv_hws_action;
struct nv_hws_mt;
struct nv_hws_at;
struct nv_hws_matcher;
struct nv_hws_rule;
struct nv_hws_resource;
struct nv_hws_parser_graph;
struct nv_hws_parser_node;
struct nv_hws_parser_arc;
struct nv_hws_parser_sampler;
struct nv_hws_resource_queue;

enum nv_hws_debug_log_level {
	NV_HWS_DEBUG_LOG_LEVEL_ERR,
	NV_HWS_DEBUG_LOG_LEVEL_LOG,
	NV_HWS_DEBUG_LOG_LEVEL_DBG,
};

typedef void (*nv_hws_debug_log_handler)(enum nv_hws_debug_log_level log_level,
					 const char *format, ...);

struct nv_hws_devx_obj {
	uint32_t id;
	struct mlx5dv_devx_obj *obj;
};

enum nv_hws_sim_dev_type {
	NV_HWS_SIM_DEV_TYPE_NONE,
	NV_HWS_SIM_DEV_TYPE_BF3,
	NV_HWS_SIM_DEV_TYPE_CX9,
};

struct nv_hws_context_attr {
	uint16_t queues;
	uint16_t queue_size;
	struct ibv_pd *pd;
	/* Optional context for resources allocation (resource sharing) */
	struct ibv_context *shared_ibv_ctx;
	enum nv_hws_sim_dev_type sim_device;
	uint32_t comp_mask;
};

enum nv_hws_table_type {
	NV_HWS_TABLE_TYPE_NIC_RX,
	NV_HWS_TABLE_TYPE_NIC_TX,
	NV_HWS_TABLE_TYPE_RDMA_TRANSPORT_RX,
	NV_HWS_TABLE_TYPE_RDMA_TRANSPORT_TX,
	NV_HWS_TABLE_TYPE_FDB_RX,
	NV_HWS_TABLE_TYPE_FDB_TX,
	NV_HWS_TABLE_TYPE_FDB,
	NV_HWS_TABLE_TYPE_MAX,
};

struct nv_hws_table_attr {
	enum nv_hws_table_type type;
	/* Level is used for creating a table hierarchy. The Root table in
	 * level zero is shared between all applications and serves as the
	 * first table in the domain.
	 */
	uint32_t level;
	/* IB port valid only for RDMA_TRANSPORT_RX/TX tables */
	uint32_t ib_port;
	uint32_t comp_mask;
};

enum nv_hws_field_name {
	NV_HWS_FNAME_NONE,
	NV_HWS_FNAME_L2_TYPE, /* L2 Type 0=Unicast, 1=Multicast, 2=Broadcast, 3=Reserved - 2bit */
	NV_HWS_FNAME_L3_TYPE, /* L3 Type 0=None, 1=IPv4, 2=IPv6, 3=Reserved - 2bit */
	NV_HWS_FNAME_L4_TYPE, /* L4 type 0=None, 1=TCP, 2=UDP, 3=ICMP, 4-15=Reserved - 4bits */
	NV_HWS_FNAME_L4_TYPE_BWC, /* L4 Type BWC 0=None, 1=TCP, 2=UDP, 3=IPSEC - 2bit */
	NV_HWS_FNAME_ETH_TYPE, /* Ethertype - 16bit */
	NV_HWS_FNAME_ETH_PKT_LENGTH, /* Packet length (including FCS) - 16bit */
	NV_HWS_FNAME_SMAC_47_16, /* Source MAC MSB - 32bit */
	NV_HWS_FNAME_SMAC_15_0, /* Source MAC LSB - 16bit */
	NV_HWS_FNAME_DMAC_47_16, /* Destination MAC MSB - 32bit */
	NV_HWS_FNAME_DMAC_15_0, /* Destination MAC LSB - 16bit */
	NV_HWS_FNAME_FIRST_PRIO, /* First VLAN Priority (PCP) - 3bit */
	NV_HWS_FNAME_FIRST_CFI, /* First VLAN CFI/DEI - 1bit */
	NV_HWS_FNAME_FIRST_VID, /* First VLAN ID - 12bit */
	NV_HWS_FNAME_FIRST_CVLAN, /* First VLAN is CVLAN - 1bit */
	NV_HWS_FNAME_FIRST_SVLAN, /* First VLAN is SVLAN - 1bit */
	NV_HWS_FNAME_FIRST_VLAN_TYPE, /* First VLAN 0=None, 1=SVLAN, 2=CVLAN, 3=GVLAN - 2bit */
	NV_HWS_FNAME_SECOND_PRIO, /* Second VLAN Priority (PCP) - 3bit */
	NV_HWS_FNAME_SECOND_CFI, /* Second VLAN CFI/DEI - 1bit */
	NV_HWS_FNAME_SECOND_VID, /* Second VLAN ID - 12bit */
	NV_HWS_FNAME_SECOND_CVLAN, /* Second VLAN is CVLAN - 1bit */
	NV_HWS_FNAME_SECOND_SVLAN, /* Second VLAN is SVLAN - 1bit */
	NV_HWS_FNAME_SECOND_VLAN_TYPE, /* Second VLAN 0=None, 1=SVLAN, 2=CVLAN, 3=GVLAN - 2bit */
	NV_HWS_FNAME_IP_VERSION, /* IP version - 4bit */
	NV_HWS_FNAME_IP_FRAG, /* Packet is an IP fragment - 1bit */
	NV_HWS_FNAME_IP_TTL, /* IP Time To Live / Hoplimit - 8bit */
	NV_HWS_FNAME_IP_SPORT, /* IP L4 (TCP or UDP) Source Port - 16bit */
	NV_HWS_FNAME_IP_DPORT, /* IP L4 (TCP or UDP) Destination Port - 16bit */
	NV_HWS_FNAME_IP_PROTOCOL, /* IP Protocol / Next Header - 8bit */
	NV_HWS_FNAME_IP_TOS, /* IPv4 TOS / IPv6 TC, (DSCP=6bit,ECN=2bit) - 8bit */
	NV_HWS_FNAME_IP_TCP_SPORT, /* IP TCP Source Port - 16bit */
	NV_HWS_FNAME_IP_TCP_DPORT, /* IP TCP Destination Port - 16bit */
	NV_HWS_FNAME_IP_TCP_FLAGS, /* IP TCP FLAGS - 8bits */
	NV_HWS_FNAME_IP_TCP_DATA_OFFSET, /* IP TCP Data offset - 4bit */
	NV_HWS_FNAME_IP_TCP_NS, /* IP TCP NS - 1bit */
	NV_HWS_FNAME_IP_TCP_SEQ, /* TCP Sequence number - 32bit */
	NV_HWS_FNAME_IP_TCP_ACK, /* TCP Ack number - 32bit */
	NV_HWS_FNAME_IP_TCP_URG, /* TCP Urgent pointer - 16bit */
	NV_HWS_FNAME_IP_TCP_WIN, /* TCP Window size - 16bit */
	NV_HWS_FNAME_IP_TCP_UDP_CSUM, /* TCP/UDP Checksum - 16bit */
	NV_HWS_FNAME_IP_UDP_LEN, /* UDP Length - 16bit */
	NV_HWS_FNAME_IP_UDP_SPORT, /* IP UDP Source Port - 16bit */
	NV_HWS_FNAME_IP_UDP_DPORT, /* IP UDP Destination Port - 16bit */
	NV_HWS_FNAME_IPV4_ID, /* IPv4 identification - 16bit */
	NV_HWS_FNAME_IPV4_IHL, /* IPv4 IHL (Internet Header Length) - 4bit */
	NV_HWS_FNAME_IPV4_LEN, /* IPv4 Packet Length - 16bit */
	NV_HWS_FNAME_IPV4_CHECKSUM, /* IPv4 Header checksum - 16bit */
	NV_HWS_FNAME_IPV4_FLAGS, /* IPv4 Flags - 3bit */
	NV_HWS_FNAME_IPV4_FRAGMENT_OFFSET, /* IPv4 Fragment offset - 13bit */
	NV_HWS_FNAME_IPV4_SRC, /* IPv4 Source - 32bit */
	NV_HWS_FNAME_IPV4_DST, /* IPv4 Destination - 32bit */
	NV_HWS_FNAME_IPV6_FLOW_LABEL, /* IPv6 Flow Label - 20bit */
	NV_HWS_FNAME_IPV6_PAYLOAD_LEN, /* IPv6 Payload Length - 16bit */
	NV_HWS_FNAME_IPV6_DST_127_96, /* IPv6 Destination address_127_96 - 32bit */
	NV_HWS_FNAME_IPV6_DST_95_64, /* IPv6 Destination address_95_64 - 32bit */
	NV_HWS_FNAME_IPV6_DST_63_32, /* IPv6 Destination address_63_32 - 32bit */
	NV_HWS_FNAME_IPV6_DST_31_0, /* IPv6 Destination address_31_0 - 32bit */
	NV_HWS_FNAME_IPV6_SRC_127_96, /* IPv6 Source address_127_96 - 32bit */
	NV_HWS_FNAME_IPV6_SRC_95_64, /* IPv6 Source address_95_64 - 32bit */
	NV_HWS_FNAME_IPV6_SRC_63_32, /* IPv6 Source address_63_32 - 32bit */
	NV_HWS_FNAME_IPV6_SRC_31_0, /* IPv6 Source address_31_0 - 32bit */
	NV_HWS_FNAME_REG_C0, /* Register C0 - 32bit */
	NV_HWS_FNAME_REG_C1, /* Register C1 - 32bit */
	NV_HWS_FNAME_REG_C2, /* Register C2 - 32bit */
	NV_HWS_FNAME_REG_C3, /* Register C3 - 32bit */
	NV_HWS_FNAME_REG_C4, /* Register C4 - 32bit */
	NV_HWS_FNAME_REG_C5, /* Register C5 - 32bit */
	NV_HWS_FNAME_REG_C6, /* Register C6 - 32bit */
	NV_HWS_FNAME_REG_C7, /* Register C7 - 32bit */
	NV_HWS_FNAME_REG_C8, /* Register C8 - 32bit */
	NV_HWS_FNAME_REG_C9, /* Register C9 - 32bit */
	NV_HWS_FNAME_REG_C10, /* Register C10 - 32bit */
	NV_HWS_FNAME_REG_C11, /* Register C11 - 32bit */
	NV_HWS_FNAME_REG_C12, /* Register C12 - 32bit */
	NV_HWS_FNAME_REG_C13, /* Register C13 - 32bit */
	NV_HWS_FNAME_REG_C14, /* Register C14 - 32bit */
	NV_HWS_FNAME_REG_C15, /* Register C15 - 32bit */
	NV_HWS_FNAME_REG_C16, /* Register C16 - 32bit */
	NV_HWS_FNAME_REG_C17, /* Register C17 - 32bit */
	NV_HWS_FNAME_REG_C18, /* Register C18 - 32bit */
	NV_HWS_FNAME_REG_C19, /* Register C19 - 32bit */
	NV_HWS_FNAME_REG_C20, /* Register C20 - 32bit */
	NV_HWS_FNAME_REG_C21, /* Register C21 - 32bit */
	NV_HWS_FNAME_REG_C22, /* Register C22 - 32bit */
	NV_HWS_FNAME_REG_C23, /* Register C23 - 32bit */
	NV_HWS_FNAME_REG_C24, /* Register C24 - 32bit */
	NV_HWS_FNAME_REG_C25, /* Register C25 - 32bit */
	NV_HWS_FNAME_REG_C26, /* Register C26 - 32bit */
	NV_HWS_FNAME_REG_C27, /* Register C27 - 32bit */
	NV_HWS_FNAME_REG_C28, /* Register C28 - 32bit */
	NV_HWS_FNAME_REG_C29, /* Register C29 - 32bit */
	NV_HWS_FNAME_REG_C30, /* Register C30 - 32bit */
	NV_HWS_FNAME_REG_C31, /* Register C31 - 32bit */
	NV_HWS_FNAME_REG_C32, /* Register C32 - 32bit */
	NV_HWS_FNAME_REG_C33, /* Register C33 - 32bit */
	NV_HWS_FNAME_REG_C34, /* Register C34 - 32bit */
	NV_HWS_FNAME_REG_C35, /* Register C35 - 32bit */
	NV_HWS_FNAME_REG_C36, /* Register C36 - 32bit */
	NV_HWS_FNAME_REG_C37, /* Register C37 - 32bit */
	NV_HWS_FNAME_REG_C38, /* Register C38 - 32bit */
	NV_HWS_FNAME_REG_C39, /* Register C39 - 32bit */
	NV_HWS_FNAME_REG_C40, /* Register C40 - 32bit */
	NV_HWS_FNAME_REG_C41, /* Register C41 - 32bit */
	NV_HWS_FNAME_REG_C42, /* Register C42 - 32bit */
	NV_HWS_FNAME_REG_C43, /* Register C43 - 32bit */
	NV_HWS_FNAME_REG_C44, /* Register C44 - 32bit */
	NV_HWS_FNAME_REG_C45, /* Register C45 - 32bit */
	NV_HWS_FNAME_REG_C46, /* Register C46 - 32bit */
	NV_HWS_FNAME_REG_C47, /* Register C47 - 32bit */
	NV_HWS_FNAME_REG_A, /* Register A, metadata in WQE Eth segment - 32bit */
	NV_HWS_FNAME_REG_B, /* Register B, metadata to CQE FT metadata - 32bit */
	NV_HWS_FNAME_ENCAP_TYPE, /* Encapsulation 0=No_Encap, 1=L2TNL, 2=L3TNL, 3=RoCE - 2bit*/
	NV_HWS_FNAME_TNL_HDR_0, /* Generic Encapsulation Header DW0 - 32bit */
	NV_HWS_FNAME_TNL_HDR_1, /* Generic Encapsulation Header DW1 - 32bit */
	NV_HWS_FNAME_TNL_HDR_2, /* Generic Encapsulation Header DW2 - 32bit */
	NV_HWS_FNAME_TNL_HDR_3, /* Generic Encapsulation Header DW3- 32bit */
	NV_HWS_FNAME_FLEX_PARSER_0, /* Flex Parser 0 - 32bit */
	NV_HWS_FNAME_FLEX_PARSER_1, /* Flex Parser 1 - 32bit */
	NV_HWS_FNAME_FLEX_PARSER_2, /* Flex Parser 2 - 32bit */
	NV_HWS_FNAME_FLEX_PARSER_3, /* Flex Parser 3 - 32bit */
	NV_HWS_FNAME_FLEX_PARSER_4, /* Flex Parser 4 - 32bit */
	NV_HWS_FNAME_FLEX_PARSER_5, /* Flex Parser 5 - 32bit */
	NV_HWS_FNAME_FLEX_PARSER_6, /* Flex Parser 6 - 32bit */
	NV_HWS_FNAME_FLEX_PARSER_7, /* Flex Parser 7 - 32bit */
	NV_HWS_FNAME_FLEX_PARSER_0_OK, /* Flex parser 0 is valid - 1bit */
	NV_HWS_FNAME_FLEX_PARSER_1_OK, /* Flex parser 1 is valid - 1bit */
	NV_HWS_FNAME_FLEX_PARSER_2_OK, /* Flex parser 2 is valid - 1bit */
	NV_HWS_FNAME_FLEX_PARSER_3_OK, /* Flex parser 3 is valid - 1bit */
	NV_HWS_FNAME_FLEX_PARSER_4_OK, /* Flex parser 4 is valid - 1bit */
	NV_HWS_FNAME_FLEX_PARSER_5_OK, /* Flex parser 5 is valid - 1bit */
	NV_HWS_FNAME_FLEX_PARSER_6_OK, /* Flex parser 6 is valid - 1bit */
	NV_HWS_FNAME_FLEX_PARSER_7_OK, /* Flex parser 7 is valid - 1bit */
	NV_HWS_FNAME_RANDOM_NUM, /* Random Number - 16bit */
	NV_HWS_FNAME_SOURCE_QP, /* Source QP number - 24bit */
	NV_HWS_FNAME_HASH_RESULT, /* Hash result - 32bit */
	NV_HWS_FNAME_UTC_63_32, /* UTC high - 32bit */
	NV_HWS_FNAME_UTC_31_0, /* UTC low - 32bit */
	NV_HWS_FNAME_FRC_63_32, /* Free Running Clock high - 32bit */
	NV_HWS_FNAME_FRC_31_0, /* Free Running Clock low - 32bit */
	NV_HWS_FNAME_IB_L4_OPCODE, /* IB L4 BTH OPCODE - 8bit */
	NV_HWS_FNAME_IB_L4_QPN, /* IB L4 BTH QPN - 24bit */
	NV_HWS_FNAME_IB_L4_ACK_REQ, /* IB L4 BTH Acknowledgment bit - 1bit */
	NV_HWS_FNAME_IB_L4_RESERVED_7, /* L4 BTH Reserved7 (next to PSN) - 7bit */
	NV_HWS_FNAME_IB_L4_PKEY, /* IB L4 BTH Partition Key (P_KEY) - 16bit */
	NV_HWS_FNAME_IB_L4_PSN, /* IB L4 BTH Packet Sequence Number - 24bit */
	NV_HWS_FNAME_ICMP_DW0, /* ICMP 1st DW - 32bit */
	NV_HWS_FNAME_ICMP_DW1, /* ICMP 2nd DW - 32bit */
	NV_HWS_FNAME_ICMP_DW2, /* ICMP 3rd DW - 32bit */
	NV_HWS_FNAME_ICMP_TYPE, /* ICMP type - 8bit */
	NV_HWS_FNAME_ICMP_CODE, /* ICMP code - 8bit */
	NV_HWS_FNAME_IPSEC_LAYER, /* IPsec layer 0=None, 1=IPSECoIP, 2=IPSECoUDP - 2bit */
	NV_HWS_FNAME_IPSEC_SPI, /* IPsec SPI - 32bit */
	NV_HWS_FNAME_IPSEC_NEXT_HEADER,  /* IPsec next header - 8bit*/
	NV_HWS_FNAME_IPSEC_SEQ_NUM, /* IPsec sequence number - 32bit */
	NV_HWS_FNAME_IPSEC_SYNDROME, /* IPsec syndrome - 8bit */
	NV_HWS_FNAME_OK_L2, /* L2 layer is valid after passing all HW checks  - 1bit */
	NV_HWS_FNAME_OK_L3, /* L3 layer is valid after passing all HW checks  - 1bit */
	NV_HWS_FNAME_OK_L4, /* L4 layer is valid after passing all HW checks - 1bit */
	NV_HWS_FNAME_OK_IPV4_CSUM, /* IPv4 layer checksum is valid - 1bit */
	NV_HWS_FNAME_OK_L4_CSUM, /* L4 layer checksum is valid - 1bit */
	NV_HWS_FNAME_PSP_DW0, /* PSP DW 0 - 32bit */
	NV_HWS_FNAME_PSP_DW1, /* PSP DW 1 - 32bit */
	NV_HWS_FNAME_PSP_DW2, /* PSP DW 2 - 32bit */
	NV_HWS_FNAME_PSP_DW3, /* PSP DW 3 - 32bit */
	NV_HWS_FNAME_PSP_DW4, /* PSP DW 4 - 32bit */
	NV_HWS_FNAME_PSP_DW5, /* PSP DW 5 - 32bit */
	NV_HWS_FNAME_PSP_DW6, /* PSP DW 6 - 32bit */
	NV_HWS_FNAME_PSP_DW7, /* PSP DW 7 - 32bit */
	NV_HWS_FNAME_PSP_DW8, /* PSP DW 8 - 32bit */
	NV_HWS_FNAME_PSP_DW9, /* PSP DW 9 - 32bit */
	NV_HWS_FNAME_PSP_SYNDROME, /* PSP syndrome - 8bit */
	NV_HWS_FNAME_GTP_TEID, /* GTP Tunnel endpoint identifier - 32bit */
	NV_HWS_FNAME_GTP_MSG_TYPE, /* Indicates the type of GTP message - 8bit */
	NV_HWS_FNAME_GTP_EXT_FLAG, /* Indicates if an extension header exists - 1bit */
	NV_HWS_FNAME_GTP_NEXT_EXT_HDR, /* Indicates the next extension header type - 8bit */
	NV_HWS_FNAME_GTP_EXT_HDR_PDU, /* PDU type - 4bit */
	NV_HWS_FNAME_GTP_EXT_HDR_QFI, /* Qos Flow Identifier - 8bit */
	NV_HWS_FNAME_GRE_C_PRESENT, /* Checksum present - 1bit */
	NV_HWS_FNAME_GRE_K_PRESENT, /* Key present - 1bit */
	NV_HWS_FNAME_GRE_S_PRESENT, /* Sequence present - 1bit */
	NV_HWS_FNAME_GRE_VERSION, /* GRE version - 3bit */
	NV_HWS_FNAME_GRE_PROTOCOL, /* Protocol type of encapsulation - 16bit */
	NV_HWS_FNAME_GRE_CSUM, /* Checksum - 16bit */
	NV_HWS_FNAME_GRE_KEY, /* Key value - 32bit */
	NV_HWS_FNAME_GRE_SEQ_NUM, /* Sequence number - 32bit */
	NV_HWS_FNAME_NVGRE_PROTOCOL, /* Protocok type - 16bit */
	NV_HWS_FNAME_NVGRE_VSID, /* Virtual Subnet ID - 24bit */
	NV_HWS_FNAME_NVGRE_FLOW_ID, /* Flow ID - 8bit */
	NV_HWS_FNAME_VXLAN_GPE_FLAGS, /* GPE MSB - 8bit */
	NV_HWS_FNAME_VXLAN_GPE_NEXT_PROTOCOL, /* Next protocol type - 8bit */
	NV_HWS_FNAME_VXLAN_GPE_VNI, /* VXLAN Identifier - 24bit */
	NV_HWS_FNAME_VXLAN_GPE_VNI_RSVD_DW1, /* Reserved following VNI - 8bit*/
	NV_HWS_FNAME_VXLAN_FLAGS, /* VXLAN MSB - 8bit */
	NV_HWS_FNAME_VXLAN_VNI, /* VXLAN Identifier - 24bit */
	NV_HWS_FNAME_VXLAN_VNI_RSVD_DW1, /* Reserved following VNI - 8bit*/
	NV_HWS_FNAME_VXLAN_GBP_GROUP_POLICY_ID, /*Group Policy ID - 16bit */
	NV_HWS_FNAME_VXLAN_GBP_VNI, /* GBP VNI - 24bit */
	NV_HWS_FNAME_MPLS0, /* MPLS label 0 - 32bit */
	NV_HWS_FNAME_MPLS1, /* MPLS label 1 - 32bit */
	NV_HWS_FNAME_MPLS2, /* MPLS label 2 - 32bit */
	NV_HWS_FNAME_MPLS3, /* MPLS label 3 - 32bit */
	NV_HWS_FNAME_MPLS4, /* MPLS label 4 - 32bit */
	NV_HWS_FNAME_MPLS0_OK, /* MPLS 0 label detected - 1bit */
	NV_HWS_FNAME_MPLS1_OK, /* MPLS 1 label detected - 1bit */
	NV_HWS_FNAME_MPLS2_OK, /* MPLS 2 label detected - 1bit */
	NV_HWS_FNAME_MPLS3_OK, /* MPLS 3 label detected - 1bit */
	NV_HWS_FNAME_MPLS4_OK, /* MPLS 4 label detected - 1bit */
	NV_HWS_FNAME_BTH_PAYLOAD_0, /* BTH payload 0 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_1, /* BTH payload 1 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_2, /* BTH payload 2 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_3, /* BTH payload 3 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_4, /* BTH payload 4 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_5, /* BTH payload 5 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_6, /* BTH payload 6 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_7, /* BTH payload 7 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_8, /* BTH payload 8 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_9, /* BTH payload 9 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_10, /* BTH payload 10 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_11, /* BTH payload 11 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_12, /* BTH payload 12 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_13, /* BTH payload 13 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_14, /* BTH payload 14 - 32bit */
	NV_HWS_FNAME_BTH_PAYLOAD_15, /* BTH payload 15 - 32bit */
	NV_HWS_FNAME_RCX_MP_PATH_SELECT, /* RC ext multipath path select - Dynamic */
	NV_HWS_FNAME_RCX_MP_CTRL_RESPONSE, /* RC ext multipath ctrl resp - Dynamic */
	NV_HWS_FNAME_RCX_OOO_ELIGIBLE, /* RC ext out of order - Dynamic */
	NV_HWS_FNAME_RCX_RETX_INDICATION, /* RC ext retx indication - Dynamic */
	NV_HWS_FNAME_PLB_PORT_ID, /* PLB port id - Dynamic */
	NV_HWS_FNAME_PLB_PORT_STRICT, /* PLB port strict - Dynamic */
	NV_HWS_FNAME_PLB_PORT_PREFERRED, /* PLB port preferred - Dynamic */
	NV_HWS_FNAME_MAX,
};

enum nv_hws_field_header {
	/* Field is not located in packet headers */
	NV_HWS_HEADER_NONE,
	/* Field is located in the outer headers */
	NV_HWS_HEADER_OUTER,
	/* Field is located in the first inner header */
	NV_HWS_HEADER_INNER,
	NV_HWS_HEADER_MAX,
};

enum nv_hws_field_match_op {
	NV_HWS_FIELD_MATCH_OP_EQ, /* Equal */
	NV_HWS_FIELD_MATCH_OP_NE, /* Not equal */
	NV_HWS_FIELD_MATCH_OP_LT, /* Less than */
	NV_HWS_FIELD_MATCH_OP_LE, /* Less than or equal */
	NV_HWS_FIELD_MATCH_OP_GT, /* Great than */
	NV_HWS_FIELD_MATCH_OP_GE, /* Great than or equal */
};

enum nv_hws_field_match_type {
	/* Matching is done between a field and a value */
	NV_HWS_FIELD_MATCH_TYPE_TO_VALUE,
	/* Matching is done between two fields */
	NV_HWS_FIELD_MATCH_TYPE_TO_FIELD,
};

enum nv_hws_field_user_conv_flag {
	NV_HWS_FIELD_USER_CONV_FLAG_MATCH,
	NV_HWS_FIELD_USER_CONV_FLAG_MASK,
	NV_HWS_FIELD_USER_CONV_FLAG_LAST,
};

struct nv_hws_item_data {
	/* Note: match, mask and last size must be 32b aligned */
	const void *match; /* Match value */
	const void *mask;  /* Mask for match */
	const void *last;  /* Range from match to last with mask */
};

struct nv_hws_field_match_type_to_value {
	/* Bit offset to field MSB in match/mask/last buffer */
	uint32_t bit_off;

	/* Optional user value convert function, this will be
	 * called in case a translation is needed for a field
	 * in item data. conv_flag indicates if this is a value/mask/last
	 * translate, ret_conv_data points to the converted data.
	 */
	void *user_conv_data;
	void (*user_conv_func)(const struct nv_hws_item_data *item_data,
			       void *user_conv_data,
			       enum nv_hws_field_user_conv_flag conv_flag,
			       uint32_t *ret_conv_data);
};

struct nv_hws_field_match_type_to_field {
	enum nv_hws_field_name fname_b;
	enum nv_hws_field_header header_b;
};

struct nv_hws_field {
	/* Field description */
	enum nv_hws_field_name fname;
	enum nv_hws_field_header header;

	/* Operation to execute between field and value / field_b */
	enum nv_hws_field_match_op op;
	enum nv_hws_field_match_type type;

	union {
		struct nv_hws_field_match_type_to_value value;
		struct nv_hws_field_match_type_to_field field;
	};
};

struct nv_hws_item {
	struct nv_hws_item_data data;
	uint8_t num_of_fields;
	struct nv_hws_field *fields;
};

struct nv_hws_mt_attr {
	uint32_t comp_mask;
};

enum nv_hws_matcher_resource_mode {
	/* Matcher resource size based on number of rules */
	NV_HWS_MATCHER_RESOURCE_MODE_RULE,
	/* Allocate fixed size hash table based on given column and rows */
	NV_HWS_MATCHER_RESOURCE_MODE_HTABLE,
};

enum nv_hws_matcher_insert_mode {
	/* Rules are inserted to the matcher based on match hash */
	NV_HWS_MATCHER_INSERT_BY_HASH = 0x0,
	/* Rules are inserted to the matcher based on given index */
	NV_HWS_MATCHER_INSERT_BY_INDEX = 0x1,
};

enum nv_hws_matcher_distribute_mode {
	/* Incoming packets are distributed based on packet hash */
	NV_HWS_MATCHER_DISTRIBUTE_BY_HASH = 0x0,
	/* Incoming packets are distributed based on packet register index */
	NV_HWS_MATCHER_DISTRIBUTE_BY_LINEAR = 0x1,
};

struct nv_hws_matcher_attr {
	/* Processing priority inside table */
	uint32_t priority;
	/* Rule insertion mode */
	enum nv_hws_matcher_insert_mode insert_mode;
	/* Packet distribution mode */
	enum nv_hws_matcher_distribute_mode distribute_mode;
	/* Resource mode and corresponding size */
	enum nv_hws_matcher_resource_mode resource_mode;
	union {
		/* Htable resource mode logarithmic matcher sizes */
		struct {
			uint8_t sz_row_log;
			uint8_t sz_col_log;
		} table;
		/* Rule resource mode logarithmic matcher sizes */
		struct {
			uint8_t num_log;
		} rule;
	};
	/* Increase insertion rate by using unique per rule_idx [0..2^num_log] */
	bool optimize_using_rule_idx;
	/* Define whether the created matcher supports resizing */
	bool resizable;
	/* Isolated matcher is not part of the matchers chain of parent table */
	bool isolated;
	/* Inserted entries ignore match input and perform always hit */
	bool always_hit;
	/* Optional AT attach configuration - Max number of additional AT */
	uint8_t max_num_of_at_attach;
	uint32_t comp_mask;
};

struct nv_hws_rule_attr {
	uint16_t queue_id;
	uint32_t burst:1;
	/* Valid if matcher optimize_using_rule_idx is set or
	 * if matcher is configured to insert rules by index.
	 */
	uint32_t rule_idx;
	void *user_data;
	uint32_t comp_mask;
};

struct nv_hws_rule_query_data {
	size_t match_tag_sz;
	uint8_t match_tag[64];
	uint32_t comp_mask;
};

enum nv_hws_resource_type {
	NV_HWS_RESOURCE_TYPE_ASO_COUNTER,
	NV_HWS_RESOURCE_TYPE_ASO_CONNTRACK,
	NV_HWS_RESOURCE_TYPE_ASO_FLOW_HIT,
	NV_HWS_RESOURCE_TYPE_ASO_METER,
	NV_HWS_RESOURCE_TYPE_ASO_QUEUE_MNG,
	NV_HWS_RESOURCE_TYPE_ASO_ENTROPY,
	NV_HWS_RESOURCE_TYPE_ASO_MEMORY,
	NV_HWS_RESOURCE_TYPE_ASO_FIFO_2B,
	NV_HWS_RESOURCE_TYPE_ASO_FIFO_4B,
	NV_HWS_RESOURCE_TYPE_IPSEC_OFFLOAD,
	NV_HWS_RESOURCE_TYPE_DEK,
	NV_HWS_RESOURCE_TYPE_ARGUMENT_64B,
	NV_HWS_RESOURCE_TYPE_ARGUMENT_128B,
	NV_HWS_RESOURCE_TYPE_ARGUMENT_256B,
	NV_HWS_RESOURCE_TYPE_PROG_MASTER_KEY,
	NV_HWS_RESOURCE_TYPE_MAX,
};

struct nv_hws_resource_attr {
	enum nv_hws_resource_type type;
	size_t bulk_log_size;
	/* Whether this resource allows allocating contiguous blocks of
	 * power-of-two size offsets. Resources created without this
	 * flag only allow allocating one offset at a time.
	 */
	uint8_t block_alloc_support:1;
	/* Optional user created devx obj, mandatory for
	 * NV_HWS_RESOURCE_TYPE_IPSEC_OFFLOAD,
	 * NV_HWS_RESOURCE_TYPE_DEK.
	 */
	struct nv_hws_devx_obj *obj;
	uint32_t comp_mask;
};

struct nv_hws_resource_enqueue_aso_attr {
	/* Returned user data in the completion entry */
	void *user_data;
	/* Resource offset */
	uint32_t offset;
	/* Batching multiple operations */
	uint32_t burst:1;
	uint32_t comp_mask;
};

struct nv_hws_resource_queue_attr {
	uint8_t size_log; /* Log size of queue, in WQEs. */
	uint8_t wqe_tmpl_size; /* Size of a WQE, in WQEBBs. */
	/* Template for each request. Must be (64 * wqe_tmpl_size - 12) bytes. */
	void *wqe_tmpl;
};

struct nv_hws_resource_queue_send_attr {
	/* Non-null signals the end of a batch. */
	void *user_data;
	/* Optional. Any requests that fail will be reported by a bit of 1 in
	 * this bitmap.
	 */
	uint64_t *result_bitmap;
	uint8_t resource_type; /* enum nv_hws_resource_type */
	uint8_t fence:1;
};

struct nv_hws_resource_queue_result {
	void *user_data;
	/* It is guaranteed that num_failures + num_successes match the size of
	 * the original batch.
	 */
	uint16_t num_failures;
	uint16_t num_successes;
};

/* All the operations over URISC_MEM, the result is written both to URISC_MEM and to the
 * return 2 registers.
 */
enum nv_hws_action_aso_memory_op {
	/* Load the value from the memory location into the return register */
	NV_HWS_ACTION_ASO_MEMORY_OP_LOAD = 0x0,
	/* Store the value from the registers into the memory location,
	 * keep the original value in the registers
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_STORE = 0x1,
	/* Increment the value in the memory location with the value in the registers.
	 * keep the original value in the registers.
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_INC = 0x2,
	/* Bitwise XOR the value in the memory location with the value in the registers.
	 * keep the original value in the registers.
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_BITWISE_XOR = 0x3,
	/* Bitwise OR the value in the memory location with the value in the registers.
	 * keep the original value in the registers.
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_BITWISE_OR = 0x4,
	/* Bitwise AND the value in the memory location with the value in the registers.
	 * keep the original value in the registers.
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_BITWISE_AND = 0x5,
	/* Find the first set bit in URISC_MEM and return the bit offset (starting from msb).
	 * The result Bit offset (starting from msb) of the most significant set bit in URISC_MEM
	 * copied to the registers.
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_FFS_ID_MSB = 0x6,
	/* Find the first set bit in URISC_MEM and set the bit in the return register.
	 * The result URISC_MEM with all bits cleared except for the most significant set bit
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_FFS_BITMAP_MSB = 0x7,
	/* Find the first set bit in URISC_MEM and return the bit offset (starting from lsb).
	 * The result Bit offset (starting from lsb) of the least significant set bit in URISC_MEM
	 * copied to the registers.
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_FFS_ID_LSB = 0x8,
	/* Find the first set bit in URISC_MEM and set the bit in the return register.
	 * The result URISC_MEM with all bits cleared except for the least significant set bit
	 * copied to the registers.
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_FFS_BITMAP_LSB = 0x9,
	/* Clear the URISC_MEM.
	 * The result is the original URISC_MEM value copied to the registers.
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_RESET = 0xa,
	/* Clear the most significant set bit in URISC_MEM.
	 * The result Bit offset (starting from lsb) of the most significant set bit in URISC_MEM
	 * copied to the registers.
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_FFS_AND_RESET_ID_MSB = 0xb,
	/* Find the first set bit in URISC_MEM and clear it.
	 * The result URISC_MEM with all bits cleared except for the most significant set bit
	 * copied to the registers.
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_FFS_AND_RESET_BITMAP_MSB = 0xc,
	/* Clear the least significant set bit in URISC_MEM.
	 * The result Bit offset (starting from lsb) of the least significant set bit in
	 * URISC_MEM copied to the registers.
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_FFS_AND_RESET_ID_LSB = 0xd,
	/* Clear the least significant set bit in URISC_MEM.
	 * The result URISC_MEM with all bits cleared except for the least significant set bit
	 * copied to the registers.
	 */
	NV_HWS_ACTION_ASO_MEMORY_OP_FFS_AND_RESET_BITMAP_LSB = 0xe,
};

/* Operations over FIFO ASO */
enum nv_hws_action_aso_fifo_op {
	/* The attached C register is pushed to TAIL (LIFO), tail pointer decremented */
	NV_HWS_ACTION_ASO_FIFO_PUSH_TAIL = 0x0,
	/* The attached C register is pushed to HEAD (LIFO), head pointer decremented */
	NV_HWS_ACTION_ASO_FIFO_PUSH_HEAD = 0x1,
	/* The entry in the head is copied to the attached C register, head pointer decremented */
	NV_HWS_ACTION_ASO_FIFO_POP = 0x2,
};

enum nv_hws_queue_op_status {
	/* The operation was completed successfully */
	NV_HWS_QUEUE_OP_SUCCESS,
	/* The operation was not completed successfully */
	NV_HWS_QUEUE_OP_ERROR,
};

enum nv_hws_action_flags {
	NV_HWS_ACTION_FLAG_NIC_RX = 1 << 0,
	NV_HWS_ACTION_FLAG_NIC_TX = 1 << 1,
	NV_HWS_ACTION_FLAG_RDMA_TRANSPORT_RX = 1 << 2,
	NV_HWS_ACTION_FLAG_RDMA_TRANSPORT_TX = 1 << 3,
	NV_HWS_ACTION_FLAG_FDB_RX = 1 << 4,
	NV_HWS_ACTION_FLAG_FDB_TX = 1 << 5,
	NV_HWS_ACTION_FLAG_FDB = 1 << 6,
	NV_HWS_ACTION_FLAG_ROOT = 1 << 7,
};

enum nv_hws_action_reparse {
	/* Packet is re-parsed when needed based on nv_hws logic */
	NV_HWS_ACTION_REPARSE_AUTO,
	/* Packet is re-parsed after action execution */
	NV_HWS_ACTION_REPARSE_FORCE,
	/* Packet is not re-parsed after action execution */
	NV_HWS_ACTION_REPARSE_SKIP,
};

enum nv_hws_action_aso_meter_color {
	NV_HWS_ACTION_ASO_METER_COLOR_RED = 0x0,
	NV_HWS_ACTION_ASO_METER_COLOR_YELLOW = 0x1,
	NV_HWS_ACTION_ASO_METER_COLOR_GREEN = 0x2,
	NV_HWS_ACTION_ASO_METER_COLOR_UNDEFINED = 0x3,
};

enum nv_hws_action_aso_ct_direction {
	NV_HWS_ACTION_ASO_CT_DIRECTION_INITIATOR = 0 << 0,
	NV_HWS_ACTION_ASO_CT_DIRECTION_RESPONDER = 1 << 0,
};

enum nv_hws_action_reg_c64 {
	NV_HWS_ACTION_REG_C64_0_1,
	NV_HWS_ACTION_REG_C64_2_3,
	NV_HWS_ACTION_REG_C64_4_5,
	NV_HWS_ACTION_REG_C64_6_7,
	NV_HWS_ACTION_REG_C64_8_9,
	NV_HWS_ACTION_REG_C64_10_11,
	NV_HWS_ACTION_REG_C64_12_13,
	NV_HWS_ACTION_REG_C64_14_15,
	NV_HWS_ACTION_REG_C64_16_17,
	NV_HWS_ACTION_REG_C64_18_19,
	NV_HWS_ACTION_REG_C64_20_21,
	NV_HWS_ACTION_REG_C64_22_23,
	NV_HWS_ACTION_REG_C64_24_25,
	NV_HWS_ACTION_REG_C64_26_27,
	NV_HWS_ACTION_REG_C64_28_29,
	NV_HWS_ACTION_REG_C64_30_31,
	NV_HWS_ACTION_REG_C64_32_33,
	NV_HWS_ACTION_REG_C64_34_35,
	NV_HWS_ACTION_REG_C64_36_37,
	NV_HWS_ACTION_REG_C64_38_39,
	NV_HWS_ACTION_REG_C64_40_41,
	NV_HWS_ACTION_REG_C64_42_43,
	NV_HWS_ACTION_REG_C64_44_45,
	NV_HWS_ACTION_REG_C64_46_47,
};

struct nv_hws_action_aso_attr {
	/* RegisterC ID to write ASO executuion result to */
	enum nv_hws_action_reg_c64 return_reg;
	/* Execute ASO using control and resource_id taken from register */
	struct {
		/* Enable execute ASO from register */
		bool enable;
		/* RegisterC ID to use for executing ASO from register */
		enum nv_hws_action_reg_c64 input_reg;
	} take_from_reg;
};

enum nv_hws_action_crypto_type {
	NV_HWS_ACTION_CRYPTO_TYPE_IPSEC,
	NV_HWS_ACTION_CRYPTO_TYPE_PSP,
};

enum nv_hws_action_crypto_op {
	NV_HWS_ACTION_CRYPTO_OP_ENCRYPT,
	NV_HWS_ACTION_CRYPTO_OP_DECRYPT,
};

enum nv_hws_action_trailer_type {
	NV_HWS_ACTION_TRAILER_TYPE_IPSEC = 0x0,
	NV_HWS_ACTION_TRAILER_TYPE_PSP = 0x2,
};

enum nv_hws_action_trailer_op {
	NV_HWS_ACTION_TRAILER_OP_INSERT,
	NV_HWS_ACTION_TRAILER_OP_REMOVE,
};

struct nv_hws_action_trailer_attr {
	enum nv_hws_action_trailer_type type;
	enum nv_hws_action_trailer_op op;
	/* Trailer size in bytes */
	uint8_t size;
	uint32_t comp_mask;
};

struct nv_hws_action_modify_header {
	/* Byte size of modify_actions */
	size_t sz;
	/* PRM format modify actions pattern */
	__be64 *modify_actions;
};

struct nv_hws_action_hdr_data {
	/* Byte size of hdr_data */
	size_t sz;
	/* Header data */
	uint8_t *hdr_data;
};

enum nv_hws_action_anchor {
	NV_HWS_ACTION_ANCHOR_PACKET_START = 0x0,
	NV_HWS_ACTION_ANCHOR_MAC = 0x1,
	NV_HWS_ACTION_ANCHOR_FIRST_VLAN = 0x2,
	NV_HWS_ACTION_ANCHOR_SECOND_VLAN = 0x3,
	NV_HWS_ACTION_ANCHOR_FIRST_CONFIG_ETHERTYPE = 0x4,
	NV_HWS_ACTION_ANCHOR_SECOND_CONFIG_ETHERTYPE = 0x5,
	NV_HWS_ACTION_ANCHOR_FIRST_MPLS = 0x6,
	NV_HWS_ACTION_ANCHOR_IP_START = 0x7,
	NV_HWS_ACTION_ANCHOR_ESP = 0x8,
	NV_HWS_ACTION_ANCHOR_L4 = 0x9,
	NV_HWS_ACTION_ANCHOR_GRE = 0xa,
	NV_HWS_ACTION_ANCHOR_VXLAN = 0xa,
	NV_HWS_ACTION_ANCHOR_VXLAN_GPE = 0xa,
	NV_HWS_ACTION_ANCHOR_GENEVE = 0xa,
	NV_HWS_ACTION_ANCHOR_TUNNEL = 0xa,
	NV_HWS_ACTION_ANCHOR_FLEX_PARSER0 = 0xb,
	NV_HWS_ACTION_ANCHOR_FLEX_PARSER1 = 0xc,
	NV_HWS_ACTION_ANCHOR_FLEX_PARSER2 = 0xd,
	NV_HWS_ACTION_ANCHOR_FLEX_PARSER3 = 0xe,
	NV_HWS_ACTION_ANCHOR_FLEX_PARSER4 = 0xf,
	NV_HWS_ACTION_ANCHOR_FLEX_PARSER5 = 0x10,
	NV_HWS_ACTION_ANCHOR_FLEX_PARSER6 = 0x11,
	NV_HWS_ACTION_ANCHOR_FLEX_PARSER7 = 0x12,
	NV_HWS_ACTION_ANCHOR_INNER_MAC = 0x13,
	NV_HWS_ACTION_ANCHOR_INNER_FIRST_VLAN = 0x14,
	NV_HWS_ACTION_ANCHOR_INNER_SECOND_VLAN = 0x15,
	NV_HWS_ACTION_ANCHOR_INNER_FIRST_CONFIG_ETHERTYPE = 0x16,
	NV_HWS_ACTION_ANCHOR_INNER_SECOND_CONFIG_ETHERTYPE = 0x17,
	NV_HWS_ACTION_ANCHOR_INNER_FIRST_MPLS = 0x18,
	NV_HWS_ACTION_ANCHOR_INNER_IP = 0x19,
	NV_HWS_ACTION_ANCHOR_INNER_L4 = 0x1a,
	NV_HWS_ACTION_ANCHOR_L4_PAYLOAD = 0x1b,
	NV_HWS_ACTION_ANCHOR_MACSEC = 0x1d,
	NV_HWS_ACTION_ANCHOR_PSP = 0x1e,
	NV_HWS_ACTION_ANCHOR_PSP_PAYLOAD = 0x1f,
	NV_HWS_ACTION_ANCHOR_BTH_HEADER_START = 0x2d,
};

struct nv_hws_action_insert_header {
	struct nv_hws_action_hdr_data hdr_data;
	/* Start anchor for header insertion */
	enum nv_hws_action_anchor anchor;
	/* Header insertion offset in bytes, from the start
	 * anchor to the location where new header will be inserted.
	 */
	uint8_t offset;
	/* Indicates this header insertion adds encapsulation header to the packet,
	 * requiring device to update offloaded fields (for example IPv4 total length).
	 */
	bool encap;
	/* Must be set when insert header is ESP. Insert ESP also sets the
	 * IPSEC trailer next_header value.
	 */
	bool push_esp;
	/* Skip re-evaluation of packet headers after modifications */
	bool skip_reparse;
};

enum nv_hws_action_remove_header_type {
	NV_HWS_ACTION_REMOVE_HEADER_TYPE_BY_HEADER,
	NV_HWS_ACTION_REMOVE_HEADER_TYPE_BY_OFFSET,
};

struct nv_hws_action_remove_header_attr {
	enum nv_hws_action_remove_header_type type;
	union {
		struct {
			/* Start anchor from which header will be removed */
			enum nv_hws_action_anchor start_anchor;
			/* End anchor until the header will be removed */
			enum nv_hws_action_anchor end_anchor;
			/* True if remove header decapsulates the packet */
			bool decap;
		} by_header;
		struct {
			/* Start anchor from which header will be removed */
			enum nv_hws_action_anchor start_anchor;
			uint8_t size;
		} by_offset;
	};
	/* Skip re-evaluation of packet headers after modifications */
	bool skip_reparse;
};

enum nv_hws_action_dest_reformat_type {
	NV_HWS_ACTION_DEST_REFORMAT_TYPE_NONE,
	NV_HWS_ACTION_DEST_REFORMAT_TYPE_L2_TO_TNL_L2,
	NV_HWS_ACTION_DEST_REFORMAT_TYPE_L2_TO_TNL_L3,
};

struct nv_hws_action_dest_attr {
	/* Destination action to forward the packet */
	struct nv_hws_action *dest_action;

	/* Optional reformat action */
	struct {
		enum nv_hws_action_dest_reformat_type type;
		struct nv_hws_action_hdr_data hdr_data;
	} reformat;
};

enum nv_hws_action_dest_matcher_type {
	NV_HWS_ACTION_DEST_MATCHER_BY_INDEX,
};

enum nv_hws_action_nat64_flags {
	NV_HWS_ACTION_NAT64_V4_TO_V6 = 1 << 0,
	NV_HWS_ACTION_NAT64_V6_TO_V4 = 1 << 1,
	/* Indicates if to backup ipv4 addresses in last two registers */
	NV_HWS_ACTION_NAT64_BACKUP_ADDR = 1 << 2,
};

struct nv_hws_action_nat64_attr {
	/* According to the user, num of registers for the action to used.
	 * In case users set the NV_HWS_ACTION_NAT64_BACKUP_ADDR bit
	 * we except 3 registers.
	 */
	uint8_t num_of_registers;
	/* Array of the registers numbers */
	uint8_t *registers;
	uint32_t flags; /* From nv_hws_action_nat64_flags */

	/* Optional external resource to force NAT64 implementation using
	 * modify action list.
	 * The required ARG resource size should be 512B (256Bx2 / 128Bx4 / ..)
	 * If not supplied, the action will be created using internal resources.
	 */
	struct nv_hws_resource *resource;
	uint32_t resource_offset;
	uint32_t comp_mask;
};

enum nv_hws_action_rdma_resp {
	NV_HWS_ACTION_RDMA_RESP_TRIM_NACK = 1,
	NV_HWS_ACTION_RDMA_RESP_PROBE = 2,
};

struct nv_hws_action_enqueue_arg_write_attr {
	/* Returned user data in the completion entry */
	void *user_data;
	size_t resource_offset;
	/* Size (bytes) to write */
	size_t data_size;
	/* Data to write */
	uint8_t *arg_data;
	/* Batching multiple operations */
	uint32_t burst:1;
	uint32_t comp_mask;
};

struct nv_hws_at_attr {
	uint32_t comp_mask;
};

struct nv_hws_action_data {
	union {
		struct {
			__be32 value;
		} tag;

		struct {
			__be32 vlan_hdr;
		} push_vlan;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			__be64 *actions_data;
		} modify_header;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			uint8_t *hdr_data;
		} reformat_l2_to_tnl_l2;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			uint8_t *hdr_data;
		} reformat_l2_to_tnl_l3;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			uint8_t *hdr_data;
		} reformat_tnl_l3_to_l2;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			uint8_t *hdr_data;
		} insert_header;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			__be32 *aso_wqe;
			uint16_t wqe_size;
		} counter;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			__be32 *aso_wqe;
			uint16_t wqe_size;
			/* Control: Meter initial color */
			enum nv_hws_action_aso_meter_color init_color;
		} aso_meter;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			__be32 *aso_wqe;
			uint16_t wqe_size;
			/* Control: Select CT direction */
			enum nv_hws_action_aso_ct_direction direction;
		} aso_ct;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			__be32 *aso_wqe;
			uint16_t wqe_size;
			/* Control: Read first hit ASO value without modifying */
			bool read;
		} aso_first_hit;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			__be32 *aso_wqe;
			uint16_t wqe_size;
		} aso_ipsec;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			__be32 *aso_wqe;
			uint16_t wqe_size;
		} aso_queue_mng;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			__be32 *aso_wqe;
			uint16_t wqe_size;
			/* Control: Use last policy as previous hit packet */
			bool policy_index_last;
		} aso_entropy;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			__be32 *aso_wqe;
			uint16_t wqe_size;
		} aso_memory;

		struct {
			uint32_t resource_offset; /* Required only if aso_wqe set */
			uint8_t resource_idx;
			__be32 *aso_wqe;
			uint16_t wqe_size;
		} aso_from_reg;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
			__be32 *aso_wqe;
			uint16_t wqe_size;
			/* Control: The stack operation to perform */
			uint8_t op;
		} aso_fifo;

		struct {
			/* PRM format, control must be 0 (data only) */
			__be64 data;
		} inline_action;

		struct {
			uint32_t index;
		} dest_matcher;

		struct {
			uint32_t resource_offset;
			uint8_t resource_idx;
		} crypto;
	};
};

struct nv_hws_queue_op_result {
	/* Returns the status of the operation that this completion signals */
	enum nv_hws_queue_op_status status;
	/* The user data that will be returned on the completion events */
	void *user_data;
};

enum nv_hws_queue_op {
	/* Start executing all pending queued operations */
	NV_HWS_QUEUE_OP_DRAIN_ASYNC = 1 << 0,
	/* Start executing all pending queued operations wait till completion */
	NV_HWS_QUEUE_OP_DRAIN_SYNC = 1 << 1,
};

enum nv_hws_metric_flag {
	NV_HWS_METRIC_FLAG_INFO = 1 << 0,
	NV_HWS_METRIC_FLAG_COST = 1 << 1,
	NV_HWS_METRIC_FLAG_RESOURCE = 1 << 2,
};

struct nv_hws_metric_table {
	struct {
		uint32_t ft_id;
		uint32_t ft_fw_type;
		uint32_t level;
	} info;
};

struct nv_hws_metric_matcher {
	struct {
		/* Memory allocated for the STE tables of this matcher in bytes */
		uint64_t matcher_tables_mem_sz;
		uint64_t matcher_id;
		uint32_t priority;
		uint8_t log_num_of_rows;
		uint8_t log_num_of_columns;
		bool col_matcher_exist;
		uint8_t col_matcher_log_num_of_rows;
		uint8_t col_matcher_log_num_of_columns;
		uint8_t num_of_match_stes;
		uint8_t num_of_action_stes;
		bool is_jumbo;
	} info;
	struct {
		/* Cost of STE tables memory usage for this matcher */
		uint64_t memory;
		/* Cost for miss case of packet processing for this matcher */
		uint32_t miss_pkt_processing;
	} cost;
};

struct nv_hws_metric_matcher_template {
	/* This struct contains number of HW resources used by this
	 * matcher template.
	 */
	struct {
		uint8_t match_stes;
		uint8_t action_stes;
		uint8_t counters;
		uint8_t aso;
		uint8_t args;
		uint8_t crypto;
	} resource;
	struct {
		/* Weighted cost of HW memory resources usage for this specific
		 * match and action templates combination.
		 */
		uint64_t memory;
		/* Weighted cost of packet processing for this specific match
		 * and action templates combination.
		 */
		uint32_t pkt_processing;
		/* Weighted cost of rule insertion for this specific match and action
		 * templates combination.
		 */
		uint32_t insertion;
	} cost;
};

struct nv_hws_metric_matcher_template_attr {
	/* From enum nv_hws_metric_flag */
	uint64_t flags;
	struct nv_hws_action *dest_action;
	/* Optional action data array used for more accurate cost calculation */
	struct nv_hws_action_data *actions_data;
	/* Index of match template */
	uint8_t mt_idx;
	/* Index of action template */
	uint8_t at_idx;
	uint32_t comp_mask;
};

enum nv_hws_parser_hdr_len_mode {
	/* Header length is fixed */
	NV_HWS_PARSER_HDR_LEN_MODE_FIXED,
	/* Header length is explicit in a field */
	NV_HWS_PARSER_HDR_LEN_MODE_FIELD,
};

enum nv_hws_parser_node_type {
	NV_HWS_PARSER_NODE_TYPE_NATIVE,
	NV_HWS_PARSER_NODE_TYPE_FLEX,
};

enum nv_hws_parser_native_node {
	NV_HWS_PARSER_NATIVE_NODE_MAC,
	NV_HWS_PARSER_NATIVE_NODE_IP,
	NV_HWS_PARSER_NATIVE_NODE_GRE,
	NV_HWS_PARSER_NATIVE_NODE_UDP,
	NV_HWS_PARSER_NATIVE_NODE_MPLS,
	NV_HWS_PARSER_NATIVE_NODE_TCP,
	NV_HWS_PARSER_NATIVE_NODE_VXLAN_GPE,
	NV_HWS_PARSER_NATIVE_NODE_GENEVE,
	NV_HWS_PARSER_NATIVE_NODE_IPSEC_ESP,
	NV_HWS_PARSER_NATIVE_NODE_IPV4,
	NV_HWS_PARSER_NATIVE_NODE_IPV6,
	NV_HWS_PARSER_NATIVE_NODE_NISP,
};

struct nv_hws_parser_field_cfg {
	/* Location of the field within the header */
	uint32_t bit_offset;
	/* Size of the field */
	uint32_t bit_length;
};

struct nv_hws_parser_hdr_len_cfg {
	enum nv_hws_parser_hdr_len_mode mode;
	union {
		struct {
			uint32_t hdr_len;
		} fixed;
		struct {
			/* Field to extract the length value from */
			struct nv_hws_parser_field_cfg field;
			/* Extracted field is multiplied by this amount */
			uint32_t multiplier;
			/* Extracted field is added with this value after multiplication */
			uint32_t addition;
		} field;
	};
};

struct nv_hws_parser_node_cfg {
	/* How to calculate the header length */
	struct nv_hws_parser_hdr_len_cfg hdr_len;
	/* Options (TLVs) are processed for this header */
	bool has_options;
	/* Field is extracted from the header to use as the selection value */
	bool has_selection_field;
	/* Offset at which to start parsing options. Unused if (!has_options) */
	uint32_t option_offset;
	/* How to calculate length of an option. Unused if (!has_options) */
	struct nv_hws_parser_hdr_len_cfg option_len;
	/* Configuration of an option's type field. Unused if (!has_options) */
	struct nv_hws_parser_field_cfg option_type_field;
	/* Configuration of the selection value field. Unused if (!has_selection_field) */
	struct nv_hws_parser_field_cfg selection_field;
};

struct nv_hws_parser_arc_node_cfg {
	enum nv_hws_parser_node_type type;
	union {
		struct {
			enum nv_hws_parser_native_node node;
		} native;
		struct {
			struct nv_hws_parser_node *node;
		} flex;
	};
};

struct nv_hws_parser_arc_cfg {
	/* Source and destination nodes of the arc */
	struct nv_hws_parser_arc_node_cfg src_node;
	struct nv_hws_parser_arc_node_cfg dst_node;
	/* Encapsulation level of source node */
	uint32_t src_encap_level;
	/* The selection field data/value that selects this specific arc.
	 * For source flex nodes where !has_selection_field, this field is unused.
	 */
	uint32_t selection_field_data;
	/* Does the arc transition between encapsulation levels, e.g. from outer to inner */
	bool is_inner_transition;
};

struct nv_hws_parser_sampler_cfg {
	/* Optional previously created sampler to share HW resources with */
	struct nv_hws_parser_sampler *share_hw_resources;
	/* The encapsulation level to sample from */
	uint32_t node_encap_level;
	/* false: sample from main header, true: sample from an option.
	 * Must be false if (!header.has_options).
	 */
	bool sample_from_option;
	/* The option type to sample from. Unused if (!sample_from_option) */
	uint32_t option_type;
	/* The configuration of the field to sample */
	struct nv_hws_parser_field_cfg field_cfg;
};

struct nv_hws_parser_geneve_option_cfg {
	uint16_t option_class;
	uint8_t option_type;
	uint8_t option_data_len:5;
	uint8_t option_class_ignore:1;
	uint8_t offset_valid:1;
	uint8_t sample_offset;
};

enum nv_hws_encap_entropy_hash_sz {
	NV_HWS_ENCAP_ENTROPY_HASH_SZ_8,
	NV_HWS_ENCAP_ENTROPY_HASH_SZ_16,
};

union nv_hws_encap_entropy_hash_field_ip {
	uint8_t ipv6_addr[16];
	struct {
		uint8_t reserved[12];
		__be32 ipv4_addr;
	};
};

#pragma pack(push, 1)
struct nv_hws_encap_entropy_hash_fields {
	union nv_hws_encap_entropy_hash_field_ip dst;
	union nv_hws_encap_entropy_hash_field_ip src;
	uint8_t next_protocol;
	__be16 dst_port;
	__be16 src_port;
};
#pragma pack(pop)

enum nv_hws_ll_definer_type {
	NV_HWS_LL_DEFINER_TYPE_MATCH,
	NV_HWS_LL_DEFINER_TYPE_JUMBO,
	NV_HWS_LL_DEFINER_TYPE_RANGE,
	NV_HWS_LL_DEFINER_TYPE_COMPARE,
};

struct nv_hws_ll_definer {
	enum nv_hws_ll_definer_type type;
	uint8_t dw_selector[9];
	uint8_t byte_selector[8];
	uint8_t match_mask[44];
};

struct nv_hws_ll_mt_attr {
	uint32_t comp_mask;
};

/**
 * @brief Open a new context used for flow steering.
 *
 * The context is the parent object for all nv_hws API.
 * Context is bounded to an ibv_context representing the ibv_device.
 *
 * @param[in] ibv_ctx ibv context supporting devx.
 * @param[in] attr Attributes used for context open.
 * @return Context on success or NULL with errno set.
 */
struct nv_hws_context *
nv_hws_context_open(struct ibv_context *ibv_ctx,
		    struct nv_hws_context_attr *attr);

/**
 * @brief Close a flow steering context.
 *
 * @param[in] ctx Context to close.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_context_close(struct nv_hws_context *ctx);

/**
 * @brief Create a new table.
 *
 * Each table can contain multiple matchers, the matchers are chained to
 * create a packet processing pipe. Each table type represents a different
 * steering domain, RX, TX, FDB, other. There can be multiple tables from
 * each table type.
 *
 * @param[in] ctx The context in which the new table will be created.
 * @param[in] attr Attributes used for table creation.
 * @return Table on success or NULL with errno set.
 */
struct nv_hws_table *
nv_hws_table_create(struct nv_hws_context *ctx,
		    struct nv_hws_table_attr *attr);

/**
 * @brief Destroy table.
 *
 * Destroy an existing table.
 *
 * @param[in] tbl Table to destroy.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_table_destroy(struct nv_hws_table *tbl);

/**
 * @brief Set table default miss.
 *
 * Set the default miss table for "tbl" using another "miss_tbl".
 * In case a packet will be missed by all the matchers in "tbl" the packet will
 * be forwarded to the "miss_tbl".
 *
 * @param[in] tbl Table to set the miss operation on.
 * @param[in] miss_tbl Miss table to use, or NULL to remove current.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_table_set_default_miss(struct nv_hws_table *tbl,
				  struct nv_hws_table *miss_tbl);

/**
 * @brief Get field width.
 *
 * Each field has a width documented in the field name enum, to make it
 * easier for some developer we can also query it using the API.
 *
 * @param[in] ctx The context used with this field.
 * @param[in] fname Field name to query.
 * @return The length of the field on success, zero and errno otherwise.
 */
uint32_t nv_hws_match_field_get_length(struct nv_hws_context *ctx,
				       enum nv_hws_field_name fname);

/**
 * @brief Create a new match template.
 *
 * Match template describes the items and fields to match on using the matcher.
 * To create the match template users can provide multiple items, each can
 * contain multiple fields residing in the item. The format of each item data
 * is flexible and depends on user`s match structures.
 *
 * @param[in] ctx The context in which the match template will be created.
 * @param[in] items Match items describing fields to match on.
 * @param[in] attr Match template attributes.
 * @return Match template on success or NULL with errno set.
 */
struct nv_hws_mt *
nv_hws_match_template_create(struct nv_hws_context *ctx,
			     const struct nv_hws_item items[],
			     uint8_t num_of_items,
			     struct nv_hws_mt_attr *attr);

/**
 * @brief Destroy a match template.
 *
 * Destroy an existing match template.
 *
 * @param[in] mt Match template to destroy.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_match_template_destroy(struct nv_hws_mt *mt);

/**
 * @brief Create a new action template.
 *
 * Create a new action template containing the sequence of actions to execute
 * on packet match. The action template will be provided on matcher creation.
 *
 * @param[in] actions Array of actions based on the order of actions which will
 *                    be provided with action_data to nv_hws_rule_create.
 * @param[in] num_of_actions Number of actions in actions.
 * @param[in] attr Action template attributes.
 * @return pointer to action template on success, NULL with errno otherwise.
 */
struct nv_hws_at *
nv_hws_action_template_create(struct nv_hws_action *actions[],
			      size_t num_of_actions,
			      struct nv_hws_at_attr *attr);

/**
 * @brief Destroy action template.
 *
 * Destroy an existing action template.
 *
 * @param[in] at Action template to destroy.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_action_template_destroy(struct nv_hws_at *at);

/**
 * @brief: Create a new matcher.
 *
 * Create a new matcher object supporting provided match and action templates.
 * Each matcher can contain multiple rules, the matchers are chained to
 * create a packet processing pipe. Matchers can filter packet over provided
 * match templates and perform actions based on given action templates.
 *
 * @param[in] tbl Table that will contain the new matcher.
 * @param[in] mt Array of match templates.
 * @param[in] num_of_mt Number of match templates in mt array.
 * @param[in] at Array of action templates.
 * @param[in] num_of_at Number of action templates in at array.
 * @param[in] attr Attributes for matcher creation.
 * @return Matcher on success or NULL with errno set.
 */
struct nv_hws_matcher *
nv_hws_matcher_create(struct nv_hws_table *tbl,
		      struct nv_hws_mt *mt[],
		      uint8_t num_of_mt,
		      struct nv_hws_at *at[],
		      uint8_t num_of_at,
		      struct nv_hws_matcher_attr *attr);

/**
 * @brief Destroy matcher.
 *
 * Destroy an existing matcher.
 *
 * @param[in] matcher Matcher to destroy.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_matcher_destroy(struct nv_hws_matcher *matcher);

/**
 * @brief Start matcher resize.
 *
 * Link two matchers and enable moving rules from src matcher to dst matcher.
 * Both matchers must be in the same table type, must be created with 'resizable'
 * property, and should have the same characteristics (e.g. same mt, same at).
 *
 * Once the function is completed, the user is:
 *  - Allowed to move rules from src into dst matcher
 *  - No longer allowed to insert rules to the src matcher
 *
 * The user is always allowed to insert rules to the dst matcher and
 * to delete rules from any matcher.
 *
 * @param[in] src_matcher source matcher for moving rules from.
 * @param[in] dst_matcher destination matcher for moving rules to.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_matcher_resize_set_target(struct nv_hws_matcher *src_matcher,
				     struct nv_hws_matcher *dst_matcher);

/**
 * @brief: Enqueue move rule operation.
 *
 * After setting a matcher with a resize target rules move from source to
 * destination matcher can be enqueued using the rule move method.
 *
 * @param[in] src_matcher matcher that the rule belongs to.
 * @param[in] rule the rule to move.
 * @param[in] attr rule attributes.
 * @return zero on success, non zero otherwise.
 */
int nv_hws_matcher_resize_rule_move(struct nv_hws_matcher *src_matcher,
				    struct nv_hws_rule *rule,
				    struct nv_hws_rule_attr *attr);

/**
 * @brief: Attach an additional action template.
 *
 * Attach an additional action template to an existing matcher in case an
 * optional AT attach configuration was provided on matcher creation attributes.
 *
 * @param[in] matcher matcher to attach at to.
 * @param[in] at action template to attach.
 * @return zero on success, non zero otherwise.
 */
int nv_hws_matcher_attach_at(struct nv_hws_matcher *matcher,
			     struct nv_hws_at *at);

/**
 * @brief Get the size of the rule handle.
 *
 * This functions provides the size of the rule handle, rule handle objects are
 * allocated and managed by the user. The handle is opaque and initalized by
 * nv_hws API during rule creation.
 */
size_t nv_hws_rule_get_handle_size(void);

/**
 * @brief Enqueue create rule operation.
 *
 * Enqueue a new create rule operation in a matcher.
 * Rule matching template is specified by the "mt_idx" and the values for
 * matching are provided in "item_data" array.
 * Rule action template is specified by the "at_idx" and the values for
 * actions are provided in "actions_data" array.
 * In case of a packet match to the values in item_data action_data will be
 * executed on the packet and the packet will be directed to the "dest_action".
 * Parallel rule creation can be executed over different queues.
 *
 * @param[in] matcher The matcher in which the new rule will be created.
 * @param[in] mt_idx Match template index to create the match with.
 * @param[in] item_data Item data for matching.
 * @param[in] at_idx Action template index to apply the actions with.
 * @param[in] action_data Data corresponding to the action template and dest_action.
 * @param[in] dest_action The destination action in case of match.
 * @param[in] attr Rule creation attributes.
 * @param[out] rule_handle Preallocated uninitialized rule handle.
 * @return zero on successful enqueue non zero with errno set otherwise.
 */
int nv_hws_rule_create(struct nv_hws_matcher *matcher,
		       uint8_t mt_idx,
		       struct nv_hws_item_data item_data[],
		       uint8_t at_idx,
		       struct nv_hws_action_data action_data[],
		       struct nv_hws_action *dest_action,
		       struct nv_hws_rule_attr *attr,
		       struct nv_hws_rule *rule_handle);

/**
 * @brief Enqueue destroy rule operation.
 *
 * Destroy an existing rule (after a valid completion) from a matcher.
 * Parallel rule destruction can be executed over different queues.
 *
 * @param[in] rule The rule destruction to enqueue.
 * @param[in] attr Rule destruction attributes.
 * @return zero on successful enqueue non zero with errno set otherwise.
 */
int nv_hws_rule_destroy(struct nv_hws_rule *rule,
			struct nv_hws_rule_attr *attr);

/**
 * @brief Update rule action data.
 *
 * Update an exiting rule action data with a new action data, rule match stays
 * the same but the executed actions and values differs based on the at_idx and
 * action data.
 *
 * @param[in] rule_handle The rule handle to update.
 * @param[in] at_idx Action template index to apply the actions with.
 * @param[in] action_data Data corresponding to the action template and dest_action.
 * @param[in] dest_action The destination action in case of match.
 * @param[in] attr Rule update attributes.
 * @return zero on successful enqueue non zero with errno set otherwise.
 */
int nv_hws_rule_action_data_update(struct nv_hws_rule *rule_handle,
				   uint8_t at_idx,
				   struct nv_hws_action_data actions_data[],
				   struct nv_hws_action *dest_action,
				   struct nv_hws_rule_attr *attr);

/**
 * @brief Get rule match tag.
 *
 * Each rule handle contains a match tag build from the item_data match values.
 * The match tag format is constructed based on the match fields and HW.
 * Get tag allows the user to print the tag and use it for debug.
 * NOTE: Tag data can be accessed safely for existing, created rules.
 *
 * @param[in] rule The rule to get the match tag from.
 * @param[out] ret_query Query data struct to fill the data into.
 * @return zero on successful query, non zero with errno set otherwise.
 */
int nv_hws_rule_query(struct nv_hws_rule *rule,
		      struct nv_hws_rule_query_data *ret_query);

/**
 * @brief Calculate rule hash table index.
 *
 * Each rule is being stored in a hash table inside the matcher, the hash
 * index is calculated by the HW for each rule based on the item_data and
 * the mt_idx. This function allows to precalculate the hash index.
 * This is useful for predicting packet distribution in the hash table.
 *
 * @param[in] matcher The matcher which will hold the rule.
 * @param[in] item_data Item data values to calculate the hash.
 * @param[in] mt_idx Match template index.
 * @param[out] ret_hash_idx Returned calculated hash index.
 * @param[out] ret_hash_raw Returned raw calculated hash.
 * @return zero on successful calculation, non zero with errno set otherwise.
 */
int nv_hws_rule_hash_calculate(struct nv_hws_matcher *matcher,
			       struct nv_hws_item_data item_data[],
			       uint8_t mt_idx,
			       uint32_t *ret_hash_idx,
			       uint32_t *ret_hash_raw);

/**
 * @brief Allocate resource object.
 *
 * Allocate a new resource bulk based on provided resource attributes.
 * Resources are used for rule creation and actions, the resource API
 * provides an optional queue based offset management for the resource bulk.
 *
 * @param[in] ctx Context.
 * @param[in] attr Resource attribute that defines the resource.
 * @return struct nv_hws_resource* on success or NULL with errno set.
 */
struct nv_hws_resource *
nv_hws_resource_alloc(struct nv_hws_context *ctx,
		      struct nv_hws_resource_attr *attr);

/**
 * @brief Free allocated resource object.
 *
 * @param[in] resource Resource object.
 * @return Zero on success, non zero with errno set otherwise.
 */
int nv_hws_resource_free(struct nv_hws_resource *resource);

/**
 * @brief Get the maximum block size a resource can allocate, in log2.
 *
 * For non-block alloc resources this will always be 0.
 *
 * @param[in] resource The resource object.
 * @return The log2 of the maximum resource block the resource can
 * allocate.
 */
int nv_hws_resource_max_block_size(const struct nv_hws_resource *resource);

/**
 * @brief Get the size of a resource.
 *
 * @param[in] resource The resource object.
 * @return The total number of items managed by the resource.
 */
int nv_hws_resource_log_size(const struct nv_hws_resource *resource);

/**
 * @brief Allocate a single resource offset from the bulk.
 *
 * Find and return an unused resource offset from the bulk.
 * Parallel resource offset get is allowed only over different queue IDs.
 *
 * @param[in] resource Resource object.
 * @param[in] qid Queue id used for locking purposes only.
 * @param[out] resource_offset Resource offset.
 * @return Zero on success, non zero with errno set otherwise.
 */
int nv_hws_resource_get_offset(struct nv_hws_resource *resource,
			       uint16_t qid,
			       uint32_t *resource_offset);

/**
 * @brief Free a single resource offset back to the bulk.
 *
 * Free a single resource and return it to the bulk.
 * Parallel resource offset put is allowed only over different queue IDs.
 *
 * @param[in] resource Resource object.
 * @param[in] qid Queue id used for locking purposes only.
 * @param[in] resource_offset Resource offset to release.
 * @return Zero on success, non zero with errno set otherwise.
 */
int nv_hws_resource_put_offset(struct nv_hws_resource *resource,
			       uint16_t qid,
			       uint32_t resource_offset);

/**
 * @brief Allocate a resource offset block.
 *
 * Find and return an unused contiguous resource offset block.
 * Parallel resource offset get is allowed only over different queue IDs.
 *
 * @param[in] resource The resource object.
 * @param[in] qid Queue id used for locking purposes only.
 * @param[in] log_sz Log2 of the size of offsets to allocate.
 * @param[out] resource_offset Resource offset.
 * @return Zero on success, non zero with errno set otherwise.
 */
int nv_hws_resource_get_offset_block(struct nv_hws_resource *resource,
				     uint16_t qid, uint8_t log_sz,
				     uint32_t *resource_offset);

/**
 * @brief Free a block of resource offsets back to the bulk.
 *
 * Free a block of resource offsets and return it to the bulk.
 * Parallel resource offset put is allowed only over different queue IDs.
 * The block size must match the initial allocated size.
 *
 * @param[in] resource The Resource object.
 * @param[in] qid Queue id used for locking purposes only.
 * @param[in] log_sz Log2 of the size of offsets to allocate.
 * @param[in] resource_offset Resource offset to release.
 * @return Zero on success, non zero with errno set otherwise.
 */
int nv_hws_resource_put_offset_block(struct nv_hws_resource *resource,
				     uint16_t qid, uint8_t log_sz,
				     uint32_t resource_offset);

/**
 * @brief Get devx_obj pointer from the resource.
 *
 * Get the devx_obj representing the resource.
 *
 * @param[in] resource Resource object.
 * @param[out] ret_obj Devx object representing this resource.
 * @param[out] ret_obj_id Devx object base index representing this resource.
 * @return Zero on success, non zero with errno set otherwise.
 */
int nv_hws_resource_get_devx_obj(struct nv_hws_resource *resource,
				 struct nv_hws_devx_obj **ret_obj,
				 uint32_t *ret_obj_id);

/**
 * @brief Post a raw ASO WQE to the queue.
 *
 * Posting ASO WQE into the queue (raw).
 *
 * @param[in] resource Resource object.
 * @param[in] attr Enqueue ASO wqe attribute.
 * @param[in] qid Queue id.
 * @param[in] aso_wqe Raw ASO CTRL and DATA segments for enqueue in BE format.
 * @param[in] wqe_len The size of the wqe to be posted.
 * @return Zero on success non zero otherwise with errno set.
 */
int
nv_hws_resource_enqueue_aso_wqe(struct nv_hws_resource *resource,
				uint16_t qid,
				__be32 *aso_wqe,
				size_t wqe_len,
				struct nv_hws_resource_enqueue_aso_attr *attr);

/**
 * @brief Create a resource queue.
 *
 * Resource queues allow efficient posting of mostly-identical requests. They
 * initialize requests with a template given at creation time and allow in-place
 * creation of WQEs, so users need only fill in variable data. Resource queues
 * also enable efficient batching by issuing a single completion event per
 * batch.
 *
 * @param[in] ctx The context of the device.
 * @param[in] attr Attributes controlling resource queue creation.
 * @return Pointer to the resource queue on success, NULL with errno otherwise.
 */
struct nv_hws_resource_queue *
nv_hws_resource_queue_create(struct nv_hws_context *ctx,
			     struct nv_hws_resource_queue_attr *attr);

/**
 * @brief Destroy a resource queue.
 *
 * @param[in] queue The resource queue to be destroyed.
 */
void nv_hws_resource_queue_destroy(struct nv_hws_resource_queue *queue);

/**
 * @brief Start a new request for a resource queue.
 *
 * @param[in] queue The resource queue to start a request for.
 * @return Pointer to the WQE data of the request.
 */
void *nv_hws_resource_queue_start(struct nv_hws_resource_queue *queue);

/**
 * @brief Post the current WQE to the resource queue.
 *
 * @param[in] queue The resource queue to post the WQE to.
 * @param[in] attr Send attribute for the WQE.
 * @return Zero on success, non zero with errno set otherwise.
 */
int nv_hws_resource_queue_end(struct nv_hws_resource_queue *queue,
			      struct nv_hws_resource_queue_send_attr *attr);

/**
 * @brief Poll a resource queue for completions.
 *
 * A resource queue only posts exactly one result for each batch. The result
 * user_data will match the one passed with the request that ended the batch.
 *
 * @param[in] queue The resource queue to poll.
 * @param[out] results Result array.
 * @param[in] num_results Maximum number of results to return.
 * @return The number of completions on success. Otherwise, negative number with
 * errno set.
 */
int nv_hws_resource_queue_poll(struct nv_hws_resource_queue *queue,
			       struct nv_hws_resource_queue_result *results,
			       uint32_t num_results);

/**
 * @brief Execute an operation on the resource queue.
 *
 * This involves flushing any outstanding requests. As such, a user_data is
 * needed to associate with the corresponding batch.
 *
 * @param[in] queue The resource queue to operate on.
 * @param[in] queue_op Queue operation to perform.
 * @param[in] user_data The user_data to associate with the current open batch.
 * @param[in] result_bitmap The optional result bitmap the current batch will
 * report errors to.
 * @return Zero on success, non-zero with errno set otherwise.
 */
int nv_hws_resource_queue_execute_op(struct nv_hws_resource_queue *queue,
				     enum nv_hws_queue_op queue_op,
				     void *user_data,
				     uint64_t *result_bitmap);

/**
 * @brief Set IB port into action flags.
 *
 * Set IB port allows the user to encode a single ib port into action_flags.
 * IB port is currently required for actions used in PRMDA tables only.
 *
 * @param[in] ctx Context to query IB port details.
 * @param[in] ib_port IB port number to encode.
 * @param[out] action_flags Action flags to set IB port into.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_action_flags_set_ib_port(struct nv_hws_context *ctx,
				    uint32_t ib_port,
				    uint64_t *action_flags);

/**
 * @brief Create destination drop action.
 *
 * Matching packets with this destination action will be dropped.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_dest_drop(struct nv_hws_context *ctx,
			       uint64_t action_flags);

/**
 * @brief Create destination default miss action.
 *
 * Matching packets with this destination action will be forwarded
 * to the default miss based on the table type.
 * Defaults are:
 *   NIC_RX: Drop
 *   NIC_TX: Wire
 *   FDB: Go to e-switch manager Vport
 *   RDMA_TRANSPORT_RX: RDMA Transport Offload
 *   RDMA_TRANSPORT_TX: RDMA TX
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_dest_default_miss(struct nv_hws_context *ctx,
				       uint64_t action_flags);

/**
 * @brief Create destination goto table action.
 *
 * Matching packets with this destination action will be forwarded to the
 * provided "tbl" as the destination table.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] tbl Destination table to forward to packets to.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_dest_table(struct nv_hws_context *ctx,
				struct nv_hws_table *tbl,
				uint64_t action_flags);

/**
 * @brief Create destination goto vport action.
 *
 * Matching packets with this destination action will be forwarded to the
 * provided "ib_port_num".
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] ib_port_num Destination ib_port number.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_dest_vport(struct nv_hws_context *ctx,
				uint32_t ib_port_num,
				uint64_t action_flags);

/**
 * @brief Create destination goto TIR action.
 *
 * Matching packets with this destination action will be forwarded to the
 * the TIR (Transport Interface Receive) provided as a devx object.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] obj TIR devx object.
 * @param[in] is_local If context was created with shared_ibv_ctx, is_local
 *                     describes if TIR belongs to ibv_ctx (local) or
 *                     shared_ibv_ctx (not local).
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_dest_tir(struct nv_hws_context *ctx,
			      struct nv_hws_devx_obj *obj,
			      bool is_local,
			      uint64_t action_flags);

/**
 * @brief Create destination RDMA responder action.
 *
 * Matching packets with this destination action will be forwarded to
 * their destination RDMA QP and will generate the required response.
 * This action is only supported in table type RDMA_TRANSPORT_RX.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resp Action type to perform by the responder.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_dest_rdma_resp(struct nv_hws_context *ctx,
				    enum nv_hws_action_rdma_resp resp,
				    uint64_t action_flags);

/**
 * @brief Create destination matcher action.
 *
 * Matching packets with this destination action will be forwarded to
 * the destination matcher index provided in rule_actions array. The index
 * is specified after the last template rule_action in dest_matcher.
 * following .
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] matcher Destination matcher.
 * @param[in] type Matcher destination type.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_dest_matcher(struct nv_hws_context *ctx,
				  struct nv_hws_matcher *matcher,
				  enum nv_hws_action_dest_matcher_type type,
				  uint64_t action_flags);

/**
 * @brief Create destination root table action.
 *
 * Matching packets with this destination action will be forwarded back
 * to the root table (kernel) based on the provided root matchers priority.
 * This action created only for non-root tables.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] priority The priority of matcher in the root table to jump to.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_dest_root(struct nv_hws_context *ctx,
			       uint16_t priority,
			       uint64_t action_flags);

/**
 * @brief Create a multi destination array.
 *
 * Matching packets with this destination action will be duplicated to
 * multiple destinations provided in the destination list.
 *
 * @param[in] ctx the context in which the new action will be created.
 * @param[in] num_dest The number of destination in the destination array.
 * @param[in] dests The destination array. Each contains a destination action
 *                  and can have additional actions to be executed prior to it.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_dest_array(struct nv_hws_context *ctx,
				size_t num_dest,
				struct nv_hws_action_dest_attr dests[],
				uint64_t action_flags);

/**
 * @brief Create TAG action.
 *
 * Matching packets with this action will be tagged with defined value.
 * Tag value is provided as part of actions_data.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_tag(struct nv_hws_context *ctx,
			 uint64_t action_flags);

/**
 * @brief Create pop VLAN action.
 *
 * Matching packets with this action will be removed from its first outer
 * VLAN tag.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
*/
struct nv_hws_action *
nv_hws_action_create_pop_vlan(struct nv_hws_context *ctx,
			      uint64_t action_flags);

/**
 * @brief Create push VLAN action.
 *
 * Matching packets with this action will get a VLAN tag in the outer
 * first VLAN. VLAN tag value is provided as part of actions_data.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_push_vlan(struct nv_hws_context *ctx,
			       uint64_t action_flags);

/**
 * @brief Create modify header action.
 *
 * Matching packets with this action will be modified according to the
 * provided modify actions in the pattern. Modify actions format is
 * defined in the PRM.
 * Per rule modify values and offset are provided as part of actions_data.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Argument array of resource corresponding to the pattern size.
 *                     If the action is used for root, resource should not be
 *                     supplied. Exact pattern with values should not be
 *                     supplied for root actions.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] pattern PRM pattern containing modify action information.
 * @param[in] reparse Reevaluate packet headers after modifications mode.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_modify_header(struct nv_hws_context *ctx,
				   struct nv_hws_resource *resource[],
				   uint8_t num_of_resources,
				   struct nv_hws_action_modify_header *pattern,
				   enum nv_hws_action_reparse reparse,
				   uint64_t action_flags);

/**
 * @brief Create reformat decapsulation L2 action.
 *
 * Matching packets with this action will be stripped from the outer L2 tunnel
 * header.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_reformat_tnl_l2_to_l2(struct nv_hws_context *ctx,
					   uint64_t action_flags);

/**
 * @brief Create reformat L2 encapsulation action.
 *
 * Matching packets with this action will be added with and outer L2 tunnel
 * header. Header data and resource offset are provided as part of actions_data.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Argument array of resources corresponding to the L2 header size.
 *                     resource should not be supplied for root actions.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] hdr_data Header data information.
 *                     Exact hdr_data should be supplied for root actions.
 *                     Exact hdr_data size should be supplied for non-root actions.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_reformat_l2_to_tnl_l2(struct nv_hws_context *ctx,
					   struct nv_hws_resource *resource[],
					   uint8_t num_of_resources,
					   struct nv_hws_action_hdr_data *hdr_data,
					   uint64_t action_flags);

/**
 * @brief Create reformat L3 encapsulation action.
 *
 * Matching packets with this action will removed from the existing outer L2
 * header and added with a new provided outer L2 and L3 header.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Argument array of resource corresponding to the L2 L3 header size.
 *                     resource should not be supplied for root actions.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] hdr_data The new modified header information.
 *                     Exact hdr_data should be supplied for root actions.
 *                     Exact hdr_data size should be supplied for non-root actions.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_reformat_l2_to_tnl_l3(struct nv_hws_context *ctx,
					   struct nv_hws_resource *resource[],
					   uint8_t num_of_resources,
					   struct nv_hws_action_hdr_data *hdr_data,
					   uint64_t action_flags);

/**
 * @brief Create reformat trailer action..
 *
 * Support reformat trailer action, this action allows to
 * insert/remove specific crypto security protocol trailer
 * on the packet.
 * For now support IPsec crypto protocol trailer.
 * The trailer should be added before encrypting the packet
 * in Tx flow, and it can be removed after decryption in Rx flow.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] attr of type struct nv_hws_action_trailer_attr.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_reformat_trailer(struct nv_hws_context *ctx,
				      struct nv_hws_action_trailer_attr *attr,
				      uint64_t action_flags);

/**
 * @brief Create reformat L3 decapsulation action.
 *
 * Matching packets with this action will be removed from the outer tunnel L2
 * and L3 and added with a new outer L2 header. Header data and resource offset
 * are provided as part of actions_data.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Argument array of resource corresponding to the L2 header size.
 *                     resource should not be supplied for root actions.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] hdr_data The new L2 header information.
 *                     Exact L2 header should be supplied for root actions.
 *                     Exact hdr_data size should be supplied for non-root actions.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_reformat_tnl_l3_to_l2(struct nv_hws_context *ctx,
					   struct nv_hws_resource *resource[],
					   uint8_t num_of_resources,
					   struct nv_hws_action_hdr_data *hdr_data,
					   uint64_t action_flags);

/**
 * @brief Create insert header action.
 *
 * Inserts a list of words (byte pairs) as a header inside the packet.
 * An anchor is used as a pre-defined location for the insertion
 * and an additional offset can be added relative to the anchor.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Argument array of resource corresponding to the input header size.
 * @param[in] num_of_resuroces The size of the resource array.
 * @param[in] hdr Insert header information.
 * @param[in] flags Action creation flags. (enum nv_hws_action_flags).
 * @return Pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_insert_header(struct nv_hws_context *ctx,
				   struct nv_hws_resource *resource[],
				   uint8_t num_of_resources,
				   struct nv_hws_action_insert_header *hdr,
				   uint64_t action_flags);

/**
 * @brief Create remove header action.
 *
 * Removes a portion of the packet header based on specified anchors or offset.
 * Supports removal by header type (using start and end anchors) or by offset (size).
 * Provides options for decapsulation, reparse control, and flexible header manipulation.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] attr Specifies the remove header type, start/end anchor and size.
 * @param[in] flags Action creation flags. (enum nv_hws_action_flags)
 * @return Pointer to nv_hws_action on success NULL otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_remove_header(struct nv_hws_context *ctx,
				   struct nv_hws_action_remove_header_attr *attr,
				   uint64_t action_flags);

/**
 * @brief Create counter action.
 *
 * Matching packets with this action will be counted using a counter resource.
 * Counter offset from the resource is provided as part of action_data.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Array of counter resources used for that action.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_counter(struct nv_hws_context *ctx,
			     struct nv_hws_resource *resource[],
			     uint8_t num_of_resources,
			     uint64_t action_flags);

/**
 * @brief Create ASO meter action.
 *
 * Meter ASO allows monitoring the packet rate for specific flows.
 * Per rule ASO values and ASO offset are provided as part of rule action_data.
 * After ASO execution, the meter color will be written to register C
 * specified by the user in attr.return_reg.
 * Per rule ASO values and ASO offset are provided as part of rule action_data.
 *
 * RegisterC: [63:32] - Unchanged
 *            [31:0]  - Output: Meter color
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Array of ASO resources corresponding to the ASO object.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] attr ASO action creation attributes.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_aso_meter(struct nv_hws_context *ctx,
			       struct nv_hws_resource *resource[],
			       uint8_t num_of_resources,
			       struct nv_hws_action_aso_attr *attr,
			       uint64_t action_flags);

/**
 * @brief Create ASO Connection tracking action.
 *
 * Connection tracking ASO allows tracking the TCP connection and examine the
 * validity of incoming or outgoing packets on this connection with respect to
 * the connection state. After ASO execution, the connection tracking syndrome
 * will be written to register C specified by the user in attr.return_reg.
 * Per rule ASO values and ASO offset are provided as part of rule action_data.
 *
 * RegisterC: [63:32] - Unchanged
 *            [31:0]  - Output: CT syndrome
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Array of ASO resources corresponding to the ASO object.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] attr ASO action creation attributes.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_aso_ct(struct nv_hws_context *ctx,
			    struct nv_hws_resource *resource[],
			    uint8_t num_of_resources,
			    struct nv_hws_action_aso_attr *attr,
			    uint64_t action_flags);

/**
 * @brief Create ASO first-hit indication action.
 *
 * First-hit ASO object allows tracking when a rule is hit by a packet.
 * After ASO execution, the previous first-hit value will be written to register C
 * specified by the user in attr.return_reg.
 * Per rule ASO values and ASO offset are provided as part of rule action_data.
 *
 * RegisterC: [63:32] - Unchanged
 *            [31:0]  - Output: Previous first-hit value
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Array of ASO resources corresponding to the ASO object.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] attr ASO action creation attributes.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_aso_first_hit(struct nv_hws_context *ctx,
				  struct nv_hws_resource *resource[],
				  uint8_t num_of_resources,
				  struct nv_hws_action_aso_attr *attr,
				  uint64_t action_flags);

/**
 * @brief Create ASO IPsec action.
 *
 * ASO IPsec action for hardware offload operations.
 * In Tx flow, the sequence number is incremented before packet encryption.
 * In Rx flow, the decrypted packet is validated against the replay protection
 * window. The supplied resource should be configured with the full_offload attribute.
 * After ASO execution, the IP-Sec syndrome will be written to register C
 * specified by the user in attr.return_reg.
 * Per rule ASO values and ASO offset are provided as part of rule action_data.
 *
 * RegisterC: [63:32] - Unchanged
 *            [31:0]  - Output: IP-Sec syndrome
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Array of ASO resources of type IPSEC_OFFLOAD.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] attr ASO action creation attributes.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_aso_ipsec(struct nv_hws_context *ctx,
			       struct nv_hws_resource *resource[],
			       uint8_t num_of_resources,
			       struct nv_hws_action_aso_attr *attr,
			       uint64_t action_flags);

/**
 * @brief Create ASO queue management action.
 *
 * ASO queue management action allows to manage the specified queue management
 * context. The ASO is used to verify there is enough space in the queue and to
 * increase the producer index and advance it accordingly.
 * After ASO execution, the queue state syndrome will be written to register C
 * specified by the user in attr.return_reg.
 * Per rule ASO values and ASO offset are provided as part of rule action_data.
 *
 * RegisterC: [63:32] - Unchanged
 *            [31:0]  - Output: Queue state syndrome
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Array of ASO resources of type NV_HWS_RESOURCE_TYPE_ASO_QUEUE_MNG.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] attr ASO action creation attributes.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_aso_queue_mng(struct nv_hws_context *ctx,
				   struct nv_hws_resource *resource[],
				   uint8_t num_of_resources,
				   struct nv_hws_action_aso_attr *attr,
				   uint64_t action_flags);

/**
 * @brief Create ASO flow entropy action.
 *
 * ASO flow entropy action allows the user to configure a routing policy and
 * distribute packets across different planes. Each matched packet will be assigned
 * to a selected plane. After ASO execution, the plane details will be written to
 * register C specified by the user in attr.return_reg.
 * Per rule ASO values and ASO offset are provided as part of rule action_data.
 *
 * RegisterC: [63:32] - Unchanged
 *            [31:0]  - Output: Plane details
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Array of ASO resources of type NV_HWS_RESOURCE_TYPE_ASO_FLOW_ENTROPY.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] attr ASO action creation attributes.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_aso_flow_entropy(struct nv_hws_context *ctx,
				      struct nv_hws_resource *resource[],
				      uint8_t num_of_resources,
				      struct nv_hws_action_aso_attr *attr,
				      uint64_t action_flags);

/**
 * @brief Create ASO memory action.
 *
 * ASO memory (URISC_MEM) action allows the user to perform a specific memory operation on
 * the aso memory resource.
 * The possible memory operations are described in the enum nv_hws_action_aso_memory_op,
 * The operation is provided as part of action creation in operation parameter.
 * The input and output values for the operation are set into the same register C
 * selected by the user in attr, return_reg.
 * Per rule ASO values and ASO offset are provided as part of rule action_data.
 * RegisterC: [63:0] - Input/Output: value
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Array of ASO resources of type NV_HWS_RESOURCE_TYPE_ASO_MEMORY.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] attr ASO action creation attributes.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @param[in] operation ASO memory operation.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_aso_memory(struct nv_hws_context *ctx,
				struct nv_hws_resource *resource[],
				uint8_t num_of_resources,
				struct nv_hws_action_aso_attr *attr,
				enum nv_hws_action_aso_memory_op operation,
				uint64_t action_flags);

/**
 * @brief Create ASO FIFO action.
 *
 * ASO FIFO action allows the user to manage and execute operation over a ASO object
 * which performs as a stack.
 * The operations allowed, are described in nv_hws_action_aso_fifo_op
 * such as push and pop.
 * The input and output values for the operation are set into the same register C
 * selected by the user in attr, return_reg.
 * Per rule ASO values and ASO offset are provided as part of rule action_data,
 * as well as the stack operation to be performed.
 * RegisterC: [63:0] - Input/Output: value, stack operation output / input stored in this register,
 * According to the size of the ASO object, the output will be stored in the lower 32 bits.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Array of ASO resources of type NV_HWS_RESOURCE_TYPE_ASO_FIFO_2B or
 *	NV_HWS_RESOURCE_TYPE_ASO_FIFO_4B.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] attr ASO action creation attributes.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_aso_fifo(struct nv_hws_context *ctx,
			      struct nv_hws_resource *resource[],
			      uint8_t num_of_resources,
			      struct nv_hws_action_aso_attr *attr,
			      uint64_t action_flags);

/**
 * @brief Create forced execution barrier action.
 *
 * Execution barrier allows forcing a barrier between action execution.
 * nv_hws API assures action execution order based on the action template.
 * Using the force barrier is discouraged since it might create an unnecessary
 * barrier that can lead to performance degredation and high memory usage.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_barrier(struct nv_hws_context *ctx,
			     uint64_t action_flags);

/**
 * @brief Create NAT64 action.
 *
 * The action converts IPv4 header to IPv6 and vice versa based on RFC 6052.
 * The user defines which direction is required (IPv4 -> IPv6, IPv6 -> IPv4)
 * and the action will collect the relevant control values and process it to fit
 * to the converted type.
 * As well the original src/dst address can be backup to the target type,
 * according to the RFC-6052 that defines how to backup the address.
 * In order to process that protocol parameters the user needs supply few registers
 * for the action to be used at this flow.
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] attr The relevant attribute of the NAT action.
 * (see nv_hws_action_nat64_attr for more details).
 * @param[in] action_flags
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_nat64(struct nv_hws_context *ctx,
			   struct nv_hws_action_nat64_attr *attr,
			   uint64_t action_flags);

/**
 * @brief Create inline action.
 *
 * Create a single modify header action as inline.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] inline_action PRM format modify actions control pattern.
 * @param[in] reparse Reevaluate packet headers after modifications.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return Pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_inline(struct nv_hws_context *ctx,
			   __be64 inline_action,
			   enum nv_hws_action_reparse reparse,
			   uint64_t action_flags);

/**
 * @brief Create crypto action.
 *
 * Create crypto action, this action will create specific security protocol.
 * Supporting resources of type NV_HWS_RESOURCE_TYPE_IPSEC_OFFLOAD,
 * NV_HWS_RESOURCE_TYPE_DEK and NV_HWS_RESOURCE_TYPE_PROG_MASTER_KEY.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] resource Array of crypto resources corresponding to the crypto type.
 * @param[in] num_of_resources The size of the resource array.
 * @param[in] crypto_type Crypto protocol (IPsec/PSP).
 * @param[in] crypto_op Crypto operation encrypt/decrypt.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_crypto(struct nv_hws_context *ctx,
			    struct nv_hws_resource *resource[],
			    uint8_t num_of_resources,
			    enum nv_hws_action_crypto_type crypto_type,
			    enum nv_hws_action_crypto_op crypto_op,
			    uint64_t action_flags);

/**
 * @brief Create generate CQE action.
 *
 * Create generate CQE action, this action will give the ability to copy
 * specified metadata registers and generate a CQE with this info and
 * post it to a specified CQ.
 *
 * @param[in] ctx The context in which the new action will be created.
 * @param[in] cqn The CQ number to post the CQE to.
 * @param[in] start_reg The first registerC 64b to copy data from.
 * @param[in] reg_count Number of registerC 64b to copy from start_reg to the CQE.
 * @param[in] action_flags From enum nv_hws_action_flags.
 * @return pointer to nv_hws_action on success NULL with errno otherwise.
 */
struct nv_hws_action *
nv_hws_action_create_gen_cqe(struct nv_hws_context *ctx,
			     uint32_t cqn,
			     enum nv_hws_action_reg_c64 start_reg,
			     uint8_t reg_count,
			     uint64_t action_flags);

/**
 * @brief Destroy action.
 *
 * Destroy a previously created action.
 *
 * @param[in] action Action to destroy.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_action_destroy(struct nv_hws_action *action);

/**
 * @brief Enqueue data argument write object.
 *
 * Posting argument write into the queue.
 *
 * @param[in] action Action object.
 * @param[in] resource Resource object.
 * @param[in] qid Queue id.
 * @param[in] attr Argument write attribute.
 * @return Zero on success non zero otherwise with errno set.
 */
int
nv_hws_action_enqueue_arg_write(struct nv_hws_action *action,
				struct nv_hws_resource *resource,
				uint16_t qid,
				struct nv_hws_action_enqueue_arg_write_attr *attr);

/**
 * @brief Query number of queued operations.
 *
 * @param[in] ctx The context to which the queue belongs to.
 * @param[in] queue_id The id of the queue to query.
 * @return Negative number on failure with errno set.
 *         The number of pending operations otherwise.
 */
int nv_hws_queue_pending(struct nv_hws_context *ctx, uint16_t queue_id);

/**
 * @brief Poll queue for operation completions.
 *
 * Poll for queue operations completion status. An operation is not finalized
 * until a valid completion is acquired using queue poll. Polling is required
 * for asynchronous rule and resource operations.
 *
 * @param[in] ctx The context to which the queue belong to.
 * @param[in] queue_id The id of the queue to poll.
 * @param[out] res Result completion array.
 * @param[in] res_nb Maximum number of results to return.
 * @return Negative number on failure with errno set.
 *         The number of completions otherwise.
 */
int nv_hws_queue_poll(struct nv_hws_context *ctx,
		      uint16_t queue_id,
		      struct nv_hws_queue_op_result res[],
		      uint32_t res_nb);

/**
 * @brief Execute an operation on the queue.
 *
 * Perform an operation on the queue.
 *
 * @param[in] ctx Context to which the queue belong to.
 * @param[in] queue_id The id of the queue to perform the action on.
 * @param[in] queue_op Queue operation to perform. (enum nv_hws_queue_op)
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_queue_execute_op(struct nv_hws_context *ctx,
			    uint16_t queue_id,
			    uint32_t queue_op);

/**
 * @brief Get table metrics.
 *
 * Get metric information regarding the table.
 *
 * @param[in] tbl The table to extract the info from.
 * @param[in] flags From enum nv_hws_metric_flag.
 * @param[out] out A struct to fill with the requested info.
 * @return Zero on success and a negative number on failure with errno set.
 */
int nv_hws_metric_query_table(struct nv_hws_table *tbl,
			      uint64_t flags,
			      struct nv_hws_metric_table *out);

/**
 * @brief Get matcher metrics.
 *
 * Get metric information for the matcher.
 * Get cost analysis and general information regarding the matcher.
 * Cost analysis can be used to compare different matcher configurations
 * cost in a few aspects, and optimize accordingly.
 * The cost reflects worst case scenario of match template and action template
 * combination in this matcher.
 *
 * @param[in] matcher The matcher to extract the info from.
 * @param[in] flags From enum nv_hws_metric_flag.
 * @param[out] out A struct to fill with the requested info and metrics.
 * @return Zero on success and a negative number on failure with errno set.
 */
int nv_hws_metric_query_matcher(struct nv_hws_matcher *matcher,
				uint64_t flags,
				struct nv_hws_metric_matcher *out);

/**
 * @brief Get matcher template metrics.
 *
 * Extract metric information regarding a specific match template and
 * action template combination cost analytics and general info.
 * This cost analysis can be used to measure how much a rule of this match
 * and action templates combination cost in insertion or how much HW memory
 * foot print it takes, or how much it costs to process a packet reaching
 * this combination.
 *
 * @param[in] matcher The matcher to extract the info from.
 * @param[in] attr contains the attributes of the query.
 * @param[out] out A struct to fill with the requested info and metrics.
 * @return Zero on success and a negative number on failure with errno set.
 */
int
nv_hws_metric_query_matcher_template(struct nv_hws_matcher *matcher,
				     struct nv_hws_metric_matcher_template_attr *attr,
				     struct nv_hws_metric_matcher_template *out);

/**
 * @brief Set the log handler for NV_HWS logging.
 *
 * Set a handler function that will be used to log NV_HWS messages.
 * The handler will be called for each log message NV_HWS generates, instead
 * of the default behavior of redirecting to stderr. Setting a NULL handler will
 * restore the default behavior.
 *
 * The function is not thread-safe; it should be called once during application
 * initialization, before any logging occurs.
 *
 * @param[in] handler The handler function pointer to set.
 */
void nv_hws_debug_set_log_handler(nv_hws_debug_log_handler handler);

/**
 * @brief Dump flow steering app info.
 *
 * Dump all the info regarding the steering objects used by
 * the application to a file, this file will be used as input
 * to the steering dump tool for further info extracting and parsing.
 *
 * @param[in] ctx The context which to dump the info from.
 * @param[in] f The file to write the dump to.
 * @return Zero on success non zero otherwise with errno set.
 */
int nv_hws_debug_dump(struct nv_hws_context *ctx, FILE *f);

/**
 * @brief Create a new flex parsing graph.
 *
 * Create a user-defined packet parsing tree that extends the default predefined
 * parsing tree. This allows the processing of non-native headers that are not
 * supported by the device.
 *
 * @param[in] ctx The context in which the new graph will be created.
 * @return Pointer to nv_hws_parser_graph on success NULL with errno otherwise.
 */
struct nv_hws_parser_graph *
nv_hws_parser_graph_create(struct nv_hws_context *ctx);

/**
 * @brief Destroys the parser graph.
 *
 * Destroy previously created unbounded graph.
 *
 * @param[in] graph Graph object to destroy.
 * @return Zero on success, otherwise non zero with errno set.
 */
int nv_hws_parser_graph_destroy(struct nv_hws_parser_graph *graph);

/**
 * @brief Create a new parser graph node.
 *
 * @param[in] graph Graph object that includes the node.
 * @param[in] node_cfg Node configuration struct.
 * @return Pointer to nv_hws_parser_node on success NULL with errno otherwise.
 */
struct nv_hws_parser_node *
nv_hws_parser_node_create(struct nv_hws_parser_graph *graph,
			  struct nv_hws_parser_node_cfg *node_cfg);

/**
 * @brief Destroy an existing parser graph node.
 *
 * @param[in] node Parser node.
 * @return Zero on success, otherwise non zero with errno set.
 */
int nv_hws_parser_node_destroy(struct nv_hws_parser_node *node);

/**
 * @brief Create a new parser graph arc.
 *
 * Arc in the parse graph of a network device.
 * The arc represents a transition between nodes (headers) in the packet
 * parsing process, defined by specific conditions.
 *
 * @param[in] graph Graph object.
 * @param[in] arc_cfg Arc configuration struct.
 * @return Pointer to nv_hws_parser_arc on success NULL with errno
 * otherwise.
 */
struct nv_hws_parser_arc *
nv_hws_parser_arc_create(struct nv_hws_parser_graph *graph,
			 struct nv_hws_parser_arc_cfg *arc_cfg);

/**
 * @brief Destroy an existing parser graph arc.
 *
 * @param[in] arc Parser graph arc.
 * @return Zero on success, otherwise non zero with errno set.
 */
int nv_hws_parser_arc_destroy(struct nv_hws_parser_arc *arc);

/**
 * @brief Create a new parse graph sampler.
 *
 * Add a 32 bit sampler to the parser node.
 *
 * @param[in] node Parser node object.
 * @param[in] sampler_cfg Sampler configuration struct.
 * @return Pointer to nv_hws_parser_sampler on success NULL with errno
 * otherwise.
 */
struct nv_hws_parser_sampler *
nv_hws_parser_sampler_create(struct nv_hws_parser_node *node,
			     struct nv_hws_parser_sampler_cfg *sampler_cfg);

/**
 * @brief Query sampler attributes.
 *
 * Query the sampler for its attributes, including the sampled field name, the
 * sample success indication ok bit, and the header modification field ID.
 *
 * @param[in] sampler Parser sampler object.
 * @param[out] parser_fname FNAME for matching on the sampled field.
 * @param[out] parser_ok_bit_fname FNAME for Indication of successful parsing and sampling.
 * @param[out] field_header Field header location.
 * @param[out] modify_header_field Modify header field id that refers to this samplers' value.
 * @return Zero on success, otherwise non zero with errno set.
 */
int nv_hws_parser_sampler_query(struct nv_hws_parser_sampler *sampler,
				enum nv_hws_field_name *parser_fname,
				enum nv_hws_field_name *parser_ok_bit_fname,
				enum nv_hws_field_header *field_header,
				uint32_t *modify_header_field);

/**
 * @brief Destroy an existing sampler.
 *
 * @param[in] sampler Parser sampler object.
 * @return Zero on success, non zero with errno set otherwise.
 */
int nv_hws_parser_sampler_destroy(struct nv_hws_parser_sampler *sampler);

/**
 * @brief Bind parse graph.
 *
 * Bind the graph to the default parsing graph, a bound graph cannot be altered.
 *
 * @param[in] graph Parser graph object.
 * @return Zero on success, non zero with errno set otherwise.
 */
int nv_hws_parser_graph_bind(struct nv_hws_parser_graph *graph);

/**
 * @brief Unbind parse graph.
 *
 * Unbind all bound operations used with this graph.
 *
 * @param[in] graph Previously bound parser graph.
 * @return Zero on success, non zero with errno set otherwise.
 */
int nv_hws_parser_graph_unbind(struct nv_hws_parser_graph *graph);

/**
 * @brief Create a sampler for geneve TLV options.
 *
 * Initializes a Geneve TLV (Type-Length-Value) options sampler based on the
 * provided context and the configuration.
 *
 * @param[in] ctx The context in which the new geneve object will be created.
 * @param[in] cfg Geneve option configuration.
 * @return Pointer to the sampler on success, NULL with errno otherwise.
 */
struct nv_hws_parser_sampler *
nv_hws_parser_geneve_tlv_options_create(struct nv_hws_context *ctx,
					struct nv_hws_parser_geneve_option_cfg *cfg);

/**
 * @brief Calculate encapsulation header entropy.
 *
 * When using packet encapsulation offload, HW calculates an entropy value,
 * based on the inner packet headers. The entropy field is placed inside
 * the outer packet headers. This function allows to precalculate this value.
 *
 * @param[in] ctx The context of the device.
 * @param[in] fields The fields to calculate entropy over.
 * @param[in] entropy_sz Entropy size to calculate and write into ret_entropy.
 * @param[out] ret_entropy Entropy array to hold calculation result.
 * @return Zero on success, non zero with errno set otherwise.
 */
int nv_hws_encap_entropy_hash(struct nv_hws_context *ctx,
			      struct nv_hws_encap_entropy_hash_fields *fields,
			      enum nv_hws_encap_entropy_hash_sz entropy_sz,
			      uint8_t *ret_entropy);

/**
 * @brief Create a new low-level match template.
 *
 * Match template specifies the items and fields to match on using the matcher.
 * The low-level match template interface allows the user to precisely describe
 * the layout of definers and masks that constitute this template.
 *
 * @param[in] ctx The context of the device.
 * @param[in] definers An array of definer layouts for matching.
 * @param[in] num_of_definers Number of definers (must be 1 or 2).
 * @param[in] attr Attributes controlling low-level match template creation.
 * @return Match template on success, or NULL with errno set on failure.
 */
struct nv_hws_mt *
nv_hws_ll_match_template_create(struct nv_hws_context *ctx,
				struct nv_hws_ll_definer definers[],
				uint8_t num_of_definers,
				struct nv_hws_ll_mt_attr *attr);

/**
 * @brief Destroy a low-level match template.
 *
 * This function releases all resources associated with a low-level match
 * template that was previously created using nv_hws_ll_match_template_create().
 *
 * @param[in] ctx The context of the device.
 * @param[in] mt Pointer to the match template to be destroyed.
 * @return Zero on success, or negative errno value on failure.
 */
int nv_hws_ll_match_template_destroy(struct nv_hws_context *ctx,
				     struct nv_hws_mt *mt);

#ifdef __cplusplus
}
#endif

#endif /* NV_HWS */
