#include "./lv_i18n.h"


////////////////////////////////////////////////////////////////////////////////
// Define plural operands
// http://unicode.org/reports/tr35/tr35-numbers.html#Operands

// Integer version, simplified

#define UNUSED(x) (void)(x)

static inline uint32_t op_n(int32_t val) { return (uint32_t)(val < 0 ? -val : val); }
static inline uint32_t op_i(uint32_t val) { return val; }
// always zero, when decimal part not exists.
static inline uint32_t op_v(uint32_t val) { UNUSED(val); return 0;}
static inline uint32_t op_w(uint32_t val) { UNUSED(val); return 0; }
static inline uint32_t op_f(uint32_t val) { UNUSED(val); return 0; }
static inline uint32_t op_t(uint32_t val) { UNUSED(val); return 0; }

static lv_i18n_phrase_t ar_singulars[] = {
    {"settings", "الإعدادات"},
    {"menu", "القائمة"},
    {"Ready", "جاهز"},
    {"welcome", "مرحبا"},
    {"wifi", "الشبكة"},
    {"language", "اللغة"},
    {"sound", "الصوت"},
    {"time&date", "الوقت والتاريخ"},
    {"about", "عنا"},
    {"current network", "الشبكة الحالية"},
    {"available network", "الشبكة المتاحة"},
    {"mute", "كتم"},
    {"touch sound", "صوت اللمس"},
    {"chosse language", "اختار اللغة"},
    {"save", "حفظ"},
    {"connect", "اتصال"},
    {"wrong password", "كلمة السر خطا"},
    {"connected", "تم الاتصال"},
    {"time", "الوقت"},
    {"date", "التاريخ"},
    {"employee", "الموظفون"},
    {"password", "كلمة السر"},
    {"Record", "السجل"},
    {"employee name", "اسم الموظف"},
    {"id", "رقم"},
    {"add employee", "اضافة موظف"},
    {"Check In", "دخول"},
    {"Check Out", "خروج"},
    {"error", "خطأ"},
    {"Done successfully", "تم بنجاح"},
    {"place your finger again", "رجاء وضع اصبعك مجددا"},
    {"fingerprint used or error", "البصمة مستعملة او خطا"},
    {"please place your finger", "الرجاء ضع اصبعك"},
    {"are you sure?", "هل انت متاكد"},
    {"yes", "نعم"},
    {"no", "لا"},
    {NULL, NULL} // End mark
};



static uint8_t ar_plural_fn(int32_t num)
{
    uint32_t n = op_n(num); UNUSED(n);

    uint32_t n100 = n % 100;
    if ((n == 0)) return LV_I18N_PLURAL_TYPE_ZERO;
    if ((n == 1)) return LV_I18N_PLURAL_TYPE_ONE;
    if ((n == 2)) return LV_I18N_PLURAL_TYPE_TWO;
    if (((3 <= n100 && n100 <= 10))) return LV_I18N_PLURAL_TYPE_FEW;
    if (((11 <= n100 && n100 <= 99))) return LV_I18N_PLURAL_TYPE_MANY;
    return LV_I18N_PLURAL_TYPE_OTHER;
}

static const lv_i18n_lang_t ar_lang = {
    .locale_name = "ar",
    .singulars = ar_singulars,

    .locale_plural_fn = ar_plural_fn
};

static lv_i18n_phrase_t en_singulars[] = {
    {"settings", "settings"},
    {"menu", "Menu"},
    {"Ready", "Ready"},
    {"welcome", "welcome"},
    {"wifi", "wifi"},
    {"language", "language"},
    {"sound", "sound"},
    {"time&date", "time&date"},
    {"about", "about"},
    {"current network", "current network"},
    {"available network", "available network"},
    {"mute", "mute"},
    {"touch sound", "touch sound"},
    {"chosse language", "chosse language"},
    {"save", "save"},
    {"connect", "connect"},
    {"wrong password", "wrong password"},
    {"connected", "connected"},
    {"time", "time"},
    {"date", "date"},
    {"employee", "employee"},
    {"password", "password"},
    {"Record", "Record"},
    {"employee name", "employee name"},
    {"id", "id"},
    {"add employee", "add employee"},
    {"Check In", "Check In"},
    {"Check Out", "Check Out"},
    {"error", "error"},
    {"Done successfully", "Done successfully"},
    {"place your finger again", " place your finger agai"},
    {"fingerprint used or error", "fingerprint used or error"},
    {"please place your finger", "please place your finger"},
    {"are you sure?", "are you sure?"},
    {"yes", "yes"},
    {"no", "no"},
    {NULL, NULL} // End mark
};



static uint8_t en_plural_fn(int32_t num)
{
    uint32_t n = op_n(num); UNUSED(n);
    uint32_t i = op_i(n); UNUSED(i);
    uint32_t v = op_v(n); UNUSED(v);

    if ((i == 1 && v == 0)) return LV_I18N_PLURAL_TYPE_ONE;
    return LV_I18N_PLURAL_TYPE_OTHER;
}

static const lv_i18n_lang_t en_lang = {
    .locale_name = "en",
    .singulars = en_singulars,

    .locale_plural_fn = en_plural_fn
};

static lv_i18n_phrase_t fr_singulars[] = {
    {"settings", "Reglages"},
    {"menu", "Menu"},
    {"Ready", "Pret"},
    {"welcome", "Bienvenue"},
    {"wifi", "WiFi"},
    {"language", "Langue"},
    {"sound", "Son"},
    {"time&date", "Heure&Date"},
    {"about", "A propos"},
    {"current network", "Reseau actuel"},
    {"available network", "Reseau disponible"},
    {"mute", "Muette"},
    {"touch sound", "Son tactile"},
    {"chosse language", "Choisir la langue"},
    {"save", "Enregistrer"},
    {"connect", "Connecter"},
    {"wrong password", "Mot de passe incorrect"},
    {"connected", "Connecte"},
    {"date", "Date"},
    {"time", "Heure"},
    {"employee", "Employe"},
    {"password", "Mot de passe"},
    {"Record", "Enregistrer"},
    {"employee name", "Nom de l employe"},
    {"id", "ID"},
    {"add employee", "Ajouter un employé"},
    {"Check In", "Pointage entree"},
    {"Check Out", "Pointage sortie"},
    {"error", "Erreur"},
    {"Done successfully", "Effectue avec succès"},
    {"place your finger again", "Placez votre doigt a nouveau"},
    {"fingerprint used or error", "Empreinte utilisee ou erreur"},
    {"please place your finger", "Veuillez placer votre doigt"},
    {"are you sure?", "Tu es sur que?"},
    {"yes", "oui"},
    {"no", "no"},
    {NULL, NULL} // End mark
};



static uint8_t fr_plural_fn(int32_t num)
{
    uint32_t n = op_n(num); UNUSED(n);
    uint32_t i = op_i(n); UNUSED(i);

    if ((((i == 0) || (i == 1)))) return LV_I18N_PLURAL_TYPE_ONE;
    return LV_I18N_PLURAL_TYPE_OTHER;
}

static const lv_i18n_lang_t fr_lang = {
    .locale_name = "fr",
    .singulars = fr_singulars,

    .locale_plural_fn = fr_plural_fn
};

const lv_i18n_language_pack_t lv_i18n_language_pack[] = {
    &ar_lang,
    &en_lang,
    &fr_lang,
    NULL // End mark
};

////////////////////////////////////////////////////////////////////////////////


// Internal state
static const lv_i18n_language_pack_t * current_lang_pack;
static const lv_i18n_lang_t * current_lang;


/**
 * Reset internal state. For testing.
 */
void __lv_i18n_reset(void)
{
    current_lang_pack = NULL;
    current_lang = NULL;
}

/**
 * Set the languages for internationalization
 * @param langs pointer to the array of languages. (Last element has to be `NULL`)
 */
int lv_i18n_init(const lv_i18n_language_pack_t * langs)
{
    if(langs == NULL) return -1;
    if(langs[0] == NULL) return -1;

    current_lang_pack = langs;
    current_lang = langs[0];     /*Automatically select the first language*/
    return 0;
}

/**
 * Change the localization (language)
 * @param l_name name of the translation locale to use. E.g. "en-GB"
 */
int lv_i18n_set_locale(const char * l_name)
{
    if(current_lang_pack == NULL) return -1;

    uint16_t i;

    for(i = 0; current_lang_pack[i] != NULL; i++) {
        // Found -> finish
        if(strcmp(current_lang_pack[i]->locale_name, l_name) == 0) {
            current_lang = current_lang_pack[i];
            return 0;
        }
    }

    return -1;
}


static const char * __lv_i18n_get_text_core(lv_i18n_phrase_t * trans, const char * msg_id)
{
    uint16_t i;
    for(i = 0; trans[i].msg_id != NULL; i++) {
        if(strcmp(trans[i].msg_id, msg_id) == 0) {
            /*The msg_id has found. Check the translation*/
            if(trans[i].translation) return trans[i].translation;
        }
    }

    return NULL;
}


/**
 * Get the translation from a message ID
 * @param msg_id message ID
 * @return the translation of `msg_id` on the set local
 */
const char * lv_i18n_get_text(const char * msg_id)
{
    if(current_lang == NULL) return msg_id;

    const lv_i18n_lang_t * lang = current_lang;
    const void * txt;

    // Search in current locale
    if(lang->singulars != NULL) {
        txt = __lv_i18n_get_text_core(lang->singulars, msg_id);
        if (txt != NULL) return txt;
    }

    // Try to fallback
    if(lang == current_lang_pack[0]) return msg_id;
    lang = current_lang_pack[0];

    // Repeat search for default locale
    if(lang->singulars != NULL) {
        txt = __lv_i18n_get_text_core(lang->singulars, msg_id);
        if (txt != NULL) return txt;
    }

    return msg_id;
}

/**
 * Get the translation from a message ID and apply the language's plural rule to get correct form
 * @param msg_id message ID
 * @param num an integer to select the correct plural form
 * @return the translation of `msg_id` on the set local
 */
const char * lv_i18n_get_text_plural(const char * msg_id, int32_t num)
{
    if(current_lang == NULL) return msg_id;

    const lv_i18n_lang_t * lang = current_lang;
    const void * txt;
    lv_i18n_plural_type_t ptype;

    // Search in current locale
    if(lang->locale_plural_fn != NULL) {
        ptype = lang->locale_plural_fn(num);

        if(lang->plurals[ptype] != NULL) {
            txt = __lv_i18n_get_text_core(lang->plurals[ptype], msg_id);
            if (txt != NULL) return txt;
        }
    }

    // Try to fallback
    if(lang == current_lang_pack[0]) return msg_id;
    lang = current_lang_pack[0];

    // Repeat search for default locale
    if(lang->locale_plural_fn != NULL) {
        ptype = lang->locale_plural_fn(num);

        if(lang->plurals[ptype] != NULL) {
            txt = __lv_i18n_get_text_core(lang->plurals[ptype], msg_id);
            if (txt != NULL) return txt;
        }
    }

    return msg_id;
}

/**
 * Get the name of the currently used locale.
 * @return name of the currently used locale. E.g. "en-GB"
 */
const char * lv_i18n_get_current_locale(void)
{
    if(!current_lang) return NULL;
    return current_lang->locale_name;
}
