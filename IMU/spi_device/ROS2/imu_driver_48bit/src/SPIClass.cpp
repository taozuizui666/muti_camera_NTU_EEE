#include "imu_node/SCH16T.h"
#include <cstring>

bool SPIClass::begin(uint32_t speed_hz) {
    this->fd = open(dev_path, O_RDWR);
    if (this->fd < 0) {
        perror("Failed to open SPI device");
        return false;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits_per_word = 8;

    if (ioctl(this->fd, SPI_IOC_WR_MODE, &mode) == -1 ||
        ioctl(this->fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) == -1 ||
        ioctl(this->fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) == -1) {
        perror("Failed to configure SPI device");
        close(this->fd);
        this->fd = -1;
        return false;
    }

    return true;
}

bool SPIClass::end() {
    if (this->fd >= 0) {
        close(this->fd);
        this->fd = -1;
        return true;
    }
    return false;
}

void SPIClass::transfer(uint8_t* buf, uint8_t len) {
    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long)buf;
    tr.rx_buf = (unsigned long)buf;
    tr.len = len;
    tr.speed_hz = 10000000;
    tr.bits_per_word = 8;
    tr.delay_usecs = 0;

    if (ioctl(this->fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("Failed to send SPI message");
    }
}