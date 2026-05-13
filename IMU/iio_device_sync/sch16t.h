/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * sch16t.h - Linux IIO kernel driver for Murata SCH16T IMU
 *
 * Based on original userspace library:
 * Copyright (c) 2024, Murata Electronics Oy. All rights reserved.
 *
 * Kernel driver adaptation:
 * Supports SCH16T-K01 and SCH16T-K10 variants via SPI + IIO subsystem.
 */

#ifndef _SCH16T_H_
#define _SCH16T_H_

#include <linux/types.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>

/* -----------------------------------------------------------------------
 * Driver identity
 * --------------------------------------------------------------------- */
#define SCH16T_DRIVER_NAME      "sch16t"
#define SCH16T_K01_NAME         "sch16t-k01"
#define SCH16T_K10_NAME         "sch16t-k10"

/* -----------------------------------------------------------------------
 * SPI frame constants
 * Each transaction is exactly 6 bytes (48 bits).
 * --------------------------------------------------------------------- */
#define SCH16T_SPI_FRAME_LEN    6   /* bytes */

/* -----------------------------------------------------------------------
 * API return status codes
 * --------------------------------------------------------------------- */
#define SCH16T_OK                      0
#define SCH16T_ERR_NULL_POINTER       -1
#define SCH16T_ERR_INVALID_PARAM      -2
#define SCH16T_ERR_SENSOR_INIT        -3
#define SCH16T_ERR_SPI_COMMUNICATION  -4
#define SCH16T_ERR_OTHER              -5

/* -----------------------------------------------------------------------
 * SPI request opcodes (48-bit frames, stored as u64, MSB first)
 * Lower 8 bits are CRC8; upper bits carry TA/SA/data fields.
 * --------------------------------------------------------------------- */

/* Rate / acceleration read requests */
#define REQ_READ_RATE_X1    0x0048000000ACULL
#define REQ_READ_RATE_Y1    0x00880000009AULL
#define REQ_READ_RATE_Z1    0x00C80000006DULL
#define REQ_READ_ACC_X1     0x0108000000F6ULL
#define REQ_READ_ACC_Y1     0x014800000001ULL
#define REQ_READ_ACC_Z1     0x018800000037ULL
#define REQ_READ_ACC_X3     0x01C8000000C0ULL
#define REQ_READ_ACC_Y3     0x02080000002EULL
#define REQ_READ_ACC_Z3     0x0248000000D9ULL
#define REQ_READ_RATE_X2    0x0288000000EFULL
#define REQ_READ_RATE_Y2    0x02C800000018ULL
#define REQ_READ_RATE_Z2    0x030800000083ULL
#define REQ_READ_ACC_X2     0x034800000074ULL
#define REQ_READ_ACC_Y2     0x038800000042ULL
#define REQ_READ_ACC_Z2     0x03C8000000B5ULL

/* Status read requests */
#define REQ_READ_STAT_SUM       0x05080000001CULL
#define REQ_READ_STAT_SUM_SAT   0x0548000000EBULL
#define REQ_READ_STAT_COM       0x0588000000DDULL
#define REQ_READ_STAT_RATE_COM  0x05C80000002AULL
#define REQ_READ_STAT_RATE_X    0x0608000000C4ULL
#define REQ_READ_STAT_RATE_Y    0x064800000033ULL
#define REQ_READ_STAT_RATE_Z    0x068800000005ULL
#define REQ_READ_STAT_ACC_X     0x06C8000000F2ULL
#define REQ_READ_STAT_ACC_Y     0x070800000069ULL
#define REQ_READ_STAT_ACC_Z     0x07480000009EULL

/* Temperature / traceability */
#define REQ_READ_TEMP       0x0408000000B1ULL
#define REQ_READ_SN_ID1     0x0F4800000065ULL
#define REQ_READ_SN_ID2     0x0F8800000053ULL
#define REQ_READ_SN_ID3     0x0FC8000000A4ULL
#define REQ_READ_COMP_ID    0x0F0800000092ULL

/* Filter / control read requests */
#define REQ_READ_FILT_RATE  0x0948000000FAULL
#define REQ_READ_FILT_ACC12 0x0988000000CCULL
#define REQ_READ_FILT_ACC3  0x09C80000003BULL
#define REQ_READ_RATE_CTRL  0x0A08000000D5ULL
#define REQ_READ_ACC12_CTRL 0x0A4800000022ULL
#define REQ_READ_ACC3_CTRL  0x0A8800000014ULL
#define REQ_READ_MODE_CTRL  0x0D4800000010ULL
#define REQ_READ_USER_IF_CTRL 0x0CC80000007CULL

/* Filter / control write base frames (CRC appended at runtime) */
#define REQ_SET_FILT_RATE   0x0968000000ULL
#define REQ_SET_FILT_ACC12  0x09A8000000ULL
#define REQ_SET_FILT_ACC3   0x09E8000000ULL
#define REQ_SET_RATE_CTRL   0x0A28000000ULL
#define REQ_SET_ACC12_CTRL  0x0A68000000ULL
#define REQ_SET_ACC3_CTRL   0x0AA8000000ULL
#define REQ_SET_MODE_CTRL   0x0D68000000ULL
#define REQ_SET_USER_IF_CTRL 0x0CE8000000ULL

/* Soft reset */
#define REQ_SOFTRESET       0x0DA800000AC3ULL

/* -----------------------------------------------------------------------
 * Frame field masks (applied to 48-bit received u64)
 * --------------------------------------------------------------------- */
#define SCH16T_MASK_TA      0xFFC000000000ULL
#define SCH16T_MASK_SA      0x7FE000000000ULL  /* unused in rx path */
#define SCH16T_MASK_DATA    0x00000FFFFF00ULL
#define SCH16T_MASK_CRC     0x0000000000FFULL
#define SCH16T_MASK_ERROR   0x001E00000000ULL

/* -----------------------------------------------------------------------
 * Status register bit definitions (16-bit values)
 * --------------------------------------------------------------------- */
#define S_SUM_INIT_RDY      BIT(0)
#define S_SUM_ACC_Z         BIT(1)
#define S_SUM_ACC_Y         BIT(2)
#define S_SUM_ACC_X         BIT(3)
#define S_SUM_RATE_Z        BIT(4)
#define S_SUM_RATE_Y        BIT(5)
#define S_SUM_RATE_X        BIT(6)
#define S_SUM_CMN           BIT(7)

/* -----------------------------------------------------------------------
 * Default sensor configuration (mirrors imu_node.cpp)
 * --------------------------------------------------------------------- */
#define SCH16T_DEFAULT_FILTER_RATE_HZ   68
#define SCH16T_DEFAULT_FILTER_ACC12_HZ  68
#define SCH16T_DEFAULT_FILTER_ACC3_HZ   68

/* K10 variant sensitivities */
#define SCH16T_K10_SENS_RATE_LSB_DPS    200   /* LSB/dps  → ±163.84 dps FS  */
#define SCH16T_K10_SENS_ACC_LSB_MS2     3200  /* LSB/m/s² → ±327.68 m/s² FS */

/* K01 variant sensitivities */
#define SCH16T_K01_SENS_RATE_LSB_DPS    3200
#define SCH16T_K01_SENS_ACC_LSB_MS2     3200

#define SCH16T_DEFAULT_DECIMATION       4

/* -----------------------------------------------------------------------
 * Device variant IDs (used in iio_device_id match table)
 * --------------------------------------------------------------------- */
enum sch16t_variant {
    SCH16T_VARIANT_K01 = 0,
    SCH16T_VARIANT_K10,
};

/* data buffer */
struct scan_buf{
    s32 data[7];
    s64 ts __aligned(8);
};

/* -----------------------------------------------------------------------
 * Private driver state
 * --------------------------------------------------------------------- */
struct sch16t_state {
    struct spi_device       *spi;
    enum sch16t_variant      variant;
    u8                       _ta9_8;  /* TA9 and TA8 bits for target address (from solder jumper pads) */
    
    /* Protect concurrent SPI access */
    struct mutex             lock;

    /* PWM clock source */
    struct pwm_device       *pwm;
    u32                     pwm_period_ns;      /* current PWM period */
    u32                     pwm_duty_ns;          /* duty cycle (usually 50%) */

    /* external interrupt trigger */
    int                     irq_gpio;             /* trigger GPIO number */
    int                     irq_number;           /* allocated IRQ number */
    struct iio_trigger      *trig;                /* custom trigger */

    /* Configured sensitivities (LSB per engineering unit) */
    int                      sens_rate1;   /* LSB/dps  */
    int                      sens_rate2;
    int                      sens_acc1;    /* LSB/m/s² */
    int                      sens_acc2;
    int                      sens_acc3;

    /* IMU work status and raw data */
    u16                      summary;
    u16                      summary_sat;
    u16                      common;
    u16                      rate_common;
    s32                      rate_x_raw;
    s32                      rate_y_raw;
    s32                      rate_z_raw;
    s32                      acc_x_raw;
    s32                      acc_y_raw;
    s32                      acc_z_raw;
    s32                      temp_raw;
    bool                     frame_error;
    struct scan_buf                 scan_buffer;   /* buffer for pushing to IIO */

    /* result data */
    // double                    rate_x;
    // double                    rate_y;
    // double                    rate_z;
    // double                    acc_x;
    // double                    acc_y;
    // double                    acc_z;
    // double                    temp;

    /*
     * DMA-safe SPI transfer buffers.
     * We always transfer 6 bytes; keep them at end of struct so the
     * compiler can align them on a cache line boundary.
     */
    u8 __aligned(IIO_DMA_MINALIGN) tx_buf[SCH16T_SPI_FRAME_LEN];
    u8 __aligned(IIO_DMA_MINALIGN) rx_buf[SCH16T_SPI_FRAME_LEN];
};

#endif /* _SCH16T_H_ */
