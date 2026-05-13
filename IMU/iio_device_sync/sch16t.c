// SPDX-License-Identifier: BSD-3-Clause
/*
 * sch16t.c - Linux IIO kernel driver for Murata SCH16T IMU
 *
 * Supported parts:
 *   SCH16T-K01  (rate sensitivity 1600/3200/6400 LSB/dps)
 *   SCH16T-K10  (rate sensitivity  100/ 200/ 400 LSB/dps)
 *
 * Based on original userspace library:
 *   Copyright (c) 2024, Murata Electronics Oy. All rights reserved.
 *
 * Interface : SPI (MODE 0, up to 10 MHz, 48-bit frames)
 * IIO channels exposed:
 *   - angular velocity X/Y/Z  (rad/s, processed)
 *   - acceleration   X/Y/Z    (m/s², processed)
 *   - temperature              (milli-°C, processed)
 *
 * DT binding example:
 *   &spi0 {
 *       imu@0 {
 *           compatible = "murata,sch16t-k10";
 *           reg = <0>;
 *           spi-max-frequency = <10000000>;
 *           spi-cpol;    // not set — MODE 0
 *           spi-cpha;    // not set — MODE 0
 *       };
 *   };
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/pwm.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include "sch16t.h"

/* =========================================================================
 * Low-level SPI helpers
 * ======================================================================= */

/**
 * sch16t_crc8() - Compute CRC-8 for a 48-bit SPI frame.
 *
 * The polynomial used by SCH16T is x^8+x^2+x+1 (0x07), initial value 0xFF.
 * CRC is computed over the upper 40 bits (bytes 5..1); result replaces byte 0.
 *
 * @frame: 48-bit frame value (MSB = byte 5)
 * Returns: 8-bit CRC value
 */
static u8 sch16t_crc8(u64 frame)
{
    u64 data = frame & 0xFFFFFFFFFF00LL;
    u8 crc = 0xFF;

    for (int i = 47; i >= 0; i--)
    {
        u8 data_bit = (data >> i) & 0x01;
        crc = crc & 0x80 ? (u8)((crc << 1) ^ 0x2F) ^ data_bit : (u8)(crc << 1) | data_bit;
    }

    return crc;
}

/**
 * @brief Check if the CRC8 is correct for the given SPI frame.
 *
 * @param frame - 48 bit SPI frame
 * 
 * @return true = ok
 *         false = error
 */
bool sch16t_check_crc8(u64 frame)
{
    if((u8)(frame & 0xff) == sch16t_crc8(frame))
        return true;
    else
        return false;
}

/**
 * sch16t_crc3() - Compute CRC-3 for a 32-bit SPI frame.
 *
 * The polynomial used by SCH16T is x^3+x+1 (0x03), initial value 0x05.
 * CRC is computed over the upper 29 bits; result occupies bits [2:0].
 *
 * @frame: 32-bit frame value
 * Returns: 3-bit CRC value in lower bits of the return value
 */
static u8 sch16t_crc3(u32 frame)
{
    u32 data = frame & 0xFFFFFFF8;
    u8 crc = 0x05;
 
    for (int i = 31; i >= 0; i--)
    {
        u8 data_bit = (data >> i) & 0x01;
        crc = crc & 0x4 ? (u8)((crc << 1) ^ 0x3) ^ data_bit : (u8)(crc << 1) | data_bit;
        crc &= 0x07;
    }
 
    return crc;
}

/**
 * @brief Check if the CRC3 is correct for the given SPI frame.
 *
 * @param frame - 32 bit SPI frame
 * 
 * @return true = ok
 *         false = error
 */ 
bool sch16t_check_crc3(u32 frame)
{
    if((u8)(frame & 0x07) == sch16t_crc3(frame))
        return true;
    else
        return false;
}

/**
 * sch16t_frame_to_buf() - Serialise a u64 frame into a 6-byte tx buffer (BE).
 */
static void sch16t_frame_to_buf(u64 frame, u8 *buf)
{
    buf[0] = (u8)(frame >> 40);
    buf[1] = (u8)(frame >> 32);
    buf[2] = (u8)(frame >> 24);
    buf[3] = (u8)(frame >> 16);
    buf[4] = (u8)(frame >> 8);
    buf[5] = (u8)(frame);
}

/**
 * sch16t_buf_to_frame() - Deserialise a 6-byte rx buffer into a u64 frame (BE).
 */
static u64 sch16t_buf_to_frame(const u8 *buf)
{
    return ((u64)buf[0] << 40) |
           ((u64)buf[1] << 32) |
           ((u64)buf[2] << 24) |
           ((u64)buf[3] << 16) |
           ((u64)buf[4] <<  8) |
            (u64)buf[5];
}

/**
 * sch16t_add_crc() - Append CRC-8 to a 48-bit request frame.
 *
 * The frame layout already has a zero byte in [7:0]; this function
 * fills that byte with the computed CRC.
 */
static u64 sch16t_add_crc(u64 frame)
{
    return (frame & ~0xFFULL) | sch16t_crc8(frame);
}

/**
 * sch16t_sendRequest() - Execute one 48-bit SPI full-duplex transfer.
 *
 * Caller must hold st->lock.
 *
 * @st:     driver state
 * @tx:     48-bit request frame (CRC must already be set)
 * @rx_out: pointer to store the 48-bit response (may be NULL)
 *
 * Returns 0 on success, negative errno on failure.
 */
static int sch16t_sendRequest(struct sch16t_state *st, u64 tx, u64 *rx_out)
{
    struct spi_transfer t = {
        .tx_buf = st->tx_buf,
        .rx_buf = st->rx_buf,
        .len    = SCH16T_SPI_FRAME_LEN,
        .bits_per_word = 8,
    };
    struct spi_message m;
    int ret;

    sch16t_frame_to_buf(tx, st->tx_buf);

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);

    ret = spi_sync(st->spi, &m);
    if (ret)
        return ret;

    if (rx_out)
        *rx_out = sch16t_buf_to_frame(st->rx_buf);
    
    // dev_info(&st->spi->dev, "RX: \n");
    // for(int i = 0; i < SCH16T_SPI_FRAME_LEN; i++) {
    //     dev_info(&st->spi->dev, "0x%02X ", st->rx_buf[i]);
    // }
    // dev_info(&st->spi->dev, "\n");

    return 0;
}

/**
 * @brief Add sensor target address bits and ta (9,8) pins information to request word
 *
 * @param  Request - 48-bit MOSI data
 * 
 * @return 48-bit MOSI data with correct target address                     
 */
static u64 sch16t_addTargetAddress(u64 Request, u8 ta9_8)
{
    /*
     * Write frame layout:
     *   [47:40] TA[9:2]
     *   [39:32] TA[1:0] | SA[7:0]   (already in base)
     *   [31:24] SA[1:0] | reserved  (already in base)
     *   [23:8]  DATA[15:0]
     *   [7:0]   CRC8
     */
    u64 frame = (Request | (u64) (ta9_8) << 46) & 0xFFFFFFFFFF00;
    frame = sch16t_add_crc(frame);
    return frame;
}

/**
 * @brief Add sensor target address bits and ta (9,8) pins information to request word (that does not have CRC bits appended yet)
 *
 * @param  Request - 40-bit MOSI data
 * 
 * @return 40-bit MOSI data with correct target address                     
 */
static u64 sch16t_addTargetAddressNoCRC(u64 Request, u8 ta9_8)
{
    u64 frame = (Request | (u64) (ta9_8) << 46) & 0xFFFFFFFFFF00;
    return frame;
}

/* =========================================================================
 * Frame data extraction helpers
 * ======================================================================= */

/**
 * sch16t_data_int32() - Extract signed 32-bit integer from a 48-bit frame.
 *
 * The 32-bit data field occupies bits [31:0] of the frame.
 * Sign-extend it to s32 by shifting up then arithmetically right.
 */
static s32 sch16t_data_int32(u64 frame)
{
    /* Extract 32-bit field then sign-extend */
    return ((s32)(((frame) << 4)  & 0xfffff000UL)) >> 12;
}

/**
 * sch16t_data_uint16() - Extract unsigned 16-bit integer from a 48-bit frame.
 */
static u16 sch16t_data_uint16(u64 frame)
{
    return (u16)((frame >> 8) & 0xFFFFULL);
}

/* =========================================================================
 * Sensor configuration helpers
 * ======================================================================= */

/**
 * sch16t_rate_filter_to_bits() - Convert rate filter Hz to register bitfield.
 *
 * Valid Hz values: 0, 13, 30, 68, 235, 280, 370.
 * Returns the 3-bit field value, or 0xFF on invalid input.
 */
static u32 sch16t_rate_filter_to_bits(u32 hz)
{
    switch (hz) {
    case   0: return 0x1FF;  /* Bypass */
    case  13: return 0x092;
    case  30: return 0x049;
    case  68: return 0x000;
    case 235: return 0x16D;
    case 280: return 0x0DB;
    case 370: return 0x124;
    default:  return 0x000;
    }
}

/**
 * sch16t_acc_filter_to_bits() - Convert accel filter Hz to register bitfield.
 *
 * Valid Hz values: 0, 13, 30, 68, 210, 240, 290.
 */
static u32 sch16t_acc_filter_to_bits(u32 hz)
{
    switch (hz) {
    case   0: return 0x1FF;
    case  13: return 0x092;
    case  30: return 0x049;
    case  68: return 0x000;
    case 210: return 0x16D;
    case 240: return 0x0DB;
    case 290: return 0x124;
    default:  return 0x000;
    }
}

/**
 * sch16t_decimation_to_bits() - Convert decimation ratio to register bitfield.
 *
 * Valid ratios: 2, 4, 8, 16, 32.
 */
static u32 sch16t_decimation_to_bits(u32 ratio)
{
    switch (ratio) {
    case  2: return 0x00;
    case  4: return 0x01;
    case  8: return 0x02;
    case 16: return 0x03;
    case 32: return 0x04;
    default: return 0x00;
    }
}

/**
 * sch16t_bits_to_decimation() - Convert decimation bits to ratio.
 *
 * Valid bits: 0x01, 0x02, 0x03, 0x04, 0x05.
 */
static u32 sch16t_bits_to_decimation(u32 bits)
{
    switch (bits) {
    case  0x00: return 2;
    case  0x01: return 4;
    case  0x02: return 8;
    case  0x03: return 16;
    case  0x04: return 32;
    default: return 0;
    }
}

/**
 * sch16t_k01_rate_sens_to_bits() - K01-specific rate sensitivity bitfield.
 *
 * K01 valid sensitivities (LSB/dps): 100, 200, 400.
 */
static u32 sch16t_k01_rate_sens_to_bits(u32 sens)
{
    switch (sens) {
    case 100: return 0x02;
    case 200: return 0x03;
    case 400: return 0x04;
    default:  return 0x01;
    }
}

/**
 * sch16t_k01_bits_to_rate_sens() - K01-specific rate sensitivity bitfield.
 *
 * K01 valid sensitivities (LSB/dps): 100, 200, 400.
 */
static u32 sch16t_k01_bits_to_rate_sens(u32 bits)
{
    switch (bits) {
    case 0x02: return 100;
    case 0x03: return 200;
    case 0x04: return 400;
    default:  return 0x00;
    }
}

/**
 * sch16t_k10_rate_sens_to_bits() - K10-specific rate sensitivity bitfield.
 *
 * K10 valid sensitivities (LSB/dps): 1600, 3200, 6400.
 */
static u32 sch16t_k10_rate_sens_to_bits(u32 sens)
{
    switch (sens) {
    case 1600: return 0x02;
    case 3200: return 0x03;
    case 6400: return 0x04;
    default:   return 0x01;
    }
}

/**
 * sch16t_k10_bits_to_rate_sens() - K10-specific rate sensitivity bitfield.
 *
 * K10 valid sensitivities (LSB/dps): 1600, 3200, 6400.
 */
static u32 sch16t_k10_bits_to_rate_sens(u32 bits)
{
    switch (bits) {
    case 0x02: return 1600;
    case 0x03: return 3200;
    case 0x04: return 6400;
    default:  return 0x00;
    }
}

/**
 * sch16t_acc_sens_to_bits() - Accelerometer sensitivity bitfield (common).
 *
 * Valid sensitivities (LSB/m/s²): 3200, 6400, 12800, 25600.
 */
static u32 sch16t_acc_sens_to_bits(u32 sens)
{
    switch (sens) {
    case  3200: return 0x01;
    case  6400: return 0x02;
    case 12800: return 0x03;
    case 25600: return 0x04;
    default:    return 0x00;
    }
}

/**
 * sch16t_bits_to_acc_sens() - Convert accelerometer sensitivity bits to LSB/m/s².
 * 
 * Valid sensitivities (LSB/m/s²): 3200, 6400, 12800, 25600.
 */
static u32 sch16t_bits_to_acc_sens(u32 bits)
{
    switch (bits) {
    case 0x01: return 3200;
    case 0x02: return 6400;
    case 0x03: return 12800;
    case 0x04: return 25600;
    default:   return 0x00;
    }
}

/* =========================================================================
 * Sensor initialisation
 * ======================================================================= */

/**
 * sch16t_set_filters() - Write filter configuration to the sensor.
 *
 * Caller must hold st->lock.
 */
static int sch16t_set_filters(struct sch16t_state *st,
                               u32 rate_hz, u32 acc12_hz, u32 acc3_hz)
{
    // bits value for filter settings
    u32  rb = sch16t_rate_filter_to_bits(rate_hz);
    u32  ab12 = sch16t_acc_filter_to_bits(acc12_hz);
    u32  ab3  = sch16t_acc_filter_to_bits(acc3_hz);

    // command frame data
    u64 requestFrame_Rate12;
    u64 responseFrame_Rate12;
    u64 requestFrame_Acc12;
    u64 responseFrame_Acc12;
    u64 requestFrame_Acc3;
    u64 responseFrame_Acc3;

    u8  CRCvalue;
    u64 outData;

    int ret;

    // if (rb == 0x000 || ab12 == 0x000 || ab3 == 0x000)
    //     return SCH16T_ERR_INVALID_PARAM;

    /* FILT_RATE: bits [2:0] for X, [5:3] for Y, [8:6] for Z — same field */
    requestFrame_Rate12 = sch16t_addTargetAddressNoCRC(REQ_SET_FILT_RATE, st->_ta9_8);
    requestFrame_Rate12 |= rb;
    requestFrame_Rate12 <<= 8;
    CRCvalue = sch16t_crc8(requestFrame_Rate12);
    requestFrame_Rate12 |= CRCvalue;
    ret = sch16t_sendRequest(st, requestFrame_Rate12, &outData);

    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    /* FILT_ACC12 */
    requestFrame_Acc12 = sch16t_addTargetAddressNoCRC(REQ_SET_FILT_ACC12, st->_ta9_8);
    requestFrame_Acc12 |= ab12;
    requestFrame_Acc12 <<= 8;
    CRCvalue = sch16t_crc8(requestFrame_Acc12);
    requestFrame_Acc12 |= CRCvalue;
    ret = sch16t_sendRequest(st, requestFrame_Acc12, &outData);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    /* FILT_ACC3 */
    requestFrame_Acc3 = sch16t_addTargetAddressNoCRC(REQ_SET_FILT_ACC3, st->_ta9_8);
    requestFrame_Acc3 |= ab3;
    requestFrame_Acc3 <<= 8;
    CRCvalue = sch16t_crc8(requestFrame_Acc3);
    requestFrame_Acc3 |= CRCvalue;
    ret = sch16t_sendRequest(st, requestFrame_Acc3, &outData);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Read back filter register contents.
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_SET_FILT_RATE, st->_ta9_8), &outData);  // dummy request to get the first response
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_SET_FILT_ACC12, st->_ta9_8), &responseFrame_Rate12);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_SET_FILT_ACC3, st->_ta9_8), &responseFrame_Acc12);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_SET_FILT_ACC3, st->_ta9_8), &responseFrame_Acc3);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Check that return frame is not blank.
    if ((responseFrame_Rate12 == 0xFFFFFFFFFFFF) || (responseFrame_Rate12 == 0x00))
        return SCH16T_ERR_OTHER;
    if ((responseFrame_Acc12 == 0xFFFFFFFFFFFF) || (responseFrame_Acc12 == 0x00))
        return SCH16T_ERR_OTHER;
    if ((responseFrame_Acc3 == 0xFFFFFFFFFFFF) || (responseFrame_Acc3 == 0x00))
        return SCH16T_ERR_OTHER;

     // Check that Source Address matches Target Address.
    if (((requestFrame_Rate12 & SCH16T_MASK_TA) >> 38) != ((responseFrame_Rate12 & SCH16T_MASK_SA) >> 37))
        return SCH16T_ERR_OTHER;
    if (((requestFrame_Acc12 & SCH16T_MASK_TA) >> 38) != ((responseFrame_Acc12 & SCH16T_MASK_SA) >> 37))
        return SCH16T_ERR_OTHER;
    if (((requestFrame_Acc3 & SCH16T_MASK_TA) >> 38) != ((responseFrame_Acc3 & SCH16T_MASK_SA) >> 37))
        return SCH16T_ERR_OTHER;

    // Check that read and written data match.
    if ((requestFrame_Rate12 & SCH16T_MASK_DATA) != (responseFrame_Rate12 & SCH16T_MASK_DATA))
        return SCH16T_ERR_OTHER;
    if ((requestFrame_Acc12 & SCH16T_MASK_DATA) != (responseFrame_Acc12 & SCH16T_MASK_DATA))
        return SCH16T_ERR_OTHER;
    if ((requestFrame_Acc3 & SCH16T_MASK_DATA) != (responseFrame_Acc3 & SCH16T_MASK_DATA))
        return SCH16T_ERR_OTHER;

    return SCH16T_OK;
}

/**
 * sch16t_set_rate_sens_dec() - Write rate sensitivity and decimation.
 *
 * Caller must hold st->lock.
 *
 * The RATE_CTRL register layout (16 bits):
 *   [2:0]  Rate1 sensitivity (same 3-bit code for all axes)
 *   [5:3]  Rate2 sensitivity
 *   [9:6]  Rate2 decimation (4-bit)
 */
static int sch16t_set_rate_sens_dec(struct sch16t_state *st,
                                 u16 sens1, u16 sens2, u16 dec2)
{
    u32 dataField;
    u32 bitField;
    u64 requestFrame_Rate_Ctrl;
    u64 responseFrame_Rate_Ctrl;
    u8  CRCvalue;
    u64 outData;
    int ret;

    st->sens_rate1 = sens1;
    st->sens_rate2 = sens2;

    // Set sensitivities for Rate_XYZ1 (interpolated) and Rate_XYZ2 (decimated) outputs.
    // Also set decimation for Rate_XYZ2.
    requestFrame_Rate_Ctrl = sch16t_addTargetAddressNoCRC(REQ_SET_RATE_CTRL, st->_ta9_8);
    if(st->variant == SCH16T_VARIANT_K01)
    {
        dataField = sch16t_k01_rate_sens_to_bits(sens1);
        dataField <<= 3;
        bitField = sch16t_k01_rate_sens_to_bits(sens2);
        dataField |= bitField;
        dataField <<= 3;
    }
    else{
        dataField = sch16t_k10_rate_sens_to_bits(sens1);
        dataField <<= 3;
        bitField = sch16t_k10_rate_sens_to_bits(sens2);
        dataField |= bitField;
        dataField <<= 3;
    }
    bitField = sch16t_decimation_to_bits(dec2);
    dataField |= bitField;
    dataField <<= 3;
    dataField |= bitField;
    dataField <<= 3;
    dataField |= bitField;
    
    requestFrame_Rate_Ctrl |= dataField;
    requestFrame_Rate_Ctrl <<= 8;
    CRCvalue = sch16t_crc8(requestFrame_Rate_Ctrl);
    requestFrame_Rate_Ctrl |= CRCvalue;
    sch16t_sendRequest(st,requestFrame_Rate_Ctrl,&outData);

    // Read back rate control register contents.
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_RATE_CTRL, st->_ta9_8), &responseFrame_Rate_Ctrl);
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_RATE_CTRL, st->_ta9_8), &responseFrame_Rate_Ctrl);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Check that return frame is not blank.
    if ((responseFrame_Rate_Ctrl == 0xFFFFFFFFFFFF) || (responseFrame_Rate_Ctrl == 0x00))
        return SCH16T_ERR_OTHER;

    // Check that Source Address matches Target Address.
    if (((requestFrame_Rate_Ctrl & SCH16T_MASK_TA) >> 38) != ((responseFrame_Rate_Ctrl & SCH16T_MASK_SA) >> 37))
        return SCH16T_ERR_OTHER;

    // Check that read and written data match.
    if ((requestFrame_Rate_Ctrl & SCH16T_MASK_DATA) != (responseFrame_Rate_Ctrl & SCH16T_MASK_DATA))
        return SCH16T_ERR_OTHER;

    return SCH16T_OK;
}

/**
 * @brief Gets Rate_XYZ1 & Rate_XYZ2 channel sensitivities and Rate_XYZ2 channel decimation for the SCH1.
 * 
 * @param Sens_Rate1 - Sensitivity for Rate_XYZ1 (interpolated) output.
 * @param Sens_Rate2 - Sensitivity for Rate_XYZ2 (decimated) output.
 * @param Dec_Rate2  - Decimation for Rate_XYZ2 output (FPRIM / Dec_Rate2).
 * 
 * @note It is assumed that the same decimation is used for all Rate_XYZ2 axis.
 *
 * @return SCH16T_OK = success
 *         SCH16T_ERR_* = failure. Please see header file for error definitions.                      
 */
int sch16t_get_rate_sens_dec(struct sch16t_state *st, u16 *Sens_Rate1, u16 *Sens_Rate2, u16 *Dec_Rate2)
{
    u32 dataField;
    u64 responseFrame_Rate_Ctrl;
    int ret;

    // Read Rate control register contents.
    sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_RATE_CTRL, st->_ta9_8), &responseFrame_Rate_Ctrl);
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_RATE_CTRL, st->_ta9_8), &responseFrame_Rate_Ctrl);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Check that return frame is not blank.
    if ((responseFrame_Rate_Ctrl == 0xFFFFFFFFFFFF) || (responseFrame_Rate_Ctrl == 0x00))
        return SCH16T_ERR_OTHER;

    // Check that Source Address matches Target Address.
    if (((sch16t_addTargetAddress(REQ_READ_RATE_CTRL, st->_ta9_8) & SCH16T_MASK_TA) >> 38) != ((responseFrame_Rate_Ctrl & SCH16T_MASK_SA) >> 37))
        return SCH16T_ERR_OTHER;

    
    // Get sensitivities for Rate_XYZ1 (interpolated) and Rate_XYZ2 (decimated) outputs.
    // Also get decimation for Rate_XYZ2.
    
    // First get the Rate_XYZ2 decimation
    dataField = (u16)(responseFrame_Rate_Ctrl >> 8) & 0x07;
    *Dec_Rate2 = (u16)sch16t_bits_to_decimation(dataField);

    
    if(st->variant == SCH16T_VARIANT_K01)
    {
        // Rate_XYZ2 sensitivity
        dataField = (u16)(responseFrame_Rate_Ctrl >> 17) & 0x07;
        *Sens_Rate2 = (u16)sch16t_k01_bits_to_rate_sens(dataField);
        // Rate_XYZ1 sensitivity
        dataField = (u16)(responseFrame_Rate_Ctrl >> 20) & 0x07;
        *Sens_Rate1 = (u16)sch16t_k01_bits_to_rate_sens(dataField);
    }
    else
    {
        // Rate_XYZ2 sensitivity
        dataField = (u16)(responseFrame_Rate_Ctrl >> 17) & 0x07;
        *Sens_Rate2 = (u16)sch16t_k10_bits_to_rate_sens(dataField);
        // Rate_XYZ1 sensitivity
        dataField = (u16)(responseFrame_Rate_Ctrl >> 20) & 0x07;
        *Sens_Rate1 = (u16)sch16t_k10_bits_to_rate_sens(dataField);
    }

    return SCH16T_OK;
}

/**
 * sch16t_set_acc_sens_dec() - Write accelerometer sensitivity and decimation.
 *
 * Caller must hold st->lock.
 */
static int sch16t_set_acc_sens_dec(struct sch16t_state *st,
                                u16 sens1, u16 sens2, u16 sens3, u16 dec2)
{
    u32 dataField;
    u32 bitField;
    u64 requestFrame_Acc12_Ctrl;
    u64 responseFrame_Acc12_Ctrl;
    u64 requestFrame_Acc3_Ctrl;
    u64 responseFrame_Acc3_Ctrl;
    u8  CRCvalue;
    int ret;

    st->sens_acc1 = sens1;
    st->sens_acc2 = sens2;
    st->sens_acc3 = sens3;

    // Set sensitivities for Acc_XYZ1 (interpolated) and Acc_XYZ2 (decimated) outputs.
    // Also set decimation for Acc_XYZ2.
    requestFrame_Acc12_Ctrl = sch16t_addTargetAddressNoCRC(REQ_SET_ACC12_CTRL, st->_ta9_8);
    dataField = sch16t_acc_sens_to_bits(sens1);
    dataField <<= 3;
    bitField = sch16t_acc_sens_to_bits(sens2);
    dataField |= bitField;
    dataField <<= 3;
    bitField = sch16t_decimation_to_bits(dec2);
    dataField |= bitField;
    dataField <<= 3;
    dataField |= bitField;
    dataField <<= 3;
    dataField |= bitField;
    
    requestFrame_Acc12_Ctrl |= dataField;
    requestFrame_Acc12_Ctrl <<= 8;
    CRCvalue = sch16t_crc8(requestFrame_Acc12_Ctrl);
    requestFrame_Acc12_Ctrl |= CRCvalue;
    ret = sch16t_sendRequest(st,requestFrame_Acc12_Ctrl,&responseFrame_Acc12_Ctrl);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Set sensitivity for Acc_XYZ3 (interpolated) output.
    requestFrame_Acc3_Ctrl = sch16t_addTargetAddressNoCRC(REQ_SET_ACC3_CTRL, st->_ta9_8);
    dataField = sch16t_acc_sens_to_bits(sens3);
    requestFrame_Acc3_Ctrl |= dataField;
    requestFrame_Acc3_Ctrl <<= 8;
    CRCvalue = sch16t_crc8(requestFrame_Acc3_Ctrl);
    requestFrame_Acc3_Ctrl |= CRCvalue;
    ret = sch16t_sendRequest(st,requestFrame_Acc3_Ctrl,&responseFrame_Acc3_Ctrl);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Read back sensitivity control register contents.
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_ACC12_CTRL, st->_ta9_8), &responseFrame_Acc12_Ctrl);  // dummy request to get the first response
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_ACC3_CTRL, st->_ta9_8), &responseFrame_Acc12_Ctrl);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_ACC3_CTRL, st->_ta9_8), &responseFrame_Acc3_Ctrl);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Check that return frame is not blank.
    if ((responseFrame_Acc12_Ctrl == 0xFFFFFFFFFFFF) || (responseFrame_Acc12_Ctrl == 0x00))
        return SCH16T_ERR_OTHER;
    if ((responseFrame_Acc3_Ctrl == 0xFFFFFFFFFFFF) || (responseFrame_Acc3_Ctrl == 0x00))
        return SCH16T_ERR_OTHER;

    // Check that Source Address matches Target Address.
    if (((requestFrame_Acc12_Ctrl & SCH16T_MASK_TA) >> 38) != ((responseFrame_Acc12_Ctrl & SCH16T_MASK_SA) >> 37))
        return SCH16T_ERR_OTHER;
    if (((requestFrame_Acc3_Ctrl & SCH16T_MASK_TA) >> 38) != ((responseFrame_Acc3_Ctrl & SCH16T_MASK_SA) >> 37))
        return SCH16T_ERR_OTHER;

    // Check that read and written data match.
    if ((requestFrame_Acc12_Ctrl & SCH16T_MASK_DATA) != (responseFrame_Acc12_Ctrl & SCH16T_MASK_DATA))
        return SCH16T_ERR_OTHER;
    if ((requestFrame_Acc3_Ctrl & SCH16T_MASK_DATA) != (responseFrame_Acc3_Ctrl & SCH16T_MASK_DATA))
        return SCH16T_ERR_OTHER;

    return SCH16T_OK;
}

/**
 * @brief Gets Acc_XYZ1/2/3 channel sensitivities and Acc_XYZ2 channel decimation for the SCH1.
 * 
 * @param Sens_Acc1 - Sensitivity for Acc_XYZ1 (interpolated) output.
 * @param Sens_Acc2 - Sensitivity for Acc_XYZ2 (decimated) output.
 * @param Sens_Acc3 - Sensitivity for Acc_XYZ3 (interpolated) output.
 * @param Dec_Acc2  - Decimation for Acc_XYZ2 output (FPRIM / Dec_Acc2).
 * 
 * @note It is assumed that the same decimation is used for all Acc_XYZ2 axis.
 *
 * @return 0 = success
 *         other = failure. Please see header file for error definitions.                      
 */
int sch16t_get_acc_sens_dec(struct sch16t_state *st, u16 *Sens_Acc1, u16 *Sens_Acc2, u16 *Sens_Acc3, u16 *Dec_Acc2)
{
    u32 dataField;
    u64 responseFrame_Acc12_Ctrl;
    u64 responseFrame_Acc3_Ctrl;
    int ret;

    // Read Acc12 and Acc3 control register contents.
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_ACC12_CTRL, st->_ta9_8), &responseFrame_Acc12_Ctrl);  // dummy request to get the first response
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_ACC3_CTRL, st->_ta9_8), &responseFrame_Acc12_Ctrl);
    if(ret)
        return SCH16T_ERR_SPI_COMMUNICATION;
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_ACC3_CTRL, st->_ta9_8), &responseFrame_Acc3_Ctrl);
    if(ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Check that return frame is not blank.
    if ((responseFrame_Acc12_Ctrl == 0xFFFFFFFFFFFF) || (responseFrame_Acc12_Ctrl == 0x00))
        return SCH16T_ERR_OTHER;
    if ((responseFrame_Acc3_Ctrl == 0xFFFFFFFFFFFF) || (responseFrame_Acc3_Ctrl == 0x00))
        return SCH16T_ERR_OTHER;

    // Check that Source Address matches Target Address.
    if (((sch16t_addTargetAddress(REQ_READ_ACC12_CTRL, st->_ta9_8) & SCH16T_MASK_TA) >> 38) != ((responseFrame_Acc12_Ctrl & SCH16T_MASK_SA) >> 37))
        return SCH16T_ERR_OTHER;
    if (((sch16t_addTargetAddress(REQ_READ_ACC3_CTRL, st->_ta9_8) & SCH16T_MASK_TA) >> 38) != ((responseFrame_Acc3_Ctrl & SCH16T_MASK_SA) >> 37))
        return SCH16T_ERR_OTHER;

    
    // Get sensitivities for Acc_XYZ1 (interpolated) and Acc_XYZ2 (decimated) outputs.
    // Also get decimation for Acc_XYZ2.
    
    // Firat get the Acc_XYZ2 decimation
    dataField = (u16)(responseFrame_Acc12_Ctrl >> 8) & 0x07;
    *Dec_Acc2 = (u16)sch16t_decimation_to_bits(dataField);

    // Acc_XYZ2 sensitivity
    dataField = (u16)(responseFrame_Acc12_Ctrl >> 17) & 0x07;
    *Sens_Acc2 = (u16)sch16t_bits_to_acc_sens(dataField);

    // Acc_XYZ1 sensitivity
    dataField = (u16)(responseFrame_Acc12_Ctrl >> 20) & 0x07;
    *Sens_Acc1 = (u16)sch16t_bits_to_acc_sens(dataField);

    // Acc_XYZ3 sensitivity
    dataField = (u16)(responseFrame_Acc3_Ctrl >> 8) & 0x07;
    *Sens_Acc3 = (u16)sch16t_bits_to_acc_sens(dataField);

    return SCH16T_OK;
}

/**
 * @brief Activates/deactivates SCH1 measurement mode and sets the EOI (End Of Initialization) bit if needed.
 *
 * @param st - Pointer to sch16t_state struct.
 * @param enableSensor - Enables/disables the sensor.
 * @param setEOI - Sets EOI-bit. Locks all R/W registers, except soft reset. Can only be set when no errors in common status.
 *
 * @return SCH16T_OK = success
 *         SCH16T_ERR_* = failure. Please see header file for error definitions.                      
 */
static int sch16t_enable_meas(struct sch16t_state *st, bool enableSensor, bool setEOI)
{
    u64 requestFrame_Mode_Ctrl;
    u64 responseFrame_Mode_Ctrl;
    u64 outData;
    u8  CRCvalue;
    int ret;

    requestFrame_Mode_Ctrl = sch16t_addTargetAddressNoCRC(REQ_SET_MODE_CTRL, st->_ta9_8);

    // Handle EN_SENSOR -bit
    if (enableSensor)
        requestFrame_Mode_Ctrl |= 0x01;

    // Handle EOI_CTRL -bit
    if (setEOI)
        requestFrame_Mode_Ctrl |= 0x02;

    requestFrame_Mode_Ctrl <<= 8;
    CRCvalue = sch16t_crc8(requestFrame_Mode_Ctrl);
    requestFrame_Mode_Ctrl |= CRCvalue;
    ret = sch16t_sendRequest(st, requestFrame_Mode_Ctrl, &outData);

    // Read back sensitivity control register contents.
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_MODE_CTRL, st->_ta9_8), &responseFrame_Mode_Ctrl);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_MODE_CTRL, st->_ta9_8), &responseFrame_Mode_Ctrl);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Check that return frame is not blank.
    if ((responseFrame_Mode_Ctrl == 0xFFFFFFFFFFFF) || (responseFrame_Mode_Ctrl == 0x00))
        return SCH16T_ERR_OTHER;

    // Check that Source Address matches Target Address.
    if (((requestFrame_Mode_Ctrl & SCH16T_MASK_TA) >> 38) != ((responseFrame_Mode_Ctrl & SCH16T_MASK_SA) >> 37))
        return SCH16T_ERR_OTHER;
    
    return SCH16T_OK;
}

/**
 * @brief Configures Data Ready (DRY) -pin
 *
 * @param polarity - Data Ready polarity control: 0 - high active (default), 1 - low active. -1 = don't care
 * @param enable   - Data Ready -pin disable/enable
 *
 * @return SCH16T_OK = success
 *         SCH16T_ERR_* = failure. Please see header file for error definitions.                      
 */
int sch16t_setDRY(struct sch16t_state *st, int8_t polarity, bool enable)
{
    u64 requestFrame_User_If_Ctrl;
    u64 responseFrame_User_If_Ctrl;
    u64 dataContent;
    u64 temp;
    u8  CRCvalue;
    int ret;

    if ((polarity < -1) || (polarity > 1))
        return SCH16T_ERR_INVALID_PARAM;
    
    // Read USER_IF_CTRL -register content
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_USER_IF_CTRL, st->_ta9_8), &dataContent);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_USER_IF_CTRL, st->_ta9_8), &responseFrame_User_If_Ctrl);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;
    dataContent = (responseFrame_User_If_Ctrl & SCH16T_MASK_DATA) >> 8;
    
    if (polarity == 0)
        dataContent &= (u16)~0x40;   // Set DRY active high (0b01000000)
    else if (polarity == 1)
        dataContent |= 0x40;              // Set DRY active low
    
    if (enable)
        dataContent |= 0x20;              // Set DRY enabled (0b00100000)
    else
        dataContent &= (u16)~0x20;   // Set DRY disabled
        
    requestFrame_User_If_Ctrl = sch16t_addTargetAddressNoCRC(REQ_SET_USER_IF_CTRL, st->_ta9_8);
    requestFrame_User_If_Ctrl |= dataContent;
    requestFrame_User_If_Ctrl <<= 8;
    CRCvalue = sch16t_crc8(requestFrame_User_If_Ctrl);
    requestFrame_User_If_Ctrl |= CRCvalue;
    ret = sch16t_sendRequest(st, requestFrame_User_If_Ctrl, &temp);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Read back sensitivity control register contents.
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_USER_IF_CTRL, st->_ta9_8), &temp);
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_USER_IF_CTRL, st->_ta9_8), &responseFrame_User_If_Ctrl);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Check that return frame is not blank.
    if ((responseFrame_User_If_Ctrl == 0xFFFFFFFFFFFF) || (responseFrame_User_If_Ctrl == 0x00))
        return SCH16T_ERR_OTHER;

    // Check that Source Address matches Target Address.
    if (((requestFrame_User_If_Ctrl & SCH16T_MASK_TA) >> 38) != ((responseFrame_User_If_Ctrl & SCH16T_MASK_SA) >> 37))
        return SCH16T_ERR_OTHER;

    // Check that read and written data match.
    if ((requestFrame_User_If_Ctrl & SCH16T_MASK_DATA) != (responseFrame_User_If_Ctrl & SCH16T_MASK_DATA))
        return SCH16T_ERR_OTHER;
    
    return SCH16T_OK;
}

/**
 * @brief Configures Synchronization (SYNC) -pin
 *
 * @param polarity - Synchronization polarity control: 0 - high active (default), 1 - low active. -1 = don't care
 * @param enable   - Synchronization -pin disable/enable
 *
 * @return SCH16T_OK = success
 *         SCH16T_ERR_* = failure. Please see header file for error definitions.                      
 */
int sch16t_setSYNC(struct sch16t_state *st, int8_t polarity, bool enable)
{
    u64 requestFrame_User_If_Ctrl;
    u64 responseFrame_User_If_Ctrl;
    u64 dataContent;
    u64 temp;
    u8  CRCvalue;
    int ret;

    if ((polarity < -1) || (polarity > 1))
        return SCH16T_ERR_INVALID_PARAM;
    
    // Read USER_IF_CTRL -register content
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_USER_IF_CTRL, st->_ta9_8), &dataContent);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_USER_IF_CTRL, st->_ta9_8), &responseFrame_User_If_Ctrl);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;
    dataContent = (responseFrame_User_If_Ctrl & SCH16T_MASK_DATA) >> 8;
    
    if (polarity == 0)
        dataContent &= (u16)~0x0800;   // Set SYNC active high (0b01000000)
    else if (polarity == 1)
        dataContent |= 0x0800;              // Set SYNC active low
    
    if (enable)
        dataContent |= 0x0180;              // Set SYNC enabled (0b00100000)
    else
        dataContent &= (u16)~0x0180;   // Set SYNC disabled
        
    requestFrame_User_If_Ctrl = sch16t_addTargetAddressNoCRC(REQ_SET_USER_IF_CTRL, st->_ta9_8);
    requestFrame_User_If_Ctrl |= dataContent;
    requestFrame_User_If_Ctrl <<= 8;
    CRCvalue = sch16t_crc8(requestFrame_User_If_Ctrl);
    requestFrame_User_If_Ctrl |= CRCvalue;
    ret = sch16t_sendRequest(st, requestFrame_User_If_Ctrl, &temp);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Read back sensitivity control register contents.
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_USER_IF_CTRL, st->_ta9_8), &temp);
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_USER_IF_CTRL, st->_ta9_8), &responseFrame_User_If_Ctrl);
    if (ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Check that return frame is not blank.
    if ((responseFrame_User_If_Ctrl == 0xFFFFFFFFFFFF) || (responseFrame_User_If_Ctrl == 0x00))
        return SCH16T_ERR_OTHER;

    // Check that Source Address matches Target Address.
    if (((requestFrame_User_If_Ctrl & SCH16T_MASK_TA) >> 38) != ((responseFrame_User_If_Ctrl & SCH16T_MASK_SA) >> 37))
        return SCH16T_ERR_OTHER;

    // Check that read and written data match.
    if ((requestFrame_User_If_Ctrl & SCH16T_MASK_DATA) != (responseFrame_User_If_Ctrl & SCH16T_MASK_DATA))
        return SCH16T_ERR_OTHER;
    
    return SCH16T_OK;
}

/**
 * @brief Read status register values.
 *
 * @param Status - reference to SCH1 status register structure.
 *
 * @return SCH16T_MT_OK = success, SCH16T_MT_ERR_* = failure. Please see header file for error definitions.
 */
int sch16t_get_status(struct sch16t_state *Status)
{
    int ret;
    u64 outData;

    if (Status == NULL) {
        return SCH16T_ERR_NULL_POINTER;
    }

    /* Caller must hold Status->lock. */
    if (!mutex_is_locked(&Status->lock))
        return SCH16T_ERR_OTHER;

    ret = sch16t_sendRequest(Status, sch16t_addTargetAddress(REQ_READ_STAT_SUM, Status->_ta9_8), &outData);
    ret = sch16t_sendRequest(Status, sch16t_addTargetAddress(REQ_READ_STAT_SUM_SAT, Status->_ta9_8), &outData);
    Status->summary     = sch16t_data_uint16(outData);
    ret = sch16t_sendRequest(Status, sch16t_addTargetAddress(REQ_READ_STAT_COM, Status->_ta9_8), &outData);
    Status->summary_sat = sch16t_data_uint16(outData);
    ret = sch16t_sendRequest(Status, sch16t_addTargetAddress(REQ_READ_STAT_RATE_COM, Status->_ta9_8), &outData);
    Status->common      = sch16t_data_uint16(outData);
    ret = sch16t_sendRequest(Status, sch16t_addTargetAddress(REQ_READ_STAT_RATE_X, Status->_ta9_8), &outData);
    Status->rate_common = sch16t_data_uint16(outData);
    ret = sch16t_sendRequest(Status, sch16t_addTargetAddress(REQ_READ_STAT_RATE_Y, Status->_ta9_8), &outData);
    Status->rate_x_raw      = sch16t_data_uint16(outData);
    ret = sch16t_sendRequest(Status, sch16t_addTargetAddress(REQ_READ_STAT_RATE_Z, Status->_ta9_8), &outData);
    Status->rate_y_raw      = sch16t_data_uint16(outData);
    ret = sch16t_sendRequest(Status, sch16t_addTargetAddress(REQ_READ_STAT_ACC_X, Status->_ta9_8), &outData);
    Status->rate_z_raw      = sch16t_data_uint16(outData);
    ret = sch16t_sendRequest(Status, sch16t_addTargetAddress(REQ_READ_STAT_ACC_Y, Status->_ta9_8), &outData);
    Status->acc_x_raw       = sch16t_data_uint16(outData);
    ret = sch16t_sendRequest(Status, sch16t_addTargetAddress(REQ_READ_STAT_ACC_Z, Status->_ta9_8), &outData);
    Status->acc_y_raw       = sch16t_data_uint16(outData);
    ret = sch16t_sendRequest(Status, sch16t_addTargetAddress(REQ_READ_STAT_ACC_Z, Status->_ta9_8), &outData);
    Status->acc_z_raw       = sch16t_data_uint16(outData);

    if(ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    return SCH16T_OK;
}

/**
 * sch16t_verify_status() - Check that no error bits are set in the status summary.
 *
 * Caller must hold st->lock.
 *
 * Returns true if status is clean, false otherwise.
 */
static bool sch16t_verify_status(struct sch16t_state *st)
{
    if (st == NULL) {
        return SCH16T_ERR_NULL_POINTER;
    }

    if (st->summary != 0xffff)
        return false;
    if (st->summary_sat != 0xffff)
        return false;
    if (st->common != 0xffff)
        return false;
    if (st->rate_common != 0xffff)
        return false;
    if (st->rate_x_raw != 0xffff)
        return false;
    if (st->rate_y_raw != 0xffff)
        return false;
    if (st->rate_z_raw != 0xffff)
        return false;
    if (st->acc_x_raw != 0xffff)
        return false;
    if (st->acc_y_raw != 0xffff)
        return false;
    if (st->acc_z_raw != 0xffff)
        return false;
    
    return true;
}

/**
 * sch16t_init_sensor() - Full sensor initialisation sequence.
 *
 * Mirrors the begin() logic from the original userspace library:
 *   1. SPI soft reset
 *   2. Set filters
 *   3. Set rate ctrl (sensitivity + decimation)
 *   4. Set acc ctrl  (sensitivity + decimation)
 *   5. Enable measurement / EOI
 *   6. Wait for INIT_RDY
 *   7. Verify no error bits
 *
 * Must be called with st->lock held.
 */
static int sch16t_init_sensor(struct sch16t_state *st)
{
    int ret = SCH16T_OK;
    uint8_t startup_attempt = 0;
    bool SCH1status = false;
    u64 outData;

    /* SPI soft reset */
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_SOFTRESET, st->_ta9_8), &outData);
    if (ret)
        return ret;
    usleep_range(25000, 30000); /* 25 ms reset recovery */

    for (startup_attempt = 0; startup_attempt < 2; startup_attempt++) {
        // Wait 32 ms for the non-volatile memory (NVM) Read
        usleep_range(32000, 35000);

        /* Filters */
        ret = sch16t_set_filters(st,
                                SCH16T_DEFAULT_FILTER_RATE_HZ,
                                SCH16T_DEFAULT_FILTER_ACC12_HZ,
                                SCH16T_DEFAULT_FILTER_ACC3_HZ);
        if (ret){
            dev_info(&st->spi->dev, "Failed to set filters on attempt %d, error code %d\n", startup_attempt + 1, ret);
            // continue;    // Sensor failed, reset and retry.;
        }

        /* Rate control */
        ret = sch16t_set_rate_sens_dec(st,
                                    st->sens_rate1,
                                    st->sens_rate2,
                                    SCH16T_DEFAULT_DECIMATION);
        if (ret){
            dev_info(&st->spi->dev, "Failed to set rate control on attempt %d, error code %d\n", startup_attempt + 1, ret);
            // continue;    // Sensor failed, reset and retry.;
        }

        /* Accel control */
        ret = sch16t_set_acc_sens_dec(st,
                                    st->sens_acc1,
                                    st->sens_acc2,
                                    st->sens_acc3,
                                    SCH16T_DEFAULT_DECIMATION);
        if (ret){
            dev_info(&st->spi->dev, "Failed to set accel control on attempt %d, error code %d\n", startup_attempt + 1, ret);
            // continue;    // Sensor failed, reset and retry.;
        }

        /* enable SYNC function */
        ret = sch16t_setSYNC(st, 0, true);
        if (ret) {
            dev_info(&st->spi->dev, "Failed to enable SYNC function on attempt %d, error code %d\n", startup_attempt + 1, ret);
        }

        /* Enable measurement */
        ret = sch16t_enable_meas(st, true, false);
        if (ret){
            dev_info(&st->spi->dev, "Failed to enable measurement on attempt %d, error code %d\n", startup_attempt + 1, ret);
            // continue;    // Sensor failed, reset and retry.;
        }

        // Wait 215 ms
        usleep_range(215000, 220000);

        // Read all status registers once. No critization
        sch16t_get_status(st);

        // Write EOI = 1 (End of Initialization command)
        sch16t_enable_meas(st, true, true);
        
        // Wait 3 ms
        usleep_range(3000, 3500);
        
        // Read all status registers twice.
        sch16t_get_status(st);
        sch16t_get_status(st);

        // Read all user control registers and verify content - Add verification here if needed for FuSa.

        // Check that all status registers have OK status.
        if (!sch16t_verify_status(st)) {
            SCH1status = false;            
            ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_SOFTRESET, st->_ta9_8), &outData);    // Sensor failed, reset and retry.
        }
        else {
            SCH1status = true;           
            break;
        }

    }

    if (SCH1status != true)
        ret = SCH16T_ERR_SENSOR_INIT;
             
    return ret;
}

/**
 * @brief Convert summed raw data from sensor to real values. Also calculate averages values.
 *
 * @param data_in - pointer to summed raw data from sensor
 *        data_out - pointer to converted values
 * 
 * @return None                       
 */
// void sch16t_convertData(struct sch16t_state *st)
// {
//     // Convert from raw counts to sensitivity and calculate averages here for faster execution
//     st->rate_x = (float)st->rate_x_raw / (float)st->sens_rate1;
//     st->rate_y = (float)st->rate_y_raw / (float)st->sens_rate1;
//     st->rate_z = (float)st->rate_z_raw / (float)st->sens_rate1;
//     st->acc_x = (float)st->acc_x_raw / (float)st->sens_acc1;
//     st->acc_y = (float)st->acc_y_raw / (float)st->sens_acc1;
//     st->acc_z = (float)st->acc_z_raw / (float)st->sens_acc1;

//     // Convert temperature and calculate average
//     st->temp = (float)st->temp_raw / 100;
// }


/**
 * @brief Check if 48-bit MISO frames have any error bits set. Return true on the first error encountered.
 *
 * @param data - pointer to 48-bit MISO frames from sensor
 *        size - number of frames to check
 * 
 * @return true = any error bit set
 *         false = no error
 */
bool sch16t_check48bitFrameError(u64 *data, int size)
{
    for (int i = 0; i < size; i++) 
    {
        u64 value = data[i];
        if (value & SCH16T_MASK_ERROR)
            return true;
    }
    
    return false;
}

/**
 * @brief Read rate, acceleration and temperature data from sensor. Called by sampling_callback()
 *
 * @param data - pointer to "raw" data from sensor
 * 
 * @return None                       
 */
int sch16t_getData(struct sch16t_state *st)
{
    u64 outData;
    u64 rate_x_raw;
    u64 rate_y_raw;
    u64 rate_z_raw;
    u64 acc_x_raw;
    u64 acc_y_raw;
    u64 acc_z_raw;
    u64 temp_raw;
    int ret;

    /* Caller must hold st->lock. */
    if (!mutex_is_locked(&st->lock))
        return SCH16T_ERR_OTHER;

    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_RATE_X1, st->_ta9_8), &outData);
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_RATE_Y1, st->_ta9_8), &rate_x_raw);
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_RATE_Z1, st->_ta9_8), &rate_y_raw);
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_ACC_X1, st->_ta9_8), &rate_z_raw);
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_ACC_Y1, st->_ta9_8), &acc_x_raw);
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_ACC_Z1, st->_ta9_8), &acc_y_raw);
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_TEMP, st->_ta9_8), &acc_z_raw);
    ret = sch16t_sendRequest(st, sch16t_addTargetAddress(REQ_READ_TEMP, st->_ta9_8), &temp_raw);

    if(ret)
        return SCH16T_ERR_SPI_COMMUNICATION;

    // Get possible frame errors
    u64 miso_words[] = {rate_x_raw, rate_y_raw, rate_z_raw, acc_x_raw, acc_y_raw, acc_z_raw, temp_raw};       
    st->frame_error = sch16t_check48bitFrameError(miso_words, (sizeof(miso_words) / sizeof(u64)));
    
    // Parse MISO data to structure
    st->rate_x_raw = sch16t_data_int32(rate_x_raw);
    st->rate_y_raw = sch16t_data_int32(rate_y_raw);
    st->rate_z_raw = sch16t_data_int32(rate_z_raw);
    st->acc_x_raw = sch16t_data_int32(acc_x_raw);
    st->acc_y_raw = sch16t_data_int32(acc_y_raw);
    st->acc_z_raw = sch16t_data_int32(acc_z_raw);

    // Temperature data is always 16 bits wide. Drop 4 LSBs as they are not used.
    st->temp_raw = sch16t_data_int32(temp_raw) >> 4;

    return SCH16T_OK;
}

/* =========================================================================
 * IIO channel definitions
 * ======================================================================= */

/*
 * We expose the "decimated" Rate2 / Acc2 channels because those are what
 * imu_node.cpp used.  All channels report processed (scaled) values.
 *
 * Scale factors are stored in the iio_chan_spec.scan_index field (unused for
 * buffered access here) and applied in read_raw().
 */

static const struct iio_chan_spec sch16t_channels[] = {
    /* Angular velocity — rad/s */
    {
        .type           = IIO_ANGL_VEL,
        .modified       = 1,
        .channel2       = IIO_MOD_X,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
        .scan_index     = 0,
        .scan_type = {
            .sign       = 's',
            .realbits   = 20,
            .storagebits= 32,
            .shift      = 0,
            .endianness = IIO_BE,
        },
    },
    {
        .type           = IIO_ANGL_VEL,
        .modified       = 1,
        .channel2       = IIO_MOD_Y,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
        .scan_index     = 1,
        .scan_type = {
            .sign       = 's',
            .realbits   = 20,
            .storagebits= 32,
            .shift      = 0,
            .endianness = IIO_BE,
        },
    },
    {
        .type           = IIO_ANGL_VEL,
        .modified       = 1,
        .channel2       = IIO_MOD_Z,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
        .scan_index     = 2,
        .scan_type = {
            .sign       = 's',
            .realbits   = 20,
            .storagebits= 32,
            .shift      = 0,
            .endianness = IIO_BE,
        },
    },

    /* Linear acceleration — m/s² */
    {
        .type           = IIO_ACCEL,
        .modified       = 1,
        .channel2       = IIO_MOD_X,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
        .scan_index     = 3,
        .scan_type = {
            .sign       = 's',
            .realbits   = 20,
            .storagebits= 32,
            .shift      = 0,
            .endianness = IIO_BE,
        },
    },
    {
        .type           = IIO_ACCEL,
        .modified       = 1,
        .channel2       = IIO_MOD_Y,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
        .scan_index     = 4,
        .scan_type = {
            .sign       = 's',
            .realbits   = 20,
            .storagebits= 32,
            .shift      = 0,
            .endianness = IIO_BE,
        },
    },
    {
        .type           = IIO_ACCEL,
        .modified       = 1,
        .channel2       = IIO_MOD_Z,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
        .scan_index     = 5,
        .scan_type = {
            .sign       = 's',
            .realbits   = 20,
            .storagebits= 32,
            .shift      = 0,
            .endianness = IIO_BE,
        },
    },

    /* Temperature — reported in milli-°C */
    {
        .type           = IIO_TEMP,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
        .scan_index     = 6,
        .scan_type = {
            .sign       = 's',
            .realbits   = 16,
            .storagebits= 32,
            .shift      = 0,
            .endianness = IIO_BE,
        },
    },

    /* Timestamp channel (required for triggered buffer) */
    IIO_CHAN_SOFT_TIMESTAMP(7),
};

static const unsigned long sch16t_scan_masks[] = {
    BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6) | BIT(7),
    0
};

/* =========================================================================
 * IIO read_raw callback
 * ======================================================================= */

/**
 * sch16t_read_raw() - IIO read_raw handler.
 *
 * Handles IIO_CHAN_INFO_RAW (returns raw 20-bit ADC counts) and
 * IIO_CHAN_INFO_SCALE (returns the channel's physical scaling factor).
 *
 * For angular velocity the scale converts counts → rad/s:
 *   scale = (1 / sens_rate_lsb_per_dps) × (π / 180)
 *         = π / (180 × sens_rate)   [in rad/s per count]
 *
 * For acceleration the scale converts counts → m/s²:
 *   scale = 1 / sens_acc_lsb_per_ms2
 *
 * IIO represents fractional scales as val + val2/1e9  (IIO_VAL_INT_PLUS_NANO).
 */
static int sch16t_read_raw(struct iio_dev *indio_dev,
                            struct iio_chan_spec const *chan,
                            int *val, int *val2, long mask)
{
    struct sch16t_state *st = iio_priv(indio_dev);
    int ret = 0;

    switch (mask) {

    case IIO_CHAN_INFO_RAW:

        // mutex_lock(&st->lock);

        // ret = sch16t_getData(st);
        // if (ret) {
        //     mutex_unlock(&st->lock);
        //     return -EINVAL;
        // }

        switch (chan->type) {

        case IIO_ANGL_VEL:
            switch (chan->channel2) {
            case IIO_MOD_X:
                *val = st->rate_x_raw;
                break;
            case IIO_MOD_Y:
                *val = st->rate_y_raw;
                break;
            case IIO_MOD_Z:
                *val = st->rate_z_raw;
                break;
            default:
                ret = -EINVAL;
            }
            break;

        case IIO_ACCEL:
            switch (chan->channel2) {
            case IIO_MOD_X:
                *val = st->acc_x_raw;
                break;
            case IIO_MOD_Y:
                *val = st->acc_y_raw;
                break;
            case IIO_MOD_Z:
                *val = st->acc_z_raw;
                break;
            default:
                ret = -EINVAL;
            }
            break;

        case IIO_TEMP:
            *val = st->temp_raw;
            break;

        default:
            ret = -EINVAL;
        }

        // mutex_unlock(&st->lock);

        if (ret)
            return ret;

        return IIO_VAL_INT;

    case IIO_CHAN_INFO_SCALE:
        switch (chan->type) {
        case IIO_ANGL_VEL: {
            /*
             * scale = π / (180 × sens_rate)  [rad/s per count]
             * Expressed as val=0, val2 = (π/180/sens_rate) × 1e9
             *
             * π/180 ≈ 17453293 × 1e-9   (nano-radians per milli-degree)
             * val2  = 17453293 / sens_rate
             */
            *val  = 0;
            *val2 = 17453293 / st->sens_rate2;
            return IIO_VAL_INT_PLUS_NANO;
        }
        case IIO_ACCEL: {
            /*
             * scale = 1 / sens_acc  [m/s² per count]
             * val=0, val2 = 1e9 / sens_acc
             */
            *val  = 0;
            *val2 = 1000000000 / st->sens_acc2;
            return IIO_VAL_INT_PLUS_NANO;
        }
        case IIO_TEMP:
            /*
             * Raw temperature is in units of 0.01 °C (100 counts per °C).
             * IIO temperature is in milli-°C, so scale = 10.
             */
            *val  = 0;
            *val2 = 10000;   /* 0.01 = 10000 / 1000000 */
            return IIO_VAL_INT_PLUS_MICRO;
        default:
            return -EINVAL;
        }

    default:
        return -EINVAL;
    }
}

/* =========================================================================
 * IIO PWM configuration and sysfs interface
 * ======================================================================= */
/**
 * sch16t_set_pwm_freq() -set PWM frequency, i.e., IMU data update rate
 * @st: driver state
 * @freq_hz: target frequency (Hz)
 *
 * Supported frequency range depends on SCH16T's ODR and PWM hardware capabilities.
 * Typical values: 100Hz, 200Hz, 400Hz, 800Hz, 1600Hz
 */
static int sch16t_set_pwm_freq(struct sch16t_state *st, u32 freq_hz)
{
    struct pwm_device *pwm = st->pwm;
    u32 period_ns, duty_ns;
    int ret;

    if (!pwm)
        return -ENODEV;

    /* freq_hz = 0 represents PWM disabled */
    if (freq_hz == 0) {
        pwm_disable(pwm);
        st->pwm_period_ns = 0;
        st->pwm_duty_ns = 0;
        dev_info(&st->spi->dev, "PWM stopped\n");
        return 0;
    }

    /* Limit frequency range: SCH16T typical ODR 1600Hz, minimum recommended 100Hz */
    if (freq_hz < 100 || freq_hz > 1600)
        return -EINVAL;

    period_ns = NSEC_PER_SEC / freq_hz;
    duty_ns = period_ns / 4;  /* 25% duty cycle */

    ret = pwm_config(pwm, duty_ns, period_ns);
    if (ret)
        return ret;

    ret = pwm_enable(pwm);
    if (ret)
        return ret;

    st->pwm_period_ns = period_ns;
    st->pwm_duty_ns = duty_ns;

    dev_info(&st->spi->dev, "PWM set to %u Hz (period=%u ns, duty=%u ns)\n",
             freq_hz, period_ns, duty_ns);

    return 0;
}

/**
 * sch16t_pwm_freq_store() - sysfs write interface, allowing users to modify PWM frequency
 */
static ssize_t sch16t_pwm_freq_store(struct device *dev,
                                      struct device_attribute *attr,
                                      const char *buf, size_t len)
{
    struct iio_dev *indio_dev = dev_to_iio_dev(dev);
    struct sch16t_state *st = iio_priv(indio_dev);
    u32 freq_hz;
    int ret;

    ret = kstrtou32(buf, 10, &freq_hz);
    if (ret)
        return ret;

    mutex_lock(&st->lock);
    ret = sch16t_set_pwm_freq(st, freq_hz);
    mutex_unlock(&st->lock);

    return ret ? ret : len;
}

/**
 * sch16t_pwm_freq_show() - sysfs read interface, displaying current PWM frequency
 */
static ssize_t sch16t_pwm_freq_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
    struct iio_dev *indio_dev = dev_to_iio_dev(dev);
    struct sch16t_state *st = iio_priv(indio_dev);
    u32 freq_hz;

    if (!st->pwm_period_ns)
        return sprintf(buf, "0\n");

    freq_hz = NSEC_PER_SEC / st->pwm_period_ns;
    return sprintf(buf, "%u\n", freq_hz);
}

static IIO_DEVICE_ATTR(pwm_frequency, S_IRUGO | S_IWUSR,
                       sch16t_pwm_freq_show, sch16t_pwm_freq_store, 0);



/* =========================================================================
 * IIO external trigger buffer handler
 *
 * Called by the IIO core when an external trigger is received.
 * Reads Rate2 X/Y/Z + Acc2 X/Y/Z + temperature in a burst and
 * pushes them into the IIO buffer.
 * ======================================================================= */

/**
 * sch16t_irq_thread_fn() - Interrupt thread function for external trigger.
 *
 * Data layout in buffer[] (matching scan_index order):
 *   [0..2]  Rate2 X/Y/Z  (s32)
 *   [3..5]  Acc2  X/Y/Z  (s32)
 *   [6]     Temperature  (s32)
 *   [7]     Timestamp    (s64, filled by IIO core)
 */
static irqreturn_t sch16t_irq_thread_fn(int irq, void *p)
{
    struct iio_dev *indio_dev = p;   // ← 直接就是 indio_dev，不是 poll_func
    struct sch16t_state *st = iio_priv(indio_dev);

    /*
     * Buffer: 7 × s32 data + 1 × s64 timestamp = 36 bytes.
     * Align to 8 bytes for the timestamp.
     */

    int ret;

    /*
     * SCH16T is pipelined: each SPI transaction returns the previous
     * register's value.  We prime the pipeline by sending the first
     * request, then issue N more to collect N responses.
     *
     * Sequence (7 registers, 8 transactions):
     *   TX[0] = REQ_RATE_X2  → response discarded (pipeline prime)
     *   TX[1] = REQ_RATE_Y2  → RX[1] = RATE_X2
     *   TX[2] = REQ_RATE_Z2  → RX[2] = RATE_Y2
     *   TX[3] = REQ_ACC_X2   → RX[3] = RATE_Z2
     *   TX[4] = REQ_ACC_Y2   → RX[4] = ACC_X2
     *   TX[5] = REQ_ACC_Z2   → RX[5] = ACC_Y2
     *   TX[6] = REQ_TEMP     → RX[6] = ACC_Z2
     *   TX[7] = REQ_TEMP     → RX[7] = TEMP
     */
    mutex_lock(&st->lock);
    ret = sch16t_getData(st); // get data from sensor
    if (ret) {
        dev_err_ratelimited(&st->spi->dev,
                            "Failed to read sensor data in IRQ: %d\n", ret);
        mutex_unlock(&st->lock);
        return IRQ_HANDLED;
    }

    // sch16t_convertData(st); // convert raw data to real values

    st->scan_buffer.data[0] = st->rate_x_raw;
    st->scan_buffer.data[1] = st->rate_y_raw;
    st->scan_buffer.data[2] = st->rate_z_raw;
    st->scan_buffer.data[3] = st->acc_x_raw;
    st->scan_buffer.data[4] = st->acc_y_raw;
    st->scan_buffer.data[5] = st->acc_z_raw;
    st->scan_buffer.data[6] = st->temp_raw;
    st->scan_buffer.ts = iio_get_time_ns(indio_dev);

    iio_push_to_buffers_with_timestamp(indio_dev, &st->scan_buffer.data, st->scan_buffer.ts);

    mutex_unlock(&st->lock);
    return IRQ_HANDLED;
}

/**
 * sch16t_trigger_handler() - IIO框架要求的pollfunc占位符
 *
 * 实际数据读取在中断服务程序中完成，这里仅通知IIO核心触发完成。
 */
static irqreturn_t sch16t_trigger_handler(int irq, void *p)
{
    struct iio_poll_func *pf = p;
    struct iio_dev *indio_dev = pf->indio_dev;

    /* 数据已在中断中推送到buffer，这里只需通知完成 */
    iio_trigger_notify_done(indio_dev->trig);
    return IRQ_HANDLED;
}

/**
 * sch16t_setup_trigger() - register IIO trigger
 *
 * Uses external interrupt as hardware trigger source, bound to IIO triggered buffer.
 */
static int sch16t_setup_trigger(struct iio_dev *indio_dev)
{
    struct sch16t_state *st = iio_priv(indio_dev);
    struct device *dev = &st->spi->dev;
    struct iio_trigger *trig;
    int ret;

    /* 创建IIO触发器实例 */
    trig = devm_iio_trigger_alloc(dev, "%s-dev%d", indio_dev->name,
                                  iio_device_id(indio_dev));
    if (!trig) {
        dev_err(dev, "Failed to allocate iio trigger\n");
        return -ENOMEM;
    }

    st->trig = trig;
    iio_trigger_set_drvdata(trig, indio_dev);

    ret = devm_iio_trigger_register(dev, trig);
    if (ret) {
        dev_err(dev, "Failed to register iio trigger: %d\n", ret);
        return -EINVAL;
    }

    /* 绑定触发器到indio_dev */
    indio_dev->trig = iio_trigger_get(trig);
    
    /* 从设备树获取中断GPIO */
    st->irq_gpio = of_get_named_gpio(dev->of_node, "irq-gpios", 0);
    if (!gpio_is_valid(st->irq_gpio)) {
        dev_err(dev, "Invalid irq-gpio in DT\n");
        return -EINVAL;
    }

    ret = gpio_request_one(st->irq_gpio, GPIOF_IN, "sch16t_irq");
    if (ret) {
        dev_err(dev, "Failed to request irq gpio: %d\n", ret);
        return ret;
    }

    st->irq_number = gpio_to_irq(st->irq_gpio);
    if (st->irq_number < 0) {
        ret = st->irq_number;
        goto err_free_gpio;
    }

    /* 申请中断：上升沿触发（PWM输出上升沿或下降沿均可配置） */
    ret = request_threaded_irq(st->irq_number, NULL, sch16t_irq_thread_fn,
                      IRQF_TRIGGER_RISING | IRQF_ONESHOT,
                      "sch16t", indio_dev);
    if (ret) {
        dev_err(dev, "Failed to request IRQ %d: %d\n", st->irq_number, ret);
        goto err_free_irq;
    }

    dev_info(dev, "Trigger IRQ %d on GPIO %d registered\n",
             st->irq_number, st->irq_gpio);

    return 0;

err_free_irq:
    free_irq(st->irq_number, indio_dev);
err_free_gpio:
    gpio_free(st->irq_gpio);
    return ret;
}

/**
 * sch16t_free_trigger() - clear up trigger resources on driver remove
 */
static void sch16t_free_trigger(struct sch16t_state *st)
{
    struct iio_dev *indio_dev;

    if (!st)
        return;

    /* 通过 spi->dev 获取 iio_dev（probe 中 spi_set_drvdata 设置的） */
    indio_dev = spi_get_drvdata(st->spi);

    if (st->irq_number > 0)
        free_irq(st->irq_number, indio_dev);
    if (gpio_is_valid(st->irq_gpio))
        gpio_free(st->irq_gpio);
    // if (st->trig)
    // {
    //     iio_trigger_put(st->trig);
    //     st->trig = NULL;
    // }
}

/**
 * sch16t_remove() - SPI driver remove function, called when the device is removed or driver is unloaded.
 * Performs cleanup of resources allocated in probe, including:
 *   - Disabling PWM output
 */
static void sch16t_remove(struct spi_device *spi)
{
    struct iio_dev *indio_dev = spi_get_drvdata(spi);
    struct sch16t_state *st = iio_priv(indio_dev);

    if (indio_dev->trig) {
        iio_trigger_put(indio_dev->trig);
        indio_dev->trig = NULL;
    }

    /* 停止PWM */
    pwm_disable(st->pwm);

    /* 释放触发器资源 */
    sch16t_free_trigger(st);

    // iio_device_unregister(indio_dev);

    return;
}

/* =========================================================================
 * IIO ops structure
 * ======================================================================= */
static struct attribute *sch16t_attributes[] = {
    &iio_dev_attr_pwm_frequency.dev_attr.attr,
    NULL,
};
static const struct attribute_group sch16t_attribute_group = {
    .attrs = sch16t_attributes,
};
static const struct iio_info sch16t_info = {
    .read_raw = sch16t_read_raw,
    .attrs = &sch16t_attribute_group,
};

/* =========================================================================
 * SPI probe / remove
 * ======================================================================= */

static int sch16t_probe(struct spi_device *spi)
{
    struct iio_dev      *indio_dev;
    struct sch16t_state *st;
    const struct spi_device_id *id = spi_get_device_id(spi);
    int ret;

    dev_info(&spi->dev, "sch16t probe start\n");
    if(id->driver_data != SCH16T_VARIANT_K01 && id->driver_data != SCH16T_VARIANT_K10) {
        dev_err(&spi->dev, "Unknown device variant\n");
        return -EINVAL;
    }
    indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
    if (!indio_dev)
        return -ENOMEM;

    st = iio_priv(indio_dev);
    st->spi     = spi;
    st->variant = (enum sch16t_variant)(uintptr_t)id->driver_data;
    mutex_init(&st->lock);

    /* init PWM device (get from device tree)*/
    st->pwm = devm_pwm_get(&spi->dev, NULL);
    if (IS_ERR(st->pwm)) {
        ret = PTR_ERR(st->pwm);
        if (ret != -EPROBE_DEFER)
            dev_err(&spi->dev, "Failed to get PWM: %d\n", ret);
        return ret;
    }

    /* Set sensitivities based on variant */
    if (st->variant == SCH16T_VARIANT_K10) {
        st->sens_rate1 = SCH16T_K10_SENS_RATE_LSB_DPS;
        st->sens_rate2 = SCH16T_K10_SENS_RATE_LSB_DPS;
        st->sens_acc1  = SCH16T_K10_SENS_ACC_LSB_MS2;
        st->sens_acc2  = SCH16T_K10_SENS_ACC_LSB_MS2;
        st->sens_acc3  = SCH16T_K10_SENS_ACC_LSB_MS2;
        st->_ta9_8 = 0x00; // Target address bits for TA address.
    } else {
        st->sens_rate1 = SCH16T_K01_SENS_RATE_LSB_DPS;
        st->sens_rate2 = SCH16T_K01_SENS_RATE_LSB_DPS;
        st->sens_acc1  = SCH16T_K01_SENS_ACC_LSB_MS2;
        st->sens_acc2  = SCH16T_K01_SENS_ACC_LSB_MS2;
        st->sens_acc3  = SCH16T_K01_SENS_ACC_LSB_MS2;
        st->_ta9_8 = 0x00; // Target address bits for TA address.
    }

    /* Configure the SPI controller */
    spi->mode          = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz  = 10000000;
    ret = spi_setup(spi);
    if (ret) {
        dev_err(&spi->dev, "spi_setup failed: %d\n", ret);
        goto err_pwm_disable;
    }

    /* IIO device setup */
    spi_set_drvdata(spi, indio_dev);
    indio_dev->name            = SCH16T_DRIVER_NAME;
    indio_dev->info            = &sch16t_info;
    indio_dev->modes           = INDIO_DIRECT_MODE | INDIO_BUFFER_TRIGGERED;
    indio_dev->channels        = sch16t_channels;
    indio_dev->num_channels    = ARRAY_SIZE(sch16t_channels);
    indio_dev->available_scan_masks = sch16t_scan_masks;
    /* add sysfs attributes */
    indio_dev->info = &sch16t_info;  /* need to add .attrs in sch16t_info */

    /* Initialise the sensor hardware */
    mutex_lock(&st->lock);
    ret = sch16t_init_sensor(st);
    mutex_unlock(&st->lock);

    if (ret) {
        dev_err(&spi->dev, "sensor init failed: %d\n", ret);
        goto err_pwm_disable;
    }

    /* 设置触发器（外部中断） */
    ret = sch16t_setup_trigger(indio_dev);
    if (ret) {
        dev_err(&spi->dev, "Trigger setup failed: %d\n", ret);
        goto err_pwm_disable;
    }

    // /* 设置triggered buffer（绑定到自定义触发器） */
    ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev,
                                         iio_pollfunc_store_time,
                                         sch16t_trigger_handler, NULL);
    if (ret) {
        dev_err(&spi->dev, "triggered buffer setup failed: %d\n", ret);
        goto err_free_trigger;
    }

    ret = devm_iio_device_register(&spi->dev, indio_dev);
    if (ret) {
        dev_err(&spi->dev, "IIO device register failed: %d\n", ret);
        goto err_free_trigger;
    }

    /* configure PWM frequency(default: closed) */
    ret = sch16t_set_pwm_freq(st, 0);
    if (ret) {
        dev_err(&spi->dev, "Failed to configure PWM: %d\n", ret);
        return ret;
    }

    dev_info(&spi->dev, "SCH16T-%s initialised with PWM+IRQ trigger\n",
             st->variant == SCH16T_VARIANT_K10 ? "K10" : "K01");
    return 0;

err_free_trigger:
    sch16t_free_trigger(st);
err_pwm_disable:
    pwm_disable(st->pwm);
    return ret;
}

/* =========================================================================
 * Driver tables
 * ======================================================================= */

static const struct spi_device_id sch16t_id[] = {
    { SCH16T_K01_NAME, SCH16T_VARIANT_K01 },
    { SCH16T_K10_NAME, SCH16T_VARIANT_K10 },
    { }
};
MODULE_DEVICE_TABLE(spi, sch16t_id);

static const struct of_device_id sch16t_of_match[] = {
    { .compatible = "murata,sch16t-k01", .data = (void *)SCH16T_VARIANT_K01 },
    { .compatible = "murata,sch16t-k10", .data = (void *)SCH16T_VARIANT_K10 },
    { }
};
MODULE_DEVICE_TABLE(of, sch16t_of_match);

static struct spi_driver sch16t_driver = {
    .driver = {
        .name           = SCH16T_DRIVER_NAME,
        .of_match_table = sch16t_of_match,
    },
    .probe    = sch16t_probe,
    .remove   = sch16t_remove,
    .id_table = sch16t_id,
};
module_spi_driver(sch16t_driver);

MODULE_AUTHOR("Derived from Murata Electronics Oy userspace library");
MODULE_DESCRIPTION("Murata SCH16T IMU IIO driver");
MODULE_LICENSE("Dual BSD/GPL");
