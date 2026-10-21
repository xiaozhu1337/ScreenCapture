#include "SettingsDialog.h"
#include "Settings.h"
#include "GlobalShortcuts.h"
#include "Ui.h"
#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QStyleOptionFocusRect>
#include <QTableWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

namespace {
QPixmap iconPixels(const QString& id, const QSize& size, const QColor& color)
{
    auto pixels = Ui::icon(id).pixmap(size);
    QPainter painter(&pixels);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixels.rect(), color);
    return pixels;
}

class ToggleCheckBox : public QCheckBox
{
public:
    explicit ToggleCheckBox(QWidget* parent) : QCheckBox(parent) { setFixedSize(60, 39); }
protected:
    bool hitButton(const QPoint& point) const override { return rect().contains(point); }
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.drawPixmap(QRect((width() - 24) / 2, (height() - 24) / 2, 24, 24),
            iconPixels(isChecked() ? "toggle-on" : "toggle-off", QSize(24, 24),
                QColor(isChecked() ? "#597ef7" : "#666666")));
        if (hasFocus()) {
            QStyleOptionFocusRect option;
            option.initFrom(this);
            option.rect = rect().adjusted(2, 2, -2, -2);
            style()->drawPrimitive(QStyle::PE_FrameFocusRect, &option, &painter, this);
        }
    }
};

class CloseButton : public QPushButton
{
public:
    explicit CloseButton(QWidget* parent) : QPushButton(parent) { setFixedSize(42, 32); }
protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPushButton::paintEvent(event);
        QPainter painter(this);
        painter.drawPixmap(QRect(11, 6, 20, 20), iconPixels("close", QSize(20, 20),
            QColor(underMouse() ? "#ffffff" : "#333333")));
    }
};

class ShortcutEdit : public QKeySequenceEdit
{
public:
    using QKeySequenceEdit::QKeySequenceEdit;
protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
            const QSignalBlocker blocker(this);
            window()->close();
            return;
        }
        QKeySequenceEdit::keyPressEvent(event);
    }
};

class ToolDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const QString id = index.data(Qt::UserRole).toString();
        const bool enabled = index.data(Qt::UserRole + 1).toBool();
        painter->save();
        painter->drawPixmap(QRect(option.rect.left() + 4, option.rect.top() + 3, 20, 20),
            iconPixels(id, QSize(20, 20), QColor(enabled ? "#597ef7" : "#999999")));
        painter->setFont(option.font);
        painter->setPen(QColor("#333333"));
        const auto textRect = option.rect.adjusted(28, 0, 0, 0);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
            option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, textRect.width()));
        painter->restore();
    }
};

QVBoxLayout* pageLayout(QWidget* page)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 40, 20, 40);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignTop);
    return layout;
}

QLabel* label(const QString& key, QWidget* parent)
{
    auto* result = new QLabel(Ui::text(key), parent);
    result->setProperty("textKey", key);
    result->setWordWrap(true);
    result->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return result;
}
QPushButton* button(const QString& key, QWidget* parent)
{
    auto* result = new QPushButton(Ui::text(key), parent);
    result->setProperty("textKey", key);
    result->setObjectName(key);
    result->setAutoDefault(false);
    return result;
}

void addRow(QVBoxLayout* layout, const QString& key, QWidget* control)
{
    auto* page = layout->parentWidget();
    auto* row = new QWidget(page);
    row->setFixedHeight(39);
    auto* contents = new QHBoxLayout(row);
    contents->setContentsMargins(0, 0, 0, 0);
    contents->setSpacing(0);
    auto* text = label(key, row);
    text->setBuddy(control);
    contents->addWidget(text, 1);
    contents->addWidget(control);
    layout->addWidget(row);
    auto* separator = new QFrame(page);
    separator->setObjectName("separator");
    separator->setFixedHeight(1);
    separator->setStyleSheet("background: #e0e0e0;");
    layout->addWidget(separator);
}
}

SettingsDialog::SettingsDialog(Settings& settings, QWidget* parent) : QDialog(parent), settings(settings)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setObjectName("settingsDialog");
    Ui::setLanguage(settings.values.language);
    resize(680, 560);
    setStyleSheet(
        "#settingsDialog { background: #fafafa; }"
        "QWidget { color: #333333; font-size: 14px; }"
        "#settingsSidebar { background: #eeeef0; }"
        "QPushButton { border: none; border-radius: 0; background: transparent; padding: 0; }"
        "QPushButton:hover { background: #f2f2f2; }"
        "QPushButton:disabled { color: #cccccc; }"
        "QPushButton:focus { border: 1px dotted #597ef7; }"
        "#settingsSidebar QPushButton { text-align: center; color: #333333; }"
        "#settingsSidebar QPushButton:hover { background: #e1e1e3; }"
        "#settingsSidebar QPushButton:checked { background: #597ef7; color: white; }"
        "#settingsClose:hover { background: #e81123; color: white; }"
        "QTabWidget::pane { border: none; }"
        "QComboBox, QKeySequenceEdit { border: 1px solid #e0e0e0; border-radius: 0; background: white; padding: 0; }"
        "QComboBox QAbstractItemView { background: white; selection-background-color: #f2f2f2; selection-color: black; }"
        "QComboBox QAbstractItemView::item { min-height: 30px; }"
        "QLabel[hint=\"true\"] { color: #666666; font-size: 12px; }"
        "QTableWidget { background: transparent; border: none; }"
        "QPushButton[toolbarToggle=\"true\"] { color: #597ef7; }"
        "QPushButton[toolbarToggle=\"true\"]:hover { background: #e6f4ff; }"
        "QPushButton[link=\"true\"] { color: #597ef7; text-align: right; }"
        "QPushButton[link=\"true\"]:hover { background: transparent; }");
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* sidebar = new QWidget(this);
    sidebar->setObjectName("settingsSidebar");
    sidebar->setFixedWidth(160);
    auto* menu = new QVBoxLayout(sidebar);
    menu->setContentsMargins(0, 40, 0, 0);
    menu->setSpacing(0);
    navigation = new QButtonGroup(this);
    const QStringList keys{"setting.common", "setting.shortcut", "setting.toolbar", "setting.about"};
    for (int index = 0; index < keys.size(); ++index) {
        auto* item = button(keys[index], sidebar);
        item->setCheckable(true);
        item->setFixedHeight(40);
        navigation->addButton(item, index);
        menu->addWidget(item);
    }
    menu->addStretch();
    navigation->button(0)->setChecked(true);
    layout->addWidget(sidebar);
    tabs = new QTabWidget(this);
    // QTabWidget includes hidden tabs in its minimum height.
    tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    layout->addWidget(tabs);
    connect(navigation, &QButtonGroup::idClicked, tabs, &QTabWidget::setCurrentIndex);
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        navigation->button(index)->setChecked(true);
    });
    addGeneral();
    addShortcuts();
    addToolbar();
    addAbout();
    tabs->tabBar()->hide();
    auto* close = new CloseButton(this);
    close->setObjectName("settingsClose");
    close->setAutoDefault(false);
    close->move(width() - close->width(), 0);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    retranslate();
}

void SettingsDialog::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->position().y() < 40
        && event->position().x() < width() - 42 && windowHandle()) {
        windowHandle()->startSystemMove();
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

void SettingsDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (auto* close = findChild<QPushButton*>("settingsClose")) close->move(width() - close->width(), 0);
}

bool SettingsDialog::save(Preferences next)
{
    const QString problem = Settings::validate(next);
    const QList<QKeySequence> pinReserved{QKeySequence("Ctrl+H"), QKeySequence("Ctrl+R"),
        QKeySequence("Ctrl+K"), QKeySequence("Ctrl+P")};
    if (!problem.isEmpty() || pinReserved.contains(next.pinKey)) {
        QMessageBox::warning(this, Ui::text("setting.title"), Ui::text("shortcut.invalid") + '\n' + problem);
        return false;
    }
    for (const auto& key : {next.captureKey, next.ocrKey}) {
        if (!shortcutAvailable(key)) {
            QMessageBox::warning(this, Ui::text("setting.title"), Ui::text("shortcut.invalid") + '\n' + key.toString());
            return false;
        }
    }
    bool replaceInvalid = false;
    if (!settings.error.isEmpty()) {
        replaceInvalid = QMessageBox::question(this, Ui::text("setting.title"), settings.error
            + tr("\n是否用当前设置替换配置文件？")) == QMessageBox::Yes;
        if (!replaceInvalid) return false;
    }
    const auto previous = settings.values;
    settings.values = next;
    if (settings.save(replaceInvalid)) return true;
    settings.values = previous;
    QMessageBox::warning(this, Ui::text("setting.title"), settings.error);
    return false;
}

void SettingsDialog::addGeneral()
{
    auto* page = new QWidget(tabs);
    auto* form = pageLayout(page);
    auto* startup = new ToggleCheckBox(page);
    startup->setObjectName("autoStart");
    startup->setChecked(settings.autoStart());
    addRow(form, "setting.autoStart", startup);
    connect(startup, &QCheckBox::toggled, this, [this, startup](bool enabled) {
        if (settings.setAutoStart(enabled)) return;
        const QSignalBlocker blocker(startup);
        startup->setChecked(settings.autoStart());
        QMessageBox::warning(this, Ui::text("setting.title"), settings.error);
    });
    auto* bottom = new ToggleCheckBox(page);
    bottom->setObjectName("toolbarBottom");
    bottom->setChecked(settings.values.toolbarBottom);
    addRow(form, "setting.toolbarBottom", bottom);
    auto* hint = label("setting.toolbarBottomHint", page);
    hint->setProperty("hint", true);
    hint->setFixedHeight(44);
    form->addWidget(hint);
    connect(bottom, &QCheckBox::toggled, this, [this, bottom](bool enabled) {
        auto next = settings.values;
        next.toolbarBottom = enabled;
        if (save(next)) return;
        const QSignalBlocker blocker(bottom);
        bottom->setChecked(settings.values.toolbarBottom);
    });
    auto* language = new QComboBox(page);
    language->setObjectName("language");
    language->setFixedSize(160, 28);
    const QMap<QString, QString> names{{"zh-CN", "简体中文"}, {"zh-TW", "繁體中文"},
        {"en-US", "English"}, {"ja-JP", "日本語"}, {"ru-RU", "Русский"}, {"id-ID", "Bahasa Indonesia"}};
    for (const auto& code : Ui::languages()) language->addItem(names.value(code, code), code);
    language->addItem(Ui::text("setting.getMoreLang"), "setting.getMoreLang");
    language->setCurrentIndex(language->findData(settings.values.language));
    addRow(form, "setting.language", language);
    connect(language, &QComboBox::currentIndexChanged, this, [this, language] {
        if (language->currentData().toString() == "setting.getMoreLang") {
            const QSignalBlocker blocker(language);
            language->setCurrentIndex(language->findData(settings.values.language));
            QDesktopServices::openUrl(QUrl("https://github.com/xland/ScreenCapture/tree/main/Lang"));
            return;
        }
        auto next = settings.values;
        next.language = language->currentData().toString();
        if (save(next)) {
            Ui::setLanguage(next.language);
            retranslate();
        } else {
            const QSignalBlocker blocker(language);
            language->setCurrentIndex(language->findData(settings.values.language));
        }
    });
    tabs->addTab(page, Ui::text("setting.common"));
}

void SettingsDialog::addShortcuts()
{
    auto* page = new QWidget(tabs);
    auto* form = pageLayout(page);
    const QList<QKeySequence Preferences::*> fields{&Preferences::captureKey, &Preferences::pinKey};
    const QStringList labels{"shortcut.cap", "shortcut.pin"};
    const QStringList names{"captureKey", "pinKey"};
    for (int index = 0; index < fields.size(); ++index) {
        const auto field = fields[index];
        auto* row = new QWidget(page);
        auto* contents = new QHBoxLayout(row);
        contents->setContentsMargins(0, 0, 0, 0);
        contents->setSpacing(0);
        row->setFixedWidth(index == 0 ? 160 : 216);
        auto* edit = new ShortcutEdit(settings.values.*field, row);
        edit->setObjectName(names[index]);
        edit->setFixedSize(160, 28);
        edit->setMaximumSequenceLength(1);
        contents->addWidget(edit);
        addRow(form, labels[index], row);
        auto persist = [this, edit, field] {
            if (edit->keySequence() == settings.values.*field) return;
            const QSignalBlocker blocker(edit);
            auto next = settings.values;
            next.*field = edit->keySequence();
            if (!save(next)) edit->setKeySequence(settings.values.*field);
        };
        connect(edit, &QKeySequenceEdit::editingFinished, this, persist);
        if (index == 1) {
            auto* clear = button("shortcut.clear", row);
            clear->setObjectName("pinKeyClear");
            clear->setFixedSize(56, 28);
            contents->addWidget(clear);
            connect(clear, &QPushButton::clicked, this, [edit, persist] { edit->clear(); persist(); });
        }
    }
    auto* hint = label("shortcut.pinHint", page);
    hint->setProperty("hint", true);
    hint->setFixedHeight(56);
    form->addWidget(hint);
    tabs->addTab(page, Ui::text("setting.shortcut"));
}

void SettingsDialog::addToolbar()
{
    auto* page = new QWidget(tabs);
    auto* layout = pageLayout(page);
    auto* title = label("toolbar.title", page);
    title->setFixedHeight(28);
    layout->addWidget(title);
    auto* hint = label("toolbar.hint", page);
    hint->setProperty("hint", true);
    hint->setFixedHeight(64);
    layout->addWidget(hint);
    auto* reset = button("toolbar.reset", page);
    reset->setFixedSize(104, 28);
    reset->setStyleSheet("QPushButton { border: 1px solid #e0e0e0; }");
    layout->addWidget(reset, 0, Qt::AlignLeft);
    tools = new QTableWidget(Ui::captureTools().size(), 4, page);
    tools->setObjectName("captureToolbar");
    tools->setShowGrid(false);
    tools->setFrameShape(QFrame::NoFrame);
    tools->setIconSize(QSize(20, 20));
    tools->setItemDelegateForColumn(0, new ToolDelegate(tools));
    tools->setFixedHeight(tools->rowCount() * 26);
    tools->verticalHeader()->setMinimumSectionSize(26);
    tools->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    tools->setColumnWidth(1, 44);
    tools->setColumnWidth(2, 44);
    tools->setColumnWidth(3, 64);
    tools->horizontalHeader()->hide();
    tools->verticalHeader()->hide();
    tools->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    tools->setSelectionMode(QAbstractItemView::NoSelection);
    tools->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int row = 0; row < tools->rowCount(); ++row) {
        tools->setItem(row, 0, new QTableWidgetItem);
        tools->setRowHeight(row, 26);
        for (int column = 1; column < 4; ++column) {
            auto* cell = new QWidget(tools);
            auto* contents = new QHBoxLayout(cell);
            contents->setContentsMargins(0, 0, 0, 0);
            contents->setSpacing(0);
            auto* action = new QPushButton(cell);
            action->setAutoDefault(false);
            action->setFixedSize(column == 3 ? 64 : 44, 24);
            action->setProperty("toolbarToggle", column == 3);
            contents->addWidget(action, 0, Qt::AlignVCenter);
            tools->setCellWidget(row, column, cell);
            connect(action, &QPushButton::clicked, this, [this, row, column] {
                const auto id = tools->item(row, 0)->data(Qt::UserRole).toString();
                auto next = settings.values;
                const int index = next.captureToolbar.indexOf(id);
                if (column == 3) {
                    if (index < 0) next.captureToolbar.append(id);
                    else next.captureToolbar.removeAt(index);
                } else {
                    const int target = index + (column == 1 ? -1 : 1);
                    if (index < 0 || target < 0 || target >= next.captureToolbar.size()) return;
                    next.captureToolbar.swapItemsAt(index, target);
                }
                save(next);
                refreshToolbar();
            });
        }
    }
    layout->addWidget(tools);
    connect(reset, &QPushButton::clicked, this, [this] {
        auto next = settings.values;
        next.captureToolbar = Preferences{}.captureToolbar;
        save(next);
        refreshToolbar();
    });
    tabs->addTab(page, Ui::text("setting.toolbar"));
}

void SettingsDialog::refreshToolbar()
{
    const auto enabled = settings.values.captureToolbar;
    auto order = enabled;
    for (const auto& id : Ui::captureTools()) if (!order.contains(id)) order.append(id);
    for (int row = 0; row < order.size(); ++row) {
        const auto id = order[row];
        const int index = enabled.indexOf(id);
        const QString key = (Ui::drawingTools().contains(id) ? "tool." : "cap.") + id;
        auto* item = tools->item(row, 0);
        item->setText((index < 0 ? QString() : QString::number(index + 1) + ". ") + Ui::text(key));
        item->setIcon(Ui::icon(id));
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, index >= 0);
        for (int column = 1; column < 4; ++column) {
            auto* action = tools->cellWidget(row, column)->findChild<QPushButton*>();
            const QString operation = column == 1 ? "up" : column == 2 ? "down" : index < 0 ? "add" : "remove";
            action->setText(Ui::text("toolbar." + operation));
            action->setObjectName((column == 3 ? "toggle" : operation) + '-' + id);
            action->setAccessibleName(Ui::text("toolbar." + operation) + ' ' + Ui::text(key));
            action->setEnabled(column == 3 || (index >= 0 && (column == 1 ? index > 0 : index + 1 < enabled.size())));
        }
    }
}

void SettingsDialog::addAbout()
{
    auto* page = new QWidget(tabs);
    auto* form = pageLayout(page);
    auto* version = new QLabel(QCoreApplication::applicationVersion(), page);
    version->setFixedSize(120, 28);
    version->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    addRow(form, "about.version", version);
    auto* project = new QPushButton("github.com/xland/ScreenCapture", page);
    project->setAutoDefault(false);
    project->setProperty("link", true);
    project->setFixedSize(120, 28);
    addRow(form, "about.project", project);
    connect(project, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl("https://github.com/xland/ScreenCapture"));
    });
    auto* author = button("about.wechat", page);
    author->setProperty("link", true);
    author->setFixedSize(120, 28);
    addRow(form, "about.author", author);
    connect(author, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText("liulun_007");
        QMessageBox::information(this, Ui::text("about.sysTip"), Ui::text("about.copySuccess"));
    });
    tabs->addTab(page, Ui::text("setting.about"));
}

void SettingsDialog::retranslate()
{
    setWindowTitle(Ui::text("setting.title"));
    const QStringList keys{"setting.common", "setting.shortcut", "setting.toolbar", "setting.about"};
    for (int index = 0; index < keys.size(); ++index) tabs->setTabText(index, Ui::text(keys[index]));
    for (auto* text : findChildren<QLabel*>()) {
        const auto key = text->property("textKey").toString();
        if (!key.isEmpty()) text->setText(Ui::text(key));
    }
    for (auto* action : findChildren<QAbstractButton*>()) {
        const auto key = action->property("textKey").toString();
        if (!key.isEmpty()) action->setText(Ui::text(key));
    }
    auto* language = findChild<QComboBox*>("language");
    language->setItemText(language->count() - 1, Ui::text("setting.getMoreLang"));
    findChild<QCheckBox*>("autoStart")->setAccessibleName(Ui::text("setting.autoStart"));
    findChild<QCheckBox*>("toolbarBottom")->setAccessibleName(Ui::text("setting.toolbarBottom"));
    findChild<QPushButton*>("settingsClose")->setAccessibleName(Ui::text("tool.close"));
    refreshToolbar();
}
