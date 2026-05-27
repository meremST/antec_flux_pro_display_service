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

typedef struct {
    char sensor[128];
    char name[128];
} sensor_config;

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
// CONF PARSER
// =========================================================
static int load_sensor_config(const char *section, sensor_config *cfg)
{
    FILE *f;
    char line[256];
    char current[64] = {0};

    memset(cfg, 0, sizeof(*cfg));

    f = fopen(CONFIG_FILE, "r");
    if (!f) {
        syslog(LOG_ERR, "Cannot open config file: %s", CONFIG_FILE);
        return 0;
    }

    while (fgets(line, sizeof(line), f)) {

        trim(line);

        // Ignore comments & empty lines
        if (line[0] == '#' || line[0] == ';' || line[0] == '\0')
            continue;

        // Section [cpu]
        if (line[0] == '[') {
            char *end = strchr(line, ']');

            if (!end)
                continue;

            *end = '\0';

            snprintf(current, sizeof(current), "%.63s", line + 1);
            current[sizeof(current) - 1] = '\0';
            continue;
        }

        // Bad section
        if (strcmp(current, section) != 0)
            continue;

        char *eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = '\0';

        char *key = line;
        char *value = eq + 1;

        trim(key);
        trim(value);

        if (strcmp(key, "sensor") == 0) {
            snprintf(cfg->sensor, sizeof(cfg->sensor), "%s", value);
        }
        else if (strcmp(key, "name") == 0) {
            snprintf(cfg->name, sizeof(cfg->name), "%s", value);
        }
    }

    fclose(f);

    return (cfg->sensor[0] && cfg->name[0]);
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

/*
 * Reads the sensor name from "<base>/name" and compares it to `sensor`.
 * Returns 1 if the hwmon directory matches the requested sensor,
 * or if no sensor filter is set (NULL or empty). Returns 0 otherwise.
 */
static int hwmon_matches_sensor(const char *base, const char *sensor)
{
    char namefile[PATH_MAX];
    char hwname[128];
    FILE *nf;

    if (snprintf(namefile, sizeof(namefile), "%s/name", base) >= (int)sizeof(namefile))
        return 0;

    nf = fopen(namefile, "r");
    if (!nf) return 0;

    if (!fgets(hwname, sizeof(hwname), nf)) hwname[0] = '\0';
    fclose(nf);

    hwname[strcspn(hwname, "\n")] = 0;
    trim(hwname);

    /* No sensor filter: accept all hwmon entries */
    if (!sensor || !sensor[0])
        return 1;

    return strcmp(hwname, sensor) == 0;
}

/*
 * Scans "*_label" files inside `base` looking for one whose content matches
 * `label`. If found, writes the corresponding "_input" path into `out`
 * (same prefix, suffix replaced) and returns 1. Returns 0 otherwise.
 */
static int find_input_for_label(const char *base, const char *label, char *out)
{
    struct dirent *de;
    char label_path[PATH_MAX];
    char val[128];
    FILE *f;
    DIR *d;
    char *p;

    d = opendir(base);
    if (!d) return 0;

    while ((de = readdir(d))) {

        /* Only process files whose name contains "_label" */
        if (!strstr(de->d_name, "_label"))
            continue;

        if (snprintf(label_path, sizeof(label_path), "%s/%s", base,
                     de->d_name) >= (int)sizeof(label_path))
            continue;

        f = fopen(label_path, "r");
        if (!f) continue;

        if (!fgets(val, sizeof(val), f)) val[0] = '\0';
        fclose(f);

        val[strcspn(val, "\n")] = 0;
        trim(val);

        if (strcmp(val, label) != 0)
            continue;

        /* Label matched: build the "_input" path from the "_label" path */
        snprintf(out, PATH_MAX, "%s", label_path);
        p = strstr(out, "_label");
        if (p) strcpy(p, "_input");

        closedir(d);
        return 1;
    }

    closedir(d);
    return 0;
}

/*
 * Searches /sys/class/hwmon for the "_input" temperature file matching
 * both `sensor` (hwmon chip name, NULL to match any) and `label`.
 * Writes the absolute path into `out` (PATH_MAX bytes) and returns 1 if
 * found, 0 otherwise.
 */
static int find_temp_file(const char *sensor,
                          const char *label,
                          char *out)
{
    struct dirent *de;
    char base[PATH_MAX];
    DIR *d;

    d = opendir("/sys/class/hwmon");
    if (!d) return 0;

    while ((de = readdir(d))) {

        if (de->d_name[0] == '.')
            continue;

        snprintf(base, sizeof(base), "/sys/class/hwmon/%s", de->d_name);

        if (!hwmon_matches_sensor(base, sensor))
            continue;

        if (find_input_for_label(base, label, out)) {
            closedir(d);
            return 1;
        }
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

    if (!fgets(buf, sizeof(buf), f)) buf[0] = '\0';
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
    sensor_config cpu_cfg, gpu_cfg;
    usb_ctx usb;
    float cpu, gpu;
    int len, ret;

    openlog("sensor-daemon", LOG_PID, LOG_DAEMON);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    syslog(LOG_INFO, "Daemon starting...");

    

    if (!load_sensor_config("cpu", &cpu_cfg)) {
        syslog(LOG_ERR, "Failed to load CPU config");
        return 1;
    }

    if (!load_sensor_config("gpu", &gpu_cfg)) {
        syslog(LOG_ERR, "Failed to load GPU config");
        return 1;
    }

    find_temp_file(cpu_cfg.sensor, cpu_cfg.name, cpu_path);

    find_temp_file(gpu_cfg.sensor,gpu_cfg.name, gpu_path);

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
