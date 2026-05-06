#ifndef _SM_H_
#define _SM_H_

#include "bl_lmac_msg.h"

#include "mac.h"
#include "ke_task.h"

/// Maximum length of the AssocReq IEs
#define SM_MAX_IE_LEN   256
#define SDIO_E2A_MSG_BUF_SIZE 1024

struct rxu_mgt_ind;
struct softmac_vif_info_tag;

/// station environment structure
struct sm_disconnect_tag
{
    /// Pointer to the message structure used for the disconnect indication
    struct ke_msg *ind;
    /// Indicate if pending disconnection was requested by host or not
    bool host_initiated;
    bool host_initiated_org;
    /// Pointer to the VIF structure for which the disconnection procedure is pending
    struct softmac_vif_info_tag *vif;
    /// Reason code for disconnection
    uint16_t reason_code;
};

/// station environment structure
struct sm_env_tag
{
    /// Disconnection parameters
    struct sm_disconnect_tag disconnect;
    /// Pointer to the connection parameters
    struct sm_connect_req *connect_param;
    /// Pointer to the structure used for the connect indication upload
    struct sm_connect_ind *connect_ind;
    /// List of BSS configuration messages to send
    struct co_list bss_config;
    /// Indicate if passive scan has been used during join procedure
    bool join_passive;
    /// Whether Reassociation should be used instead of association
    bool reassoc;
    /// Flag indicating whether the transmission of a management frame requiring a
    /// response (e.g. Auth or AssocReq) is ongoing. This flag remains set until the frame
    /// is successfully transmitted, or the response timeout expires. While it is set, the
    /// frame can be retried by the SM.
    /// BL616 use counter instead of bool, record ongoing frame cnt, reset before connect.
    uint8_t tx_frame_ongoing;
    uint8_t tx_frame_failed;
    struct txl_frame_desc_tag * tx_frame;
    /// MAC address of previous AP - valid only if reassoc is true
    struct mac_addr prev_bssid;
};

/**
 ****************************************************************************************
 * @brief Handler for returning the size available for the API message
 *
 * @return the length of the API msg in bytes
 ****************************************************************************************
 */
#define MACIF_MAX_API_MSG_LEN (SDIO_E2A_MSG_BUF_SIZE - sizeof(struct inf_hdr) - sizeof(struct ke_msg))

/**
 ****************************************************************************************
 * @brief Convenient wrapper to ipc_e2a_msg that provides the maximum size that remains
 * in the msg for further data
 *
 *
 * @param[in] param_str parameter structure tag
 *
 * @return the max size available that can be allocated for a further use
 ****************************************************************************************
 */
#define MACIF_MAX_PARAM_LEN(param_str) (MACIF_MAX_API_MSG_LEN - sizeof_b(struct param_str))


/**
 ****************************************************************************************
 * @brief Initialize the SM context.
 ****************************************************************************************
 */
void sm_init(void);

/**
 ****************************************************************************************
 * @brief Search for the BSSID and Channel information in the scan results and/or the
 * connection parameters.
 *
 * @param[out] bssid  Pointer to the BSSID to join
 * @param[out] chan   Pointer to the channel on which the BSSID is
 ****************************************************************************************
 */
void sm_get_bss_params(struct mac_addr const **bssid,
                                struct mac_chan_def const **chan);

/**
 ****************************************************************************************
 * @brief Try to join the BSS indicated by the parameters.
 *
 * @param[in] bssid   Pointer to the BSSID to join
 * @param[in] chan    Pointer to the channel on which the BSSID is
 * @param[in] passive Indicate if passive scan has to be started
 ****************************************************************************************
 */
void sm_join_bss(struct mac_addr const *bssid,
                      struct mac_chan_def const *chan,
                      bool passive);

/**
 ****************************************************************************************
 * @brief Launch the scan to find the target BSS.
 *
 * @param[in] bssid   Pointer to the BSSID to join, if known. If not the WildCard BSSID
 *                    will be used for the scan.
 * @param[in] chan    Pointer to the channel on which the BSSID is, if known. If not the
 *                    scan is performed on all channels.
 ****************************************************************************************
 */
void sm_scan_bss(struct mac_addr const *bssid,
                       struct mac_chan_def const *chan);

/**
 ****************************************************************************************
 * @brief Function called at any time during the connection, used to indicate to the
 *        host the completion of the procedure (either successful or not).
 *
 * @param[in] status  Status of the connection procedure (@ref MAC_ST_SUCCESSFUL or any
 *                    other 802.11 status code)
 ****************************************************************************************
 */
void sm_connect_ind(uint16_t status);

/**
 ****************************************************************************************
 * @brief Function called upon reception of a AUTH frame from the AP.
 *
 * @param[in] param  Pointer to the kernel message containing the AUTH frame.
 ****************************************************************************************
 */
void sm_auth_handler(struct rxu_mgt_ind const *param);

/**
 ****************************************************************************************
 * @brief Function called upon reception of a ASSOC_RSP frame from the AP.
 *
 * @param[in] param  Pointer to the kernel message containing the ASSOC_RSP frame.
 ****************************************************************************************
 */
void sm_assoc_rsp_handler(struct rxu_mgt_ind const *param);

/**
 ****************************************************************************************
 * @brief Function called upon reception of a DEAUTH or DISASSOC frame from the AP.
 *
 * @param[in] param  Pointer to the kernel message containing the DEAUTH/DISASSOC frame.
 *
 * @return The message status to be returned to the kernel (@ref KE_MSG_CONSUMED or
 *         @ref KE_MSG_SAVED)
 ****************************************************************************************
 */
int sm_deauth_handler(struct rxu_mgt_ind const *param);

#ifdef NX_MFP
/**
 ****************************************************************************************
 * @brief Function called upon reception of a SA QUERY action frame.
 *
 * Only SA QUERY Request received on STA interfaces are handled.
 *
 * @param[in] param  Pointer to the kernel message containing SA_QUERY Action frame.
 ****************************************************************************************
 */
void sm_sa_query_handler(struct rxu_mgt_ind const *param);
#endif // NX_MFP

/**
 ****************************************************************************************
 * @brief Set the BSS parameters
 * This function prepares the list of BSS configuration messages that will be transmitted
 * to the Lower MAC.
 *
 ****************************************************************************************
 */
void sm_set_bss_param(void);

/**
 ****************************************************************************************
 * @brief Send the next BSS parameter message present in the list
 *
 ****************************************************************************************
 */
void sm_bss_config_send(void);

/**
 ****************************************************************************************
 * @brief Start the disconnection procedure.
 *
 * This function is called upon reception of a @ref SM_DISCONNECT_REQ, a
 * @ref MM_CONNECTION_LOSS_IND, a @ref RXU_MGT_IND carrying a DEAUTH/DISASSOC frame, or a
 * @ref SM_CONNECT_REQ request a re-association procedure to be started.
 * It initializes the disconnection parameter structure, and then checks if the data path
 * is currently empty for the entry associated to the AP we are connected to. If empty it
 * directly continues the disconnection process by calling the @ref sm_disconnect_continue
 * function.
 * Otherwise the disconnection process will continue upon reception of the
 * @ref ME_DATA_PATH_FLUSHED_IND message.
 *
 * @param[in] vif Pointer to the VIF structure attached to the disconnection procedure
 * @param[in] reason_code Reason code for the disconnection
 * @param[in] host_initiated Flag indicating whether the disconnection is initiated by the
 *                           host or not
 ****************************************************************************************
 */
void sm_disconnect_start(struct softmac_vif_info_tag *vif, 
                                uint16_t reason_code, bool host_initiated);

/**
 ****************************************************************************************
 * @brief Continues the disconnection procedure after notification of data path flush.
 *
 * This function is called upon reception of a @ref ME_DATA_PATH_FLUSHED_IND, or directly
 * from @ref sm_disconnect_start if data path was already empty. It continues the
 * disconnection procedure by sending the DEAUTH frame if required, or terminates it
 * otherwise.
 ****************************************************************************************
 */
void sm_disconnect_continue(void);

/**
 ****************************************************************************************
 * @brief Association completed operations.
 *
 * @param[in] aid Association Identifier provided by AP on connection (0 if IBSS).
 ****************************************************************************************
 */
void sm_assoc_done(uint16_t aid);

/**
 ****************************************************************************************
 * @brief Send MAC_FCTRL_ASSOCREQ or MAC_FCTRL_REASSOCREQ to the air
 ****************************************************************************************
 */
void sm_assoc_req_send(void);

/**
 ****************************************************************************************
 * @brief Send Authentication frame
 *
 * @param[in] auth_seq  Authentication sequence
 * @param[in] challenge Pointer on authentication challenge. Only needed for third
                        sequence of SHARED_KEY authentication. Should be NULL otherwise
 ****************************************************************************************
 */
void sm_auth_send(uint16_t auth_seq, uint32_t *challenge);

/**
 ****************************************************************************************
 * @brief Start External Authentication procedure
 *
 * Used when fw doesn't support an authentication method.
 * In this case the authentication is offloaded to the host.
 *
 * @param[in] akm  Authentication Key Management used for this connection
 ****************************************************************************************
 */
void sm_external_auth_start(uint32_t akm);

/**
 ****************************************************************************************
 * @brief End External Authentication procedure
 *
 * Called when host send status for external authentication.
 *
 * @param[in] status  Status (as in Status code of AUTH frame) of the external
 *                    authentication procedure
 ****************************************************************************************
 */
void sm_external_auth_end(uint16_t status);

/**
 ****************************************************************************************
 * @brief Check if external authentication is in progress
 * @return true if external authentication is in progress and false otherwise
 ****************************************************************************************
 */
bool sm_external_auth_in_progress(void);

/**
 ****************************************************************************************
 * @brief Starts FT over the air transitional state
 *
 * During FT over the air MIC must be computed between authentication and association.
 * As firmware doesn't support this, ask host to compute it. This function sends the
 * computation request to the host.
 *
 * @param[in] vif_idx    Index of the VIF
 * @param[in] ft_ie      HW address of the FT elements buffer
 * @param[in] ft_ie_len  Length, in bytes, of the ft_ie buffer
 ****************************************************************************************
 */
void sm_ft_auth_over_air_start(uint8_t vif_idx, PTR2UINT ft_ie, uint16_t ft_ie_len);

/**
 ****************************************************************************************
 * @brief Ends FT over the air transitional state
 *
 * Once host send back the updated elements, the association can continue.
 * The structure sm_connect_req of FT is reused and filled with missing information.
 * The current sm_connect_req structure stored inside sm_env.connect_param is updated and
 * replaced with the new one
 *
 * @param[in] param      Pointer to the structure sm_connect_req to use. To avoid several
 * copies of IEs elements,the FT uses directly a structure sm_connect_req message
 ****************************************************************************************
 */
void sm_ft_auth_over_air_end(struct sm_connect_req *param);

/**
 ****************************************************************************************
 * @brief Return PMKID count from RSN IE
 *
 * Find PMKID count field of RSN IE in a buffer of several IE.
 *
 * @param[in] ies     HW Address of IEs buffer
 * @param[in] ies_len Size, in bytes, of IEs buffer
 * @return PMKID count inside RSN IE. If RSN IE is not present or PMKID count is not
 * present then 0 is returned
 ****************************************************************************************
 */
int sm_get_rsnie_pmkid_count(PTR2UINT ies, uint16_t ies_len);
void sm_deauth_cfm(void *env, uint32_t status);


/// SM module environment
extern struct sm_env_tag sm_env;




#endif // _SM_H_
