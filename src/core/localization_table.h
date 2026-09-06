//################################################################################
// localization_table.h
//--------------------------------------------------------------------------------
// LocalizationEntry    one row of the table: an identifier plus its languages
//                      text (see below)
// kLocalizationTable   every user-facing string World Events translates, one
//                      row per identifier
// kLocalizationCount   number of rows in kLocalizationTable
//--------------------------------------------------------------------------------
// One flat table for the whole addon: Localization_Load (localization.cpp) walks
// it once at AddonLoad, and a single file is easier to keep languages in sync.
//
// Identifier is World Events' own key, not shown to the user - prefixed "WE_" so
// it can't collide with Nexus's own identifiers (Nexus's are either short
// "KB_..." names or "((000123))"-style numeric placeholders - see Nexus-
// Translations on GitHub). Group identifiers by the UI area they belong to
// (CHANGELOG_ below; BASIC_/CYCLIC_/LIVE_ etc. as those get localized) so the
// table reads in feature-sized chunks instead of one undifferentiated list.
//
// Add a row here, then call Tr("WE_...") at the call site (see changelog_window.
// cpp for the pattern) - never format languages into the same literal; Tr() picks
// per Nexus's active language at render time, not at compile time.
//--------------------------------------------------------------------------------

#pragma once

//********************************************************************************
// LocalizationEntry
//--------------------------------------------------------------------------------
// Identifier   World Events' own key, passed to Tr()
// English      shown when Nexus's active language is English or as fallback
// German       shown when Nexus's active language is German
//--------------------------------------------------------------------------------
struct LocalizationEntry
{
    const char* Identifier;
    const char* English;
    const char* German;
};

static constexpr LocalizationEntry kLocalizationTable[] = {

    //_ changelog_window.cpp - "What's New" popup shown after every build
    { "WE_CHANGELOG_TITLE",          "World Events - What's New", "World Events - Was ist neu" },
    { "WE_CHANGELOG_LATEST_SUFFIX",  " (latest)",                 " (neueste)" },
    { "WE_CHANGELOG_GOT_IT",         "Got it",                    "Geht klar" },

    //_ reset_defaults.cpp - "Default" button + confirmation popup in the options panel.
    { "WE_RESET_BUTTON",       "Default",             "Standard" },
    { "WE_RESET_POPUP_TITLE",  "Reset to Defaults",   "Auf Standard zurücksetzen" },
    { "WE_RESET_BODY",
        //_ en
        "Deletes and rebuilds events.json from scratch.",
        //_ de
        "Löscht events.json und erstellt sie mit Grundeinstellungen." },
    { "WE_RESET_WARNING",  "This cannot be undone.", "Dies kann nicht rückgängig gemacht werden." },
    { "WE_RESET_CONFIRM",  "Reset everything",       "Alles zurücksetzen" },
    { "WE_RESET_CANCEL",   "Cancel",                 "Abbrechen" },

    //_ ws_debug_window.cpp - WS connection log viewer. TX/RX log-direction tags stay as-is as a fixed protocol vocabulary.
    { "WE_WSDEBUG_TITLE",             "World Events - WS Debug Log", "World Events - WS Debug Log" },
    { "WE_WSDEBUG_CONNECTED",         "Connected",                   "Verbunden" },
    { "WE_WSDEBUG_CONNECTING",        "Connecting...",               "Verbinde..." },
    { "WE_WSDEBUG_DISCONNECTED",      "Disconnected",                "Getrennt" },
    { "WE_WSDEBUG_SERVER",            "Server:",                     "Server:" },
    { "WE_WSDEBUG_RESETS_ON_RELOAD",
        //_ en
        "This window resets on reload - see Nexus's log (WorldEvents-WS) too.",
        //_ de
        "Dieses Fenster wird beim Neuladen zurückgesetzt – siehe auch Nexus’ "
        "Log (WorldEvents-WS)." },
    { "WE_WSDEBUG_AUTOSCROLL",        "Auto-scroll",                  "Auto scrollen" },
    { "WE_WSDEBUG_CLEAR",             "Clear",                        "Leeren" },
    { "WE_WSDEBUG_FILTER_ALL",        "All",                          "Alle" },
    { "WE_WSDEBUG_FILTER_INFO",       "Info",                         "Info" },
    { "WE_WSDEBUG_FILTER_ERROR",      "Error",                        "Fehler" },
    { "WE_WSDEBUG_SEARCH_HINT",       "Search text...",               "Durchsuche Text..." },
    { "WE_WSDEBUG_COL_ELAPSED",       "T+",                           "T+" },
    { "WE_WSDEBUG_COL_DIR",           "Dir",                          "Ordner" },
    { "WE_WSDEBUG_COL_MESSAGE",       "Message",                      "Nachricht" },

    //_ addon_options.cpp - the Nexus options panel. Only the generic settings.
    { "WE_OPT_RELEASE",                     "Release",                                       "Version" },
    { "WE_OPT_DISABLE_COMPETITIVE",         "Disable overlay in PvP/WvW",                    "Overlay in PvP/WvW deaktivieren" },
    { "WE_OPT_WINDOW",                      "Window",                                        "Fenster" },
    { "WE_OPT_TOAST",                       "Toast",                                         "Benachrichtigung" },
    { "WE_OPT_BAR",                         "Bar",                                           "Leiste" },
    { "WE_OPT_OVERLAY_SETTINGS",            "Overlay Settings",                              "Overlay-Einstellungen" },
    { "WE_OPT_SHOW_SUBS_WINDOW",            "Show subscriptions window",                     "Abo-Fenster anzeigen" },
    { "WE_OPT_HIDE_ACTIVE_IN_WINDOW",       "Hide active in window",                         "Aktive Events im Fenster ausblenden" },
    { "WE_OPT_ACTIVE",                      "Active",                                        "Aktiv" },
    { "WE_OPT_SOON",                        "Soon",                                          "Bald" },
    { "WE_OPT_ENABLE_NOTIFY_POPUPS",        "Enable notification popups",                    "Benachrichtigungs-Popups aktivieren" },
    { "WE_OPT_WARN_BEFORE_START",           "Warn before start (min)",                       "Warnung vor Start (Min.)" },
    { "WE_OPT_NOTIFY_ON_START",             "Notify on start",                               "Start Benachrichtigung" },
    { "WE_OPT_POPUP_DURATION",              "Popup duration (sec)",                          "Popup-Dauer (Sek.)" },
    { "WE_OPT_SOUND_NONE",                  "(none)",                                        "(keiner)" },
    { "WE_OPT_SOUND",                       "Sound",                                         "Ton" },
    { "WE_OPT_RESCAN",                      "Rescan",                                        "Neu einlesen" },
    { "WE_OPT_TEST",                        "Test",                                          "Testen" },
    { "WE_OPT_SHOW_SUBS_BAR",               "Show subscriptions bar",                        "Abo-Leiste anzeigen" },
    { "WE_OPT_HIDE_ACTIVE_ON_BAR",          "Hide active on bar",                            "Aktive auf Leiste ausblenden" },
    { "WE_OPT_MINIMAL_MODE",                "Minimal Mode",                                  "Minimalmodus" },
    { "WE_OPT_BOTTOM_LINE",                 "Bottom Line",                                   "Untere Linie" },
    { "WE_OPT_DOT_COLOR",                   "Dot Color",                                     "Punktfarbe" },
    { "WE_OPT_POPOUT_HEIGHT",               "Pop-out height (px)",                           "Ausklapphöhe (px)" },
    { "WE_OPT_POPOUT_DELAY",                "Pop-out delay (ms)",                            "Ausklappverzögerung (ms)" },
    { "WE_OPT_UNSAFE_ZONE",                 "Unsafe zone",                                   "Unsichere Zone" },
    { "WE_OPT_LEFT",                        "Left",                                          "Links" },
    { "WE_OPT_RIGHT",                       "Right",                                         "Rechts" },
    { "WE_OPT_HEIGHT",                      "Height",                                        "Höhe" },
    { "WE_OPT_EVENTS_SETTINGS_HEADER",      "Events Settings (Basic|Cyclic)",                "Event-Einstellungen (Normal|Zyklisch)" },
    { "WE_OPT_CHAT_SETTINGS",               "Chat settings:",                                "Chat-Einstellungen:" },
    { "WE_OPT_PASTE_DELAY",                 "Paste Delay",                                   "Einfügeverzögerung" },
    { "WE_OPT_PASTE_TO",                    "Paste to",                                      "Einfügen in" },
    { "WE_OPT_BETTER_CHAT_NOT_LOADED",      "Better Chat not loaded (optional)",             "Better Chat nicht geladen (optional)" },
    { "WE_OPT_BETTER_CHAT_SELF_DISABLED",   "Better Chat loaded, /self disabled",            "Better Chat geladen, /self deaktiviert" },
    { "WE_OPT_BETTER_CHAT_SELF_ENABLED",    "Better Chat loaded, /self enabled",             "Better Chat geladen, /self aktiviert" },
    { "WE_OPT_GROW_MARKERS_ZOOM",           "Grow markers when zooming in",                  "Markierungen beim Hineinzoomen vergrößern" },
    { "WE_OPT_START_GROWING_AT",            "Start growing at",                              "Beginn Vergrößerung bei" },
    { "WE_OPT_MAX_SIZE_AT_ZOOM",            "Max size at 100% zoom",                         "Maximalgröße bei 100% Zoom" },
    { "WE_OPT_GW2_API_KEY",                 "GW2 API key",                                   "GW2-API-Schlüssel" },
    { "WE_OPT_API_KEY_DELAY_NOTE",          "Can take up to 5min to take effect.",           "Kann bis zu 5 Min. dauern, bis Änderungen sichtbar sind." },
    { "WE_OPT_API_NO_KEY",                  "No key set",                                    "Kein API-Schlüssel gesetzt" },
    { "WE_OPT_API_CHECKING",                "Checking...",                                   "Wird geprüft..." },
    { "WE_OPT_API_CONNECTED",               "Connected",                                     "Verbunden" },
    { "WE_OPT_API_INVALID_KEY",             "Invalid key / missing permission",              "Ungültiger Schlüssel / fehlende Berechtigung" },
    { "WE_OPT_API_NETWORK_ERROR",           "Network error, retrying",                       "Netzwerkfehler, erneuter Versuch" },
    { "WE_OPT_AUTO_MARK_API_DONE",          "Automatically mark API-confirmed events done",  "API-bestätigte Events automatisch als erledigt markieren" },
    { "WE_OPT_AUTO_TRACK_VAULT",            "Auto-track weekly Wizard's Vault targets",      "Wöchentliche Gewölbe des Zauberers-Ziele automatisch verfolgen" },
    { "WE_OPT_WEEKLY_COLOR",                "Weekly Color",                                  "Wochenfarbe" },
    { "WE_OPT_CLEAR_DONE_MARKERS",          "Clear events manually marked done",             "Manuell als erledigt markierte Events zurücksetzen" },
    { "WE_OPT_ONLY_SHOW_STARTING_IN",       "Only show events starting in",                  "Nur Events zeigen, die in X beginnen." },
    { "WE_OPT_WAITING",                     "Waiting",                                       "Pause" },
    { "WE_OPT_DOT_RADIUS",                  "Dot radius",                                    "Punktradius" },
    { "WE_OPT_ICON_SIZE",                   "Icon size",                                     "Symbolgröße" },
    { "WE_OPT_SHOW_CYCLIC_ON_MAP",          "Show cyclic events on map",                     "Zyklische Events auf der Karte anzeigen" },
    { "WE_OPT_RING_APPEARANCE",             "Ring appearance",                               "Ring-Erscheinungsbild" },
    { "WE_OPT_RADIUS",                      "Radius",                                        "Radius" },
    { "WE_OPT_THICKNESS",                   "Thickness",                                     "Dicke" },
    { "WE_OPT_ENTRY_EXIT_WINDOW",           "Entry / exit window",                           "Ein-/Austrittsfenster" },
    { "WE_OPT_FUTURE_WINDOW",               "Future window",                                 "Zukunftsfenster" },
    { "WE_OPT_PAST_WINDOW",                 "Past window",                                   "Vergangenheitsfenster" },
    { "WE_OPT_FADE_PAST_EVENTS",            "Fade past events",                              "Vergangenes langsam ausblenden" },
    { "WE_OPT_HAND",                        "Hand",                                          "Zeiger" },
    { "WE_OPT_COLOR",                       "Color",                                         "Farbe" },
    { "WE_OPT_USE_IMAGE",                   "Use texture",                                   "Textur verwenden" },
    { "WE_OPT_NONE",                        "None",                                          "Keine" },
    { "WE_OPT_WIDTH",                       "Width",                                         "Breite" },
    { "WE_OPT_RING_EDGE_IMAGE",             "Ring edge texture",                             "Ringkanten-Textur" },
    { "WE_OPT_IMAGE",                       "Texture",                                       "Textur" },
    { "WE_OPT_OFFSET",                      "Offset",                                        "Versatz" },
    { "WE_OPT_FILL_TEXTURE",                "Fill texture",                                  "Füll-Textur" },
    { "WE_OPT_OPACITY",                     "Opacity",                                       "Deckkraft" },
};

inline constexpr int kLocalizationCount = sizeof(kLocalizationTable) / sizeof(kLocalizationTable[0]);