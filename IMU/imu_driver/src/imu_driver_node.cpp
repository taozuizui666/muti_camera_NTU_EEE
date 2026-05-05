#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/imu.hpp"
#include "sensor_msgs/msg/temperature.hpp"
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

using namespace std::chrono_literals;

// ===================== 你原版 test.c 完整复制 1:1 =====================
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
#define REG_COMP_ID         0x3C

#define GYRO_SENS   6.25f
#define ACC_SENS    200.0f
#define TEMP_SENS   100.0f

static int g_spi_fd = -1;

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

static uint32_t spi_xfer32(uint32_t tx)
{
    uint8_t tx_buf[4], rx_buf[4];
    tx_buf[0] = (tx >> 24) & 0xFF;
    tx_buf[1] = (tx >> 16) & 0xFF;
    tx_buf[2] = (tx >>  8) & 0xFF;
    tx_buf[3] = (tx >>  0) & 0xFF;
    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf        = (unsigned long)tx_buf;
    tr.rx_buf        = (unsigned long)rx_buf;
    tr.len           = 4;
    tr.speed_hz      = SPI_SPEED_HZ;
    tr.bits_per_word = SPI_BITS;
    tr.delay_usecs   = 0;

    if (ioctl(g_spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("SPI transfer error");
        return 0;
    }
    return ((uint32_t)rx_buf[0] << 24) |
           ((uint32_t)rx_buf[1] << 16) |
           ((uint32_t)rx_buf[2] <<  8) |
           ((uint32_t)rx_buf[3]);
}

// ===================== 你原版代码：>>4 完全保留 =====================
static uint16_t read_reg(uint8_t addr)
{
    uint32_t req = build_read_frame(addr);
    spi_xfer32(req);
    usleep(1);
    uint32_t rx = spi_xfer32(req);
    return (uint16_t)((rx >> 4) & 0xFFFF);
}

static void startup_sequence(void)
{
    usleep(50000);
    spi_xfer32(0x0D60000AUL); usleep(1); spi_xfer32(0x0D60000AUL);
    usleep(215000);
    spi_xfer32(0x0D60001CUL); usleep(1); spi_xfer32(0x0D60001CUL);
    usleep(3000);
}

static bool spi_init(void)
{
    g_spi_fd = open(SPI_DEVICE, O_RDWR);
    if (g_spi_fd < 0) {
        perror("无法打开SPI设备");
        return false;
    }
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = SPI_BITS;
    uint32_t speed = SPI_SPEED_HZ;

    if (ioctl(g_spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(g_spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(g_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("SPI配置失败");
        close(g_spi_fd);
        return false;
    }
    return true;
}

// ===================== ROS2 节点：topic 一定出现 =====================
class Sch16tImuNode : public rclcpp::Node
{
public:
    Sch16tImuNode() : Node("sch16t_imu_node")
    {
        // ? 第一步先创建 topic（不管 SPI 是否成功）
        imu_pub_ = this->create_publisher<sensor_msgs::msg::Imu>("/imu/data", 10);
        temp_pub_ = this->create_publisher<sensor_msgs::msg::Temperature>("/imu/temperature", 10);

        // 初始化 SPI
        /*
        bool ok = spi_init();
        if (ok) {
            startup_sequence();
            RCLCPP_INFO(get_logger(), "? IMU 初始化成功");
        } else {
            RCLCPP_WARN(get_logger(), "?? SPI 打开失败，但 topic 已创建");
        }
        */

        // ? 定时器一定启动
        timer_ = this->create_wall_timer(10ms, std::bind(&Sch16tImuNode::pub_cb, this));
    }

private:
    rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
    rclcpp::Publisher<sensor_msgs::msg::Temperature>::SharedPtr temp_pub_;
    rclcpp::TimerBase::SharedPtr timer_;

    void pub_cb()
    {
        // 读数据（即使失败也发空消息，保证 topic 存在）
        int16_t gx=0;// = (int16_t)read_reg(REG_RATE_X1);
        int16_t gy=0;// = (int16_t)read_reg(REG_RATE_Y1);
        int16_t gz=0;// = (int16_t)read_reg(REG_RATE_Z1);
        int16_t ax=0;// = (int16_t)read_reg(REG_ACC_X1);
        int16_t ay=0;// = (int16_t)read_reg(REG_ACC_Y1);
        int16_t az=0;// = (int16_t)read_reg(REG_ACC_Z1);
        int16_t tp=0;// = (int16_t)read_reg(REG_TEMP);

        float wx = gx / GYRO_SENS * M_PI / 180.0f;
        float wy = gy / GYRO_SENS * M_PI / 180.0f;
        float wz = gz / GYRO_SENS * M_PI / 180.0f;
        float axf = ax / ACC_SENS;
        float ayf = ay / ACC_SENS;
        float azf = az / ACC_SENS;
        float tf = tp / TEMP_SENS;
        
        // RCLCPP_INFO(this->get_logger(), "1234");

        auto imu_msg = sensor_msgs::msg::Imu();
        imu_msg.header.stamp = get_clock()->now();
        imu_msg.header.frame_id = "imu";
        imu_msg.angular_velocity.x = wx;
        imu_msg.angular_velocity.y = wy;
        imu_msg.angular_velocity.z = wz;
        imu_msg.linear_acceleration.x = axf;
        imu_msg.linear_acceleration.y = ayf;
        imu_msg.linear_acceleration.z = azf;
        imu_msg.orientation.w = 1.0;

        auto temp_msg = sensor_msgs::msg::Temperature();
        temp_msg.header = imu_msg.header;
        temp_msg.temperature = tf;

        imu_pub_->publish(imu_msg);
        temp_pub_->publish(temp_msg);
    }
};

int main(int argc, char *argv[])
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<Sch16tImuNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    // close(g_spi_fd);
    return 0;
}