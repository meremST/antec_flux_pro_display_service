#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <syslog.h>
#include <signal.h>

#include <libusb-1.0/libusb.h>

#define CONFIG_FILE "/etc/antec/sensors.conf"
#define VENDOR_ID  0x2022
#define PRODUCT_ID 0x0522

// =========================================================
// GLOBAL STOP FLAG (systemd stop)
// =========================================================

static volatile int running = 1;

static void handle_signal(int sig)
{
    syslog(LOG_INFO, "Received signal %d, stopping daemon...", sig);
    running = 0;
}

// =========================================================
// USB CONTEXT
// =========================================================

typedef struct {
    libusb_device_handle *dev;
    unsigned char ep;
    int ep_type;
    libusb_context *ctx;
} usb_ctx;

// =========================================================
// TRIM
// =========================================================

static void trim(char *s)
{
    char *start = s;

    if (!s)
        return;

    while (*start == ' ' || *start == '\t')
        start++;

    if (start != s)
        memmove(s, start, strlen(start) + 1);

    char *end = s + strlen(s);
    while (end > s &&
          (*(end - 1) == ' ' ||
           *(end - 1) == '\t' ||
           *(end - 1) == '\n'))
        *(--end) = '\0';
}

// =========================================================
// USB INIT
// =========================================================

static usb_ctx init_usb(void)
{
    struct libusb_config_descriptor *cfg;
    usb_ctx ctx = {0};
    int ret;

    if (libusb_init(&ctx.ctx) != 0) {
        syslog(LOG_ERR, "libusb_init failed");
        return ctx;
    }

    ctx.dev = libusb_open_device_with_vid_pid(ctx.ctx, VENDOR_ID, PRODUCT_ID);

    if (!ctx.dev) {
        syslog(LOG_ERR, "USB device not found");
        return ctx;
    }

    ret = libusb_set_configuration(ctx.dev, 1);
    if (ret != 0 && ret != LIBUSB_ERROR_BUSY) {
        syslog(LOG_ERR, "libusb_set_configuration failed: %s",
               libusb_error_name(ret));
        return ctx;
    }

    ret = libusb_claim_interface(ctx.dev, 0);
    if (ret != 0) {
        syslog(LOG_ERR, "libusb_claim_interface failed: %s",
                libusb_error_name(ret));

        libusb_close(ctx.dev);
        ctx.dev = NULL;
        return ctx;
    }

    if (libusb_get_active_config_descriptor(
            libusb_get_device(ctx.dev), &cfg) != 0) {
        syslog(LOG_ERR, "config descriptor failed");
        return ctx;
    }

    for (int i = 0; i < cfg->interface[0].altsetting[0].bNumEndpoints; i++) {

        const struct libusb_endpoint_descriptor *ep =
            &cfg->interface[0].altsetting[0].endpoint[i];

        if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK)
            == LIBUSB_ENDPOINT_OUT)
        {
            ctx.ep = ep->bEndpointAddress;
            ctx.ep_type = ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
        }
    }

    libusb_free_config_descriptor(cfg);

    syslog(LOG_INFO, "USB initialized EP=0x%02x", ctx.ep);

    return ctx;
}

// =========================================================
// USB WRITE
// =========================================================

static int usb_write(usb_ctx *ctx,
                      unsigned char *data,
                      int len)
{
    int transferred = 0;
    int ret;

    ret = libusb_bulk_transfer(ctx->dev,
                               ctx->ep,
                               data,
                               len,
                               &transferred,
                               1000);

    if (ret == 0)
        return ret;

    ret = libusb_interrupt_transfer(ctx->dev,
                                    ctx->ep,
                                    data,
                                    len,
                                    &transferred,
                                    1000);

    if (ret == 0)
        return ret;

    syslog(LOG_ERR, "USB transfer failed: %s", libusb_error_name(ret));
    return -1;
}

// =========================================================
// HWMON SEARCH
// =========================================================

static int find_temp_file(const char *sensor,
                          const char *label,
                          char *out)
{
    struct dirent *de;
    struct dirent *de2;
    char base[PATH_MAX];
    char namefile[PATH_MAX];
    char label_path[PATH_MAX];
    char hwname[128];
    char val[128];
    FILE *nf, *f;
    DIR *d2, *d;

    d = opendir("/sys/class/hwmon");
    if (!d) return 0;

    while ((de = readdir(d))) {

        if (de->d_name[0] == '.')
            continue;

        snprintf(base, sizeof(base),
                 "/sys/class/hwmon/%s", de->d_name);

        if (snprintf(namefile, sizeof(namefile),
                     "%s/name", base) >= (int)sizeof(namefile)) {
                continue;
        }

        nf = fopen(namefile, "r");
        if (!nf) continue;

        fgets(hwname, sizeof(hwname), nf);
        fclose(nf);

        hwname[strcspn(hwname, "\n")] = 0;
        trim(hwname);

        if (sensor && sensor[0] && strcmp(hwname, sensor) != 0)
            continue;

        d2 = opendir(base);
        if (!d2) continue;

        while ((de2 = readdir(d2))) {

            if (!strstr(de2->d_name, "_label"))
                continue;

            if (snprintf(label_path, sizeof(label_path), "%s/%s", base,
                         de2->d_name) >= (int)sizeof(label_path)) {
                continue;
            }

            f = fopen(label_path, "r");
            if (!f) continue;
       
            fgets(val, sizeof(val), f);
            fclose(f);

            val[strcspn(val, "\n")] = 0;
            trim(val);

            if (strcmp(val, label) == 0) {
                char *p;

                snprintf(out, PATH_MAX, "%s", label_path);

                p = strstr(out, "_label");
                if (p) strcpy(p, "_input");

                closedir(d2);
                closedir(d);
                return 1;
            }
        }

        closedir(d2);
    }

    closedir(d);
    return 0;
}

// =========================================================
// TEMP READ
// =========================================================

static float read_temp(const char *path)
{
    char buf[64];

    FILE *f = fopen(path, "r");
    if (!f)
        return 0.0f;

    fgets(buf, sizeof(buf), f);
    fclose(f);

    return atof(buf) / 1000.0f;
}

// =========================================================
// ENCODING
// =========================================================

static void encode_temperature(float t, char *out)
{
    int a = (int)(t / 10);
    int b = (int)t % 10;
    int c = (int)(t * 10) % 10;

    sprintf(out, "%02x%02x%02x", a, b, c);
}

// =========================================================
// PAYLOAD
// =========================================================

static int generate_payload(float cpu,
                            float gpu,
                            unsigned char *out)
{
    char c1[8], c2[8];
    unsigned char raw[6];
    int sum = 7;

    encode_temperature(cpu, c1);
    encode_temperature(gpu, c2);

    sscanf(c1, "%2hhx%2hhx%2hhx", &raw[0], &raw[1], &raw[2]);
    sscanf(c2, "%2hhx%2hhx%2hhx", &raw[3], &raw[4], &raw[5]);

    for (int i = 0; i < 6; i++)
        sum += raw[i];

    out[0] = 0x55;
    out[1] = 0xAA;
    out[2] = 0x01;
    out[3] = 0x01;
    out[4] = 0x06;

    memcpy(&out[5], raw, 6);
    out[11] = sum & 0xFF;

    return 12;
}

// =========================================================
// MAIN DAEMON LOOP
// =========================================================

int main()
{
    unsigned char payload[64];
    char cpu_path[PATH_MAX] = {0};
    char gpu_path[PATH_MAX] = {0};
    usb_ctx usb;
    float cpu, gpu;
    int len, ret;

    openlog("sensor-daemon", LOG_PID, LOG_DAEMON);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    syslog(LOG_INFO, "Daemon starting...");

    find_temp_file("k10temp", "Tctl", cpu_path);
    find_temp_file("amdgpu", "junction", gpu_path);

    usb = init_usb();
    if (!usb.dev) {
        syslog(LOG_ERR, "USB init failed");
        return 1;
    }

    while (running) {
        cpu = read_temp(cpu_path);
        gpu = read_temp(gpu_path);

        len = generate_payload(cpu, gpu, payload);

        ret = usb_write(&usb, payload, len);
        if(ret)
           usleep(1500000); // Extra wait in case of failure

        usleep(500000);
    }

    syslog(LOG_INFO, "Stopping daemon...");

    if (usb.dev) {
        libusb_release_interface(usb.dev, 0);
        libusb_close(usb.dev);
    }

    if (usb.ctx)
        libusb_exit(usb.ctx);

    closelog();

    return 0;
}
