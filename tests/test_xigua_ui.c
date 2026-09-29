#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/xigua/xigua_ui.c"
const lv_font_t xigua_font_16={0}, lv_font_montserrat_14={0}, lv_font_montserrat_20={0};
static unsigned scans,saves;
static char saved_ssid[33],saved_password[65];
bool xigua_service_wifi_scan(void) { ++scans; return true; }
bool xigua_service_wifi_save(const char *ssid,const char *password) {
    ++saves; snprintf(saved_ssid,sizeof(saved_ssid),"%s",ssid);
    snprintf(saved_password,sizeof(saved_password),"%s",password); return true;
}
static xigua_state_t state;
static xigua_ui_view_t view;
static void key(xigua_ui_key_t k) {
    xigua_ui_intent_t i=xigua_ui_key(k,&view);
    assert(!i.valid); xigua_ui_render(&view);
    assert(s_label_used<=LABEL_COUNT && s_box_used<=BOX_COUNT);
}
static void keyboard_select(uint8_t target) {
    unsigned moves=0;
    while(s_keyboard.selection!=target) {
        key(s_keyboard.selection<target ? XIGUA_UI_DOWN_CLICK : XIGUA_UI_UP_CLICK);
        assert(++moves<XIGUA_KEYBOARD_KEY_COUNT);
    }
}
static void keyboard_mode(xigua_keyboard_mode_t mode) {
    static const uint8_t controls[XIGUA_KEYBOARD_MODE_COUNT]={
        XIGUA_KEYBOARD_KEY_LOWER,XIGUA_KEYBOARD_KEY_UPPER,XIGUA_KEYBOARD_KEY_SYMBOLS
    };
    keyboard_select(controls[mode]); key(XIGUA_UI_OK_CLICK);
    assert(s_keyboard.mode==mode && s_keyboard.page==0);
}
static void keyboard_text(const char *text) {
    for(const unsigned char *p=(const unsigned char *)text;*p;++p) {
        if(*p>='a'&&*p<='z') {
            keyboard_mode(XIGUA_KEYBOARD_MODE_LOWER); keyboard_select((uint8_t)(*p-'a'));
        } else if(*p>='A'&&*p<='Z') {
            keyboard_mode(XIGUA_KEYBOARD_MODE_UPPER); keyboard_select((uint8_t)(*p-'A'));
        } else {
            uint8_t index;
            keyboard_mode(XIGUA_KEYBOARD_MODE_SYMBOLS);
            if(*p>='0'&&*p<='9') index=(uint8_t)(*p-'0');
            else if(*p=='!') index=10;
            else if(*p=='-') index=22;
            else if(*p=='.') index=23;
            else assert(false);
            keyboard_select(index);
        }
        key(XIGUA_UI_OK_CLICK);
    }
    keyboard_select(XIGUA_KEYBOARD_KEY_DONE); key(XIGUA_UI_OK_CLICK);
}
int main(void) {
    xigua_init(&state,1); view.state=&state; view.battery_pct=80;
    view.service.available=true;
    xigua_ui_create(); xigua_ui_render(&view);
    /* Settings is a top-level home destination, not hidden under More. */
    for(unsigned i=0;i<6;++i) key(XIGUA_UI_DOWN_CLICK);
    key(XIGUA_UI_OK_CLICK); assert(s_page==PAGE_SETTINGS);
    key(XIGUA_UI_OK_CLICK); assert(s_page==PAGE_WIFI && scans==1);
    view.service.wifi_scan_count=1; view.service.wifi_scan_generation=1;
    strcpy(view.service.wifi_scan[0].ssid,"test-ap");
    key(XIGUA_UI_DOWN_CLICK); key(XIGUA_UI_OK_CLICK);
    assert(s_page==PAGE_WIFI_EDIT && s_wifi_field==0);
    key(XIGUA_UI_OK_CLICK); assert(s_page==PAGE_KEYBOARD && !s_keyboard_password);
    keyboard_text("sim-ap-2.4X");
    assert(s_page==PAGE_WIFI_EDIT && !strcmp(s_wifi_ssid,"sim-ap-2.4X"));
    key(XIGUA_UI_DOWN_CLICK); key(XIGUA_UI_OK_CLICK);
    assert(s_page==PAGE_KEYBOARD && s_keyboard_password && s_keyboard.max_bytes==65);
    keyboard_text("A1b2-C3d4!");
    assert(!strcmp(s_wifi_password,"A1b2-C3d4!") && !s_keyboard.text[0]);
    for(unsigned i=0;i<s_label_used;++i) assert(!strstr(s_labels[i]->text,"A1b2-C3d4!"));
    key(XIGUA_UI_DOWN_CLICK); key(XIGUA_UI_OK_CLICK);
    assert(saves==1 && !strcmp(saved_ssid,"sim-ap-2.4X") &&
           !strcmp(saved_password,"A1b2-C3d4!"));
    view.service.wifi_config_busy=true; key(XIGUA_UI_OK_CLICK); assert(saves==1);
    view.service.wifi_config_busy=false;
    key(XIGUA_UI_UP_LONG); assert(s_page==PAGE_WIFI_EDIT);
    key(XIGUA_UI_DOWN_LONG); assert(s_page==PAGE_WIFI && !s_wifi_password[0]);
    key(XIGUA_UI_OK_CLICK); assert(s_wifi_field==0);
    key(XIGUA_UI_OK_CLICK); assert(s_keyboard.max_bytes==33);
    key(XIGUA_UI_OK_LONG); assert(s_keyboard.page==0 && xigua_keyboard_page_count(&s_keyboard)==1);
    key(XIGUA_UI_DOWN_LONG);
    assert(s_page==PAGE_WIFI_EDIT && !s_wifi_ssid[0] && saves==1);
    s_page=PAGE_VOLUME; s_volume=95;
    xigua_ui_intent_t i=xigua_ui_key(XIGUA_UI_UP_CLICK,&view);
    assert(i.audio==XIGUA_UI_AUDIO_VOLUME && i.value==100);
    i=xigua_ui_key(XIGUA_UI_UP_CLICK,&view); assert(i.audio==XIGUA_UI_AUDIO_NONE && s_volume==100);
    s_volume=0; i=xigua_ui_key(XIGUA_UI_DOWN_CLICK,&view); assert(i.audio==XIGUA_UI_AUDIO_NONE);
    xigua_ui_destroy(); assert(!s_wifi_password[0] && !s_keyboard.text[0]);
    puts("xigua UI navigation, password masking and volume tests passed");
}
