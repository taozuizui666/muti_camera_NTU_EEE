#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DEV_PATH "/sys/bus/iio/devices/iio:device1"

// 读取float
int read_float(const char *path, float *val)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    fscanf(f, "%f", val);
    fclose(f);
    return 0;
}

// 读取int
int read_int(const char *path, int *val)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    fscanf(f, "%d", val);
    fclose(f);
    return 0;
}

int main()
{
    char path[256];

    int ax_raw, ay_raw, az_raw;
    int gx_raw, gy_raw, gz_raw;
    int temp_raw;

    float ax_scale, ay_scale, az_scale;
    float gx_scale, gy_scale, gz_scale;
    float temp_scale;

    // 先读取 scale（一般不会变）
    sprintf(path, "%s/in_accel_x_scale", DEV_PATH); read_float(path, &ax_scale);
    sprintf(path, "%s/in_accel_y_scale", DEV_PATH); read_float(path, &ay_scale);
    sprintf(path, "%s/in_accel_z_scale", DEV_PATH); read_float(path, &az_scale);

    sprintf(path, "%s/in_anglvel_x_scale", DEV_PATH); read_float(path, &gx_scale);
    sprintf(path, "%s/in_anglvel_y_scale", DEV_PATH); read_float(path, &gy_scale);
    sprintf(path, "%s/in_anglvel_z_scale", DEV_PATH); read_float(path, &gz_scale);

    sprintf(path, "%s/in_temp_scale", DEV_PATH); read_float(path, &temp_scale);

    printf("IMU start...\n");

    while (1) {
        // accel
        sprintf(path, "%s/in_accel_x_raw", DEV_PATH); read_int(path, &ax_raw);
        sprintf(path, "%s/in_accel_y_raw", DEV_PATH); read_int(path, &ay_raw);
        sprintf(path, "%s/in_accel_z_raw", DEV_PATH); read_int(path, &az_raw);

        // gyro
        sprintf(path, "%s/in_anglvel_x_raw", DEV_PATH); read_int(path, &gx_raw);
        sprintf(path, "%s/in_anglvel_y_raw", DEV_PATH); read_int(path, &gy_raw);
        sprintf(path, "%s/in_anglvel_z_raw", DEV_PATH); read_int(path, &gz_raw);

        // temp
        sprintf(path, "%s/in_temp_raw", DEV_PATH); read_int(path, &temp_raw);

        float ax = ax_raw * ax_scale;
        float ay = ay_raw * ay_scale;
        float az = az_raw * az_scale;

        float gx = gx_raw * gx_scale;
        float gy = gy_raw * gy_scale;
        float gz = gz_raw * gz_scale;

        float temp = temp_raw * temp_scale;

        printf("ACC[g]:   %8.4f %8.4f %8.4f\n", ax, ay, az);
        printf("GYRO[dps]:%8.4f %8.4f %8.4f\n", gx, gy, gz);
        printf("TEMP[C]:  %8.2f\n", temp);
        printf("-------------------------------------\n");

        usleep(100000); // 100ms
    }

    return 0;
}