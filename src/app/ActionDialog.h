// Add / edit one action. Shows only the fields that action type uses; validates when OK is clicked.
#pragma once

#include "core/Action.h"
#include "core/Input.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

class ActionDialog : public QDialog {
    Q_OBJECT
public:
    ActionDialog(const core::Action& a, const QStringList& labels, core::Injector* inj, QWidget* parent);
    core::Action result() const { return m_a; }

    static QStringList keyNames();   // names offered in the key drop-down
    bool hasPosition() const { return m_x != nullptr; }
    // Capture position hotkey: take the mouse pointer's position (and color, for a pixel wait) now.
    void captureNow();

private:
    void build();
    void accept() override;
    void pickPosition(bool withColor);
    QSpinBox* spin(int min, int max, int value, const QString& suffix = {}, bool groupDigits = true);

    core::Action m_a;
    QStringList m_labels;
    core::Injector* m_inj;
    QFormLayout* m_form = nullptr;
    QLabel* m_error = nullptr;

    QSpinBox *m_x = nullptr, *m_y = nullptr, *m_fx = nullptr, *m_fy = nullptr, *m_ms = nullptr, *m_notches = nullptr;
    QSpinBox *m_tol = nullptr, *m_count = nullptr, *m_delay = nullptr;
    QComboBox *m_button = nullptr, *m_clicks = nullptr, *m_key = nullptr, *m_match = nullptr, *m_target = nullptr, *m_mode = nullptr;
    QCheckBox *m_rel = nullptr, *m_horiz = nullptr, *m_enabled = nullptr;
    QList<QCheckBox*> m_mods;
    QLineEdit *m_exe = nullptr, *m_title = nullptr, *m_color = nullptr, *m_program = nullptr, *m_args = nullptr, *m_message = nullptr;
    QLineEdit *m_label = nullptr, *m_comment = nullptr, *m_file = nullptr;
    QComboBox* m_fileWhen = nullptr;
    QPlainTextEdit* m_text = nullptr;

    // Variables, waits, windows, pictures
    QLineEdit *m_var = nullptr, *m_expr = nullptr, *m_default = nullptr;
    QSpinBox* m_maxMs = nullptr;
    QComboBox* m_state = nullptr;
    QSpinBox *m_rx = nullptr, *m_ry = nullptr, *m_rw = nullptr, *m_rh = nullptr;
    QComboBox* m_lookIn = nullptr;
    QSpinBox *m_ax = nullptr, *m_ay = nullptr, *m_aw = nullptr, *m_ah = nullptr;
    QWidget* m_areaRow = nullptr;
    QComboBox* m_findDo = nullptr;
    QSpinBox *m_ox = nullptr, *m_oy = nullptr;
    QCheckBox *m_waitGone = nullptr, *m_stopMissing = nullptr, *m_usePicture = nullptr;
    QLabel *m_imageView = nullptr, *m_imageInfo = nullptr;
    QByteArray m_image;   // PNG
    void setImage(const QImage& img);
    void pickArea();
    void pickWindow();

public:
    // Tests: the editor's picture (Find Image)
    QByteArray imageBytes() const { return m_image; }
};
