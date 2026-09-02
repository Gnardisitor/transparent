#pragma once

#include "core/ModelCatalog.h"
#include "core/ModelManager.h"

#include <QHash>
#include <QPushButton>
#include <QWidget>

#include <utility>
#include <vector>

class QButtonGroup;
class QGroupBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;
class QVBoxLayout;

// Settings dialog content: one model picker per category (install status
// plus Download button), user-provided models scanned from the models
// directory (folder-as-state), an "Add from disk…" button per category, and
// the bokeh-strength slider.
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(ModelManager* modelManager, QWidget* parent = nullptr);

    // Reflects an externally-applied choice in the radio button without
    // re-emitting modelSelected.
    void setActiveModel(ModelCategory category, const QString& filename);

    // Reflects the bokeh strength (0-100) in the slider without emitting
    // bokehStrengthChanged.
    void setBokehStrength(int percent);

    // Disables every control while a model load or processing run is in
    // flight.
    void setBusy(bool busy);

signals:
    // User picked an installed model's radio button. MainWindow owns the
    // actual swap; this widget only reports intent.
    void modelSelected(ModelCategory category, QString filename);

    // Fired on every slider tick (0-100).
    void bokehStrengthChanged(int percent);

protected:
    // Rescans the models directory, so files added or removed behind the
    // app's back are reflected whenever the dialog is shown.
    void showEvent(QShowEvent* event) override;

private:
    struct ModelRow {
        QRadioButton* radio;
        QLabel* statusLabel;
        // Null for custom (scanned) rows, which never download.
        QPushButton* downloadButton = nullptr;
        QProgressBar* progressBar = nullptr;
    };

    QWidget* buildCategorySection(ModelCategory category, const QString& title);
    QWidget* buildBokehStrengthSection();
    void refreshRow(const QString& filename);
    // Rebuilds custom-model rows and the grayed "other files" section from a
    // fresh directory scan. Called on construction, on every show, and after
    // an import.
    void refreshCustomModels();
    void buildCustomRow(const ModelInfo& info);
    // File dialog -> validate -> copy into the models directory -> rescan.
    void importFromDisk();

    ModelManager* modelManager_;
    QHash<QString, ModelRow> rows_;

    // Per-category home for scanned custom rows, so refreshCustomModels can
    // rebuild just those. The QButtonGroup is shared with the section's
    // curated radios so everything in a category competes for the check.
    struct CustomArea {
        QWidget* container;
        QVBoxLayout* layout;
        QButtonGroup* group;
    };
    QHash<ModelCategory, CustomArea> customAreas_;
    std::vector<QWidget*> customWidgets_;
    std::vector<QWidget*> unusableWidgets_;
    QGroupBox* otherFilesBox_ = nullptr;
    QVBoxLayout* otherFilesLayout_ = nullptr;
    QList<QPushButton*> importButtons_;

    QSlider* bokehStrengthSlider_;
    QLabel* bokehStrengthValueLabel_;
    bool busy_ = false;
};
