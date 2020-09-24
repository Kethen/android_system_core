#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <linux/uinput.h>
#include <dirent.h>
#include <cutils/properties.h>
#define LOG_TAG "libsuspend"
#include <cutils/log.h>

#define MAX_POWERBTNS 3

static int uinput_fd = -1;

static int openfds(struct pollfd pfds[])
{
    int cnt = 0;
    const char *dirname = "/dev/input";
    struct dirent *de;
    DIR *dir;

    if ((dir = opendir(dirname))) {
        while ((cnt < MAX_POWERBTNS) && (de = readdir(dir))) {
            int fd;
            char name[PATH_MAX];
            if (de->d_name[0] != 'e') /* eventX */
                continue;
            snprintf(name, PATH_MAX, "%s/%s", dirname, de->d_name);
            fd = open(name, O_RDWR | O_NONBLOCK);
            if (fd < 0) {
                ALOGE("could not open %s, %s", name, strerror(errno));
                continue;
            }
            name[sizeof(name) - 1] = '\0';
            if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), &name) < 1) {
                ALOGE("could not get device name for %s, %s", name, strerror(errno));
                name[0] = '\0';
            }
            // TODO: parse /etc/excluded-input-devices.xml
            if (strcmp(name, "Power Button")) {
                close(fd);
                continue;
            }

            ALOGI("open %s(%s) ok fd=%d", de->d_name, name, fd);
            pfds[cnt].events = POLLIN;
            pfds[cnt++].fd = fd;
        }
        closedir(dir);
    }

    return cnt;
}

static void emit_key(int ufd, int key_code, int val)
{
    struct input_event iev;
    iev.type = EV_KEY;
    iev.code = key_code;
    iev.value = val;
    iev.time.tv_sec = 0;
    iev.time.tv_usec = 0;
    write(ufd, &iev, sizeof(iev));
    iev.type = EV_SYN;
    iev.code = SYN_REPORT;
    iev.value = 0;
    write(ufd, &iev, sizeof(iev));
    ALOGD("send key %d (%d) on fd %d", key_code, val, ufd);
}

static void send_key_wakeup(int ufd)
{
    emit_key(ufd, KEY_WAKEUP, 1);
    emit_key(ufd, KEY_WAKEUP, 0);
}

void send_key_wakeup_ext(){
	send_key_wakeup(uinput_fd);
}

static void send_key_power(int ufd, bool longpress)
{
    emit_key(ufd, KEY_POWER, 1);
    if (longpress) sleep(2);
    emit_key(ufd, KEY_POWER, 0);
}

void send_key_power_ext(bool longpress){
	send_key_power(uinput_fd, longpress);
}

static void send_key_left_meta(int ufd){
	emit_key(ufd, KEY_LEFTMETA, 1);
	emit_key(ufd, KEY_LEFTMETA, 0);
}

void send_key_left_meta_ext(){
	send_key_left_meta(uinput_fd);
}

static void *powerbtnd_thread_func(void *arg __attribute__((unused)))
{
    int cnt, timeout, pollres;
    bool longpress = true;
    bool doubleclick = property_get_bool("poweroff.doubleclick", 0);
    struct pollfd pfds[MAX_POWERBTNS];

    timeout = -1;
    cnt = openfds(pfds);

    while (cnt > 0 && (pollres = poll(pfds, cnt, timeout)) >= 0) {
        ALOGV("pollres=%d %d\n", pollres, timeout);
        if (pollres == 0) {
            ALOGI("timeout, send one power key");
            send_key_power(uinput_fd, 0);
            timeout = -1;
            longpress = true;
            continue;
        }
        for (int i = 0; i < cnt; ++i) {
            if (pfds[i].revents & POLLIN) {
                struct input_event iev;
                size_t res = read(pfds[i].fd, &iev, sizeof(iev));
                if (res < sizeof(iev)) {
                    ALOGW("insufficient input data(%zd)? fd=%d", res, pfds[i].fd);
                    continue;
                }
                ALOGD("type=%d code=%d value=%d from fd=%d", iev.type, iev.code, iev.value, pfds[i].fd);
                if (iev.type == EV_KEY && iev.code == KEY_POWER && !iev.value) {
                    if (!doubleclick || timeout > 0) {
                        send_key_power(uinput_fd, longpress);
                        timeout = -1;
                    } else {
                        timeout = 1000; // one second
                    }
                } else if (iev.type == EV_SYN && iev.code == SYN_REPORT && iev.value) {
                    ALOGI("got a resuming event");
                    longpress = false;
                    timeout = 1000; // one second
                }
            }
        }
    }

    ALOGE_IF(cnt, "poll error: %s", strerror(errno));
    return NULL;
}


void init_android_power_button()
{
    static pthread_t powerbtnd_thread;
    struct uinput_user_dev ud;

    if (uinput_fd >= 0) return;

    uinput_fd = open("/dev/uinput", O_WRONLY | O_NDELAY);
    if (uinput_fd < 0) {
        ALOGE("could not open uinput device: %s", strerror(errno));
        return;
    }

    memset(&ud, 0, sizeof(ud));
    strcpy(ud.name, "Android Power Button");
    write(uinput_fd, &ud, sizeof(ud));
    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_POWER);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_WAKEUP);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_LEFTMETA);
    ioctl(uinput_fd, UI_DEV_CREATE, 0);

    pthread_create(&powerbtnd_thread, NULL, powerbtnd_thread_func, NULL);
    pthread_setname_np(powerbtnd_thread, "powerbtnd");
}
