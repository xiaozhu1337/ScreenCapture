#include "SettingsDialog.h"
#include "Settings.h"
#include "GlobalShortcuts.h"
#include <QCoreApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(Settings& settings, QWidget* parent) : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("ScreenCapture 设置"));
    resize(650, 540);
    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    layout->addWidget(tabs);
    auto* general = new QWidget(tabs);
    auto* form = new QFormLayout(general);
    auto* startup = new QCheckBox(tr("登录后在托盘中待命"), general);
    startup->setChecked(settings.autoStart());
    form->addRow(tr("登录自启"), startup);
    auto* capture = new QCheckBox(tr("普通启动时立即截图"), general);
    capture->setChecked(settings.values.startCapture);
    form->addRow(tr("启动行为"), capture);
    auto* bottom = new QCheckBox(tr("工具栏固定在底部"), general);
    bottom->setChecked(settings.values.toolbarBottom);
    form->addRow(tr("工具栏位置"), bottom);
    auto* directory = new QLineEdit(settings.values.saveDirectory, general);
    auto* browse = new QPushButton(tr("选择目录"), general);
    auto* directoryRow = new QHBoxLayout;
    directoryRow->addWidget(directory);
    directoryRow->addWidget(browse);
    form->addRow(tr("图片保存目录"), directoryRow);
    connect(browse, &QPushButton::clicked, this, [this, directory] {
        const QString path = QFileDialog::getExistingDirectory(this, tr("图片保存目录"), directory->text());
        if (!path.isEmpty()) directory->setText(path);
    });
    auto* width = new QSpinBox(general);
    width->setRange(1, 100);
    width->setValue(settings.values.lineWidth);
    form->addRow(tr("标注线宽"), width);
    auto* color = new QPushButton(settings.values.color.name(), general);
    connect(color, &QPushButton::clicked, this, [this, color] {
        const QColor selected = QColorDialog::getColor(QColor(color->text()), this, tr("标注颜色"));
        if (selected.isValid()) color->setText(selected.name());
    });
    form->addRow(tr("标注颜色"), color);
    form->addRow(tr("界面语言"), new QLabel(tr("简体中文"), general));
    auto* location = new QLabel(tr("配置文件：%1").arg(settings.path()), general);
    location->setWordWrap(true);
    location->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(location);
    tabs->addTab(general, tr("通用"));

    auto* shortcuts = new QWidget(tabs);
    auto* keys = new QFormLayout(shortcuts);
    auto* captureKey = new QKeySequenceEdit(settings.values.captureKey, shortcuts);
    auto* ocrKey = new QKeySequenceEdit(settings.values.ocrKey, shortcuts);
    auto* pinKey = new QKeySequenceEdit(settings.values.pinKey, shortcuts);
    for (auto* key : {captureKey, ocrKey, pinKey}) {
        key->setMaximumSequenceLength(1);
        key->setClearButtonEnabled(true);
    }
    keys->addRow(tr("全局截图"), captureKey);
    keys->addRow(tr("全局截图并识别文字"), ocrKey);
    keys->addRow(tr("编辑窗口置顶钉图"), pinKey);
    auto* hint = new QLabel(tr("全局快捷键由 KDE 管理，清空可禁用。若组合已被占用，保存时会提示。钉图快捷键只在编辑窗口中生效。"), shortcuts);
    hint->setWordWrap(true);
    keys->addRow(hint);
    tabs->addTab(shortcuts, tr("快捷键"));

    auto* toolbar = new QWidget(tabs);
    auto* toolbarLayout = new QVBoxLayout(toolbar);
    toolbarLayout->addWidget(new QLabel(tr("勾选要显示的标注工具，拖动调整顺序；选择、保存、复制和 OCR 始终保留。"), toolbar));
    auto* tools = new QListWidget(toolbar);
    tools->setDragDropMode(QAbstractItemView::InternalMove);
    const QStringList ids = Preferences{}.tools;
    const QStringList names{tr("画笔"), tr("矩形"), tr("椭圆"), tr("箭头"), tr("文字")};
    QStringList order = settings.values.tools;
    for (const auto& id : ids) if (!order.contains(id)) order.append(id);
    for (const auto& id : order) {
        auto* item = new QListWidgetItem(names[ids.indexOf(id)], tools);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(settings.values.tools.contains(id) ? Qt::Checked : Qt::Unchecked);
    }
    toolbarLayout->addWidget(tools);
    auto* reset = new QPushButton(tr("恢复默认工具顺序"), toolbar);
    toolbarLayout->addWidget(reset);
    connect(reset, &QPushButton::clicked, this, [tools, ids, names] {
        tools->clear();
        for (int index = 0; index < ids.size(); ++index) {
            auto* item = new QListWidgetItem(names[index], tools);
            item->setData(Qt::UserRole, ids[index]);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        }
    });
    tabs->addTab(toolbar, tr("编辑菜单"));

    auto* about = new QLabel(tr("<h3>ScreenCapture %1</h3><p>Linux 版：截图、标注、钉图、中英文 OCR 和二维码识别。</p>"
        "<p><a href=\"https://github.com/xiaozhu1337/ScreenCapture\">ScreenCapture 项目</a></p>"
        "<p><a href=\"https://github.com/xiaozhu1337/ImageReader\">ImageReader OCR 插件</a></p>")
        .arg(QCoreApplication::applicationVersion()), tabs);
    about->setOpenExternalLinks(true);
    about->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    about->setMargin(16);
    tabs->addTab(about, tr("关于"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [=, &settings, this] {
        Preferences next = settings.values;
        next.captureKey = captureKey->keySequence();
        next.ocrKey = ocrKey->keySequence();
        next.pinKey = pinKey->keySequence();
        const QString problem = Settings::validate(next);
        if (!problem.isEmpty()) { QMessageBox::warning(this, tr("设置无效"), problem); return; }
        for (const auto& key : {next.captureKey, next.ocrKey}) {
            if (!shortcutAvailable(key)) {
                QMessageBox::warning(this, tr("快捷键被占用"), tr("%1 已被其他应用使用，请更换组合。").arg(key.toString()));
                return;
            }
        }
        if (!settings.error.isEmpty() && QMessageBox::question(this, tr("替换配置"), settings.error
            + tr("\n是否用当前设置替换配置文件？")) != QMessageBox::Yes) return;
        next.startCapture = capture->isChecked();
        next.toolbarBottom = bottom->isChecked();
        next.saveDirectory = directory->text().trimmed();
        next.lineWidth = width->value();
        next.color = QColor(color->text());
        next.tools.clear();
        for (int row = 0; row < tools->count(); ++row)
            if (tools->item(row)->checkState() == Qt::Checked) next.tools.append(tools->item(row)->data(Qt::UserRole).toString());
        const auto previous = settings.values;
        settings.values = next;
        if (!settings.save(true)) {
            settings.values = previous;
            QMessageBox::warning(this, tr("保存失败"), settings.error);
            return;
        }
        if (!settings.setAutoStart(startup->isChecked())) {
            QMessageBox::warning(this, tr("登录自启未更新"), settings.error);
            return;
        }
        accept();
    });
}
