/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_ai_lcd.h"
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define WIFI_MAX_APS 48
#define WIFI_SSID_MAX 32
#define WIFI_PASSWORD_MAX 63
#define WIFI_SCREEN_W 320
#define WIFI_SCREEN_H 240
#define WIFI_MAX_OUTPUT 8191

enum page { PAGE_SCAN, PAGE_PASSWORD };
struct access_point { char ssid[WIFI_SSID_MAX + 1], bssid[18], flags[96]; int signal, secured, current; };
struct wifi_app {
    struct cam_display *display; struct cam_touch *touch; cam_canvas *canvas;
    struct access_point aps[WIFI_MAX_APS]; size_t ap_count, selected, page_offset;
    enum page page; char password[WIFI_PASSWORD_MAX + 1]; size_t password_length;
    int keyboard_mode, shifted, have_ip; char current_ssid[WIFI_SSID_MAX + 1], message[96];
    const char *wpa_cli, *control_dir;
};

static const uint32_t C_BG=0x000b1823U, C_PANEL=0x00162936U, C_BUTTON=0x00233d4dU,
    C_ACTIVE=0x000c6873U, C_TEXT=0x00e5f3f5U, C_MUTED=0x0089aab2U, C_OK=0x0049d69aU, C_WARN=0x00ffbd69U;

static void sleep_ms(unsigned int ms)
{
    struct timespec d={(time_t)(ms/1000U),(long)(ms%1000U)*1000000L};
    while(nanosleep(&d,&d)!=0 && errno==EINTR) { }
}

static int write_all(int fd,const char *p,size_t n)
{
    while(n){ssize_t k=write(fd,p,n);if(k<0&&errno==EINTR)continue;if(k<=0)return -1;p+=k;n-=(size_t)k;}return 0;
}

/* Executables receive fixed argv; credentials are sent to wpa_cli on stdin, never through a shell. */
int cam_run_capture(char *const argv[],const char *input,char *output,size_t cap,int timeout_ms)
{
    int pi[2]={-1,-1},po[2]={-1,-1},status=-1,timed_out=0;pid_t pid=-1;size_t used=0;
    struct timespec start,now;
    if(!argv||!argv[0]||!output||cap<2||pipe(pi)||pipe(po))goto done;
    pid=fork();if(pid<0)goto done;
    if(pid==0){dup2(pi[0],0);dup2(po[1],1);dup2(po[1],2);close(pi[0]);close(pi[1]);close(po[0]);close(po[1]);execvp(argv[0],argv);_exit(127);}
    close(pi[0]);pi[0]=-1;close(po[1]);po[1]=-1;
    if(input&&write_all(pi[1],input,strlen(input))){kill(pid,SIGKILL);goto reap;}
    close(pi[1]);pi[1]=-1;output[0]='\0';clock_gettime(CLOCK_MONOTONIC,&start);
    for(;;){
        struct pollfd f={.fd=po[0],.events=POLLIN|POLLHUP};clock_gettime(CLOCK_MONOTONIC,&now);
        long elapsed=(long)(now.tv_sec-start.tv_sec)*1000L+(long)(now.tv_nsec-start.tv_nsec)/1000000L;
        int remain=timeout_ms-(int)elapsed;if(remain<=0){timed_out=1;kill(pid,SIGKILL);break;}
        int ready=poll(&f,1,remain);if(ready<0&&errno==EINTR)continue;if(ready<0){kill(pid,SIGKILL);break;}if(!ready){timed_out=1;kill(pid,SIGKILL);break;}
        if(f.revents&(POLLIN|POLLHUP)){char buf[512];ssize_t n=read(po[0],buf,sizeof(buf));if(n==0)break;if(n<0&&errno==EINTR)continue;if(n<0){kill(pid,SIGKILL);break;}
            size_t copy=(size_t)n;if(copy>cap-1-used)copy=cap-1-used;if(copy){memcpy(output+used,buf,copy);used+=copy;output[used]='\0';}}
    }
reap:
    if(po[0]>=0){close(po[0]);po[0]=-1;}while(waitpid(pid,&status,0)<0&&errno==EINTR){}
    if(timed_out||!WIFEXITED(status))status=-1;else status=WEXITSTATUS(status);
done:
    if(pi[0]>=0)close(pi[0]);if(pi[1]>=0)close(pi[1]);if(po[0]>=0)close(po[0]);if(po[1]>=0)close(po[1]);
    if(output&&cap)output[used]='\0';return status;
}

static int wpa_command(struct wifi_app *a,const char *command,char *out,size_t cap)
{
    char script[512];int n=snprintf(script,sizeof(script),"%s\nquit\n",command);if(n<0||(size_t)n>=sizeof(script))return -1;
    char *argv[]={(char *)a->wpa_cli,"-i","wlan0","-p",(char *)a->control_dir,NULL};return cam_run_capture(argv,script,out,cap,5000);
}
static int response_line(const char *text,const char *expected)
{
    if(!text)return 0;
    size_t size=strlen(expected);
    for(const char *line=text;*line;){
        const char *end=strchr(line,'\n');if(!end)end=line+strlen(line);
        while(line<end&&(*line=='>'||*line==' '||*line=='\r'))++line;
        const char *last=end;while(last>line&&(last[-1]==' '||last[-1]=='\r'))--last;
        if((size_t)(last-line)==size&&!memcmp(line,expected,size))return 1;
        line=*end?end+1:end;
    }
    return 0;
}
static int failed(const char *s){return !s||response_line(s,"FAIL")||response_line(s,"UNKNOWN COMMAND");}

static int unhex(char a,char b)
{
    int x=a>='0'&&a<='9'?a-'0':a>='a'&&a<='f'?a-'a'+10:a>='A'&&a<='F'?a-'A'+10:-1;
    int y=b>='0'&&b<='9'?b-'0':b>='a'&&b<='f'?b-'a'+10:b>='A'&&b<='F'?b-'A'+10:-1;return x<0||y<0?-1:(x<<4)|y;
}
static void decode_escaped(const char *src,char *dst,size_t cap)
{
    size_t n=0;for(size_t i=0;src[i]&&n+1<cap;++i){unsigned char c=(unsigned char)src[i];
        if(src[i]=='\\'&&src[i+1]=='x'&&src[i+2]&&src[i+3]){int v=unhex(src[i+2],src[i+3]);if(v>0){c=(unsigned char)v;i+=3;}}
        else if(src[i]=='\\'&&src[i+1]&&src[i+1]!='\t')c=(unsigned char)src[++i];
        if(c)dst[n++]=(char)c;}dst[n]='\0';
}

static void parse_results(struct wifi_app *a,char *text)
{
    a->ap_count=0;char *save=NULL;
    for(char *line=strtok_r(text,"\n",&save);line&&a->ap_count<WIFI_MAX_APS;line=strtok_r(NULL,"\n",&save)){
        while(*line==' '||*line=='\r'||*line=='>')++line;char *col[5]={line};int ok=1;
        for(int i=1;i<5;++i){char *tab=strchr(col[i-1],'\t');if(!tab){ok=0;break;}*tab='\0';col[i]=tab+1;}
        if(!ok||strchr(col[0],':')==NULL||strstr(col[0],"bssid"))continue;
        struct access_point p={0};snprintf(p.bssid,sizeof(p.bssid),"%s",col[0]);p.signal=atoi(col[2]);snprintf(p.flags,sizeof(p.flags),"%s",col[3]);
        decode_escaped(col[4],p.ssid,sizeof(p.ssid));if(!p.ssid[0])continue;
        p.secured=strstr(p.flags,"WPA")||strstr(p.flags,"RSN")||strstr(p.flags,"SAE")||strstr(p.flags,"WEP");
        if(strstr(p.flags,"EAP")||strstr(p.flags,"IEEE8021X")||strstr(p.flags,"WEP")||strstr(p.flags,"OWE")||
            (strstr(p.flags,"SAE")&&!strstr(p.flags,"PSK")))continue;
        p.current=a->current_ssid[0]&&strcmp(a->current_ssid,p.ssid)==0;a->aps[a->ap_count++]=p;
    }
    for(size_t i=0;i<a->ap_count;++i)for(size_t j=i+1;j<a->ap_count;++j)if(a->aps[j].signal>a->aps[i].signal){struct access_point t=a->aps[i];a->aps[i]=a->aps[j];a->aps[j]=t;}
}

static void read_connection(struct wifi_app *a)
{
    char out[2048]={0};a->have_ip=0;a->current_ssid[0]=0;
    if(wpa_command(a,"status",out,sizeof(out))!=0||!response_line(out,"wpa_state=COMPLETED"))return;
    {char *save=NULL;for(char *line=strtok_r(out,"\n",&save);line;line=strtok_r(NULL,"\n",&save)){
        while(*line=='>'||*line==' ')++line;if(!strncmp(line,"ssid=",5))decode_escaped(line+5,a->current_ssid,sizeof(a->current_ssid));}}
    char *argv[]={"ifconfig","wlan0",NULL};char text[1024]={0};if(cam_run_capture(argv,NULL,text,sizeof(text),3000)!=0)return;
    char *ip=strstr(text,"inet addr:");if(ip)ip+=10;else{ip=strstr(text,"inet ");if(ip)ip+=5;}if(!ip)return;
    while(*ip==' ')++ip;char address[INET_ADDRSTRLEN]={0};size_t n=0;while(ip[n]&&ip[n]!=' '&&ip[n]!='\n'&&ip[n]!='\r'&&ip[n]!='/'&&n+1<sizeof(address)){address[n]=ip[n];++n;}
    struct in_addr parsed;if(inet_pton(AF_INET,address,&parsed)==1&&ntohl(parsed.s_addr)!=INADDR_ANY&&(ntohl(parsed.s_addr)>>24)!=169)a->have_ip=1;
}

static void scan_networks(struct wifi_app *a)
{
    char out[WIFI_MAX_OUTPUT+1]={0};(void)cam_canvas_clear(a->canvas,C_BG);(void)cam_canvas_text(a->canvas,10,12,"SCANNING WIFI...",C_TEXT);(void)cam_display_present(a->display);
    if(wpa_command(a,"scan",out,sizeof(out))!=0||failed(out)){snprintf(a->message,sizeof(a->message),"Scan failed; check WS73 Wi-Fi driver.");a->ap_count=0;return;}
    sleep_ms(1800);memset(out,0,sizeof(out));if(wpa_command(a,"scan_results",out,sizeof(out))!=0){snprintf(a->message,sizeof(a->message),"Cannot read Wi-Fi scan results.");a->ap_count=0;return;}
    parse_results(a,out);a->page_offset=0;if(!a->ap_count)snprintf(a->message,sizeof(a->message),"No supported Wi-Fi networks found.");
    else snprintf(a->message,sizeof(a->message),"%zu networks. Tap one to connect.",a->ap_count);
}

static void button(cam_canvas *c,int x,int y,int w,int h,const char *label,uint32_t color)
{(void)cam_canvas_rect(c,x,y,w,h,color);(void)cam_canvas_text(c,x+(w-(int)strlen(label)*6)/2,y+(h-8)/2,label,C_TEXT);}
static void header(struct wifi_app *a,const char *s){(void)cam_canvas_rect(a->canvas,0,0,320,29,C_PANEL);(void)cam_canvas_text(a->canvas,8,10,s,C_TEXT);}

static void draw_scan(struct wifi_app *a)
{
    (void)cam_canvas_clear(a->canvas,C_BG);header(a,"WIFI");button(a->canvas,192,4,59,21,"BACK",C_BUTTON);button(a->canvas,255,4,61,21,"RESCAN",C_BUTTON);
    (void)cam_canvas_text(a->canvas,8,36,a->message,C_MUTED);size_t count=a->ap_count<7?a->ap_count:7;
    for(size_t row=0;row<count;++row){size_t i=a->page_offset+row;if(i>=a->ap_count)break;struct access_point *p=&a->aps[i];int y=49+(int)row*22;
        (void)cam_canvas_rect(a->canvas,5,y,310,20,p->current?C_ACTIVE:C_PANEL);char label[34];snprintf(label,sizeof(label),"%.32s",p->ssid);label[p->current?30:32]=0;(void)cam_canvas_text(a->canvas,11,y+6,label,C_TEXT);
        char info[28];if(p->current)snprintf(info,sizeof(info),"CONNECTED");else snprintf(info,sizeof(info),"%s %ddBm",p->secured?"LOCK":"OPEN",p->signal);(void)cam_canvas_text(a->canvas,228,y+6,info,p->current?C_OK:C_MUTED);}
    if(a->ap_count>7){char range[40];snprintf(range,sizeof(range),"SWIPE %zu-%zu / %zu",a->page_offset+1,a->page_offset+count,a->ap_count);(void)cam_canvas_text(a->canvas,8,212,range,C_MUTED);}
    if(a->have_ip)button(a->canvas,204,204,111,22,"CONTINUE",C_ACTIVE);
    (void)cam_canvas_text(a->canvas,8,232,a->have_ip?"WIFI CONNECTED":"SELECT A NETWORK",a->have_ip?C_OK:C_MUTED);
}
static void key(cam_canvas *c,int x,int y,int w,int h,char ch)
{(void)cam_canvas_rect(c,x+1,y+1,w-2,h-2,C_BUTTON);char s[2]={ch,0};(void)cam_canvas_text(c,x+(w-6)/2,y+(h-8)/2,s,C_TEXT);}

static const char *keyboard_row(const struct wifi_app *a,int row,int *start)
{
    static const char *alpha[]={"qwertyuiop","asdfghjkl","zxcvbnm"};
    static const int alpha_start[]={0,16,48};
    static const char *digits[]={"1234567890","-/:;()$&@\"",".,?!'`~_+="};
    static const int digits_start[]={0,0,0};
    static const char *symbols[]={"!@#$%^&*()","-_=+[]{}?/","\\|,<>.:;'\""};
    static const int symbols_start[]={0,0,0};
    if(row<0||row>2)return "";
    if(a->keyboard_mode==1){*start=digits_start[row];return digits[row];}
    if(a->keyboard_mode==2){*start=symbols_start[row];return symbols[row];}
    *start=alpha_start[row];return alpha[row];
}

static void draw_password(struct wifi_app *a)
{
    (void)cam_canvas_clear(a->canvas,C_BG);header(a,a->aps[a->selected].secured?"ENTER WIFI PASSWORD":"CONNECT OPEN WIFI");
    (void)cam_canvas_text(a->canvas,8,36,"SSID",C_MUTED);(void)cam_canvas_text(a->canvas,43,36,a->aps[a->selected].ssid,C_TEXT);
    (void)cam_canvas_rect(a->canvas,5,51,310,22,C_PANEL);(void)cam_canvas_text(a->canvas,10,58,"KEY",C_MUTED);
    char mask[40];size_t n=a->password_length<38?a->password_length:38;memset(mask,'*',n);mask[n]=0;(void)cam_canvas_text(a->canvas,49,58,mask,C_TEXT);
    if(a->aps[a->selected].secured){
        for(int r=0;r<3;++r){int start=0;const char *row=keyboard_row(a,r,&start);
            for(int c=0;row[c];++c){char ch=row[c];if(a->keyboard_mode==0&&a->shifted)ch=(char)(ch-'a'+'A');key(a->canvas,start+c*32,77+r*28,32,26,ch);}}
        const char *mode_label=a->keyboard_mode==0?"123":a->keyboard_mode==1?"SYM":"ABC";
        button(a->canvas,0,161,52,26,mode_label,C_ACTIVE);button(a->canvas,55,161,205,26,"SPACE",C_BUTTON);button(a->canvas,263,161,57,26,"DEL",C_BUTTON);
        if(a->keyboard_mode==0)button(a->canvas,0,132,45,26,"SHIFT",a->shifted?C_ACTIVE:C_BUTTON);
    }
    button(a->canvas,5,198,105,28,"CANCEL",C_BUTTON);button(a->canvas,115,198,200,28,"CONNECT",C_ACTIVE);(void)cam_canvas_text(a->canvas,8,232,a->message,C_WARN);
}

static int network_id(const char *s)
{
    for(const char *line=s;line&&*line;){const char *end=strchr(line,'\n');if(!end)end=line+strlen(line);
        while(line<end&&(*line=='>'||*line==' '||*line=='\r'))++line;
        if(line<end&&*line>='0'&&*line<='9'){char *tail;errno=0;long id=strtol(line,&tail,10);
            while(tail<end&&(*tail==' '||*tail=='\r'))++tail;
            if(tail==end&&!errno&&id>=0&&id<4096)return (int)id;}
        line=*end?end+1:end;
    }
    return -1;
}
static int set_value(struct wifi_app *a,int id,const char *field,const char *value,char *out,size_t cap)
{char cmd[512];int n=snprintf(cmd,sizeof(cmd),"set_network %d %s %s",id,field,value);return n<0||(size_t)n>=sizeof(cmd)||wpa_command(a,cmd,out,cap)!=0||!response_line(out,"OK")?-1:0;}

static int configure(struct wifi_app *a)
{
    struct access_point *p=&a->aps[a->selected];char out[2048]={0},value[256],cmd[128];
    if(p->secured&&(a->password_length<8||a->password_length>WIFI_PASSWORD_MAX)){snprintf(a->message,sizeof(a->message),"Password must contain 8 to 63 characters.");return -1;}
    if(wpa_command(a,"add_network",out,sizeof(out))||failed(out)){snprintf(a->message,sizeof(a->message),"Could not create Wi-Fi profile.");return -1;}
    int id=network_id(out);if(id<0){snprintf(a->message,sizeof(a->message),"Invalid wpa_supplicant response.");return -1;}
    static const char hex[]="0123456789abcdef";size_t n=0;for(size_t i=0;p->ssid[i]&&n+2<sizeof(value);++i){unsigned char b=(unsigned char)p->ssid[i];value[n++]=hex[b>>4];value[n++]=hex[b&15];}value[n]=0;
    if(set_value(a,id,"ssid",value,out,sizeof(out)))goto fail;
    if(p->secured){char quoted[WIFI_PASSWORD_MAX*2+3];n=0;quoted[n++]='"';for(size_t i=0;i<a->password_length&&n+3<sizeof(quoted);++i){char ch=a->password[i];if(ch=='\\'||ch=='"')quoted[n++]='\\';quoted[n++]=ch;}quoted[n++]='"';quoted[n]=0;
        if(set_value(a,id,"psk",quoted,out,sizeof(out))||set_value(a,id,"key_mgmt","WPA-PSK",out,sizeof(out)))goto fail;
    }else if(set_value(a,id,"key_mgmt","NONE",out,sizeof(out)))goto fail;
    snprintf(cmd,sizeof(cmd),"enable_network %d",id);if(wpa_command(a,cmd,out,sizeof(out))||failed(out))goto fail;
    snprintf(cmd,sizeof(cmd),"select_network %d",id);if(wpa_command(a,cmd,out,sizeof(out))||failed(out))goto fail;
    snprintf(a->message,sizeof(a->message),"Connecting to Wi-Fi...");draw_password(a);(void)cam_display_present(a->display);
    for(int i=0;i<30;++i){sleep_ms(1000);memset(out,0,sizeof(out));if(wpa_command(a,"status",out,sizeof(out))==0&&response_line(out,"wpa_state=COMPLETED")){
            char *argv[]={"udhcpc","-i","wlan0","-q","-n","-t","5","-s","/usr/share/udhcpc/default.script",NULL};char dhcp[1024]={0};
            int dhcp_ok=cam_run_capture(argv,NULL,dhcp,sizeof(dhcp),25000)==0;read_connection(a);
            if(dhcp_ok&&a->have_ip&&!strcmp(a->current_ssid,p->ssid)){(void)chmod("/etc/wireless/wpa_supplicant.conf",0600);if(wpa_command(a,"enable_network all",out,sizeof(out))||!response_line(out,"OK")||wpa_command(a,"save_config",out,sizeof(out))||!response_line(out,"OK")){snprintf(a->message,sizeof(a->message),"Connected, but could not save Wi-Fi settings.");return -1;}
                memset(a->password,0,sizeof(a->password));a->password_length=0;snprintf(a->message,sizeof(a->message),"Wi-Fi connected and saved.");return 0;}
            break;}}
    snprintf(cmd,sizeof(cmd),"remove_network %d",id);(void)wpa_command(a,cmd,out,sizeof(out));
    (void)wpa_command(a,"enable_network all",out,sizeof(out));(void)wpa_command(a,"reconnect",out,sizeof(out));
    snprintf(a->message,sizeof(a->message),"Timed out; check password and signal.");return -1;
fail:
    snprintf(cmd,sizeof(cmd),"remove_network %d",id);(void)wpa_command(a,cmd,out,sizeof(out));snprintf(a->message,sizeof(a->message),"Wi-Fi setup failed; check wpa_supplicant.");return -1;
}

static void add_char(struct wifi_app *a,char c){if(a->password_length<WIFI_PASSWORD_MAX){a->password[a->password_length++]=c;a->password[a->password_length]=0;}}
static int touch_password(struct wifi_app *a,int x,int y)
{
    if(y>=198&&y<=228&&x<112){a->page=PAGE_SCAN;memset(a->password,0,sizeof(a->password));a->password_length=0;return 0;}
    if(y>=198&&y<=228&&x>=115)return configure(a)==0?1:0;
    if(!a->aps[a->selected].secured)return 0;
    if(y>=161&&y<187){if(x<54)a->keyboard_mode=(a->keyboard_mode+1)%3;else if(x<260)add_char(a,' ');else if(a->password_length)a->password[--a->password_length]=0;return 0;}
    if(a->keyboard_mode==0&&y>=132&&y<159&&x<47){a->shifted=!a->shifted;return 0;}
    int row=y>=77&&y<103?0:y>=105&&y<131?1:y>=133&&y<159?2:-1;
    if(row>=0){int start=0;const char *keys=keyboard_row(a,row,&start);int index=(x-start)/32;
        if(x>=start&&index>=0&&(size_t)index<strlen(keys)){char ch=keys[index];if(a->keyboard_mode==0&&a->shifted)ch=(char)(ch-'a'+'A');add_char(a,ch);}}
    return 0;
}
static int touch_scan(struct wifi_app *a,int x,int y)
{
    if(y<29&&x>=250){read_connection(a);scan_networks(a);return 0;}
    if(y<29&&x>=192)return 2;
    if(a->have_ip&&y>=204&&x>=200)return 1;
    if(y>=49&&y<203){size_t row=(size_t)(y-49)/22,index=a->page_offset+row;if(index<a->ap_count){a->selected=index;a->page=PAGE_PASSWORD;a->password[0]=0;a->password_length=0;a->keyboard_mode=a->shifted=0;snprintf(a->message,sizeof(a->message),"Enter the network password.");}}
    return 0;
}

static void swipe_scan(struct wifi_app *a,int y,int dy)
{
    if(y<49||y>=203||a->ap_count<=7)return;
    size_t maximum=a->ap_count-7;
    size_t step=(size_t)(abs(dy)/22);if(step<1)step=1;if(step>7)step=7;
    if(dy<0)a->page_offset=step>maximum-a->page_offset?maximum:a->page_offset+step;
    else a->page_offset=step>a->page_offset?0:a->page_offset-step;
}

/* Called by the camera app using its existing DRM and touch handles. */
int cam_wifi_setup(struct cam_display *display,struct cam_touch *touch,int (*cancelled)(void))
{
    struct wifi_app app={.display=display,.touch=touch,.canvas=cam_display_canvas(display),
        .wpa_cli=getenv("WPA_CLI")?getenv("WPA_CLI"):"/opt/camera_ai_lcd/wifi-runtime/bin/wpa_cli",
        .control_dir=getenv("WPA_CTRL_DIR")?getenv("WPA_CTRL_DIR"):"/etc/wireless/wpa_supplicant",.page=PAGE_SCAN};
    if(!app.canvas||app.canvas->width!=WIFI_SCREEN_W||app.canvas->height!=WIFI_SCREEN_H||!touch)return -1;
    char lock[4096]={0};const char *home=getenv("CAMERA_AI_HOME");
    if(home&&snprintf(lock,sizeof(lock),"%s/service-runtime/wifi-ui.pid",home)<(int)sizeof(lock)){
        FILE *fp=fopen(lock,"w");if(fp){fprintf(fp,"%ld\n",(long)getpid());fclose(fp);}
    }
    read_connection(&app);scan_networks(&app);int finished=0;
    while(!finished&&!(cancelled&&cancelled())){if(app.page==PAGE_SCAN)draw_scan(&app);else draw_password(&app);if(cam_display_present(display)!=0)break;
        int x=0,y=0,dy=0,event=cam_touch_poll_gesture(touch,&x,&y,&dy);if(event<0)break;
        if(event==2&&app.page==PAGE_SCAN)swipe_scan(&app,y,dy);
        if(event==1){int done=app.page==PAGE_SCAN?touch_scan(&app,x,y):touch_password(&app,x,y);if(done>0)finished=done;}sleep_ms(25);}
    memset(app.password,0,sizeof(app.password));
    if(lock[0])unlink(lock);
    if(cancelled&&cancelled())return 1;
    if(!finished)return -1;
    if(finished==2)return 1; /* BACK works even without Wi-Fi. */
    read_connection(&app);
    if(app.have_ip)printf("WIFI_CONNECTED ssid=%s\n",app.current_ssid);
    return 0;
}

/* Start optional screenshot services while the loading screen remains visible. */
int cam_wifi_start_services(void)
{
    const char *home=getenv("CAMERA_AI_HOME");if(!home)return 0;
    char script[4096];if(snprintf(script,sizeof(script),"%s/serve-screenshots.sh",home)>=(int)sizeof(script))return -1;
    char address[32];if(cam_interface_ipv4("wlan0",address,sizeof(address))!=0)return -1;
    char *argv[]={"sh",script,"start",address,NULL};char output[2048];
    int rc=cam_run_capture(argv,NULL,output,sizeof(output),10000);printf("%s",output);return rc;
}

#ifndef CAM_WIFI_EMBEDDED
int main(int argc,char **argv)
{
    const char *device="/dev/dri/card0",*input=NULL;int rotation=90;
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--rotation")&&i+1<argc)rotation=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--display")&&i+1<argc)device=argv[++i];
        else if(!strcmp(argv[i],"--touch")&&i+1<argc)input=argv[++i];
        else{puts("wifi_setup [--rotation 0|90|270] [--display DEVICE] [--touch DEVICE]");return 2;}}
    struct cam_display *display=NULL;struct cam_touch *touch=NULL;
    if(cam_display_open(&display,device,rotation)!=0)return 1;
    int rc=cam_touch_open(&touch,input,rotation)==0?cam_wifi_setup(display,touch,NULL):-1;
    cam_touch_close(touch);cam_display_close(display);return rc==0?0:1;
}
#endif
