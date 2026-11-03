/* SPDX-License-Identifier: Apache-2.0 */
/* Host-only tests: real parsers, keyboard hit-testing and rendering; no board I/O. */
#define main wifi_setup_entry
#include "../src/wifi_setup.c"
#undef main
#include <assert.h>

int cam_display_open(struct cam_display **d,const char *p,int r)
{(void)d;(void)p;(void)r;return -1;}
cam_canvas *cam_display_canvas(struct cam_display *d){(void)d;return NULL;}
int cam_display_present(struct cam_display *d){(void)d;return 0;}
void cam_display_close(struct cam_display *d){(void)d;}
int cam_touch_open(struct cam_touch **t,const char *p,int r)
{(void)t;(void)p;(void)r;return -1;}
int cam_touch_poll_event(struct cam_touch *t,int *x,int *y)
{(void)t;(void)x;(void)y;return 0;}
void cam_touch_close(struct cam_touch *t){(void)t;}
int cam_touch_poll_gesture(struct cam_touch *t,int *x,int *y,int *dy)
{(void)t;(void)x;(void)y;(void)dy;return 0;}

int main(int argc,char **argv)
{
    assert(network_id("wpa_cli v2.10\nCopyright (c) 2004-2022\n> add_network\n7\n> quit\n")==7);
    assert(network_id("Copyright (c) 2004-2022\nFAIL\n")==-1);
    assert(network_id("> 0\r\n")==0);
    assert(network_id("4096\n")==-1);
    assert(!failed("> set_network 0 psk \"FAILword\"\nOK\n"));
    assert(failed("> scan\nFAIL\r\n"));
    assert(!response_line("wpa_state=DISCONNECTED\n","wpa_state=COMPLETED"));
    assert(response_line("wpa_state=COMPLETED\r\n","wpa_state=COMPLETED"));

    struct wifi_app app={0};
    assert(touch_scan(&app,210,12)==2); /* Return without a Wi-Fi connection. */
    char results[]="wpa_cli v2.10\n> scan_results\n"
        "bssid / frequency / signal level / flags / ssid\n"
        "00:11:22:33:44:01\t2412\t-70\t[WPA2-PSK-CCMP][ESS]\tHome\\x20WiFi\n"
        "00:11:22:33:44:02\t2437\t-30\t[ESS]\tCafe\n"
        "00:11:22:33:44:03\t2462\t-40\t[WPA2-EAP-CCMP][ESS]\tEnterprise\n"
        "00:11:22:33:44:04\t2462\t-40\t[WEP][ESS]\tLegacy\n"
        "00:11:22:33:44:05\t2462\t-40\t[WPA2-SAE-CCMP][ESS]\tWPA3only\n"
        "00:11:22:33:44:06\t2462\t-40\t[OWE][ESS]\tEnhancedOpen\n";
    parse_results(&app,results);
    assert(app.ap_count==2&&!strcmp(app.aps[0].ssid,"Cafe"));
    assert(!strcmp(app.aps[1].ssid,"Home WiFi")&&app.aps[1].secured);

    app.page=PAGE_PASSWORD;app.aps[0].secured=1;
    touch_password(&app,16,90); /* q */
    touch_password(&app,20,145); /* SHIFT */
    touch_password(&app,48,90); /* W */
    touch_password(&app,20,174); /* digits */
    touch_password(&app,16,90); /* 1 */
    touch_password(&app,304,90); /* 0 */
    touch_password(&app,20,174); /* symbols */
    touch_password(&app,304,145); /* quote */
    assert(!strcmp(app.password,"qW10\""));
    touch_password(&app,290,174); /* delete */
    touch_password(&app,100,174); /* space */
    assert(!strcmp(app.password,"qW10 "));
    /* Every visible digit must match the touch target, from left to right. */
    app.password_length=0;app.password[0]=0;app.keyboard_mode=1;
    for(int i=0;i<10;++i)touch_password(&app,i*32+16,90);
    assert(!strcmp(app.password,"1234567890"));
    touch_password(&app,16,118);touch_password(&app,304,146);
    assert(!strcmp(app.password,"1234567890-="));
    for(int i=0;i<100;++i)add_char(&app,'x');
    assert(app.password_length==WIFI_PASSWORD_MAX);
    touch_password(&app,40,210);assert(app.page==PAGE_SCAN&&!app.password_length);

    app.ap_count=12;app.have_ip=1;app.page_offset=0;
    assert(touch_scan(&app,260,215)==1&&app.page_offset==0);
    touch_scan(&app,80,215);assert(app.page_offset==0); /* No old DOWN target. */
    swipe_scan(&app,100,-66);assert(app.page_offset==3&&app.page==PAGE_SCAN);
    swipe_scan(&app,100,-160);assert(app.page_offset==5); /* Clamp at last full screen. */
    swipe_scan(&app,100,44);assert(app.page_offset==3);
    swipe_scan(&app,20,-66);assert(app.page_offset==3); /* Header is not scrollable. */
    swipe_scan(&app,100,200);assert(app.page_offset==0);

    char out[64];char *echo[]={"/bin/echo","mock-result",NULL};
    assert(cam_run_capture(echo,NULL,out,sizeof(out),2000)==0&&!strcmp(out,"mock-result\n"));
    char *slow[]={"/bin/sleep","2",NULL};
    assert(cam_run_capture(slow,NULL,out,sizeof(out),50)==-1);

    uint32_t pixels[320*240];cam_canvas canvas={pixels,320,240,320,sizeof(pixels)};
    app.canvas=&canvas;app.selected=0;app.page=PAGE_PASSWORD;
    snprintf(app.aps[0].ssid,sizeof(app.aps[0].ssid),"Home WiFi");
    snprintf(app.message,sizeof(app.message),"Enter the network password.");
    draw_password(&app);
    if(argc>1)assert(cam_save_png(argv[1],&canvas)==0);
    puts("Wi-Fi parser, keyboard, pagination, command timeout and rendering: PASS");
    return 0;
}
