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

// Settings dialog content: one model picker per category (install status
// plus Download button) and the bokeh-strength slider.
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
