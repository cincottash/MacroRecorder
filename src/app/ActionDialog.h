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

private:
    void build();
    void accept() override;
    void pickPosition(bool withColor);
    QSpinBox* spin(int min, int max, int value, const QString& suffix = {});

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
    QLineEdit *m_label = nullptr, *m_comment = nullptr;
    QPlainTextEdit* m_text = nullptr;
};
