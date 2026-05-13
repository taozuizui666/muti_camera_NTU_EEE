# SCH16T Linux IIO Kernel Driver

Linux kernel driver for the **Murata SCH16T** series 6-DOF IMU
(gyroscope + accelerometer + temperature), using the **IIO subsystem**
with **PWM-synchronized external interrupt triggering** over SPI.

---

## Supported devices

| Compatible string      | Variant  | Gyro sensitivity (LSB/dps)  |
|------------------------|----------|-----------------------------|
| `murata,sch16t-k01`    | K01      | 1600 / 3200 / 6400          |
| `murata,sch16t-k10`    | K10      | 100 / 200 / 400             |

---

## Architecture Overview

Unlike standard IIO hrtimer triggers, this driver uses a **hardware PWM**
as the sampling clock and an **external GPIO interrupt** (DRY pin) as the
buffer push trigger:

```
┌─────────────────┐      PWM CLK      ┌─────────────┐
│   PWM Device    │ ─────────────────>│  SCH16T     │
│  (e.g. pwm11)   │                   │   IMU       │
└─────────────────┘                   │             │
                                      │ DRY pin     │
┌─────────────────┐                   │ (Data Ready)│
│  GPIO Interrupt │ <─────────────────│             │
│  (e.g. GPIO0_A0)│                   └─────────────┘
└────────┬────────┘
         |
         V
┌─────────────────────────────────────────────────────────────┐
│                    Kernel IIO Subsystem                     │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  sch16t_irq_thread_fn()                               │  │
│  │   - SPI burst read (7 channels)                       │  │
│  │   - iio_push_to_buffers_with_timestamp()              │  │
│  └───────────────────────────────────────────────────────┘  │
│                           │                                 │
│                           ▼                                 │
│              /dev/iio:deviceN (triggered buffer)            │
└─────────────────────────────────────────────────────────────┘
```


**Key Features:**
- **PWM frequency configurable** via sysfs (`pwm_frequency`, 100~1600 Hz)
- **PWM default OFF** after probe; user must write frequency to start sampling
- **External interrupt trigger** on DRY (Data Ready) rising edge
- **Threaded IRQ** for safe SPI sleep in interrupt context
- **IIO triggered buffer** with 8 channels (7 data + 64-bit timestamp)

---

## File layout

```
sch16t_driver/
├── sch16t.c            ← Kernel driver (SPI + PWM + IRQ + IIO)
├── sch16t.h            ← Driver header (register map, structs)
├── Makefile            ← Kbuild file
├── Kconfig             ← Kernel config entry
└── sch16t-overlay.dts  ← Device tree overlay (RK3588 example)
```

---

## Building (out-of-tree)

```bash
# Go to the building folder
cd iio_device_sync

# Edit makefile
vim Makefile
# Add kernel source code path
KDIR = ~/LubanCat_SDK/kernel-6.1
# Add cross compilation toolchain and CPU architecture
ARCH = arm64
CROSS_COMPILE = /home/computer/LubanCat_SDK/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-

# Build by makefile
make
```

We can build .ko file in this folder. In target device, we can install driver by this way:

```bash
sudo insmod sch16t.ko
```

Driver unLoading

```bash
# 1. Unbind the trigger
echo "" | sudo tee /sys/bus/iio/devices/iio:deviceN/trigger/current_trigger

# 2. Stop PWM
echo 0 | sudo tee /sys/bus/iio/devices/iio:deviceN/pwm_frequency

# 3. Now unload
sudo rmmod sch16t
```
---

## Device Tree

Compile and install the overlay:

```bash
# LubanCat5 compile sample
cd ~/LubanCat_SDK

# Add toolchain path
export PATH=$PATH:/home/computer/LubanCat_SDK/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin

# Add dts to dtbo path
cp rk3588-lubancat-sch16t-overlay ~/LubanCat_SDK/kernel-X.1/arch/arm64/boot/dts/rockchip/overlay

# Edit overlay Makefile to add new dts as building target.
dtbo-$(CONFIG_CPU_RK35XX) += \
	rk3588-lubancat-sch16t-overlay.dtbo \

# build dtbo
make ARCH=arm64 -j4 CROSS_COMPILE=aarch64-none-linux-gnu- dtbs
```
We can get the dtbo in this path: ~/LubanCat_SDK/kernel-X.1/arch/arm64/boot/dts/rockchip/overlay

---

## IIO sysfs interface

After the driver loads the sensor appears at `/sys/bus/iio/devices/iio:deviceN/`.

### Custom Sysfs Attributes

| File            | Access | Description                                                     |
| --------------- | ------ | --------------------------------------------------------------- |
| `pwm_frequency` | RW     | PWM frequency in Hz (100~1600). Write `0` to stop. Default: `0` |

```bash
# Start sampling at 300 Hz
echo 300 | sudo tee /sys/bus/iio/devices/iio:device0/pwm_frequency

# Check current frequency
cat /sys/bus/iio/devices/iio:device0/pwm_frequency

# Stop PWM
echo 0 | sudo tee /sys/bus/iio/devices/iio:device0/pwm_frequency
```

### Reading individual channels

```bash
# Start sampling at 50 Hz (must have before read otherwise the data can't update)
echo 50 | sudo tee /sys/bus/iio/devices/iio:device0/pwm_frequency

# Raw gyroscope counts (20-bit signed)
cat /sys/bus/iio/devices/iio:device0/in_anglvel_x_raw
cat /sys/bus/iio/devices/iio:device0/in_anglvel_y_raw
cat /sys/bus/iio/devices/iio:device0/in_anglvel_z_raw

# Scale factor (rad/s per count)
cat /sys/bus/iio/devices/iio:device0/in_anglvel_x_scale

# Raw acceleration counts
cat /sys/bus/iio/devices/iio:device0/in_accel_x_raw

# Raw temperature counts
cat /sys/bus/iio/devices/iio:device0/in_temp_raw

# close the device
echo 0 | sudo tee /sys/bus/iio/devices/iio:device0/pwm_frequency
```

### Triggered buffer (high-rate capture)

The driver automatically registers an IIO trigger named sch16t-devN.
Use this trigger (not hrtimer) for buffer capture.

Step 1: Enable scan elements

```bash
cd /sys/bus/iio/devices/iio:device0/scan_elements

for ch in anglvel_x anglvel_y anglvel_z accel_x accel_y accel_z temp timestamp; do
    echo 1 | sudo tee in_${ch}_en
done
```

Step 2: Bind the hardware trigger

```bash
# Trigger name is auto-generated: sch16t-dev<N>
echo sch16t-dev0 | sudo tee /sys/bus/iio/devices/iio:device0/trigger/current_trigger
```
 
Step 3: Configure buffer depth

```bash
echo 256 | sudo tee /sys/bus/iio/devices/iio:device0/buffer/length
```

Step 4: Start PWM to begin data flow

```bash
echo 300 | sudo tee /sys/bus/iio/devices/iio:device0/pwm_frequency
```

Step 5: Enable buffer and read

```bash
# Terminal 1: capture binary data
sudo cat /dev/iio:device0 > /tmp/imu.bin &
PID=$!
sleep 5
kill $PID
# Terminal 2: parse with the example C program below
```

Step 6: Stop cleanly

```bash
echo 0 | sudo tee /sys/bus/iio/devices/iio:device0/buffer/enable
echo 0 | sudo tee /sys/bus/iio/devices/iio:device0/pwm_frequency
```
Or you can just run the sch16t_iio_test.c to see the data

```bash
cd user_test
gcc sch16t_iio_test.c -o sch16t_iio_test
sudo ./sch16t_iio_test

```

---

## License

BSD-3-Clause (matching the original Murata library).
Dual-licensed BSD/GPL for kernel module compatibility.
