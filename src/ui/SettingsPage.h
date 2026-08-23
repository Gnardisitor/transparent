#pragma once

#include "core/ModelCatalog.h"

#include <QHash>
#include <QWidget>

class ModelManager;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;

// Content of MainWindow's Settings dialog: one curated model picker per
// category (segmentation, upscale), each row showing install status with
// its own Download button, radio selection restricted to models that are
// actually installed; plus the bokeh-strength slider. Opened from a
// menu-bar action (like Help), not nested inside Simple/Advanced mode,
// since these are "set occasionally, then forget" preferences, not
// per-run ones. See PLAN.md's "Model management" and "Bokeh strength
// control" sections for the full design this implements.
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(ModelManager* modelManager, QWidget* parent = nullptr);

    // Reflects an externally-applied choice (the persisted QSettings value
    // at startup, or MainWindow confirming a live swap actually landed) in
    // the relevant radio button, without re-emitting modelSelected.
    void setActiveModel(ModelCategory category, const QString& filename);

    // Reflects the persisted/current bokeh strength (0-100) in the slider
    // without re-emitting bokehStrengthChanged.
    void setBokehStrength(int percent);

    // Disables every control while MainWindow is mid-swap (loading a newly
    // selected model asynchronously) or mid-processing an image, so a
    // second change can't get queued up before the first one lands.
    void setBusy(bool busy);

signals:
    // Fired when the user picks an already-installed model's radio button.
    // MainWindow owns the actual swap (async load, then setModel() on the
    // relevant Pipeline steps) — this widget only ever reports intent.
    void modelSelected(ModelCategory category, QString filename);

    // Fired on every slider tick (0-100). MainWindow live-previews this
    // when it can (see PLAN.md's "Bokeh strength control" section) and
    // always persists it to QSettings for the next real run.
    void bokehStrengthChanged(int percent);

private:
    struct ModelRow {
        QRadioButton* radio;
        QLabel* statusLabel;
        QPushButton* downloadButton;
        QProgressBar* progressBar;
    };

    QWidget* buildCategorySection(ModelCategory category, const QString& title);
    QWidget* buildBokehStrengthSection();
    void refreshRow(const QString& filename);

    ModelManager* modelManager_;
    QHash<QString, ModelRow> rows_;
    QSlider* bokehStrengthSlider_;
    QLabel* bokehStrengthValueLabel_;
    bool busy_ = false;
};
