/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "rwnx_config.h"

#include "bl_ipc_compat.h"

#include "ke_config.h"
#include "ke_task.h"
#include "ke_env.h"
#include "ke_queue.h"
#include "ke_event.h"
#include "ke_mem.h"

#include "scanu.h"
#include "scanu_task.h"
#include "sm_task.h"
#if NX_BEACONING
#include "apm_task.h"
#endif
#if NX_RM
#include "rm_task.h"
#endif
#if NX_FTM_INITIATOR
#include "ftm_task.h"
#endif

ke_task_id_t softmac_task_ids[] = {TASK_SCANU, TASK_SM
                                   #if NX_RM
                                   , TASK_RM
                                   #endif
                                   #if NX_BEACONING
                                   , TASK_APM
                                   #endif
                                   #if NX_FTM_INITIATOR
                                   , TASK_FTM
                                   #endif
                                   };

/// Table grouping the task descriptors
const struct ke_task_desc TASK_DESC[TASK_MAX] =
{
    [TASK_SCANU] = {scanu_state_handler, &scanu_default_handler, scanu_state, 
                    SCANU_STATE_MAX, SCANU_IDX_MAX},
    [TASK_SM]  = {NULL, &sm_default_handler, sm_state, 
                  SM_STATE_MAX, SM_IDX_MAX},
    
    #if NX_BEACONING
    [TASK_APM] = {NULL, &apm_default_handler, apm_state, 
                  APM_STATE_MAX, APM_IDX_MAX},
    #endif // NX_BEACONING
    
    #if NX_RM
    [TASK_RM] = {NULL, &rm_default_handler, rm_state, 
                 RM_STATE_MAX, RM_IDX_MAX},
    #endif // NX_RM
    
    #if NX_FTM_INITIATOR
    [TASK_FTM] = {NULL, &ftm_default_handler, ftm_state, 
                  FTM_STATE_MAX, FTM_IDX_MAX},
    #endif
};

bool softmac_task_ids_check(ke_task_id_t task_id) 
{
    int i = 0;

    for (i=0; i < sizeof(softmac_task_ids)/sizeof(ke_task_id_t); i++) {
        if (task_id == softmac_task_ids[i])
            return true;
    }

    return false;
}

bool ke_task_local(ke_task_id_t const id)
{
    return softmac_task_ids_check(id);
}

/**
 ****************************************************************************************
 * @brief Compare destination task callback
 ****************************************************************************************
 */
static bool cmp_dest_id(struct co_list_hdr const * msg, uint32_t dest_id)
{
    return ((struct ke_msg*)msg)->dest_id == dest_id;
}

/**
 ****************************************************************************************
 * @brief Reactivation of saved messages.
 *
 * This primitive looks for all the messages destined to the task ke_task_id that
 * have been saved and inserts them into the sent priority queue. These
 * messages will be scheduled at the next scheduler pass.
 *
 * @param[in] ke_task_id    Destination Identifier
 ****************************************************************************************
 */
static void ke_task_saved_update(ke_task_id_t const ke_task_id)
{
    struct ke_msg * msg;
    unsigned long flags;

    for(;;)
    {
        // if the state has changed look in the Save queue if a message
        // need to be handled
        msg = (struct ke_msg*) ke_queue_extract(&ke_env.queue_saved,
                                          &cmp_dest_id, (uint32_t) ke_task_id);

        if (msg == NULL) break;

        //dbg_ke ("-- saved found %x %x\n", ke_task_id, msg->id);

        GLOBAL_INT_DISABLE();
        spin_lock_irqsave(&ke_env.bl_hw->ke_queue_lock, flags);
        ke_queue_push(&ke_env.queue_sent, (struct co_list_hdr*)msg);
        ke_evt_only_set(KE_EVT_KE_MESSAGE_BIT);
        spin_unlock_irqrestore(&ke_env.bl_hw->ke_queue_lock, flags);
        GLOBAL_INT_RESTORE();

        #ifndef CONFIG_KE_TASKLET
        complete(&ke_env.bl_hw->ke_wait);
        #else
        tasklet_schedule(&ke_env.bl_hw->ke_tasklet);
        #endif
    }

    return;
}


/**
 ****************************************************************************************
 * @brief Set the state of the task identified by its Task Id.
 *
 * In this function we also handle the SAVE service: when a task state changes we
 * try to activate all the messages currently saved in the save queue for the given
 * task identifier.
 *
 * @param[in]  id       Identifier of the task instance whose state is going to be modified
 * @param[in]  state_id New State
 ****************************************************************************************
 */
void ke_state_set(ke_task_id_t const id, ke_state_t const state_id)
{
    ke_state_t *ke_stateid_ptr = NULL;
    int idx = KE_IDX_GET(id);
    int type = KE_TYPE_GET(id);

    // sanity checks
    ASSERT_ERR(type < TASK_MAX);
    if (ke_task_local(type)) {
        ASSERT_ERR(idx < TASK_DESC[type].idx_max);
    }

    // Get the state
    ke_stateid_ptr = &TASK_DESC[type].state[idx];

    ASSERT_ERR(ke_stateid_ptr);

    // set the state
    if (*ke_stateid_ptr != state_id)
    {
        *ke_stateid_ptr = state_id;

        // if the state has changed update the SAVE queue
        ke_task_saved_update(id);
    }

    if (co_list_cnt(&ke_env.queue_saved)) {
        struct ke_msg *msg = (struct ke_msg *)ke_env.queue_saved.first;
        if (msg) {
            ke_task_saved_update(msg->dest_id);
        }
    }
}


/**
 ****************************************************************************************
 * @brief Retrieve the state of a task.
 *
 * @param[in]  id   Task id.
 *
 * @return          Current state of the task
 ****************************************************************************************
 */
ke_state_t ke_state_get(ke_task_id_t const id)
{
    int idx = KE_IDX_GET(id);
    int type = KE_TYPE_GET(id);

    ASSERT_ERR(type < TASK_MAX);
    if (ke_task_local(type)) {
        ASSERT_ERR(idx < TASK_DESC[type].idx_max);
    }

    // Get the state
    return TASK_DESC[type].state[idx];
}


/**
 ****************************************************************************************
 * @brief Search message handler function matching the msg id
 *
 * @param[in] msg_id        Message identifier
 * @param[in] state_handler Pointer to the state handler
 *
 * @return                  Pointer to the message handler (NULL if not found)
 *
 ****************************************************************************************
 */
static ke_msg_func_t ke_handler_search(ke_msg_id_t const msg_id,
                                   struct ke_state_handler const *state_handler)
{
    int i;

    // Get the message handler function by parsing the message table
    for (i = (state_handler->msg_cnt-1); 0 <= i; i--)
    {
        if (state_handler->msg_table[i].id == msg_id)
        {
            // If handler is NULL, message should not have been received in this state
            ASSERT_ERR(state_handler->msg_table[i].func);

            return state_handler->msg_table[i].func;
        }
    }

    // If we execute this line of code, it means that we did not find the handler
    return NULL;
}

/**
 ****************************************************************************************
 * @brief Retrieve appropriate message handler function of a task
 *
 * @param[in]  msg_id   Message identifier
 * @param[in]  task_id  Task instance identifier
 *
 * @return              Pointer to the message handler (NULL if not found)
 *
 ****************************************************************************************
 */
static ke_msg_func_t ke_task_handler_get(ke_msg_id_t const msg_id,
                                                ke_task_id_t const task_id)
{
    ke_msg_func_t func = NULL;
    int type = KE_TYPE_GET(task_id);
    int idx = KE_IDX_GET(task_id);
    const struct ke_task_desc *desc = NULL;

    dbg("%s, type:%d, idx:%d\n", __func__, type, idx);

    ASSERT_ERR(type < TASK_MAX);
    if (ke_task_local(type)) {
        ASSERT_ERR(idx < TASK_DESC[type].idx_max);
    }

    desc = TASK_DESC + type;

    ASSERT_ERR(desc);

    dbg("%s, desc:0x%p, state_handler:0x%p, default_handler:0x%p, state:%d, id_max:%d, state_max:%d\n", 
         __func__, desc, desc->state_handler, desc->default_handler, 
          desc->state[idx], desc->idx_max, desc->state_max);

    // Retrieve a pointer to the task instance data
    if (desc->state_handler)
    {
        func = ke_handler_search(msg_id, desc->state_handler + desc->state[idx]);
    }

    // No handler... need to retrieve the default one
    if (func == NULL && desc->default_handler)
    {
        func = ke_handler_search(msg_id, desc->default_handler);
    }

    return func;
}


/**
 ****************************************************************************************
 * @brief Task scheduler entry point.
 *
 * This function is the scheduler of messages. It tries to get a message
 * from the sent queue, then try to get the appropriate message handler
 * function (from the current state, or the default one). This function
 * is called, then the message is saved or freed.
 *
 * @param[in] dummy Parameter not used but required to follow the kernel event callback
 * format
 ****************************************************************************************
 */
void ke_task_schedule(int dummy)
{
    struct ke_msg *msg;
    struct ke_msg clone_msg;
    unsigned long flags;
    
    do
    {
        int msg_status;
        ke_msg_func_t func;

        ke_msg_check();
        
        // Get a message from the queue
        GLOBAL_INT_DISABLE();
        spin_lock_irqsave(&ke_env.bl_hw->ke_queue_lock, flags);
        msg = (struct ke_msg*) ke_queue_pop(&ke_env.queue_sent);
        spin_unlock_irqrestore(&ke_env.bl_hw->ke_queue_lock, flags);
        GLOBAL_INT_RESTORE();

        if (msg == NULL)
            break;

        ke_msg_check();

        if (msg->src_id == DRV_TASK_ID || msg->src_id == TASK_API) {
            dbg("%s call send ack to drv\n", __func__);
            softmac_send_kmsg_ack_to_drv(ke_env.bl_hw);
        }

        #if 1
        if ((msg->id == SCANU_START_REQ) && 
            (ke_state_get(TASK_SCANU) == SCANU_SCANNING)) 
        {
            struct scanu_start_req  *param = (struct scanu_start_req *)msg->param;
            
            if (scanu_env.param && (param->vif_idx == scanu_env.param->vif_idx)) {
                struct scanu_start_cfm *cfm;
                
                dbg_f("WARN! duplicate scan cmd %d\r\n", msg->param_len);
                
                cfm = KE_MSG_ALLOC(SCANU_START_CFM, msg->src_id, msg->dest_id, 
                                   scanu_start_cfm);
                // fill in the message parameters
                cfm->vif_idx = param->vif_idx;
                cfm->status = CO_BUSY;
                cfm->result_cnt = 0;
                // send the message to the sender
                dbg_f("%s send scanu cfm:0x%p\r\n", __func__, cfm);
                ke_msg_send(cfm);

                ke_msg_free(msg);
                break;
            }
        }
        #endif

        clone_msg = *msg;
        
        // Retrieve a pointer to the task instance data
        func = ke_task_handler_get(msg->id, msg->dest_id);

        // Call the message handler
        if (func != NULL)
        {
            dbg("%s, call hander, msg id:0x%x, src_id:0x%x, dest_id:0x%x\n", 
                  __func__, msg->id, msg->src_id, msg->dest_id);
            msg_status = func(msg->id, ke_msg2param(msg), 
                              msg->dest_id, msg->src_id);
        }
        else
        {
            dbg_f("No handler found for msg:0x%x from tsk:0x%x to tsk:0x%x\n\r", 
                  msg->id, msg->src_id, msg->dest_id);
            msg_status = KE_MSG_CONSUMED;
        }

        switch (msg_status)
        {
            case KE_MSG_CONSUMED:
                // Free the message
                dbg("%s, msg:0x%p, msg->id:0x%x, 0x%x, 0x%x, param_len:0x%x\n", 
                      __func__, msg, msg->id, msg->src_id, msg->dest_id, msg->param_len);
                ke_msg_free(msg);

            case KE_MSG_NO_FREE:
                dbg("%s, NO_FREE, msg:0x%p, msg->id:0x%x, 0x%x, 0x%x, param_len:0x%x\n", 
                      __func__, msg, clone_msg.id, clone_msg.src_id, clone_msg.dest_id, clone_msg.param_len);
                break;

            case KE_MSG_SAVED:
                // The message has been saved
                // Insert it at the end of the save queue
                ke_queue_push(&ke_env.queue_saved, (struct co_list_hdr*) msg);
                break;

            default:
                ASSERT_ERR(0);
        } // switch case
    } while(0);

    // Verify if we can clear the event bit
    GLOBAL_INT_DISABLE();
    spin_lock_irqsave(&ke_env.bl_hw->ke_queue_lock, flags);
    if (co_list_is_empty(&ke_env.queue_sent))
    {
        ke_evt_only_clear(KE_EVT_KE_MESSAGE_BIT);
    }
    spin_unlock_irqrestore(&ke_env.bl_hw->ke_queue_lock, flags);
    GLOBAL_INT_RESTORE();
    //printf("leaving %s\n", __FUNCTION__);
}

/**
 ****************************************************************************************
 * @brief Generic message handler to consume message without handling it in the task.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return KE_MSG_CONSUMED
 ****************************************************************************************
 */
int ke_msg_discard(ke_msg_id_t const msgid, void const *param,
                         ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    dbg_f("%s, msgid:0x%x, src_id:0x%x, dest_id:0x%x\r\n",
          __func__, msgid, src_id, dest_id);
          
    return KE_MSG_CONSUMED;
}

/**
 ****************************************************************************************
 * @brief Generic message handler to consume message without handling it in the task.
 *
 * @param[in] msgid Id of the message received (probably unused)
 * @param[in] param Pointer to the parameters of the message.
 * @param[in] dest_id TaskId of the receiving task.
 * @param[in] src_id TaskId of the sending task.
 * @return KE_MSG_CONSUMED
 ****************************************************************************************
 */
int ke_msg_save(ke_msg_id_t const msgid, void const *param,
                      ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    return KE_MSG_SAVED;
}


