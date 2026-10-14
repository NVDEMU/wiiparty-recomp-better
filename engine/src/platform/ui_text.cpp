#include "wp/ui_text.h"

#include <atomic>
#include <cstring>

namespace wp::ui {

namespace {

constexpr Label kEnglishLabels[] = {
    {"video.scale", "Internal resolution"},
    {"video.fullscreen", "Full screen"},
    {"video.copy_filter", "Anti-flicker filter"},
    {"video.custom_textures", "Custom textures"},
    {"video.dump_textures", "Dump textures"},
    {"input.gamepads", "Gamepads"},
    {"input.auto_grip", "Automatic grip"},
    {"input.joycon_pairs", "Join Joy-Con pairs"},
    {"input.real_wiimotes", "Real Wii Remotes"},
    {"input.real_wiimote_mouse", "Aim Wii Remote 1 with the mouse"},
    {"input.wake_on_mouse", "Reconnect with the mouse"},
    {"input.hide_cursor", "Hide the mouse cursor"},
    {"input.controls_hint", "Controls window on instruction screens"},
    {"system.interface_language", "Interface language"},
    {"system.language", "Console language"},
    {"system.pal60", "60 Hz (PAL60)"},
    {"system.skip_notices", "Skip the strap notice"},
    {"system.options_menu", "Options menu (F10)"},
    {"audio.mute", "Mute"},
    {"audio.wiimote_speaker", "Wii Remote speaker"},
    {"saves.backups", "Save backups kept"},
    {"hint.title", "Controls on PC"},
    {"hint.pointer", "Pointer"},
    {"hint.mouse", "Mouse"},
    {"hint.dpad", "D-pad"},
    {"hint.shake", "Shake"},
    {"hint.swing", "Swing"},
    {"hint.tilt", "Tilt"},
    {"mii.title", "Miis"},
    {"mii.delete", "Delete"},
    {"mii.count", "%1 of 100 Miis"},
    {"mii.damaged", "The Mii database is damaged or is not a Wii database, so it is not edited. Creating a new one keeps a copy of the old file."},
    {"mii.create_database", "Create a new database"},
    {"mii.save_failed", "The Mii database could not be saved."},
    {"mii.import_done", "%1 Mii(s) imported."},
    {"mii.import_failed", "That file has no Wii Mii data. Use a .mii file of 74 or 76 bytes, or a Wii Mii database (RFL_DB.dat)."},
    {"mii.import_filter", "Wii Mii files"},
    {"mii.intro", "Make Miis in the online editor, download them in the Wii format and load them here. The game lists them as Wii console Miis the next time it starts, so close the game first."},
    {"mii.online", "Online Mii editor"},
    {"mii.online_detail", "Opens mii.nxw.pw in your browser. It needs an account and an internet connection; the launcher does not use it."},
    {"mii.open_online", "Open the editor"},
    {"mii.import", "Load Miis"},
    {"mii.import_detail", "Pick a Wii Mii file (.mii, 74 or 76 bytes) or a Wii Mii database (RFL_DB.dat). Up to 100 Miis in total."},
    {"mii.choose_file", "Choose a file"},
    {"mii.none", "There are no Miis in the console database yet. Load a Mii file to see it in the game's Wii console list."},
};

constexpr Label kSpanishLabels[] = {
    {"video.scale", "Resolución interna"},
    {"video.fullscreen", "Pantalla completa"},
    {"video.copy_filter", "Filtro anti-parpadeo"},
    {"video.custom_textures", "Texturas personalizadas"},
    {"video.dump_textures", "Volcar texturas"},
    {"input.gamepads", "Mandos"},
    {"input.auto_grip", "Agarre automático"},
    {"input.joycon_pairs", "Unir parejas de Joy-Con"},
    {"input.real_wiimotes", "Wii Remotes reales"},
    {"input.real_wiimote_mouse", "Apuntar el Wii Remote 1 con el ratón"},
    {"input.wake_on_mouse", "Reconectar con el ratón"},
    {"input.hide_cursor", "Ocultar el cursor del ratón"},
    {"input.controls_hint", "Ventana de controles en las explicaciones"},
    {"system.interface_language", "Idioma de la interfaz"},
    {"system.language", "Idioma de la consola"},
    {"system.pal60", "60 Hz (PAL60)"},
    {"system.skip_notices", "Saltar el aviso de la correa"},
    {"system.options_menu", "Menú de opciones (F10)"},
    {"audio.mute", "Silenciar"},
    {"audio.wiimote_speaker", "Altavoz del Wii Remote"},
    {"saves.backups", "Copias de la partida"},
    {"hint.title", "Controles en PC"},
    {"hint.pointer", "Puntero"},
    {"hint.mouse", "Ratón"},
    {"hint.dpad", "Cruceta"},
    {"hint.shake", "Agitar"},
    {"hint.swing", "Golpe"},
    {"hint.tilt", "Inclinar"},
    {"mii.title", "Miis"},
    {"mii.delete", "Eliminar"},
    {"mii.count", "%1 de 100 Miis"},
    {"mii.damaged", "La base de datos de Miis está dañada o no es de Wii, así que no se edita. Crear una nueva conserva una copia del archivo anterior."},
    {"mii.create_database", "Crear una base de datos nueva"},
    {"mii.save_failed", "No se pudo guardar la base de datos de Miis."},
    {"mii.import_done", "%1 Mii(s) importado(s)."},
    {"mii.import_failed", "Ese archivo no tiene datos de un Mii de Wii. Usa un archivo .mii de 74 o 76 bytes, o una base de datos de Miis de Wii (RFL_DB.dat)."},
    {"mii.import_filter", "Archivos de Mii de Wii"},
    {"mii.intro", "Crea Miis en el editor online, descárgalos en el formato de Wii y cárgalos aquí. El juego los muestra como Miis de la consola Wii la próxima vez que arranque, así que cierra antes el juego."},
    {"mii.online", "Editor de Miis online"},
    {"mii.online_detail", "Abre mii.nxw.pw en tu navegador. Requiere cuenta y conexión a internet; el launcher no lo usa."},
    {"mii.open_online", "Abrir el editor"},
    {"mii.import", "Cargar Miis"},
    {"mii.import_detail", "Elige un archivo de Mii de Wii (.mii, 74 o 76 bytes) o una base de datos de Miis de Wii (RFL_DB.dat). Hasta 100 Miis en total."},
    {"mii.choose_file", "Elegir un archivo"},
    {"mii.none", "Aún no hay Miis en la base de datos de la consola. Carga un archivo de Mii para verlo en la lista de la consola Wii del juego."},
};

constexpr Label kGalicianLabels[] = {
    {"video.scale", "Resolución interna"},
    {"video.fullscreen", "Pantalla completa"},
    {"video.copy_filter", "Filtro antiparpadeo"},
    {"video.custom_textures", "Texturas personalizadas"},
    {"video.dump_textures", "Envorcar texturas"},
    {"input.gamepads", "Mandos"},
    {"input.auto_grip", "Agarre automático"},
    {"input.joycon_pairs", "Unir parellas de Joy-Con"},
    {"input.real_wiimotes", "Wii Remotes reais"},
    {"input.real_wiimote_mouse", "Apuntar o Wii Remote 1 co rato"},
    {"input.wake_on_mouse", "Reconectar co rato"},
    {"input.hide_cursor", "Agochar o cursor do rato"},
    {"input.controls_hint", "Xanela de controis nas explicacións"},
    {"system.interface_language", "Idioma da interface"},
    {"system.language", "Idioma da consola"},
    {"system.pal60", "60 Hz (PAL60)"},
    {"system.skip_notices", "Saltar o aviso da correa"},
    {"system.options_menu", "Menú de opcións (F10)"},
    {"audio.mute", "Silenciar"},
    {"audio.wiimote_speaker", "Altofalante do Wii Remote"},
    {"saves.backups", "Copias da partida"},
    {"hint.title", "Controis no PC"},
    {"hint.pointer", "Punteiro"},
    {"hint.mouse", "Rato"},
    {"hint.dpad", "Cruceta"},
    {"hint.shake", "Axitar"},
    {"hint.swing", "Golpe"},
    {"hint.tilt", "Inclinar"},
    {"mii.title", "Miis"},
    {"mii.delete", "Eliminar"},
    {"mii.count", "%1 de 100 Miis"},
    {"mii.damaged", "A base de datos de Miis está danada ou non é de Wii, polo que non se edita. Crear unha nova garda unha copia do ficheiro anterior."},
    {"mii.create_database", "Crear unha base de datos nova"},
    {"mii.save_failed", "Non se puido gardar a base de datos de Miis."},
    {"mii.import_done", "%1 Mii(s) importado(s)."},
    {"mii.import_failed", "Ese ficheiro non ten datos dun Mii de Wii. Usa un ficheiro .mii de 74 ou 76 bytes, ou unha base de datos de Miis de Wii (RFL_DB.dat)."},
    {"mii.import_filter", "Ficheiros de Mii de Wii"},
    {"mii.intro", "Crea Miis no editor online, descárgaos no formato de Wii e cárgaos aquí. O xogo móstraos como Miis da consola Wii a próxima vez que arranque, así que pecha antes o xogo."},
    {"mii.online", "Editor de Miis online"},
    {"mii.online_detail", "Abre mii.nxw.pw no teu navegador. Require conta e conexión a internet; o launcher non o usa."},
    {"mii.open_online", "Abrir o editor"},
    {"mii.import", "Cargar Miis"},
    {"mii.import_detail", "Elixe un ficheiro de Mii de Wii (.mii, 74 ou 76 bytes) ou unha base de datos de Miis de Wii (RFL_DB.dat). Ata 100 Miis en total."},
    {"mii.choose_file", "Elixir un ficheiro"},
    {"mii.none", "Aínda non hai Miis na base de datos da consola. Carga un ficheiro de Mii para velo na lista da consola Wii do xogo."},
};

constexpr Text kEnglish{"FPS",
                        "DSP",
                        "NATIVE",
                        "INTERP",
                        "OFF",
                        "Options",
                        "Up/Down: choose     Left/Right/Enter or click: change     Esc or right click: close",
                        "applies on restart",
                        "On",
                        "Off",
                        "Native",
                        "System",
                        kEnglishLabels,
                        sizeof(kEnglishLabels) / sizeof(kEnglishLabels[0])};

constexpr Text kSpanish{"FPS",
                        "DSP",
                        "NATIVO",
                        "INTERP",
                        "APAGADO",
                        "Opciones",
                        "Arriba/Abajo: elegir     Izquierda/Derecha/Intro o clic: cambiar     Esc o clic derecho: cerrar",
                        "se aplica al reiniciar",
                        "Sí",
                        "No",
                        "Nativa",
                        "Sistema",
                        kSpanishLabels,
                        sizeof(kSpanishLabels) / sizeof(kSpanishLabels[0])};

constexpr Text kGalician{"FPS",
                         "DSP",
                         "NATIVO",
                         "INTERP",
                         "APAGADO",
                         "Opcións",
                         "Arriba/Abaixo: escoller     Esquerda/Dereita/Intro ou clic: cambiar     Esc ou clic dereito: pechar",
                         "aplícase ao reiniciar",
                         "Si",
                         "Non",
                         "Nativa",
                         "Sistema",
                         kGalicianLabels,
                         sizeof(kGalicianLabels) / sizeof(kGalicianLabels[0])};

std::atomic<const Text*> g_text{&kEnglish};

const char* label_in(const Text& current, const char* key) {
    for (size_t i = 0; i < current.label_count; i++) {
        if (std::strcmp(current.labels[i].key, key) == 0) {
            return current.labels[i].text;
        }
    }
    return key;
}

const Text& text_for(Language language) {
    switch (language) {
    case Language::Spanish:
        return kSpanish;
    case Language::Galician:
        return kGalician;
    default:
        return kEnglish;
    }
}

}

void set_language(Language language) {
    g_text = &text_for(language);
}

Language language_of(const std::string& code) {
    return code == "es" ? Language::Spanish : code == "gl" ? Language::Galician : Language::English;
}

void set_language(const std::string& code) {
    set_language(language_of(code));
}

const Text& text() {
    return *g_text.load();
}

const char* label(const char* key) {
    return label_in(text(), key);
}

const char* label(const char* key, Language language) {
    return label_in(text_for(language), key);
}

}
