// Translations of the mods' texts. The game's language tables are not ours to patch, but our texts are added to the game's table at
// run time, so they can be chosen by language there. One convention for every mod: a folder (the mod's own, wherever it keeps its
// files; the mod passes it to Host::register_translations) holds one UTF-8 text file per language, named like the game's language
// (the game's language name): German.txt, French.txt... One line per text, `English text = Translation`; lines starting with
// # are comments. A text with no translation stays English.
#pragma once

namespace custom_menus::translations {

// Reads `<folder>/<Language>.txt` for the game's language (nothing is read for English).
void add_folder(const char *folder);

// The game's language changed: reads every registered folder again. What `translate` returned before is no longer valid (ask again).
void reload();

// `english` translated into the game's language, or `english` itself. The result stays valid until the language changes.
const char *translate(const char *english);

// How many translations were loaded.
int count();

}  // namespace custom_menus::translations
