// ==============================================================================
// NIX QT6 NATIVE GUI LIBRARY IMPLEMENTATION (libnix_qt)
// ==============================================================================
#include "nix_qt.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <deque>
#include <unordered_map>

namespace nix {
namespace qt_gui {

// ------------------------------------------------------------------------------
// Global QApplication Singleton Management
// ------------------------------------------------------------------------------
static QApplication* g_app = nullptr;
static int g_argc = 1;
static char g_arg0[] = "nix_qt_app";
static char* g_argv[] = {g_arg0, nullptr};

static void ensure_qt_app() {
    if (!QApplication::instance()) {
        g_app = new QApplication(g_argc, g_argv);
    }
}

// ==============================================================================
// PART 1: 2D CANVAS & WINDOW ENGINE (<window> / <mac_window>)
// ==============================================================================
struct CanvasEvent {
    std::string type;    // "key", "click", "motion"
    std::string key;
    std::string button;  // "left", "right"
    std::string state;   // "down", "up"
    int x = 0;
    int y = 0;
};

class NixCanvasWindow : public QWidget {
public:
    QImage buffer;
    bool is_open_flag = true;
    std::deque<CanvasEvent> events;

    NixCanvasWindow(const std::string& title, int w, int h)
        : QWidget(nullptr), buffer(w, h, QImage::Format_RGB32) {
        setWindowTitle(QString::fromStdString(title));
        setFixedSize(w, h);
        buffer.fill(Qt::black);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        show();
        raise();
        activateWindow();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.drawImage(0, 0, buffer);
    }

    void closeEvent(QCloseEvent* ev) override {
        is_open_flag = false;
        ev->accept();
    }

    void keyPressEvent(QKeyEvent* ev) override {
        CanvasEvent ce;
        ce.type = "key";
        ce.state = "down";
        ce.key = translate_key(ev);
        events.push_back(ce);
    }

    void keyReleaseEvent(QKeyEvent* ev) override {
        CanvasEvent ce;
        ce.type = "key";
        ce.state = "up";
        ce.key = translate_key(ev);
        events.push_back(ce);
    }

    void mousePressEvent(QMouseEvent* ev) override {
        CanvasEvent ce;
        ce.type = "click";
        ce.state = "down";
        ce.button = (ev->button() == Qt::LeftButton) ? "left" : "right";
        ce.x = ev->position().x();
        ce.y = ev->position().y();
        events.push_back(ce);
    }

    void mouseReleaseEvent(QMouseEvent* ev) override {
        CanvasEvent ce;
        ce.type = "click";
        ce.state = "up";
        ce.button = (ev->button() == Qt::LeftButton) ? "left" : "right";
        ce.x = ev->position().x();
        ce.y = ev->position().y();
        events.push_back(ce);
    }

    void mouseMoveEvent(QMouseEvent* ev) override {
        CanvasEvent ce;
        ce.type = "motion";
        ce.x = ev->position().x();
        ce.y = ev->position().y();
        events.push_back(ce);
    }

private:
    static std::string translate_key(QKeyEvent* ev) {
        switch (ev->key()) {
            case Qt::Key_Left: return "left";
            case Qt::Key_Right: return "right";
            case Qt::Key_Up: return "up";
            case Qt::Key_Down: return "down";
            case Qt::Key_Space: return "space";
            case Qt::Key_Return:
            case Qt::Key_Enter: return "return";
            case Qt::Key_Escape: return "escape";
            default: {
                QString txt = ev->text();
                if (!txt.isEmpty()) return txt.toLower().toStdString();
                return QKeySequence(ev->key()).toString().toLower().toStdString();
            }
        }
    }
};

static std::unordered_map<int64_t, NixCanvasWindow*> g_canvas_windows;
static int64_t g_next_canvas_id = 1;

static NixCanvasWindow* get_canvas(const Value& v) {
    int64_t id = v.as_int();
    auto it = g_canvas_windows.find(id);
    if (it == g_canvas_windows.end() || !it->second) {
        fail("Invalid or closed window handle: " + std::to_string(id));
    }
    return it->second;
}

Value window_open(const Value* a, size_t n, const Value&) {
    if (n < 3) fail("window_open expects 3 arguments: (title, width, height)");
    ensure_qt_app();
    std::string title = to_display(a[0]);
    int w = (int)a[1].as_int();
    int h = (int)a[2].as_int();
    int64_t id = g_next_canvas_id++;
    g_canvas_windows[id] = new NixCanvasWindow(title, w, h);
    QApplication::processEvents();
    return Value::integer(id);
}

Value window_is_open(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("window_is_open expects 1 argument: (window_id)");
    NixCanvasWindow* win = get_canvas(a[0]);
    QApplication::processEvents();
    return Value::boolean(win->is_open_flag);
}

Value window_poll_events(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("window_poll_events expects 1 argument: (window_id)");
    NixCanvasWindow* win = get_canvas(a[0]);
    QApplication::processEvents();
    if (win->events.empty()) {
        return make_dict();
    }
    CanvasEvent ev = win->events.front();
    win->events.pop_front();
    Value d = make_dict();
    as_dict(d)->set(make_str("type"), make_str(ev.type));
    if (!ev.key.empty()) as_dict(d)->set(make_str("key"), make_str(ev.key));
    if (!ev.button.empty()) as_dict(d)->set(make_str("button"), make_str(ev.button));
    if (!ev.state.empty()) as_dict(d)->set(make_str("state"), make_str(ev.state));
    as_dict(d)->set(make_str("x"), Value::integer(ev.x));
    as_dict(d)->set(make_str("y"), Value::integer(ev.y));
    return d;
}

Value window_clear(const Value* a, size_t n, const Value&) {
    if (n < 4) fail("window_clear expects 4 arguments: (window_id, r, g, b)");
    NixCanvasWindow* win = get_canvas(a[0]);
    int r = (int)a[1].as_int() & 0xFF;
    int g = (int)a[2].as_int() & 0xFF;
    int b = (int)a[3].as_int() & 0xFF;
    win->buffer.fill(QColor(r, g, b));
    return Value::none();
}

Value window_draw_rect(const Value* a, size_t n, const Value&) {
    if (n < 8) fail("window_draw_rect expects 8 or 9 arguments: (win, x, y, w, h, r, g, b, [filled])");
    NixCanvasWindow* win = get_canvas(a[0]);
    int x = (int)a[1].as_int();
    int y = (int)a[2].as_int();
    int w = (int)a[3].as_int();
    int h = (int)a[4].as_int();
    int r = (int)a[5].as_int() & 0xFF;
    int g = (int)a[6].as_int() & 0xFF;
    int b = (int)a[7].as_int() & 0xFF;
    bool filled = (n >= 9) ? truthy(a[8]) : true;

    QPainter painter(&win->buffer);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor col(r, g, b);
    if (filled) {
        painter.fillRect(x, y, w, h, col);
    } else {
        painter.setPen(QPen(col, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(x, y, w, h);
    }
    return Value::none();
}

Value window_draw_circle(const Value* a, size_t n, const Value&) {
    if (n < 7) fail("window_draw_circle expects 7 or 8 arguments: (win, cx, cy, radius, r, g, b, [filled])");
    NixCanvasWindow* win = get_canvas(a[0]);
    int cx = (int)a[1].as_int();
    int cy = (int)a[2].as_int();
    int rad = (int)a[3].as_int();
    int r = (int)a[4].as_int() & 0xFF;
    int g = (int)a[5].as_int() & 0xFF;
    int b = (int)a[6].as_int() & 0xFF;
    bool filled = (n >= 8) ? truthy(a[7]) : true;

    QPainter painter(&win->buffer);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor col(r, g, b);
    if (filled) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(col);
    } else {
        painter.setPen(QPen(col, 2));
        painter.setBrush(Qt::NoBrush);
    }
    painter.drawEllipse(QPoint(cx, cy), rad, rad);
    return Value::none();
}

Value window_draw_line(const Value* a, size_t n, const Value&) {
    if (n < 8) fail("window_draw_line expects 8 or 9 arguments: (win, x1, y1, x2, y2, r, g, b, [thickness])");
    NixCanvasWindow* win = get_canvas(a[0]);
    int x1 = (int)a[1].as_int();
    int y1 = (int)a[2].as_int();
    int x2 = (int)a[3].as_int();
    int y2 = (int)a[4].as_int();
    int r = (int)a[5].as_int() & 0xFF;
    int g = (int)a[6].as_int() & 0xFF;
    int b = (int)a[7].as_int() & 0xFF;
    int width = (n >= 9) ? (int)a[8].as_int() : 1;

    QPainter painter(&win->buffer);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(r, g, b), width));
    painter.drawLine(x1, y1, x2, y2);
    return Value::none();
}

Value window_draw_text(const Value* a, size_t n, const Value&) {
    if (n < 7) fail("window_draw_text expects 7 or 8 arguments: (win, text, x, y, r, g, b, [font_size])");
    NixCanvasWindow* win = get_canvas(a[0]);
    std::string text = to_display(a[1]);
    int x = (int)a[2].as_int();
    int y = (int)a[3].as_int();
    int r = (int)a[4].as_int() & 0xFF;
    int g = (int)a[5].as_int() & 0xFF;
    int b = (int)a[6].as_int() & 0xFF;
    int fsize = (n >= 8) ? (int)a[7].as_int() : 14;

    QPainter painter(&win->buffer);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(QColor(r, g, b));
    QFont font("Helvetica", fsize, QFont::Bold);
    painter.setFont(font);
    painter.drawText(x, y + fsize, QString::fromStdString(text));
    return Value::none();
}

Value window_present(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("window_present expects 1 argument: (window_id)");
    NixCanvasWindow* win = get_canvas(a[0]);
    win->update();
    QApplication::processEvents();
    return Value::none();
}

Value window_close(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("window_close expects 1 argument: (window_id)");
    int64_t id = a[0].as_int();
    auto it = g_canvas_windows.find(id);
    if (it != g_canvas_windows.end() && it->second) {
        it->second->is_open_flag = false;
        it->second->close();
        delete it->second;
        g_canvas_windows.erase(it);
    }
    return Value::boolean(true);
}

Value window_set_title(const Value* a, size_t n, const Value&) {
    if (n < 2) fail("window_set_title expects 2 arguments: (window_id, title)");
    NixCanvasWindow* win = get_canvas(a[0]);
    win->setWindowTitle(QString::fromStdString(to_display(a[1])));
    return Value::none();
}

// ==============================================================================
// PART 2: HIGH-LEVEL QT6 WIDGETS & LAYOUTS ENGINE (<qt> / <gui>)
// ==============================================================================
enum class WType {
    Window, VBox, HBox, Frame, GroupBox,
    Label, Button, Input, TextArea, CheckBox,
    Slider, ProgressBar, ComboBox, ListWidget
};

struct WidgetEntry {
    WType type;
    QWidget* widget = nullptr;
    QLayout* layout = nullptr;
    Value on_click_cb;
    Value on_change_cb;
};

static std::unordered_map<int64_t, WidgetEntry> g_widgets;
static int64_t g_next_widget_id = 1000;

static WidgetEntry& get_widget_entry(const Value& v) {
    int64_t id = v.as_int();
    auto it = g_widgets.find(id);
    if (it == g_widgets.end()) {
        fail("Invalid Qt widget handle: " + std::to_string(id));
    }
    return it->second;
}

static void attach_to_parent(const Value& parent_val, QWidget* child) {
    if (parent_val.tag == Tag::None || parent_val.tag == Tag::Undef ||
        (parent_val.is_int_like() && parent_val.as_int() == 0)) {
        return;
    }
    WidgetEntry& pe = get_widget_entry(parent_val);
    if (pe.layout) {
        pe.layout->addWidget(child);
    } else if (pe.widget) {
        if (!pe.widget->layout()) {
            auto* vbox = new QVBoxLayout(pe.widget);
            pe.layout = vbox;
        }
        pe.widget->layout()->addWidget(child);
    }
}

static int64_t register_widget(WType t, QWidget* w, QLayout* l = nullptr) {
    int64_t id = g_next_widget_id++;
    g_widgets[id] = WidgetEntry{t, w, l, Value::none(), Value::none()};
    return id;
}

Value qt_app(const Value*, size_t, const Value&) {
    ensure_qt_app();
    return Value::boolean(true);
}

Value qt_window(const Value* a, size_t n, const Value&) {
    ensure_qt_app();
    std::string title = n >= 1 ? to_display(a[0]) : "Nix Qt Window";
    int w = n >= 2 ? (int)a[1].as_int() : 500;
    int h = n >= 3 ? (int)a[2].as_int() : 400;

    auto* win = new QWidget();
    win->setWindowTitle(QString::fromStdString(title));
    win->resize(w, h);
    auto* layout = new QVBoxLayout(win);
    win->show();
    win->raise();
    win->activateWindow();
    QApplication::processEvents();
    int64_t id = register_widget(WType::Window, win, layout);
    return Value::integer(id);
}

Value qt_show(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_show expects 1 argument: (widget_id)");
    WidgetEntry& e = get_widget_entry(a[0]);
    if (e.widget) {
        e.widget->show();
        e.widget->raise();
        e.widget->activateWindow();
    }
    return Value::none();
}

Value qt_hide(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_hide expects 1 argument: (widget_id)");
    WidgetEntry& e = get_widget_entry(a[0]);
    if (e.widget) e.widget->hide();
    return Value::none();
}

Value qt_close(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_close expects 1 argument: (widget_id)");
    WidgetEntry& e = get_widget_entry(a[0]);
    if (e.widget) e.widget->close();
    return Value::none();
}

static bool g_event_loop_ran = false;

Value qt_run(const Value* a, size_t n, const Value&) {
    ensure_qt_app();
    if (n >= 1 && a[0].is_int_like()) {
        WidgetEntry& e = get_widget_entry(a[0]);
        if (e.widget && !e.widget->isVisible()) {
            e.widget->show();
            e.widget->raise();
            e.widget->activateWindow();
        }
    }
    g_event_loop_ran = true;
    return Value::integer(QApplication::exec());
}

Value qt_quit(const Value*, size_t, const Value&) {
    if (QApplication::instance()) {
        QApplication::quit();
    }
    return Value::none();
}

// --- Containers & Layouts ---

Value qt_vbox(const Value* a, size_t n, const Value&) {
    ensure_qt_app();
    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    if (n >= 1) attach_to_parent(a[0], container);
    return Value::integer(register_widget(WType::VBox, container, layout));
}

Value qt_hbox(const Value* a, size_t n, const Value&) {
    ensure_qt_app();
    auto* container = new QWidget();
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    if (n >= 1) attach_to_parent(a[0], container);
    return Value::integer(register_widget(WType::HBox, container, layout));
}

Value qt_frame(const Value* a, size_t n, const Value&) {
    ensure_qt_app();
    auto* frame = new QFrame();
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setFrameShadow(QFrame::Raised);
    auto* layout = new QVBoxLayout(frame);
    if (n >= 1) attach_to_parent(a[0], frame);
    return Value::integer(register_widget(WType::Frame, frame, layout));
}

Value qt_groupbox(const Value* a, size_t n, const Value&) {
    ensure_qt_app();
    std::string title = n >= 2 ? to_display(a[1]) : "";
    auto* gb = new QGroupBox(QString::fromStdString(title));
    auto* layout = new QVBoxLayout(gb);
    if (n >= 1) attach_to_parent(a[0], gb);
    return Value::integer(register_widget(WType::GroupBox, gb, layout));
}

// --- Widgets ---

Value qt_label(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_label expects at least 1 argument: (parent, [text])");
    ensure_qt_app();
    std::string txt = n >= 2 ? to_display(a[1]) : "";
    auto* lbl = new QLabel(QString::fromStdString(txt));
    attach_to_parent(a[0], lbl);
    return Value::integer(register_widget(WType::Label, lbl));
}

Value qt_button(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_button expects at least 1 argument: (parent, [text], [callback])");
    ensure_qt_app();
    std::string txt = n >= 2 ? to_display(a[1]) : "Button";
    auto* btn = new QPushButton(QString::fromStdString(txt));
    attach_to_parent(a[0], btn);
    int64_t id = register_widget(WType::Button, btn);

    if (n >= 3 && a[2].tag != Tag::None && a[2].tag != Tag::Undef) {
        g_widgets[id].on_click_cb = a[2];
        QObject::connect(btn, &QPushButton::clicked, [id]() {
            Value& cb = g_widgets[id].on_click_cb;
            if (cb.tag != Tag::None && cb.tag != Tag::Undef) {
                call(cb, nullptr, 0);
            }
        });
    }
    return Value::integer(id);
}

Value qt_input(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_input expects at least 1 argument: (parent, [placeholder])");
    ensure_qt_app();
    auto* edit = new QLineEdit();
    if (n >= 2) edit->setPlaceholderText(QString::fromStdString(to_display(a[1])));
    attach_to_parent(a[0], edit);
    return Value::integer(register_widget(WType::Input, edit));
}

Value qt_textarea(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_textarea expects at least 1 argument: (parent, [initial_text])");
    ensure_qt_app();
    auto* area = new QTextEdit();
    if (n >= 2) area->setPlainText(QString::fromStdString(to_display(a[1])));
    attach_to_parent(a[0], area);
    return Value::integer(register_widget(WType::TextArea, area));
}

Value qt_checkbox(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_checkbox expects at least 1 argument: (parent, [text], [checked])");
    ensure_qt_app();
    std::string txt = n >= 2 ? to_display(a[1]) : "";
    auto* cb = new QCheckBox(QString::fromStdString(txt));
    if (n >= 3) cb->setChecked(truthy(a[2]));
    attach_to_parent(a[0], cb);
    return Value::integer(register_widget(WType::CheckBox, cb));
}

Value qt_slider(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_slider expects at least 1 argument: (parent, [min], [max], [value])");
    ensure_qt_app();
    int min_v = n >= 2 ? (int)a[1].as_int() : 0;
    int max_v = n >= 3 ? (int)a[2].as_int() : 100;
    int val_v = n >= 4 ? (int)a[3].as_int() : min_v;

    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(min_v, max_v);
    s->setValue(val_v);
    attach_to_parent(a[0], s);
    return Value::integer(register_widget(WType::Slider, s));
}

Value qt_progressbar(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_progressbar expects at least 1 argument: (parent, [min], [max], [value])");
    ensure_qt_app();
    int min_v = n >= 2 ? (int)a[1].as_int() : 0;
    int max_v = n >= 3 ? (int)a[2].as_int() : 100;
    int val_v = n >= 4 ? (int)a[3].as_int() : 0;

    auto* pb = new QProgressBar();
    pb->setRange(min_v, max_v);
    pb->setValue(val_v);
    attach_to_parent(a[0], pb);
    return Value::integer(register_widget(WType::ProgressBar, pb));
}

Value qt_combobox(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_combobox expects at least 1 argument: (parent, [items_list])");
    ensure_qt_app();
    auto* cb = new QComboBox();
    if (n >= 2 && a[1].tag == Tag::List) {
        for (const auto& item : as_list(a[1])->items) {
            cb->addItem(QString::fromStdString(to_display(item)));
        }
    }
    attach_to_parent(a[0], cb);
    return Value::integer(register_widget(WType::ComboBox, cb));
}

Value qt_listwidget(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_listwidget expects at least 1 argument: (parent, [items_list])");
    ensure_qt_app();
    auto* lw = new QListWidget();
    if (n >= 2 && a[1].tag == Tag::List) {
        for (const auto& item : as_list(a[1])->items) {
            lw->addItem(QString::fromStdString(to_display(item)));
        }
    }
    attach_to_parent(a[0], lw);
    return Value::integer(register_widget(WType::ListWidget, lw));
}

// --- Properties, Events & Manipulation ---

Value qt_set_text(const Value* a, size_t n, const Value&) {
    if (n < 2) fail("qt_set_text expects 2 arguments: (widget_id, text)");
    WidgetEntry& e = get_widget_entry(a[0]);
    QString qtxt = QString::fromStdString(to_display(a[1]));
    switch (e.type) {
        case WType::Label: static_cast<QLabel*>(e.widget)->setText(qtxt); break;
        case WType::Button: static_cast<QPushButton*>(e.widget)->setText(qtxt); break;
        case WType::Input: static_cast<QLineEdit*>(e.widget)->setText(qtxt); break;
        case WType::TextArea: static_cast<QTextEdit*>(e.widget)->setPlainText(qtxt); break;
        case WType::CheckBox: static_cast<QCheckBox*>(e.widget)->setText(qtxt); break;
        case WType::GroupBox: static_cast<QGroupBox*>(e.widget)->setTitle(qtxt); break;
        default: if (e.widget) e.widget->setWindowTitle(qtxt); break;
    }
    return Value::none();
}

Value qt_get_text(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_get_text expects 1 argument: (widget_id)");
    WidgetEntry& e = get_widget_entry(a[0]);
    switch (e.type) {
        case WType::Label: return make_str(static_cast<QLabel*>(e.widget)->text().toStdString());
        case WType::Button: return make_str(static_cast<QPushButton*>(e.widget)->text().toStdString());
        case WType::Input: return make_str(static_cast<QLineEdit*>(e.widget)->text().toStdString());
        case WType::TextArea: return make_str(static_cast<QTextEdit*>(e.widget)->toPlainText().toStdString());
        case WType::CheckBox: return make_str(static_cast<QCheckBox*>(e.widget)->text().toStdString());
        case WType::ComboBox: return make_str(static_cast<QComboBox*>(e.widget)->currentText().toStdString());
        case WType::ListWidget: {
            auto* item = static_cast<QListWidget*>(e.widget)->currentItem();
            return make_str(item ? item->text().toStdString() : "");
        }
        default: return make_str(e.widget ? e.widget->windowTitle().toStdString() : "");
    }
}

Value qt_set_value(const Value* a, size_t n, const Value&) {
    if (n < 2) fail("qt_set_value expects 2 arguments: (widget_id, value)");
    WidgetEntry& e = get_widget_entry(a[0]);
    int val = (int)a[1].as_int();
    switch (e.type) {
        case WType::Slider: static_cast<QSlider*>(e.widget)->setValue(val); break;
        case WType::ProgressBar: static_cast<QProgressBar*>(e.widget)->setValue(val); break;
        case WType::ComboBox: static_cast<QComboBox*>(e.widget)->setCurrentIndex(val); break;
        case WType::ListWidget: static_cast<QListWidget*>(e.widget)->setCurrentRow(val); break;
        default: break;
    }
    return Value::none();
}

Value qt_get_value(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_get_value expects 1 argument: (widget_id)");
    WidgetEntry& e = get_widget_entry(a[0]);
    switch (e.type) {
        case WType::Slider: return Value::integer(static_cast<QSlider*>(e.widget)->value());
        case WType::ProgressBar: return Value::integer(static_cast<QProgressBar*>(e.widget)->value());
        case WType::ComboBox: return Value::integer(static_cast<QComboBox*>(e.widget)->currentIndex());
        case WType::ListWidget: return Value::integer(static_cast<QListWidget*>(e.widget)->currentRow());
        case WType::CheckBox: return Value::boolean(static_cast<QCheckBox*>(e.widget)->isChecked());
        default: return Value::integer(0);
    }
}

Value qt_is_checked(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_is_checked expects 1 argument: (checkbox_id)");
    WidgetEntry& e = get_widget_entry(a[0]);
    if (e.type == WType::CheckBox) {
        return Value::boolean(static_cast<QCheckBox*>(e.widget)->isChecked());
    }
    return Value::boolean(false);
}

Value qt_set_checked(const Value* a, size_t n, const Value&) {
    if (n < 2) fail("qt_set_checked expects 2 arguments: (checkbox_id, checked)");
    WidgetEntry& e = get_widget_entry(a[0]);
    if (e.type == WType::CheckBox) {
        static_cast<QCheckBox*>(e.widget)->setChecked(truthy(a[1]));
    }
    return Value::none();
}

Value qt_set_style(const Value* a, size_t n, const Value&) {
    if (n < 2) fail("qt_set_style expects 2 arguments: (widget_id, qss_stylesheet)");
    WidgetEntry& e = get_widget_entry(a[0]);
    if (e.widget) {
        e.widget->setStyleSheet(QString::fromStdString(to_display(a[1])));
    }
    return Value::none();
}

Value qt_set_title(const Value* a, size_t n, const Value&) {
    if (n < 2) fail("qt_set_title expects 2 arguments: (window_id, title)");
    WidgetEntry& e = get_widget_entry(a[0]);
    if (e.widget) {
        e.widget->setWindowTitle(QString::fromStdString(to_display(a[1])));
    }
    return Value::none();
}

Value qt_resize(const Value* a, size_t n, const Value&) {
    if (n < 3) fail("qt_resize expects 3 arguments: (widget_id, width, height)");
    WidgetEntry& e = get_widget_entry(a[0]);
    if (e.widget) {
        e.widget->resize((int)a[1].as_int(), (int)a[2].as_int());
    }
    return Value::none();
}

Value qt_set_enabled(const Value* a, size_t n, const Value&) {
    if (n < 2) fail("qt_set_enabled expects 2 arguments: (widget_id, enabled)");
    WidgetEntry& e = get_widget_entry(a[0]);
    if (e.widget) {
        e.widget->setEnabled(truthy(a[1]));
    }
    return Value::none();
}

Value qt_add_item(const Value* a, size_t n, const Value&) {
    if (n < 2) fail("qt_add_item expects 2 arguments: (widget_id, item_text)");
    WidgetEntry& e = get_widget_entry(a[0]);
    QString qtxt = QString::fromStdString(to_display(a[1]));
    if (e.type == WType::ComboBox) static_cast<QComboBox*>(e.widget)->addItem(qtxt);
    else if (e.type == WType::ListWidget) static_cast<QListWidget*>(e.widget)->addItem(qtxt);
    return Value::none();
}

Value qt_clear_items(const Value* a, size_t n, const Value&) {
    if (n < 1) fail("qt_clear_items expects 1 argument: (widget_id)");
    WidgetEntry& e = get_widget_entry(a[0]);
    if (e.type == WType::ComboBox) static_cast<QComboBox*>(e.widget)->clear();
    else if (e.type == WType::ListWidget) static_cast<QListWidget*>(e.widget)->clear();
    return Value::none();
}

Value qt_on_click(const Value* a, size_t n, const Value&) {
    if (n < 2) fail("qt_on_click expects 2 arguments: (button_id, callback_fn)");
    int64_t id = a[0].as_int();
    WidgetEntry& e = get_widget_entry(a[0]);
    e.on_click_cb = a[1];
    if (e.type == WType::Button) {
        auto* btn = static_cast<QPushButton*>(e.widget);
        QObject::disconnect(btn, &QPushButton::clicked, nullptr, nullptr);
        QObject::connect(btn, &QPushButton::clicked, [id]() {
            Value& cb = g_widgets[id].on_click_cb;
            if (cb.tag != Tag::None && cb.tag != Tag::Undef) {
                call(cb, nullptr, 0);
            }
        });
    }
    return Value::none();
}

Value qt_on_change(const Value* a, size_t n, const Value&) {
    if (n < 2) fail("qt_on_change expects 2 arguments: (widget_id, callback_fn)");
    int64_t id = a[0].as_int();
    WidgetEntry& e = get_widget_entry(a[0]);
    e.on_change_cb = a[1];
    if (e.type == WType::Slider) {
        auto* s = static_cast<QSlider*>(e.widget);
        QObject::connect(s, &QSlider::valueChanged, [id](int) {
            Value& cb = g_widgets[id].on_change_cb;
            if (cb.tag != Tag::None && cb.tag != Tag::Undef) call(cb, nullptr, 0);
        });
    } else if (e.type == WType::Input) {
        auto* ed = static_cast<QLineEdit*>(e.widget);
        QObject::connect(ed, &QLineEdit::textChanged, [id](const QString&) {
            Value& cb = g_widgets[id].on_change_cb;
            if (cb.tag != Tag::None && cb.tag != Tag::Undef) call(cb, nullptr, 0);
        });
    } else if (e.type == WType::CheckBox) {
        auto* cb_w = static_cast<QCheckBox*>(e.widget);
        QObject::connect(cb_w, &QCheckBox::toggled, [id](bool) {
            Value& cb = g_widgets[id].on_change_cb;
            if (cb.tag != Tag::None && cb.tag != Tag::Undef) call(cb, nullptr, 0);
        });
    } else if (e.type == WType::ComboBox) {
        auto* combo = static_cast<QComboBox*>(e.widget);
        QObject::connect(combo, &QComboBox::currentIndexChanged, [id](int) {
            Value& cb = g_widgets[id].on_change_cb;
            if (cb.tag != Tag::None && cb.tag != Tag::Undef) call(cb, nullptr, 0);
        });
    }
    return Value::none();
}

Value qt_timer(const Value* a, size_t n, const Value&) {
    if (n < 2) fail("qt_timer expects 2 arguments: (interval_ms, callback_fn, [repeat=true])");
    ensure_qt_app();
    int ms = (int)a[0].as_int();
    Value cb = a[1];
    bool repeat = (n >= 3) ? truthy(a[2]) : true;

    auto* timer = new QTimer();
    timer->setSingleShot(!repeat);
    QObject::connect(timer, &QTimer::timeout, [cb]() {
        if (cb.tag != Tag::None && cb.tag != Tag::Undef) {
            call(cb, nullptr, 0);
        }
    });
    timer->start(ms);
    return Value::boolean(true);
}

// --- Native Dialogs ---

Value qt_msgbox(const Value* a, size_t n, const Value&) {
    if (n < 2) fail("qt_msgbox expects 2 arguments: (title, message)");
    ensure_qt_app();
    QMessageBox::information(
        nullptr,
        QString::fromStdString(to_display(a[0])),
        QString::fromStdString(to_display(a[1]))
    );
    return Value::none();
}

Value qt_confirm(const Value* a, size_t n, const Value&) {
    if (n < 2) fail("qt_confirm expects 2 arguments: (title, question)");
    ensure_qt_app();
    auto btn = QMessageBox::question(
        nullptr,
        QString::fromStdString(to_display(a[0])),
        QString::fromStdString(to_display(a[1])),
        QMessageBox::Yes | QMessageBox::No
    );
    return Value::boolean(btn == QMessageBox::Yes);
}

Value qt_file_dialog(const Value* a, size_t n, const Value&) {
    ensure_qt_app();
    std::string title = n >= 1 ? to_display(a[0]) : "Open File";
    std::string filter = n >= 2 ? to_display(a[1]) : "All Files (*)";
    QString file = QFileDialog::getOpenFileName(
        nullptr,
        QString::fromStdString(title),
        QString(),
        QString::fromStdString(filter)
    );
    return make_str(file.toStdString());
}

Value qt_color_dialog(const Value* a, size_t n, const Value&) {
    ensure_qt_app();
    std::string title = n >= 1 ? to_display(a[0]) : "Select Color";
    QColor col = QColorDialog::getColor(Qt::white, nullptr, QString::fromStdString(title));
    if (!col.isValid()) return make_str("");
    return make_str(col.name().toStdString());
}

void auto_exec_if_windows_open() {
    if (!QApplication::instance() || g_event_loop_ran) {
        return;
    }

    bool any_open = false;
    // Check 2D Canvas windows
    for (const auto& kv : g_canvas_windows) {
        if (kv.second && kv.second->is_open_flag && kv.second->isVisible()) {
            any_open = true;
            break;
        }
    }
    // Check Qt Widget windows
    if (!any_open) {
        for (const auto& kv : g_widgets) {
            if (kv.second.type == WType::Window && kv.second.widget) {
                if (!kv.second.widget->isVisible()) {
                    kv.second.widget->show();
                    kv.second.widget->raise();
                    kv.second.widget->activateWindow();
                }
                any_open = true;
                break;
            }
        }
    }

    if (any_open) {
        g_event_loop_ran = true;
        QApplication::exec();
    }
}

}  // namespace qt_gui
}  // namespace nix
