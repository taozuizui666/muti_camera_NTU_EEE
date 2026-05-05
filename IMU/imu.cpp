#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <errno.h>
#include <cmath>

#define SPI_DEVICE      "/dev/spidev0.0"
#define SPI_SPEED_HZ    10000000
#define SPI_BITS        8

#define REG_RATE_X1         0x01
#define REG_RATE_Y1         0x02
#define REG_RATE_Z1         0x03
#define REG_ACC_X1          0x04
#define REG_ACC_Y1          0x05
#define REG_ACC_Z1          0x06
#define REG_TEMP            0x10

#define GYRO_SENS   6.25f
#define ACC_SENS    200.0f
#define TEMP_SENS   100.0f

static int g_spi_fd = -1;

// ================= CRC =================
static uint8_t calc_crc3(uint32_t frame)
{
    uint32_t data = frame & 0xFFFFFFF8UL;
    uint8_t crc = 0x05;
    for (int i = 31; i >= 0; i--) {
        uint8_t bit = (data >> i) & 0x01;
        if (crc & 0x4)
            crc = (uint8_t)(((crc << 1) ^ 0x3) ^ bit);
        else
            crc = (uint8_t)(crc << 1) | bit;
        crc &= 0x7;
    }
    return crc;
}

static uint32_t build_read_frame(uint8_t addr)
{
    uint32_t frame = 0;
    frame  = (uint32_t)(addr & 0xFF) << 22;
    frame &= 0xFFFFFFF8UL;
    frame |= (calc_crc3(frame) & 0x7);
    return frame;
}

// ================= SPI =================
static uint32_t spi_xfer32(uint32_t tx)
{
    uint8_t tx_buf[4], rx_buf[4];

    tx_buf[0] = (tx >> 24) & 0xFF;
    tx_buf[1] = (tx >> 16) & 0xFF;
    tx_buf[2] = (tx >> 8)  & 0xFF;
    tx_buf[3] = (tx >> 0)  & 0xFF;

    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));

    tr.tx_buf = (unsigned long)tx_buf;
    tr.rx_buf = (unsigned long)rx_buf;
    tr.len = 4;
    tr.speed_hz = SPI_SPEED_HZ;
    tr.bits_per_word = SPI_BITS;

    if (ioctl(g_spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("SPI transfer error");
        return 0;
    }

    return ((uint32_t)rx_buf[0] << 24) |
           ((uint32_t)rx_buf[1] << 16) |
           ((uint32_t)rx_buf[2] << 8)  |
           ((uint32_t)rx_buf[3]);
}

// ================= 读寄存器 =================
static uint16_t read_reg(uint8_t addr)
{
    uint32_t req = build_read_frame(addr);
    spi_xfer32(req);
    usleep(1);
    uint32_t rx = spi_xfer32(req);
    return (uint16_t)((rx >> 4) & 0xFFFF);
}

// ================= 启动序列 =================
static void startup_sequence(void)
{
    usleep(50000);

    spi_xfer32(0x0D60000AUL);
    usleep(1);
    spi_xfer32(0x0D60000AUL);

    usleep(215000);

    spi_xfer32(0x0D60001CUL);
    usleep(1);
    spi_xfer32(0x0D60001CUL);

    usleep(3000);
}

// ================= SPI 初始化 =================
static int spi_init(void)
{
    g_spi_fd = open(SPI_DEVICE, O_RDWR);
    if (g_spi_fd < 0) {
        perror("SPI open failed");
        return -1;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = SPI_BITS;
    uint32_t speed = SPI_SPEED_HZ;

    if (ioctl(g_spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(g_spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(g_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("SPI config failed");
        return -1;
    }

    return 0;
}

// ================= 主函数 =================
int main()
{
    if (spi_init() != 0) {
        return -1;
    }

    startup_sequence();

    printf("IMU start...\n");

    while (1) {
        int16_t gx = (int16_t)read_reg(REG_RATE_X1);
        int16_t gy = (int16_t)read_reg(REG_RATE_Y1);
        int16_t gz = (int16_t)read_reg(REG_RATE_Z1);

        int16_t ax = (int16_t)read_reg(REG_ACC_X1);
        int16_t ay = (int16_t)read_reg(REG_ACC_Y1);
        int16_t az = (int16_t)read_reg(REG_ACC_Z1);

        int16_t tp = (int16_t)read_reg(REG_TEMP);

        float wx = gx / GYRO_SENS * M_PI / 180.0f;
        float wy = gy / GYRO_SENS * M_PI / 180.0f;
        float wz = gz / GYRO_SENS * M_PI / 180.0f;

        float axf = ax / ACC_SENS;
        float ayf = ay / ACC_SENS;
        float azf = az / ACC_SENS;

        float tf = tp / TEMP_SENS;

        printf("GYRO [rad/s]:  %8.4f %8.4f %8.4f\n", wx, wy, wz);
        printf("ACC  [g]:      %8.4f %8.4f %8.4f\n", axf, ayf, azf);
        printf("TEMP [C]:      %8.2f\n", tf);
        printf("-----------------------------------\n");

        usleep(100000); // 100ms
    }

    close(g_spi_fd);
    return 0;
}