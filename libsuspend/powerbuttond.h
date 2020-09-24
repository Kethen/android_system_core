#ifndef _LIBSUSPEND_POWERBUTTOND_H_
#define _LIBSUSPEND_POWERBUTTOND_H_
void init_android_power_button(void);
void send_key_power_ext(bool longpress);
void send_key_wakeup_ext(void);
void send_key_left_meta_ext(void);
#endif // _LIBSUSPEND_POWERBUTTOND_H_
