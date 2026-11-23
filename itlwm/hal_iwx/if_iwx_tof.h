/* Generated from Linux drivers/net/wireless/intel/iwlwifi/fw/api/location.h (GPL-2.0 / ISC), command TOF_RANGE_REQ_CMD v13, notif v9. */
#ifndef IF_IWX_TOF_H
#define IF_IWX_TOF_H

#define IWX_LOCATION_GROUP		0x8
#define IWX_TOF_RANGE_REQ_CMD		0x0
#define IWX_TOF_RANGE_RESPONSE_NOTIF	0xff
#define IWX_TOF_MAX_APS			5
#define IWX_TOF_BW_NUM			5
#define IWX_HLTK_11AZ_LEN		32
#define IWX_TK_11AZ_LEN			32

#define IWX_TOF_INITIATOR_FLAGS_FAST_ALGO_DISABLED	0x1
#define IWX_TOF_INITIATOR_FLAGS_RX_CHAIN_SEL_A	0x2
#define IWX_TOF_INITIATOR_FLAGS_RX_CHAIN_SEL_B	0x4
#define IWX_TOF_INITIATOR_FLAGS_RX_CHAIN_SEL_C	0x8
#define IWX_TOF_INITIATOR_FLAGS_TX_CHAIN_SEL_A	0x10
#define IWX_TOF_INITIATOR_FLAGS_TX_CHAIN_SEL_B	0x20
#define IWX_TOF_INITIATOR_FLAGS_TX_CHAIN_SEL_C	0x40
#define IWX_TOF_INITIATOR_FLAGS_MACADDR_RANDOM	0x80
#define IWX_TOF_INITIATOR_FLAGS_SPECIFIC_CALIB	0x8000
#define IWX_TOF_INITIATOR_FLAGS_COMMON_CALIB	0x10000
#define IWX_TOF_INITIATOR_FLAGS_NON_ASAP_SUPPORT	0x100000

#define IWX_INITIATOR_AP_FLAGS_ASAP	0x2
#define IWX_INITIATOR_AP_FLAGS_LCI_REQUEST	0x4
#define IWX_INITIATOR_AP_FLAGS_CIVIC_REQUEST	0x8
#define IWX_INITIATOR_AP_FLAGS_DYN_ACK	0x10
#define IWX_INITIATOR_AP_FLAGS_ALGO_LR	0x20
#define IWX_INITIATOR_AP_FLAGS_ALGO_FFT	0x40
#define IWX_INITIATOR_AP_FLAGS_MCSI_REPORT	0x100
#define IWX_INITIATOR_AP_FLAGS_NON_TB	0x200
#define IWX_INITIATOR_AP_FLAGS_TB	0x400
#define IWX_INITIATOR_AP_FLAGS_SECURED	0x800
#define IWX_INITIATOR_AP_FLAGS_LMR_FEEDBACK	0x1000
#define IWX_INITIATOR_AP_FLAGS_USE_CALIB	0x2000
#define IWX_INITIATOR_AP_FLAGS_PMF	0x4000
#define IWX_INITIATOR_AP_FLAGS_TERMINATE_ON_LMR_FEEDBACK	0x8000
#define IWX_INITIATOR_AP_FLAGS_TEST_INCORRECT_SAC	0x10000
#define IWX_INITIATOR_AP_FLAGS_TEST_BAD_SLTF	0x20000

#define IWX_LOCATION_FRAME_FORMAT_LEGACY	0x0
#define IWX_LOCATION_FRAME_FORMAT_HT	0x1
#define IWX_LOCATION_FRAME_FORMAT_VHT	0x2
#define IWX_LOCATION_FRAME_FORMAT_HE	0x3

#define IWX_LOCATION_BW_20MHZ	0x0
#define IWX_LOCATION_BW_40MHZ	0x1
#define IWX_LOCATION_BW_80MHZ	0x2
#define IWX_LOCATION_BW_160MHZ	0x3
#define IWX_LOCATION_BW_320MHZ	0x4

#define IWX_LOCATION_FMT_BW_FORMAT	0xf
#define IWX_LOCATION_FMT_BW_BANDWIDTH	0xf0

#define IWX_TOF_RANGE_REQUEST_STATUS_SUCCESS	0x0
#define IWX_TOF_RANGE_REQUEST_STATUS_BUSY	0x1

#define IWX_TOF_ENTRY_SUCCESS	0x0
#define IWX_TOF_ENTRY_GENERAL_FAILURE	0x1
#define IWX_TOF_ENTRY_NO_RESPONSE	0x2
#define IWX_TOF_ENTRY_REQUEST_REJECTED	0x3
#define IWX_TOF_ENTRY_NOT_SCHEDULED	0x4
#define IWX_TOF_ENTRY_TIMING_MEASURE_TIMEOUT	0x5
#define IWX_TOF_ENTRY_TARGET_DIFF_CH_CANNOT_CHANGE	0x6
#define IWX_TOF_ENTRY_RANGE_NOT_SUPPORTED	0x7
#define IWX_TOF_ENTRY_REQUEST_ABORT_UNKNOWN_REASON	0x8
#define IWX_TOF_ENTRY_LOCATION_INVALID_T1_T4_TIME_STAMP	0x9
#define IWX_TOF_ENTRY_11MC_PROTOCOL_FAILURE	0xa
#define IWX_TOF_ENTRY_REQUEST_CANNOT_SCHED	0xb
#define IWX_TOF_ENTRY_RESPONDER_CANNOT_COLABORATE	0xc
#define IWX_TOF_ENTRY_BAD_REQUEST_ARGS	0xd
#define IWX_TOF_ENTRY_WIFI_NOT_ENABLED	0xe
#define IWX_TOF_ENTRY_RESPONDER_OVERRIDE_PARAMS	0xf

/* cipher values for iwx_tof_range_req_ap_entry.cipher */
#define IWX_LOCATION_CIPHER_CCMP_128	0
#define IWX_LOCATION_CIPHER_GCMP_128	1
#define IWX_LOCATION_CIPHER_GCMP_256	2
#define IWX_LOCATION_CIPHER_INVALID	3
#define IWX_LOCATION_CIPHER_MAX	4

struct iwx_tof_range_req_ap_entry {
	uint32_t initiator_ap_flags;
	uint8_t channel_num;
	uint8_t format_bw;
	uint8_t ctrl_ch_position;
	uint8_t ftmr_max_retries;
	uint8_t bssid[IEEE80211_ADDR_LEN];
	uint16_t burst_period;
	uint8_t samples_per_burst;
	uint8_t num_of_bursts;
	uint8_t sta_id;
	uint8_t cipher;
	uint8_t hltk[IWX_HLTK_11AZ_LEN];
	uint8_t tk[IWX_TK_11AZ_LEN];
	uint16_t calib[IWX_TOF_BW_NUM];
	uint16_t beacon_interval;
	uint8_t rx_pn[6];
	uint8_t tx_pn[6];
	uint8_t r2i_ndp_params;
	uint8_t i2r_ndp_params;
	uint8_t r2i_max_total_ltf;
	uint8_t i2r_max_total_ltf;
	uint8_t bss_color;
	uint8_t band;
	uint16_t min_time_between_msr;
} __packed;
struct iwx_tof_range_req_cmd {
	uint32_t initiator_flags;
	uint8_t request_id;
	uint8_t num_of_ap;
	uint8_t range_req_bssid[IEEE80211_ADDR_LEN];
	uint8_t macaddr_mask[IEEE80211_ADDR_LEN];
	uint8_t macaddr_template[IEEE80211_ADDR_LEN];
	uint32_t req_timeout_ms;
	uint32_t tsf_mac_id;
	struct iwx_tof_range_req_ap_entry ap[IWX_TOF_MAX_APS];
} __packed;
struct iwx_tof_range_rsp_ap_entry_ntfy {
	uint8_t bssid[IEEE80211_ADDR_LEN];
	uint8_t measure_status;
	uint8_t measure_bw;
	uint32_t rtt;
	uint32_t rtt_variance;
	uint32_t rtt_spread;
	int8_t rssi;
	uint8_t rssi_spread;
	uint8_t last_burst;
	uint8_t refusal_period;
	uint32_t timestamp;
	uint32_t start_tsf;
	uint32_t reserved1[2];
	uint32_t t2t3_initiator;
	uint32_t t1t4_responder;
	uint16_t common_calib;
	uint16_t specific_calib;
	uint32_t papd_calib_output;
	uint8_t rttConfidence;
	uint8_t reserved[3];
	uint8_t rx_pn[6];
	uint8_t tx_pn[6];
} __packed;
struct iwx_tof_range_rsp_ntfy {
	uint8_t request_id;
	uint8_t num_of_aps;
	uint8_t last_report;
	uint8_t reserved;
	struct iwx_tof_range_rsp_ap_entry_ntfy ap[IWX_TOF_MAX_APS];
} __packed;


static_assert(sizeof(struct iwx_tof_range_req_ap_entry) == 116, "tof ap entry");
static_assert(sizeof(struct iwx_tof_range_req_cmd) == 612, "tof req cmd");
static_assert(sizeof(struct iwx_tof_range_rsp_ap_entry_ntfy) == 72, "tof rsp entry");
static_assert(sizeof(struct iwx_tof_range_rsp_ntfy) == 364, "tof rsp");

#endif /* IF_IWX_TOF_H */
