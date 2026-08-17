#ifndef LEONOS_I18N_H
#define LEONOS_I18N_H

#define LEONOS_LANG_EN 0
#define LEONOS_LANG_ZH 1
#define LEONOS_LOCALE_CONFIG_PATH "0:/system/config/locale.conf"

int leonos_i18n_language(void);
const char *leonos_i18n(const char *en, const char *zh);
int leonos_i18n_set_language(int lang);

#endif
