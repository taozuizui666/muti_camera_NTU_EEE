

#ifndef __BL_LOOPBACK_H__
#define __BL_LOOPBACK_H__

//#define LBK_SDIO_THREAD

#define MEASURE_MS 1000
#define MAX_LBK_PKT_SIZE 3940//1536
#define LOOPBACK_BUF_SIZE   (0x1800)
#define LBK_MSG_SIZE        128
typedef struct _loopbackdata {
    struct  semaphore sema;
    void *lbkthread;
    u8 bstop;
    u32 exp_data_rate;
    u16 pkt_size;
    u32 test_dir;
    u32 succ_cnt;
    u32 fail_cnt;
    u16 txsize;
    ktime_t last_txtime;
    u8 txbuf[LOOPBACK_BUF_SIZE];
    u32 rx_succ_cnt;
    u32 rx_fail_cnt;
    u32 rx_total_succ_cnt;
    u32 rx_total_fail_cnt;
    u16 rxsize;
    ktime_t rxtime;
    ktime_t last_rxtime;
    u8 rxbuf[LOOPBACK_BUF_SIZE];
    u8 msg[LBK_MSG_SIZE];
} loopbackdata, *ploopbackdata;
#endif /*__BL_LOOPBACK_H__*/
