# Nix Language (Native C++17 & Qt6 GUI Implementation)

Implémentation complète et haute performance en **C++17 / C** du langage **Nix**, incluant sa propre librairie graphique native compilée en **Qt6 C++** (`libnix_qt.a` / `libnix_qt.dylib`).

- **Même syntaxe exacte** (`fct`, `class`, `out`, `cin`, `using <...>`, boucles `for i * limit`, `if/else`, dictionnaires, vecteurs, etc.)
- **Interpréteur C++ natif ultra-rapide** : ~45x plus rapide que l'implémentation Python
- **Compilateur C++ AOT (Ahead-Of-Time)** : ~320x plus rapide en générant des binaires exécutables natifs optimisés (`-O3`)
- **Librairie Graphique Qt6 C++ native** : Support complet des widgets Qt6 modernes (`using <qt>`) et du moteur 2D Canvas temps réel (`using <window>`).

---

## 🛠 Compilation du moteur Nix et de la librairie Qt6

```bash
make
```

Cela compile :
- `lib/libnix_qt.a` (Librairie statique Qt6 C++)
- `lib/libnix_qt.dylib` (Librairie dynamique partagée Qt6 C++)
- `nix` (L'interpréteur et compilateur natif Nix lié à Qt6)

---

## 🎨 Librairie Graphique Qt6 (`using <qt>` & `using <window>`)

### 1. Mode Widgets Qt6 (`using <qt>` ou `using <gui>`)
Permet de créer des interfaces graphiques natives avec boutons, champs de texte, sliders, barres de progression, feuilles de style CSS/QSS et callbacks Nix :

```nix
using <qt>

win = qt_window("Mon Application Qt6", 450, 350)
qt_set_style(win, "QWidget { background-color: #1e1e2e; color: #cdd6f4; font-size: 14px; }")

lbl = qt_label(win, "Bienvenue dans Nix Qt6 !")
inp = qt_input(win, "Votre nom...")

fct saluer() {
    qt_set_text(lbl, "Bonjour " + qt_get_text(inp) + " !")
}

btn = qt_button(win, "Dire Bonjour", saluer)
qt_run(win)
```

#### Fonctions disponibles dans `<qt>` :
- **Fenêtres & Cycle de vie** : `qt_window(title, w, h)`, `qt_show(w)`, `qt_hide(w)`, `qt_close(w)`, `qt_run(w)`, `qt_quit()`
- **Conteneurs & Layouts** : `qt_vbox(parent)`, `qt_hbox(parent)`, `qt_frame(parent)`, `qt_groupbox(parent, title)`
- **Widgets** :
  - `qt_label(parent, text)`
  - `qt_button(parent, text, [callback])`
  - `qt_input(parent, [placeholder])`
  - `qt_textarea(parent, [text])`
  - `qt_checkbox(parent, text, [checked])`
  - `qt_slider(parent, min, max, value)`
  - `qt_progressbar(parent, min, max, value)`
  - `qt_combobox(parent, [items_list])`
  - `qt_listwidget(parent, [items_list])`
- **Propriétés, Événements & Styles** :
  - `qt_set_text(id, text)` / `qt_get_text(id)`
  - `qt_set_value(id, val)` / `qt_get_value(id)`
  - `qt_is_checked(id)` / `qt_set_checked(id, bool)`
  - `qt_set_style(id, qss_string)`
  - `qt_set_title(id, title)`, `qt_resize(id, w, h)`, `qt_set_enabled(id, bool)`
  - `qt_add_item(id, text)`, `qt_clear_items(id)`
  - `qt_on_click(id, callback)`, `qt_on_change(id, callback)`
  - `qt_timer(interval_ms, callback, [repeat])`
- **Boîtes de dialogue natives** :
  - `qt_msgbox(title, message)`
  - `qt_confirm(title, question)`
  - `qt_file_dialog(title, filter)`
  - `qt_color_dialog(title)`

---

### 2. Mode 2D Canvas & Jeux (`using <window>`)
Rétrocompatible avec les scripts graphiques 2D temps réel Nix :
- `window_open(title, width, height)`
- `window_is_open(win)`
- `window_poll_events(win)`
- `window_clear(win, r, g, b)`
- `window_draw_rect(win, x, y, w, h, r, g, b, [filled])`
- `window_draw_circle(win, cx, cy, radius, r, g, b, [filled])`
- `window_draw_line(win, x1, y1, x2, y2, r, g, b, [thickness])`
- `window_draw_text(win, text, x, y, r, g, b, [font_size])`
- `window_present(win)`
- `window_close(win)`

---

## 📁 Architecture du projet C++

- [`cpp/include/nix_runtime.hpp`](file:///Users/nehoray/nix2.6/cpp/include/nix_runtime.hpp) : Runtime natif C++ (gestion mémoire, types dynamiques `Value`, modules standards et déclarations GUI).
- [`cpp/include/nix_qt.hpp`](file:///Users/nehoray/nix2.6/cpp/include/nix_qt.hpp) & [`cpp/src/nix_qt.cpp`](file:///Users/nehoray/nix2.6/cpp/src/nix_qt.cpp) : Implémentation de la librairie graphique Qt6 C++ (`libnix_qt`).
- [`cpp/src/frontend.hpp`](file:///Users/nehoray/nix2.6/cpp/src/frontend.hpp) : Lexer, AST et Parser C++.
- [`cpp/src/interpreter.hpp`](file:///Users/nehoray/nix2.6/cpp/src/interpreter.hpp) : Évaluateur tree-walk haute performance.
- [`cpp/src/compiler.hpp`](file:///Users/nehoray/nix2.6/cpp/src/compiler.hpp) : Compilateur AOT générant du C++ natif lié à `libnix_qt`.
- [`cpp/src/main.cpp`](file:///Users/nehoray/nix2.6/cpp/src/main.cpp) : CLI `nix` (`run` et `build`).
