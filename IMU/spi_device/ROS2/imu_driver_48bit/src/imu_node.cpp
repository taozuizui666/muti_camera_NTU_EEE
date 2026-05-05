#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/imu.hpp>

#include "imu_node/SCH16T.h"
#include <linux/spi/spidev.h>

#include <cstring>
#include <cmath>

#define SPI_DEVICE "/dev/spidev0.0"
#define SPI_SPEED  10000000

#define FILTER_RATE         68.0f      // Hz, LPF0 Nominal Cut-off Frequency (-3dB).
#define FILTER_ACC12        68.0f
#define FILTER_ACC3         68.0f
#define SENSITIVITY_RATE1   200.0f     // LSB / dps, DYN1 Nominal Sensitivity for 20 bit data.
#define SENSITIVITY_RATE2   200.0f
#define SENSITIVITY_ACC1    3200.0f     // LSB / m/s2, DYN1 Nominal Sensitivity for 20 bit data.
#define SENSITIVITY_ACC2    3200.0f
#define SENSITIVITY_ACC3    3200.0f     // LSB / m/s2, DYN1 Nominal Sensitivity for 20 bit data.
#define DECIMATION_RATE     4          // DEC2, Output sample rate decimation. Nominal output rate of 5.9kHz.
#define DECIMATION_ACC      4

class ImuNode : public rclcpp::Node
{
public:
    ImuNode() : Node("imu_node")
    {
        publisher_ = this->create_publisher<sensor_msgs::msg::Imu>("/imu", 10);

        timer_ = this->create_wall_timer(
            std::chrono::milliseconds(10),   // 100Hz
            std::bind(&ImuNode::timer_callback, this)
        );

        spi_fd_ = new SPIClass(SPI_DEVICE);
        if (!spi_fd_->begin(SPI_SPEED)) {
            RCLCPP_ERROR(this->get_logger(), "Failed to initialize SPI");
            return;
        }

        sch16t_k10_ = new SCH16T_K10(*spi_fd_, -1, -1);  // CS and reset pins are not used in Linux SPI implementation
        
        // parameter configuration for SCH16T
        Filter.Rate12 = FILTER_RATE;
        Filter.Acc12  = FILTER_ACC12;
        Filter.Acc3   = FILTER_ACC3;

        Sensitivity.Rate1 = SENSITIVITY_RATE1;
        Sensitivity.Rate2 = SENSITIVITY_RATE2;
        Sensitivity.Acc1  = SENSITIVITY_ACC1;
        Sensitivity.Acc2  = SENSITIVITY_ACC2;
        Sensitivity.Acc3  = SENSITIVITY_ACC3;

        Decimation.Rate2 = DECIMATION_RATE;
        Decimation.Acc2  = DECIMATION_ACC;

        //initialize SCH16T sensor, retrying until successful
        while (init_status_ != SCH16T_OK)
        {
            init_status_ = sch16t_k10_->begin(Filter, Sensitivity, Decimation, false);
            if (init_status_ != SCH16T_OK) {
                RCLCPP_ERROR(this->get_logger(), "Failed to initialize SCH16T sensor, retrying...");
                usleep(3000);
            }
        }
    }

    ~ImuNode()
    {
        if (sch16t_k10_) {
            delete sch16t_k10_;
        }
        if (spi_fd_) {
            delete spi_fd_;
        }
    }

private:
    SCH16T_K10* sch16t_k10_;
    SPIClass* spi_fd_;
    int  init_status_ = SCH16T_ERR_OTHER;
    SCH16T_filter         Filter;
    SCH16T_sensitivity    Sensitivity;
    SCH16T_decimation     Decimation;

    rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr publisher_;
    rclcpp::TimerBase::SharedPtr timer_;

    /* ================= ROS ================= */

    void timer_callback()
    {
        auto msg = sensor_msgs::msg::Imu();

        SCH16T_raw_data raw_data;
        SCH16T_result result_data;
        sch16t_k10_->getDataDecimated(&raw_data);
        sch16t_k10_->convertDataDecimated(&raw_data, &result_data);

        /* ת */
        msg.angular_velocity.x = result_data.Rate2[SCH16T_AXIS_X];
        msg.angular_velocity.y = result_data.Rate2[SCH16T_AXIS_Y];
        msg.angular_velocity.z = result_data.Rate2[SCH16T_AXIS_Z];

        msg.linear_acceleration.x = result_data.Acc2[SCH16T_AXIS_X];
        msg.linear_acceleration.y = result_data.Acc2[SCH16T_AXIS_Y];
        msg.linear_acceleration.z = result_data.Acc2[SCH16T_AXIS_Z];

        msg.header.stamp = this->get_clock()->now();
        msg.header.frame_id = "imu_link";

        publisher_->publish(msg);
    }
};

/* ================= main ================= */

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ImuNode>());
    rclcpp::shutdown();
    return 0;
}