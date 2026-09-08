//################################################################################
// localization_table.h
//--------------------------------------------------------------------------------
// WE_LANGUAGE_LIST      every language World Events ships text for: field name +
//                       Nexus code, in the order each row's fields follow it
// LocalizationEntry     one row of the table: an identifier plus one named field
//                       per WE_LANGUAGE_LIST entry (see below)
// kLanguageSlots        WE_LANGUAGE_LIST as a runtime array of {code, field}
// kLanguageCount        number of entries in kLanguageSlots
// kLocalizationTable    every user-facing string World Events translates, one
//                       row per identifier
// kLocalizationCount    number of rows in kLocalizationTable
//--------------------------------------------------------------------------------
// One flat table for the whole addon: Localization_Load (localization.cpp) walks
// it once at AddonLoad, and a single file is easier to keep languages in sync.
// This file is the only place touched to add a language: add a line to
// WE_LANGUAGE_LIST, then add the matching string to every row below (same
// position as the new line). LocalizationEntry and kLanguageSlots are both
// generated from WE_LANGUAGE_LIST, so the field and its Nexus code can't drift
// out of sync with each other; localization.h/.cpp read kLanguageSlots
// generically and need no changes.
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

#include <cstddef>

//_ Nexus language codes (see Nexus-Translations on GitHub). First entry is the default and fallback language.
#define WE_LANGUAGE_LIST \
    WE_LANG(English, "en") \
    WE_LANG(German,  "de")

//********************************************************************************
// LocalizationEntry
//--------------------------------------------------------------------------------
// Identifier     World Events' own key, passed to Tr()
// (per-language) one field per WE_LANGUAGE_LIST entry, same order; the first
//                (English) is also the fallback for any other Nexus language
//--------------------------------------------------------------------------------
struct LocalizationEntry
{
    const char* Identifier;
#define WE_LANG(aName, aCode) const char* aName;
    WE_LANGUAGE_LIST
#undef WE_LANG
};

//********************************************************************************
// LanguageSlot / kLanguageSlots
//--------------------------------------------------------------------------------
// WE_LANGUAGE_LIST as data: pairs each Nexus language code with the
// LocalizationEntry field that holds that language's text, so localization.cpp
// can loop over every language without knowing their field names.
//--------------------------------------------------------------------------------
struct LanguageSlot
{
    const char* Code;
    const char* LocalizationEntry::* Field;
};

static constexpr LanguageSlot kLanguageSlots[] = {
#define WE_LANG(aName, aCode) { aCode, &LocalizationEntry::aName },
    WE_LANGUAGE_LIST
#undef WE_LANG
};
static constexpr size_t kLanguageCount = sizeof(kLanguageSlots) / sizeof(kLanguageSlots[0]);

static constexpr LocalizationEntry kLocalizationTable[] = {

    //_ changelog_window.cpp - "What's New" popup shown after every build
    { "WE_CHANGELOG_TITLE",
        "World Events - What's New",
        "World Events - Was ist neu" },

    { "WE_CHANGELOG_LATEST_SUFFIX",
        " (latest)",
        " (neueste)" },

    { "WE_CHANGELOG_GOT_IT",
        "Got it",
        "Geht klar" },

    //_ reset_defaults.cpp - "Default" button + confirmation popup in the options panel.
    { "WE_RESET_BUTTON",
        "Default",
        "Standard" },

    { "WE_RESET_POPUP_TITLE",
        "Reset to Defaults",
        "Auf Standard zurücksetzen" },

    { "WE_RESET_BODY",
        "Deletes and rebuilds events.json from scratch.",
        "Löscht events.json und erstellt sie mit Grundeinstellungen." },

    { "WE_RESET_WARNING",
        "This cannot be undone.",
        "Dies kann nicht rückgängig gemacht werden." },

    { "WE_RESET_CONFIRM",
        "Reset everything",
        "Alles zurücksetzen" },

    { "WE_RESET_CANCEL",
        "Cancel",
        "Abbrechen" },

    { "WE_TIP_RESET_WIPE",
        "Wipes every Basic Event, Cyclic Group, category, subscription,\n"
        "and done-today marker, restoring the compiled-in defaults.",
        "Löscht alle normalen Events, zyklische Gruppen, Kategorien, Abos und\n"
        "Erledigt-Markierungen und stellt die Grundeinstellungen wieder her." },


    //_ subscriptions_window.cpp / subscriptions_notification.cpp - shared watchlist/toast tooltip.
    { "WE_TIP_WEEKLY_VAULT",
        "Counts toward this week's Wizard's Vault objectives.",
        "Zählt zu den Gewölbe des Zauberers-Zielen dieser Woche." },


    //_ subscriptions_window.cpp / subscriptions_bar.cpp / subscriptions_notification.cpp / addon_options_helpers.cpp - shared right-click menu entries.
    { "WE_SUBS_MARK_DONE_TODAY",
        "Mark done for today",
        "Heute als erledigt markieren" },

    { "WE_SUBS_EDIT_SUBSCRIPTIONS",
        "Edit Subscriptions",
        "Abos bearbeiten" },


    //_ Title bar text for the addon's three player-facing windows. Drawn via TrId()
    //  alongside a stable, untranslated ImGui ID suffix (see kSubscriptionsWindowId/
    //  kLiveEventReportsWindowId/kEditSubscriptionsWindowId) so window position/size
    //  persistence and Nexus's GUI_RegisterCloseOnEscape lookup survive a language change.
    { "WE_SUBS_WINDOW_TITLE",
        "World Events - Subscriptions",
        "World Events - Abos" },

    { "WE_LIVE_REPORTS_WINDOW_TITLE",
        "World Events - Live Reports",
        "World Events - Live-Meldungen" },

    { "WE_EDIT_SUBS_WINDOW_TITLE",
        "World Events - Edit Subscriptions",
        "World Events - Abos bearbeiten" },


    //_ subscriptions_window.cpp - watchlist empty states and row hints.
    { "WE_SUBS_EMPTY_ACTIVE",
        "Nothing upcoming - everything subscribed is currently active.",
        "Nichts Bevorstehendes - alles Abonnierte ist derzeit aktiv." },

    { "WE_SUBS_EMPTY_DONE",
        "Nothing to show - everything subscribed is already done today.",
        "Nichts anzuzeigen - alles Abonnierte ist heute bereits erledigt." },

    { "WE_SUBS_EMPTY_NONE",
        "No subscribed events yet. Check the box next to an event's name in\n"
        "the options panel or Edit Subscriptions window to add it here.",
        "Noch keine abonnierten Events. Aktiviere das Kästchen neben einem\n"
        "Event-Namen im Optionsfenster oder im Fenster \"Abos bearbeiten\",\n"
        "um es hier hinzuzufügen." },

    { "WE_SUBS_HINT_CLICK",
        "Click a row to copy its waypoint code into the chat.",
        "Klicke eine Zeile an, um deren Wegpunkt-Code in den Chat zu kopieren." },

    { "WE_SUBS_HINT_RIGHT_CLICK",
        "Right-click a row to mark it done for today.",
        "Rechtsklick auf eine Zeile, um sie als heute erledigt zu markieren." },


    //_ subscriptions_edit_window.cpp - Basic & Cyclic tab (shares WE_OPT_SEARCH_LABEL/WE_OPT_BASIC_EVENTS/WE_OPT_CYCLIC_EVENTS with addon_options.cpp).
    { "WE_EDIT_TAB_BASIC_CYCLIC",
        "Basic & Cyclic",
        "Normal & Zyklisch" },

    { "WE_EDIT_SUBSCRIBE_ALL",
        "Subscribe all",
        "Alle abonnieren" },

    { "WE_EDIT_DONE_TODAY",
        "Done for today",
        "Heute erledigt" },


    //_ subscriptions_edit_window.cpp - Live Events tab. WE_LIVE_* rows are shared with addon_options.cpp's Live Events
    //  (Experimental) section once that's localized - same checkbox/tooltip/empty-state there.
    { "WE_EDIT_TAB_LIVE",
        "Live Events",
        "Live-Events" },

    { "WE_LIVE_SHARE_NAME_REPORTS",
        "Share my name in reports",
        "Meinen Namen in Meldungen teilen" },

    { "WE_LIVE_SHARE_NAME_REPORTS_TIP",
        "Off (default): reports are anonymous. On: your character name\n"
        "goes out with every report you send, and anyone whose toast\n"
        "notification it triggers can whisper you directly by clicking\n"
        "it, instead of just pasting the waypoint.",
        "Aus (Standard): Meldungen sind anonym. An: Dein Charaktername\n"
        "wird bei jeder gesendeten Meldung mitgeschickt, und wer die\n"
        "dadurch ausgelöste Benachrichtigung erhält, kann dich durch\n"
        "Anklicken direkt anflüstern, statt nur den Wegpunkt einzufügen." },

    { "WE_LIVE_NONE_COMPILED",
        "No live events compiled in yet.",
        "Noch keine Live-Events kompiliert." },

    { "WE_EDIT_LIVE_SUBSCRIBE_TIP_NO_KEY",
        "Requires a GW2 API key (options panel) - region-wide toast\n"
        "delivery needs it to tell NA and EU apart.",
        "Erfordert einen GW2-API-Schlüssel (Optionsfenster) - die\n"
        "regionsweite Zustellung braucht ihn, um NA und EU zu unterscheiden." },

    { "WE_EDIT_LIVE_SUBSCRIBE_TIP",
        "Subscribe to region-wide toast notifications for this event,\n"
        "regardless of which map you're currently on.",
        "Abonniere regionsweite Benachrichtigungen für dieses Event,\n"
        "unabhängig davon, auf welcher Karte du dich gerade befindest." },

    { "WE_EDIT_LIVE_NAMED_ONLY_TIP",
        "Only notify me when the reporter shared their name.\n"
        "An unnamed report can't be whispered or joined directly,\n"
        "so skip its toast rather than show one you can't act on.",
        "Nur benachrichtigen, wenn der Meldende seinen Namen geteilt hat.\n"
        "Eine anonyme Meldung kann nicht direkt angeflüstert oder\n"
        "verfolgt werden, daher entfällt ihre Benachrichtigung, statt\n"
        "eine zu zeigen, auf die du nicht reagieren kannst." },

    { "WE_EDIT_LIVE_DONE_TIP",
        "Done for today - mutes toasts for this event until the daily reset.",
        "Heute erledigt - stummt Benachrichtigungen für dieses Event bis zum täglichen Reset." },

    { "WE_EDIT_LIVE_COL_EVENT",
        "Event",
        "Event" },

    { "WE_EDIT_LIVE_COL_ONLY_NAMED",
        "Only named",
        "Nur benannt" },

    { "WE_EDIT_LIVE_COL_DONE_TODAY",
        "Done today",
        "Heute erledigt" },


    //_ addon_options.cpp - tooltips for the generic settings (Live Events tooltips live with events_live, once translated).
    { "WE_TIP_DISABLE_COMPETITIVE",
        "Hides map events, cyclic rings, and all subscriptions\n"
        "views (window/bar/toast) while you're on a PvP or WvW\n"
        "map. Doesn't change what's subscribed, only what shows.",
        "Blendet Karten-Events, Zyklische-Ringe und alle Abo-Ansichten\n"
        "(Fenster/Leiste/Toast) auf PvP- oder WvW-Karten aus.\n"
        "Ändert nichts an den Abos, nur was angezeigt wird." },

    { "WE_TIP_NOTIFY_POPUPS",
        "Pops up a small toast in the lower-right corner for events\n"
        "you have notifications enabled for, whether or not the\n"
        "window or distribution line are open. Click a popup to paste\n"
        "its waypoint code, same as clicking a row/segment there.",
        "Zeigt unten rechts eine kleine Benachrichtigung für Events,\n"
        "für welche Benachrichtigungen aktiviert sind - unabhängig davon,\n"
        "ob Fenster oder Leiste geöffnet sind. Klicke auf ein Popup oder eine\n"
        "Zeile um den Wegpunkt im Chat einzufügen." },

    { "WE_TIP_WARN_BEFORE_START",
        "How long before a subscribed event/slot starts to\n"
        "fire the \"starting soon\" popup. 0 disables it.",
        "Wie lange vor Start eines abonnierten Events/Slots die\n"
        "\"Startet bald\"-Meldung erscheint. 0 deaktiviert sie." },

    { "WE_TIP_POPUP_DURATION",
        "How long a popup stays fully visible before it fades out.\n"
        "Hovering a popup pauses its timer.",
        "Wie lange eine Benachrichtigung voll sichtbar bleibt, bevor es ausblendet.\n"
        "Der Mauszeiger über einer Benachrichtigung pausiert dessen Timer." },

    { "WE_TIP_RESCAN_SOUNDS",
        "Re-scans \"<addon dir>/sounds\" for .wav files you've\n"
        "dropped in since the dropdown was last built.",
        "Durchsucht \"<Addon-Ordner>/sounds\" erneut nach .wav-Dateien,\n"
        "die seit dem letzten Aufbau der Liste hinzugekommen sind." },

    { "WE_TIP_TEST_SOUND",
        "Drop .wav files into \"<addon dir>/sounds\" and pick one\n"
        "here to preview it. Only .wav is supported. \"Test\" just\n"
        "plays it immediately - it also plays automatically alongside\n"
        "a real notification popup, but only for events/slots whose\n"
        "own notify level has sound enabled (see the speaker icon on\n"
        "each row below).",
        "Lege .wav-Dateien in \"<Addon-Ordner>/sounds\" ab und wähle\n"
        "eine zum Vorhören aus. Nur .wav wird unterstützt. \"Testen\"\n"
        "spielt ihn sofort ab - er spielt auch automatisch bei einer\n"
        "echten Benachrichtigung, aber nur für Events/Slots, deren eigene\n"
        "Benachrichtigungsstufe Ton aktiviert hat (siehe Lautsprecher-\n"
        "Symbol bei jeder Zeile unten)." },

    { "WE_TIP_HIDE_ACTIVE_ON_BAR",
        "Active segments are dropped from the bar entirely, only upcoming events\n"
        "remain. Independent from \"Hide active in window\" above.",
        "Aktive Segmente werden vollständig von der Leiste entfernt, nur bevorstehende\n"
        "Events bleiben. Unabhängig von \"Aktive im Fenster ausblenden\"\noben." },

    { "WE_TIP_POPOUT_HEIGHT",
        "How tall the pop-out is, in px.",
        "Wie hoch das hervorstehende Segment ist, in px." },

    { "WE_TIP_POPOUT_DELAY",
        "How long the mouse has to sit still over a segment or dot\n"
        "before it pops out. 0 = instant.",
        "Wie lange die Maus über einem Segment oder Punkt verweilen\n"
        "muss, bevor dieses ausklappt. 0 = sofort." },

    { "WE_TIP_UNSAFE_LEFT",
        "Width from the LEFT screen edge, in px, treated as\n"
        "covered by your own GW2 UI. Segments in this zone\n"
        "drop lower to avoid covering it.",
        "Breite ab dem LINKEN Bildschirmrand, in px, die als von\n"
        "deinem eigenen GW2-UI belegt gilt. Segmente in\n"
        "dieser Zone weichen nach unten aus." },

    { "WE_TIP_UNSAFE_RIGHT",
        "Width from the RIGHT screen edge, in px, treated as\n"
        "covered by your own GW2 UI.Segmentsin this zone\n"
        "drop lower to avoid covering it.",
        "Breite ab dem RECHTEN Bildschirmrand, in px, die als von\n"
        "deinem eigenen GW2-UI belegt gilt. Segmente in\n"
        "dieser Zone weichen nach unten aus." },

    { "WE_TIP_UNSAFE_HEIGHT",
        "How tall your corner UI is, in px. Segments inside\n"
        "either unsafe zone start their drop this far down,\n"
        "not from the line itself, so the popped-out block\n"
        "clears your UI.",
        "Wie hoch dein UI an den Seiten ist, in px. Segmente in einer\n"
        "der beiden Zonen beeginne ab dieser Höhe auszuklappen,\n"
        "nicht ab der Linie selbst, damit der ausgeklappte Block\n"
        "dein UI nicht überdeckt." },

    { "WE_TIP_UNLOCK_PASTE_DELAY",
        "Only change this if you're having paste issues.\n"
        "Controls the internal delay used to paste text into the\n"
        "chat box. Default: 20ms.",
        "Nur ändern, wenn es beim Einfügen Probleme gibt.\n"
        "Steuert die interne Verzögerung beim Einfügen von Text\n"
        "ins Chatfeld. Standard: 20ms." },

    { "WE_TIP_PASTE_TO",
        "Which chat channel a watchlist row/segment/toast click\n"
        "pastes into, regardless of whatever channel is currently\n"
        "selected in-game. Prepends that channel's slash command\n"
        "(e.g. \"/p \") before the name/waypoint. \"Current chat\"\n"
        "pastes exactly as before, into whichever channel already\n"
        "has focus.\n\n"
        "When Better Chat's \"/self\" is enabled, it's available here too.",
        "In welchen Chat-Kanal ein Klick auf eine Zeile, Segment oder\n"
        "Popup einfügt, unabhängig vom aktuell im Spiel gewählten\n"
        "Kanal. Stellt den Slash-Befehl des Kanals voran (z.B. \"/p \")\n"
        "vor Name/Wegpunkt. \"Aktueller Chat\" fügt wie gewohnt in den\n"
        "gerade fokussierten Kanal ein.\n\n"
        "Wenn Better Chats \"/self\" aktiviert ist, steht es auch hier\n"
        "zur Verfügung." },

    { "WE_TIP_GW2_API_KEY",
        "Needs the \"progression\" permission. When set, a subscribed\n"
        "Core Boss (Admiral Taidha Covington, Tequatl, etc.) is\n"
        "automatically left off the watchlist window and bar once\n"
        "your account has already killed it since the last daily\n"
        "reset. The same applies to any subscribed slot in Verdant\n"
        "Brink, Auric Basin, Tangled Depths, Dragon's Stand, Crystal\n"
        "Oasis, Elon Riverlands, The Desolation, or Domain of Vabbi,\n"
        "once that map's Hero's Choice Chest has already been\n"
        "claimed today (the whole ring hides together, not just the\n"
        "one slot). Nothing else is affected: the public API has no\n"
        "\"already done today\" signal for any other event type in\n"
        "this addon (other map metas, invasions, LLA, convergences),\n"
        "so those are never hidden by this.",
        //_ REVIEW: progression also progression in German?
        "Benötigt die Berechtigung \"progression\". Wenn gesetzt, wird\n"
        "ein abonnierter Core Boss (Admiral Taidha Covington, Tequatl\n"
        "usw.) automatisch aus Abo-Fenster und -Leiste entfernt, sobald\n"
        "dein Account ihn seit dem letzten täglichen Reset schon\n"
        "besiegt hat. Gleiches gilt für jeden abonnierten Slot in\n"
        "Grasgrüne Schwelle, Güldener Talkessel, Verschlungene Tiefen,\n"
        "Widerstand des Drachen, Kristalloase, Elon-Flusslande, Das\n"
        "Ödland oder Domäne Vaabi, sobald die tägliche Belohnungstruhe\n"
        "dieser Karte heute schon errungen wurde (der ganze Zyklus wird\n"
        "als Gesamtes ausgeblendet, nicht nur der eine Slot). Sonst\n"
        "ändert sich nichts: Die öffentliche API hat für keinen anderen\n"
        "Event-Typ in diesem Addon (andere Karten-Metas, Invasionen,\n"
        "LLA, Konvergenzen) ein \"heute schon erledigt\"-Signal, daher\n"
        "wird dort nie etwas dadurch ausgeblendet." },

    { "WE_TIP_AUTO_MARK_API_DONE",
        "When on (default), any Basic Event/Cyclic group tagged\n"
        "(auto) in the lists below auto-hides once the GW2 API\n"
        "reports it done for the day. Turn this off to ignore\n"
        "that signal and rely only on marking events done for\n"
        "today yourself (right-click a row/segment/popup).",
        "Wenn aktiv (Standard), blenden mit (auto) markierte normale\n"
        "Events/zyklische Gruppen unten automatisch aus, sobald die\n"
        "GW2-API sie als heute erledigt meldet. Deaktivieren, um\n"
        "dieses Signal zu ignorieren und Events nur manuell als erledigt\n"
        "zu markieren (Rechtsklick auf Zeile/Segment/Popup)." },

    { "WE_TIP_AUTO_TRACK_VAULT",
        "When on (default), the subscriptions window, distribution\n"
        "line, and notification popups automatically surface any\n"
        "Basic Event/Cyclic slot that's an active, incomplete\n"
        "target of this week's Wizard's Vault rotation, even without\n"
        "a manual subscription, marked with a small red dot/border.\n"
        "Turn this off to see only what you've manually subscribed to\n"
        "in all three views.",
        "Wenn aktiv (Standard), zeigen Abo-Fenster, Leiste und\n"
        "Benachrichtigungen automatisch jedes normale Event/zyklischen\n"
        "Slot, der ein aktives, unerledigtes Ziel der Gewölbe des\n"
        "Zauberers-Rotation dieser Woche ist, auch ohne manuelles Abo,\n"
        "markiert mit einem kleinen roten Punkt/Rahmen. Deaktivieren,\n"
        "um in allen drei Ansichten nur manuell Abonniertes zu sehen." },

    { "WE_TIP_UNLOCK_DONE_MARKERS",
        "Right-click any row in the watchlist window, segment on the\n"
        "distribution line, or notification popup to mark it done for\n"
        "today. It then hides from all three views, the same way an\n"
        "API-confirmed Core Boss kill or map chest claim does, until\n"
        "the next daily reset (00:00 UTC) - or until you clear it\n"
        "manually.",
        "Rechtsklick auf eine Zeile im Abo-Fenster, ein Segment auf\n"
        "der Leiste oder ein Benachrichtigungs-Popup um es als für\n"
        "heute erledigt zu markieren. Es verschwindet dann aus allen\n"
        "drei Ansichten, genau wie bei einem API-bestätigten Core-\n"
        "Boss-Kill oder Kistenerhalt, bis zum nächsten täglichen\n"
        "Reset (00:00 UTC) - oder bis du es manuell zurücksetzt." },

    { "WE_TIP_FUTURE_WINDOW",
        "How far ahead an upcoming event starts fading into view.\n"
        "Measured in degrees of the ring.",
        "Wie weit im Voraus ein bevorstehendes Event einzublenden\n"
        "beginnt. Gemessen in Grad des Rings." },

    { "WE_TIP_PAST_WINDOW",
        "How long a finished event lingers before fading out.\n"
        "Measured in degrees of the ring.",
        "Wie lange ein beendetes Event sichtbar bleibt, bevor es\n"
        "ausblendet. Gemessen in Grad des Rings." },

    { "WE_TIP_FADE_PAST_EVENTS",
        "Fades the past window from full opacity at the hand\n"
        "down to transparent at its far edge. Turn off to keep\n"
        "it solid across the whole past window.",
        "Blendet das Vergangenheitsfenster von voller Deckkraft am\n"
        "Zeiger bis transparent am äußeren Rand aus. Deaktivieren, um\n"
        "es über das gesamte Fenster hinweg deckend zu halten." },

    { "WE_TIP_HAND_COLOR",
        "Color of the fixed \"now\" hand at the top of every ring.\n"
        "Also tints the hand texture below, if enabled.",
        "Farbe des fixen \"Jetzt\"-Zeigers oben an jedem Ring.\n"
        "Färbt bei Aktivierung auch das Zeiger-Textur unten ein." },

    { "WE_TIP_HAND_USE_TEXTURE",
        "Draws a texture in place of the plain hand tick. Like the\n"
        "Basic Event icons, the source texture's RGB should be a\n"
        "neutral gray with the shape in the alpha channel.",
        "Zeichnet eine Textur anstelle des schlichten Zeiger-Strichs.\n"
        "Wie bei den Symbolen für normale Events sollte das RGB der\n"
        "Textur neutrales Grau sein, mit der Form im Alphakanal." },

    { "WE_TIP_HAND_TEXTURE_WIDTH",
        "Length isn't separately adjustable - the testure always\n"
        "spans exactly from the ring's inner edge to its outer\n"
        "edge, stretching automatically with Radius/Thickness.",
        "Die Länge ist nicht separat einstellbar - die Textur reicht\n"
        "immer exakt vom inneren bis zum äußeren Rand des Rings und\n"
        "skaliert automatisch mit Radius/Dicke mit." },

    { "WE_TIP_RING_EDGE_TEXTURE",
        "Wraps a texture around the ring's edge(s) in place of a\n"
        "plain line. The texture should be wider than it is tall - it\n"
        "is stretched around exactly the portion of the circle\n"
        "shown by the Future/Past window above, and rescales with\n"
        "Radius/Thickness. Drop a .png/.jpg into this addon's\n"
        "\"textures\" folder to make it available below.",
        "Legt eine Textur um die Ringkante(n) statt einer schlichten\n"
        "Linie. Die Textur sollte breiter als hoch sein - es wird exakt\n"
        "über den durch das Zukunfts-/Vergangenheitsfenster oben\n"
        "festgelegten Kreisabschnitt gespannt und skaliert mit Radius/\n"
        "Dicke. Eine .png/.jpg in den \"textures\"-Ordner dieses Addons\n"
        "legen, um sie unten verfügbar zu machen." },

    { "WE_TIP_RING_TEXTURE_THICKNESS",
        "On-screen thickness of the texture band, centered on the\n"
        "edge it's drawn on. This is independent of the source\n"
        "texture file's own pixel height - the texture is always\n"
        "stretched to fill this value, so swapping in a\n"
        "shorter/taller source PNG has no effect on its own;\n"
        "drag this down to make the band thinner.",
        "Sichtbare Dicke des Texturbands, zentriert auf der Kante, auf\n"
        "der es liegt. Unabhängig von der Pixelhöhe der Quelldatei - die\n"
        "Textur wird immer auf diesen Wert gestreckt, ein kürzeres/\n"
        "höheres PNG allein ändert also nichts; für ein schmaleres Band\n"
        "den Wert verringern." },

    { "WE_TIP_RING_TEXTURE_OFFSET",
        "Nudges both copies radially outward from the ring's own\n"
        "fill - the outer copy moves further out, the inner copy\n"
        "further in - so they stay mirror-symmetric.\n"
        "Negative values pull both back in toward the ring instead.",
        "Verschiebt beide Kopien radial vom eigentlichen Ring weg.\n"
        "Die äußere Kopie weiter nach außen, die innere weiter\n"
        "nach innen, sodass sie spiegelsymmetrisch bleiben.\n"
        "Negative Werte ziehen beide zurück zum Ring hin." },

    { "WE_TIP_FILL_TEXTURE",
        "Lays a texture over the ring's own plain-color fill (the\n"
        "track and slot arcs) to break it up with some texture or\n"
        "grain.",
        "Legt eine Textur über die einfarbige Füllung des Rings (Bahn-\n"
        "und Slot-Bögen), um sie mit etwas Textur oder Körnung\n"
        "aufzulockern." },


    //_ maprender.cpp / cyclicrender.cpp - shared map-hover status format strings for Basic/Cyclic events.
    { "WE_TIP_MAP_ACTIVE_FMT",
        "%s - Active (ends in %s)",
        "%s - Aktiv (endet in %s)" },

    { "WE_TIP_MAP_UPCOMING_FMT",
        "%s - in %s",
        "%s - in %s" },


    //_ ws_debug_window.cpp - WS connection log viewer. TX/RX log-direction tags stay as-is as a fixed protocol vocabulary.
    { "WE_WSDEBUG_TITLE",
        "World Events - WS Debug Log",
        "World Events - WS Debug Log" },

    { "WE_WSDEBUG_CONNECTED",
        "Connected",
        "Verbunden" },

    { "WE_WSDEBUG_CONNECTING",
        "Connecting...",
        "Verbinde..." },

    { "WE_WSDEBUG_DISCONNECTED",
        "Disconnected",
        "Getrennt" },

    { "WE_WSDEBUG_SERVER",
        "Server:",
        "Server:" },

    { "WE_WSDEBUG_RESETS_ON_RELOAD",
        "This window resets on reload - see Nexus's log (WorldEvents-WS) too.",
        "Dieses Fenster wird beim Neuladen zurückgesetzt – siehe auch Nexus' Log (WorldEvents-WS)." },

    { "WE_WSDEBUG_AUTOSCROLL",
        "Auto-scroll",
        "Auto scrollen" },

    { "WE_WSDEBUG_CLEAR",
        "Clear",
        "Leeren" },

    { "WE_WSDEBUG_FILTER_ALL",
        "All",
        "Alle" },

    { "WE_WSDEBUG_FILTER_INFO",
        "Info",
        "Info" },

    { "WE_WSDEBUG_FILTER_ERROR",
        "Error",
        "Fehler" },

    { "WE_WSDEBUG_SEARCH_HINT",
        "Search text...",
        "Durchsuche Text..." },

    { "WE_WSDEBUG_COL_ELAPSED",
        "T+",
        "T+" },

    { "WE_WSDEBUG_COL_DIR",
        "Dir",
        "Ordner" },

    { "WE_WSDEBUG_COL_MESSAGE",
        "Message",
        "Nachricht" },


    //_ addon_options_helpers.cpp / subscriptions_edit_window.cpp - fallback label for an empty user-entered name.
    { "WE_UNNAMED",
        "(unnamed)",
        "(unbenannt)" },


    //_ addon_options_helpers.cpp - duplicate-name warning tag next to a Cyclic Group slot's name.
    { "WE_DUPLICATE_TAG",
        "[duplicate]",
        "[Duplikat]" },


    //_ addon_options_helpers.cpp - DrawNameAndContextMenu's shared right-click menu (Basic Events,
    //  Cyclic Groups, Cyclic Slots, and categories all draw through this one function).
    { "WE_ROW_NOTIFY_SUB_TOAST_SOUND",
        "Set to: Subscribed + Toast + Sound",
        "Setzen auf: Abonniert + Benachrichtigung + Ton" },

    { "WE_ROW_NOTIFY_SUB_TOAST",
        "Set to: Subscribed + Toast",
        "Setzen auf: Abonniert + Benachrichtigung" },

    { "WE_ROW_NOTIFY_SUB_ONLY",
        "Set to: Subscribed only",
        "Setzen auf: Nur abonniert" },

    { "WE_ROW_NOTIFY_UNSUBSCRIBED",
        "Set to: Unsubscribed",
        "Setzen auf: Nicht abonniert" },

    { "WE_ROW_EDIT_NAME",
        "Edit name",
        "Namen bearbeiten" },

    { "WE_ROW_RESET",
        "Reset",
        "Zurücksetzen" },

    { "WE_ROW_DELETE",
        "Delete",
        "Löschen" },

    { "WE_ROW_SAVE",
        "Save",
        "Speichern" },


    //_ addon_options_helpers.cpp - per-event/per-slot Icon combo and chat-code input label (Icon
    //  Whitener's own "Icon" combo is a separate identifier, WE_ICONWHITE_ICON_LABEL, above).
    { "WE_ICON_LABEL",
        "Icon",
        "Symbol" },

    { "WE_TEXT_TO_COPY_LABEL",
        "Text to copy",
        "Zu kopierender Text" },


    //_ addon_options_helpers.cpp - Cyclic Slot color tier picker.
    { "WE_TIER_LABEL",
        "Tier",
        "Stufe" },

    { "WE_TIER_PRIMARY",
        "Primary",
        "Primär" },

    { "WE_TIER_SECONDARY",
        "Secondary",
        "Sekundär" },

    { "WE_TIER_TERTIARY",
        "Tertiary",
        "Tertiär" },


    //_ addon_options_helpers.cpp - tooltips for the shared Basic Event / Cyclic Group row-drawing helpers.
    { "WE_TIP_NOTIFY_ICON_LVL0",
        "Click to subscribe",
        "Klicken zum Abonnieren" },

    { "WE_TIP_NOTIFY_ICON_LVL1",
        "Subscribed - click to also show a toast notification\n"
        "(right-click the name for more options)",
        "Abonniert - klicken, um zusätzlich eine Benachrichtigung anzuzeigen\n"
        "(Rechtsklick auf den Namen für weitere Optionen)" },

    { "WE_TIP_NOTIFY_ICON_LVL2",
        "Subscribed + toast notification - click to also play a sound\n"
        "(right-click the name for more options)",
        "Abonniert + Benachrichtigung - klicken, um zusätzlich einen Ton abzuspielen\n"
        "(Rechtsklick auf den Namen für weitere Optionen)" },

    { "WE_TIP_NOTIFY_ICON_LVL3",
        "Subscribed + toast + sound - click to unsubscribe\n"
        "(right-click the name for more options)",
        "Abonniert + Benachrichtigung + Ton - klicken zum Deabonnieren\n"
        "(Rechtsklick auf den Namen für weitere Optionen)" },

    { "WE_TIP_NOTIFY_BTN_LVL0",
      "Unsubscribed",
      "Nicht abonniert" },

    { "WE_TIP_NOTIFY_BTN_LVL1",
      "Subscribed - silent",
      "Abonniert - stumm" },

    { "WE_TIP_NOTIFY_BTN_LVL2",
      "Subscribed + toast notification",
      "Abonniert + Benachrichtigung" },

    { "WE_TIP_NOTIFY_BTN_LVL3",
      "Subscribed + toast + sound",
      "Abonniert + Benachrichtigung + Ton" },

    { "WE_TIP_DRAG_STOP",
      "Click to stop dragging on the map.",
      "Klicken, um das Ziehen auf der Karte zu beenden." },

    { "WE_TIP_DRAG_START",
        "Click, then left-click-drag this marker on the map to reposition it.",
        "Klicken, dann diese Markierung auf der Karte per Linksklick-Ziehen neu positionieren." },

    { "WE_TIP_AUTO_TRACKED",
        "Automatically tracked via the GW2 API.\n"
        "Drops off the Subscriptions bar/window on its own\n"
        "once claimed today (no need to check it off by hand).",
        "Wird automatisch über die GW2-API erfasst. Verschwindet\n"
        "von selbst aus Abo-Leiste/-Fenster, sobald es heute\n"
        "abgeschlossen wurde (kein manuelles Abhaken nötig)." },

    { "WE_TIP_SHOW_ON_MAP",
        "Show on the map overlay (Subscriptions bar/window are unaffected)",
        "Auf dem Karten-Overlay anzeigen (Abo-Leiste/-Fenster sind davon nicht betroffen)" },

    { "WE_TIP_SUBSCRIBE_CYCLE",
        "Subscribe/unsubscribe every occurrence in this cycle at once\n"
        "(checked only when all of them already are)",
        "Alle Abschnitte dieses Zyklus auf einmal abonnieren/deabonnieren\n"
        "(nur angehakt, wenn bereits alle abonniert sind)" },

    { "WE_TIP_SHOW_RING",
        "Show/hide this entire ring on the map overlay\n"
        "(no circle drawn at all while unchecked)",
        "Den gesamten Ring auf dem Karten-Overlay ein-/ausblenden\n"
        "(bei Deaktivierung wird gar kein Kreis gezeichnet)" },

    { "WE_TIP_SHOW_OCCURRENCE",
        "Show/hide this occurrence on the map overlay",
        "Diesen Abschnitt auf dem Karten-Overlay ein-/ausblenden" },

    { "WE_TIP_REPETITION",
        "How many times the event repeats within the period. Must\n"
        "divide the period evenly - add a second entry instead if it doesn't.\n"
        "Example: an event every hour in a 2h period repeats twice.",
        "Wie oft sich das Event innerhalb der Periode wiederholt.\n"
        "Muss die Periode ganzzahlig teilen - andernfalls stattdessen einen\n"
        "zweiten Eintrag anlegen. Beispiel: ein stündliches Event in einer\n"
        "2-Stunden-Periode wiederholt sich zweimal." },


    //_ addon_options.cpp - the Nexus options panel. Only the generic settings.
    { "WE_OPT_RELEASE",
        "Release",
        "Version" },

    { "WE_OPT_DISABLE_COMPETITIVE",
        "Disable overlay in PvP/WvW",
        "Overlay in PvP/WvW deaktivieren" },

    { "WE_OPT_WINDOW",
        "Window",
        "Fenster" },

    { "WE_OPT_TOAST",
        "Toast",
        "Benachrichtigung" },

    { "WE_OPT_BAR",
        "Bar",
        "Leiste" },

    { "WE_OPT_OVERLAY_SETTINGS",
        "Overlay Settings",
        "Overlay-Einstellungen" },

    { "WE_OPT_SHOW_SUBS_WINDOW",
        "Show subscriptions window",
        "Abo-Fenster anzeigen" },

    { "WE_OPT_HIDE_ACTIVE_IN_WINDOW",
        "Hide active in window",
        "Aktive Events im Fenster ausblenden" },

    { "WE_OPT_ACTIVE",
        "Active",
        "Aktiv" },

    { "WE_OPT_SOON",
        "Soon",
        "Bald" },

    { "WE_OPT_ENABLE_NOTIFY_POPUPS",
        "Enable notification popups",
        "Benachrichtigungs-Popups aktivieren" },

    { "WE_OPT_WARN_BEFORE_START",
        "Warn before start (min)",
        "Warnung vor Start (Min.)" },

    { "WE_OPT_NOTIFY_ON_START",
        "Notify on start",
        "Start Benachrichtigung" },

    { "WE_OPT_POPUP_DURATION",
        "Popup duration (sec)",
        "Popup-Dauer (Sek.)" },

    { "WE_OPT_SOUND_NONE",
        "(none)",
        "(keiner)" },

    { "WE_OPT_SOUND",
        "Sound",
        "Ton" },

    { "WE_OPT_RESCAN",
        "Rescan",
        "Neu einlesen" },

    { "WE_OPT_TEST",
        "Test",
        "Testen" },

    { "WE_OPT_SHOW_SUBS_BAR",
        "Show subscriptions bar",
        "Abo-Leiste anzeigen" },

    { "WE_OPT_HIDE_ACTIVE_ON_BAR",
        "Hide active on bar",
        "Aktive auf Leiste ausblenden" },

    { "WE_OPT_MINIMAL_MODE",
        "Minimal Mode",
        "Minimalmodus" },

    { "WE_OPT_BOTTOM_LINE",
        "Bottom Line",
        "Untere Linie" },

    { "WE_OPT_DOT_COLOR",
        "Dot Color",
        "Punktfarbe" },

    { "WE_OPT_POPOUT_HEIGHT",
        "Pop-out height (px)",
        "Ausklapphöhe (px)" },

    { "WE_OPT_POPOUT_DELAY",
        "Pop-out delay (ms)",
        "Ausklappverzögerung (ms)" },

    { "WE_OPT_UNSAFE_ZONE",
        "Unsafe zone",
        "Unsichere Zone" },

    { "WE_OPT_LEFT",
        "Left",
        "Links" },

    { "WE_OPT_RIGHT",
        "Right",
        "Rechts" },

    { "WE_OPT_HEIGHT",
        "Height",
        "Höhe" },

    { "WE_OPT_EVENTS_SETTINGS_HEADER",
        "Events Settings (Basic|Cyclic)",
        "Event-Einstellungen (Normal|Zyklisch)" },


    //_ addon_options.cpp - Event Lists (Basic|Cyclic) section; some rows shared with subscriptions_edit_window.cpp.
    { "WE_OPT_EVENT_LISTS_HEADER",
        "Event Lists (Basic|Cyclic)",
        "Event-Listen (Normal|Zyklisch)" },

    { "WE_OPT_SEARCH_LABEL",
        "Search",
        "Suche" },

    { "WE_OPT_RIGHT_CLICK_HINT",
        "(right-click an entry below for more options)",
        "(Rechtsklick auf einen Eintrag unten für weitere Optionen)" },

    { "WE_OPT_BASIC_EVENTS",
        "Basic Events",
        "Normale Events" },

    { "WE_OPT_CATEGORIES",
        "Categories",
        "Kategorien" },

    { "WE_OPT_CYCLIC_EVENTS",
        "Cyclic Events",
        "Zyklische Events" },

    { "WE_OPT_CHAT_SETTINGS",
        "Chat settings:",
        "Chat-Einstellungen:" },

    { "WE_OPT_PASTE_DELAY",
        "Paste Delay",
        "Einfügeverzögerung" },

    { "WE_OPT_PASTE_TO",
        "Paste to",
        "Einfügen in" },

    { "WE_OPT_BETTER_CHAT_NOT_LOADED",
        "Better Chat not loaded (optional)",
        "Better Chat nicht geladen (optional)" },

    { "WE_OPT_BETTER_CHAT_SELF_DISABLED",
        "Better Chat loaded, /self disabled",
        "Better Chat geladen, /self deaktiviert" },

    { "WE_OPT_BETTER_CHAT_SELF_ENABLED",
        "Better Chat loaded, /self enabled",
        "Better Chat geladen, /self aktiviert" },

    { "WE_OPT_GROW_MARKERS_ZOOM",
        "Grow markers when zooming in",
        "Markierungen beim Hineinzoomen vergrößern" },

    { "WE_OPT_START_GROWING_AT",
        "Start growing at",
        "Beginn Vergrößerung bei" },

    { "WE_OPT_MAX_SIZE_AT_ZOOM",
        "Max size at 100% zoom",
        "Maximalgröße bei 100% Zoom" },

    { "WE_OPT_GW2_API_KEY",
        "GW2 API key",
        "GW2-API-Schlüssel" },

    { "WE_OPT_API_KEY_DELAY_NOTE",
        "Can take up to 5min to take effect.",
        "Kann bis zu 5 Min. dauern, bis Änderungen sichtbar sind." },

    { "WE_OPT_API_NO_KEY",
        "No key set",
        "Kein API-Schlüssel gesetzt" },

    { "WE_OPT_API_CHECKING",
        "Checking...",
        "Wird geprüft..." },

    { "WE_OPT_API_CONNECTED",
        "Connected",
        "Verbunden" },

    { "WE_OPT_API_INVALID_KEY",
        "Invalid key / missing permission",
        "Ungültiger Schlüssel / fehlende Berechtigung" },

    { "WE_OPT_API_NETWORK_ERROR",
        "Network error, retrying",
        "Netzwerkfehler, erneuter Versuch" },

    { "WE_OPT_AUTO_MARK_API_DONE",
        "Automatically mark API-confirmed events done",
        "API-bestätigte Events automatisch als erledigt markieren" },

    { "WE_OPT_AUTO_TRACK_VAULT",
        "Auto-track weekly Wizard's Vault targets",
        "Wöchentliche Gewölbe des Zauberers-Ziele automatisch verfolgen" },

    { "WE_OPT_WEEKLY_COLOR",
        "Weekly Color",
        "Wochenfarbe" },

    { "WE_OPT_CLEAR_DONE_MARKERS",
        "Clear events manually marked done",
        "Manuell als erledigt markierte Events zurücksetzen" },

    { "WE_OPT_ONLY_SHOW_STARTING_IN",
        "Only show events starting in",
        "Nur Events zeigen, die in X beginnen." },

    { "WE_OPT_WAITING",
        "Waiting",
        "Pause" },

    { "WE_OPT_DOT_RADIUS",
        "Dot radius",
        "Punktradius" },

    { "WE_OPT_ICON_SIZE",
        "Icon size",
        "Symbolgröße" },

    { "WE_OPT_SHOW_CYCLIC_ON_MAP",
        "Show cyclic events on map",
        "Zyklische Events auf der Karte anzeigen" },

    { "WE_OPT_RING_APPEARANCE",
        "Ring appearance",
        "Ring-Erscheinungsbild" },

    { "WE_OPT_RADIUS",
        "Radius",
        "Radius" },

    { "WE_OPT_THICKNESS",
        "Thickness",
        "Dicke" },

    { "WE_OPT_ENTRY_EXIT_WINDOW",
        "Entry / exit window",
        "Ein-/Austrittsfenster" },

    { "WE_OPT_FUTURE_WINDOW",
        "Future window",
        "Zukunftsfenster" },

    { "WE_OPT_PAST_WINDOW",
        "Past window",
        "Vergangenheitsfenster" },

    { "WE_OPT_FADE_PAST_EVENTS",
        "Fade past events",
        "Vergangenes langsam ausblenden" },

    { "WE_OPT_HAND",
        "Hand",
        "Zeiger" },

    { "WE_OPT_COLOR",
        "Color",
        "Farbe" },

    { "WE_OPT_USE_TEXTURE",
        "Use texture",
        "Textur verwenden" },

    { "WE_OPT_NONE",
        "None",
        "Keine" },

    { "WE_OPT_WIDTH",
        "Width",
        "Breite" },

    { "WE_OPT_RING_EDGE_TEXTURE",
        "Ring edge texture",
        "Ringkanten-Textur" },

    { "WE_OPT_TEXTURE",
        "Texture",
        "Textur" },

    { "WE_OPT_OFFSET",
        "Offset",
        "Versatz" },

    { "WE_OPT_FILL_TEXTURE",
        "Fill texture",
        "Füll-Textur" },

    { "WE_OPT_OPACITY",
        "Opacity",
        "Deckkraft" },


    //_ icon_whitener.cpp - Icon Whitener tool (options panel button + its popup).
    { "WE_ICONWHITE_TITLE",
        "Icon Whitener",
        "Symbol-Aufheller" },

    { "WE_ICONWHITE_INTRO",
        "Map icons are tinted at draw time with a multiplicative color blend. "
        "This only looks correct when the icon's RGB is neutral gray - a "
        "colored image will tint unpredictably instead of cleanly turning "
        "red / orange / gray.",
        "Kartensymbole werden beim Rendern eingefärbt. Das sieht nur korrekt "
        "aus, wenn das RGB des Symbols neutral grau ist - ein farbiges Bild färbt "
        "sich unvorhersehbar ein, statt sauber rot, orange oder grau zu werden." },

    { "WE_ICONWHITE_INSTRUCTIONS",
        "Pick an icon from the \"textures\" folder and press Convert. "
        "The image will be desaturated to luminance and normalized so the "
        "brightest pixel becomes white. "
        "The result is saved as <name>_white.png next to the original.",
        "Wähle ein Symbol aus dem \"textures\"-Ordner und drücke Konvertieren. "
        "Das Bild wird auf Helligkeit entsättigt und so normalisiert, dass der "
        "hellste Pixel weiß wird. "
        "Das Ergebnis wird als <Name>_white.png neben dem Original gespeichert." },

    { "WE_ICONWHITE_ICON_LABEL",
        "Icon",
        "Symbol" },

    { "WE_ICONWHITE_REFRESH",
        "Refresh",
        "Aktualisieren" },

    { "WE_ICONWHITE_CONVERT",
        "Convert & Save",
        "Konvertieren & Speichern" },

    { "WE_ICONWHITE_CLOSE",
        "Close",
        "Schließen" },

    { "WE_ICONWHITE_SAVED_AS",
        "Saved as: ",
        "Gespeichert als: " },

};

inline constexpr int kLocalizationCount = sizeof(kLocalizationTable) / sizeof(kLocalizationTable[0]);