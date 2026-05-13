/*
 * sch16t_read_controlled.c
 *
 * 用户空间测试：先写 pwm_frequency 启动，读完再停止
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#define SYSFS_IIO_PATH  "/sys/bus/iio/devices"
#define DEV_IIO_PATH    "/dev"
#define READ_BUF_SIZE   4096
#define BYTES_PER_SAMPLE 40

#define SENS_RATE_LSB_DPS   1600.0
#define SENS_ACC_LSB_MS2    3200.0
#define TEMP_SCALE          0.01

static volatile int running = 1;

static void sigint_handler(int sig)
{
    running = 0;
}

static int write_sysfs(const char *path, const char *val)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror(path);
        return -1;
    }
    int ret = write(fd, val, strlen(val));
    close(fd);
    return (ret < 0) ? -1 : 0;
}

static int find_sch16t(char *dev_name, size_t namelen, char *dev_path, size_t pathlen)
{
    DIR *d = opendir(SYSFS_IIO_PATH);
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "iio:device", 10)) continue;
        char path[256];
        snprintf(path, sizeof(path), "%s/%s/name", SYSFS_IIO_PATH, e->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        char name[32] = {0};
        int n = read(fd, name, sizeof(name)-1);
        close(fd);
        if (n > 0) {
            name[strcspn(name, "\n")] = '\0';
            if (!strcmp(name, "sch16t")) {
                strncpy(dev_name, e->d_name, namelen-1);
                snprintf(dev_path, pathlen, "%s/%s", DEV_IIO_PATH, e->d_name);
                closedir(d);
                return 0;
            }
        }
    }
    closedir(d);
    return -1;
}

static int setup_scan_elements(const char *dev_name)
{
    const char *channels[] = {
        "in_anglvel_x_en", "in_anglvel_y_en", "in_anglvel_z_en",
        "in_accel_x_en", "in_accel_y_en", "in_accel_z_en",
        "in_temp_en", "in_timestamp_en", NULL
    };
    for (int i = 0; channels[i]; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s/scan_elements/%s",
                 SYSFS_IIO_PATH, dev_name, channels[i]);
        write_sysfs(path, "1");
    }
    return 0;
}

static int setup_trigger(const char *dev_name)
{
    char path[512], trigger_name[64];
    int dev_id = 0;
    sscanf(dev_name, "iio:device%d", &dev_id);
    snprintf(trigger_name, sizeof(trigger_name), "sch16t-dev%d", dev_id);
    snprintf(path, sizeof(path), "%s/%s/trigger/current_trigger",
             SYSFS_IIO_PATH, dev_name);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, trigger_name, strlen(trigger_name));
    close(fd);
    printf("Trigger set to: %s\n", trigger_name);
    return 0;
}

static int get_data_available(const char *dev_name)
{
    char path[512], buf[32];
    snprintf(path, sizeof(path), "%s/%s/buffer/data_available",
             SYSFS_IIO_PATH, dev_name);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    int n = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    return atoi(buf);
}

static void parse_sample(const uint8_t *p)
{
    int32_t rate_x = *(int32_t *)(p + 0);
    int32_t rate_y = *(int32_t *)(p + 4);
    int32_t rate_z = *(int32_t *)(p + 8);
    int32_t acc_x  = *(int32_t *)(p + 12);
    int32_t acc_y  = *(int32_t *)(p + 16);
    int32_t acc_z  = *(int32_t *)(p + 20);
    int32_t temp   = *(int32_t *)(p + 24);
    int64_t ts     = *(int64_t *)(p + 32);

    printf("TS=%.6f ms | Gyro: X=%8.3f Y=%8.3f Z=%8.3f dps | "
           "Acc: X=%8.3f Y=%8.3f Z=%8.3f m/s2 | Temp: %6.2f C\n",
           ts / 1000000.0,
           rate_x / SENS_RATE_LSB_DPS,
           rate_y / SENS_RATE_LSB_DPS,
           rate_z / SENS_RATE_LSB_DPS,
           acc_x / SENS_ACC_LSB_MS2,
           acc_y / SENS_ACC_LSB_MS2,
           acc_z / SENS_ACC_LSB_MS2,
           temp * TEMP_SCALE);
}

int main(int argc, char *argv[])
{
    char dev_name[32] = {0}, dev_path[64] = {0};
    int fd;
    char path[512];

    signal(SIGINT, sigint_handler);

    /* 1. 查找设备 */
    if (find_sch16t(dev_name, sizeof(dev_name), dev_path, sizeof(dev_path)) < 0) {
        fprintf(stderr, "sch16t not found\n");
        return 1;
    }
    printf("Found: %s -> %s\n", dev_name, dev_path);

    /* 2. 配置 buffer（此时 PWM 未启动，不会触发中断） */
    snprintf(path, sizeof(path), "%s/%s/buffer/enable", SYSFS_IIO_PATH, dev_name);
    write_sysfs(path, "0");

    snprintf(path, sizeof(path), "%s/%s/buffer/length", SYSFS_IIO_PATH, dev_name);
    write_sysfs(path, "256");
    setup_scan_elements(dev_name);
    setup_trigger(dev_name);

    /* 3. 打开字符设备 */
    fd = open(dev_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror(dev_path);
        return 1;
    }

    /* 4. 启动 PWM（写频率即启动） */
    snprintf(path, sizeof(path), "%s/%s/pwm_frequency", SYSFS_IIO_PATH, dev_name);
    if (write_sysfs(path, "300") < 0) {   /* 启动 300Hz */
        fprintf(stderr, "Failed to start PWM\n");
        close(fd);
        return 1;
    }
    printf("PWM started at 300 Hz\n");

    /* 5. 使能 buffer（现在中断会触发，数据开始流入） */
    snprintf(path, sizeof(path), "%s/%s/buffer/enable", SYSFS_IIO_PATH, dev_name);
    write_sysfs(path, "1");

    printf("Reading data... (Ctrl+C to stop)\n\n");

    while (running) {
        int avail = get_data_available(dev_name);
        if (avail <= 0) {
            usleep(1000);
            continue;
        }

        int to_read = (avail < READ_BUF_SIZE) ? avail : READ_BUF_SIZE;
        to_read = (to_read / BYTES_PER_SAMPLE) * BYTES_PER_SAMPLE;
        if (to_read == 0) {
            usleep(1000);
            continue;
        }

        uint8_t *buf = malloc(to_read);
        if (!buf) continue;

        int n = read(fd, buf, to_read);
        if (n > 0) {
            int nsamples = n / BYTES_PER_SAMPLE;
            for (int i = 0; i < nsamples; i++) {
                parse_sample(buf + i * BYTES_PER_SAMPLE);
            }
        }
        free(buf);
    }

    printf("\nStopping...\n");

    /* 6. 停止顺序：先停 buffer，再停 PWM */
    snprintf(path, sizeof(path), "%s/%s/buffer/enable", SYSFS_IIO_PATH, dev_name);
    write_sysfs(path, "0");

    snprintf(path, sizeof(path), "%s/%s/pwm_frequency", SYSFS_IIO_PATH, dev_name);
    write_sysfs(path, "0");   /* 写 0 停止 PWM */

    close(fd);
    printf("Stopped.\n");
    return 0;
}