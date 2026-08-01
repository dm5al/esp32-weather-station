/*
 * Interface translations.
 *
 * Every string is declared once in the I18N_STRINGS table below, carrying its
 * English, Russian and German forms on one line. The enum and the lookup table
 * are both generated from it, so a translation can never end up attached to the
 * wrong identifier — the failure mode that plagues parallel string arrays.
 *
 * Adding a language means adding a column here and a case in i18n_lang_name().
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LANG_EN,
    LANG_RU,
    LANG_DE,
    LANG_COUNT,
} lang_t;

/* X(id, english, russian, german) */
#define I18N_STRINGS(X)                                                                            \
    /* ---- boot / status ---- */                                                                  \
    X(STR_APP_NAME,        "Weather Station",  "Погода",            "Wetterstation")               \
    X(STR_STARTING,        "Starting up...",   "Запуск...",         "Startet...")                  \
    X(STR_CONNECTING,      "Connecting",       "Подключение",       "Verbinde")                    \
    X(STR_JOINING_SAVED,   "Joining the saved network...",                                         \
                           "Подключение к сохранённой сети...",                                    \
                           "Verbinde mit gespeichertem Netzwerk...")                               \
    X(STR_LOCATING,        "Locating",         "Определение места", "Standort")                    \
    X(STR_LOCATING_DETAIL, "Working out where this display is...",                                 \
                           "Определяем, где находится экран...",                                   \
                           "Ermittle den Standort des Displays...")                                \
    X(STR_LOC_FAILED,      "Location unavailable", "Место не определено",                          \
                           "Standort nicht verfügbar")                                             \
    X(STR_LOC_FAILED_MSG,  "Could not determine location. Retrying shortly.",                      \
                           "Не удалось определить место. Повторим попытку.",                       \
                           "Standort konnte nicht ermittelt werden. Neuer Versuch folgt.")         \
    X(STR_WX_FAILED,       "Weather unavailable", "Погода недоступна",                             \
                           "Wetter nicht verfügbar")                                               \
    X(STR_WX_FAILED_MSG,   "Could not reach the weather service. Retrying...",                     \
                           "Нет связи с сервисом погоды. Повторим попытку...",                     \
                           "Wetterdienst nicht erreichbar. Neuer Versuch...")                      \
    /* ---- network picker ---- */                                                                 \
    X(STR_CHOOSE_NETWORK,  "Choose a Wi-Fi network", "Выберите сеть Wi-Fi",                        \
                           "WLAN-Netzwerk wählen")                                                 \
    X(STR_BACK,            "Back",             "Назад",             "Zurück")                      \
    X(STR_RESCAN,          "Rescan",           "Обновить",          "Neu suchen")                  \
    X(STR_SCANNING,        "Scanning...",      "Поиск сетей...",    "Suche läuft...")              \
    X(STR_TAP_NETWORK,     "Tap a network to connect.", "Коснитесь сети для подключения.",         \
                           "Netzwerk antippen zum Verbinden.")                                     \
    X(STR_NO_NETWORKS,     "No networks found. Tap Rescan to try again.",                          \
                           "Сети не найдены. Нажмите «Обновить».",                                 \
                           "Keine Netzwerke gefunden. Neu suchen antippen.")                       \
    X(STR_SCAN_FAILED,     "Scan failed. Tap Rescan to try again.",                                \
                           "Поиск не удался. Нажмите «Обновить».",                                 \
                           "Suche fehlgeschlagen. Neu suchen antippen.")                           \
    X(STR_CONNECT_FAILED,  "Could not connect. Check the password and try again.",                 \
                           "Не удалось подключиться. Проверьте пароль.",                           \
                           "Verbindung fehlgeschlagen. Passwort prüfen.")                          \
    X(STR_CONNECT_BUSY,    "Could not start the connection.",                                      \
                           "Не удалось начать подключение.",                                       \
                           "Verbindung konnte nicht gestartet werden.")                            \
    X(STR_CONNECTING_TO,   "Connecting to %s...", "Подключение к %s...",                           \
                           "Verbinde mit %s...")                                                   \
    X(STR_PASSWORD_FOR,    "Password for %s",  "Пароль для %s",     "Passwort für %s")             \
    X(STR_PASSWORD_HINT,   "Network password", "Пароль сети",       "Netzwerkpasswort")            \
    X(STR_CANCEL,          "Cancel",           "Отмена",            "Abbrechen")                   \
    X(STR_CONNECT,         "Connect",          "Подключить",        "Verbinden")                   \
    /* ---- settings ---- */                                                                       \
    X(STR_SETTINGS,        "Settings",         "Настройки",         "Einstellungen")               \
    X(STR_LANGUAGE,        "Language",         "Язык",              "Sprache")                     \
    X(STR_WIFI,            "Wi-Fi",            "Wi-Fi",             "WLAN")                        \
    X(STR_WIFI_DESC,       "Choose or change the network", "Выбрать или сменить сеть",             \
                           "Netzwerk wählen oder wechseln")                                        \
    X(STR_LANGUAGE_DESC,   "Interface language", "Язык интерфейса", "Sprache der Oberfläche")      \
    /* ---- weather screen ---- */                                                                 \
    X(STR_OFFLINE,         "offline",          "нет сети",          "offline")                     \
    X(STR_CONNECTING_LC,   "connecting...",    "подключение...",    "verbinde...")                 \
    X(STR_TODAY,           "Today",            "Сегодня",           "Heute")                       \
    X(STR_UPDATED,         "Updated %s",       "Обновлено %s",      "Aktualisiert %s")             \
    X(STR_FEELS_LIKE_FMT,  "Feels like %d°C",  "Ощущается как %d°C", "Gefühlt %d°C")               \
    X(STR_UNKNOWN_PLACE,   "Unknown location", "Место неизвестно",  "Unbekannter Ort")             \
    X(STR_STAT_FEELS,      "FEELS LIKE",       "ОЩУЩАЕТСЯ",         "GEFÜHLT")                     \
    X(STR_STAT_HUMIDITY,   "HUMIDITY",         "ВЛАЖНОСТЬ",         "LUFTFEUCHTE")                 \
    X(STR_STAT_WIND,       "WIND",             "ВЕТЕР",             "WIND")                        \
    X(STR_STAT_PRESSURE,   "PRESSURE",         "ДАВЛЕНИЕ",          "LUFTDRUCK")                   \
    X(STR_STAT_PRECIP,     "PRECIPITATION",    "ОСАДКИ",            "NIEDERSCHLAG")                \
    X(STR_STAT_SUN,        "SUNRISE / SUNSET", "ВОСХОД / ЗАКАТ",    "AUFGANG / UNTERGANG")         \
    X(STR_UNIT_MS,         "m/s",              "м/с",               "m/s")                         \
    X(STR_UNIT_HPA,        "hPa",              "гПа",               "hPa")                         \
    X(STR_UNIT_MM,         "mm",               "мм",                "mm")                          \
    /* ---- weekdays, Sunday first to match tm_wday ---- */                                        \
    X(STR_DOW_SUN,         "Sun",              "Вс",                "So")                          \
    X(STR_DOW_MON,         "Mon",              "Пн",                "Mo")                          \
    X(STR_DOW_TUE,         "Tue",              "Вт",                "Di")                          \
    X(STR_DOW_WED,         "Wed",              "Ср",                "Mi")                          \
    X(STR_DOW_THU,         "Thu",              "Чт",                "Do")                          \
    X(STR_DOW_FRI,         "Fri",              "Пт",                "Fr")                          \
    X(STR_DOW_SAT,         "Sat",              "Сб",                "Sa")                          \
    /* ---- WMO weather codes ---- */                                                              \
    X(STR_WMO_CLEAR,       "Clear sky",        "Ясно",              "Klarer Himmel")               \
    X(STR_WMO_MAINLY_CLEAR,"Mainly clear",     "Малооблачно",       "Überwiegend klar")            \
    X(STR_WMO_PARTLY,      "Partly cloudy",    "Переменная облачность", "Teilweise bewölkt")       \
    X(STR_WMO_OVERCAST,    "Overcast",         "Пасмурно",          "Bedeckt")                     \
    X(STR_WMO_FOG,         "Fog",              "Туман",             "Nebel")                       \
    X(STR_WMO_RIME_FOG,    "Rime fog",         "Изморозь",          "Reifnebel")                   \
    X(STR_WMO_DRIZZLE_L,   "Light drizzle",    "Слабая морось",     "Leichter Niesel")             \
    X(STR_WMO_DRIZZLE,     "Drizzle",          "Морось",            "Niesel")                      \
    X(STR_WMO_DRIZZLE_D,   "Dense drizzle",    "Сильная морось",    "Dichter Niesel")              \
    X(STR_WMO_DRIZZLE_F,   "Freezing drizzle", "Ледяная морось",    "Gefrierender Niesel")         \
    X(STR_WMO_RAIN_L,      "Light rain",       "Небольшой дождь",   "Leichter Regen")              \
    X(STR_WMO_RAIN,        "Rain",             "Дождь",             "Regen")                       \
    X(STR_WMO_RAIN_H,      "Heavy rain",       "Сильный дождь",     "Starker Regen")               \
    X(STR_WMO_RAIN_F,      "Freezing rain",    "Ледяной дождь",     "Gefrierender Regen")          \
    X(STR_WMO_SNOW_L,      "Light snow",       "Небольшой снег",    "Leichter Schnee")             \
    X(STR_WMO_SNOW,        "Snow",             "Снег",              "Schnee")                      \
    X(STR_WMO_SNOW_H,      "Heavy snow",       "Сильный снег",      "Starker Schneefall")          \
    X(STR_WMO_SNOW_GRAINS, "Snow grains",      "Снежные зёрна",     "Schneegriesel")               \
    X(STR_WMO_SHOWERS_L,   "Light showers",    "Слабые ливни",      "Leichte Schauer")             \
    X(STR_WMO_SHOWERS,     "Showers",          "Ливни",             "Schauer")                     \
    X(STR_WMO_SHOWERS_V,   "Violent showers",  "Сильные ливни",     "Heftige Schauer")             \
    X(STR_WMO_SNOW_SHOWER, "Snow showers",     "Снежные ливни",     "Schneeschauer")               \
    X(STR_WMO_SNOW_SHOWER_H, "Heavy snow showers", "Сильные снежные ливни",                        \
                           "Starke Schneeschauer")                                                 \
    X(STR_WMO_THUNDER,     "Thunderstorm",     "Гроза",             "Gewitter")                    \
    X(STR_WMO_THUNDER_HAIL,"Thunderstorm, hail", "Гроза с градом",  "Gewitter mit Hagel")          \
    X(STR_WMO_UNKNOWN,     "Unknown",          "Неизвестно",        "Unbekannt")

#define I18N_ENUM_ENTRY(id, en, ru, de) id,
typedef enum { I18N_STRINGS(I18N_ENUM_ENTRY) STR_COUNT } str_id_t;
#undef I18N_ENUM_ENTRY

/** @brief Look up @p id in the active language. Never returns NULL. */
const char *T(str_id_t id);

/** @brief Load the stored language from NVS. Defaults to English. */
void i18n_init(void);

lang_t i18n_get(void);

/** @brief Switch language and persist it. Callers must redraw the UI. */
esp_err_t i18n_set(lang_t lang);

/** @brief Endonym for the picker: "English", "Русский", "Deutsch". */
const char *i18n_lang_name(lang_t lang);

/** @brief Translated text for a WMO weather interpretation code. */
str_id_t i18n_wmo_string(int code);

/** @brief Translated short weekday for a "YYYY-MM-DD" date. */
const char *i18n_weekday(const char *iso_date);

/** @brief Day of week for a "YYYY-MM-DD" date: 0 = Sunday .. 6 = Saturday, -1 if unparseable. */
int i18n_weekday_index(const char *iso_date);

#ifdef __cplusplus
}
#endif
