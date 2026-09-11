SET(SOURCES screens/ui_main.c
    screens/ui_settings.c
    screens/ui_wifiset.c
    screens/ui_connectwifi.c
    screens/ui_soundset.c
    screens/ui_langset.c
    screens/ui_date.c
    screens/ui_menu.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_background_480x272_png.c
    images/ui_img_724231620.c
    images/ui_img_706591335.c
    images/ui_img_1385604958.c
    images/ui_img_567465477.c
    images/ui_img_1687889417.c
    images/ui_img_1475031593.c
    images/ui_img_1198081141.c
    images/ui_img_1958657997.c
    images/ui_img_668791536.c
    images/ui_img_1832640421.c
    images/ui_img_310155206.c
    images/ui_img_1799210772.c
    images/ui_img_1176085953.c
    images/ui_img_443122641.c
    fonts/ui_font_arabic.c
    fonts/ui_font_arabic1.c
    fonts/ui_font_arabic12.c)

add_library(ui ${SOURCES})
