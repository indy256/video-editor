#include "timeline.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

QString timeText(qint64 ms) {
    ms = std::max(qint64(0), ms);
    return QStringLiteral("%1:%2:%3.%4").arg(ms / 3600000, 2, 10, QChar('0'))
        .arg(ms / 60000 % 60, 2, 10, QChar('0')).arg(ms / 1000 % 60, 2, 10, QChar('0'))
        .arg(ms % 1000, 3, 10, QChar('0'));
}
Timeline::Timeline(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(94);
    setMinimumWidth(320);
    setCursor(Qt::CrossCursor);
    setToolTip("Left-click / drag: selection start\nRight-click / drag: selection end\nShift-click: seek");
    setAccessibleName("Selection timeline");
}
void Timeline::setDuration(qint64 value) {
    duration_ = std::max(qint64(0), value);
    start_ = 0; end_ = duration_; position_ = 0;
    emit selectionChanged(start_, end_); update();
}
void Timeline::setPosition(qint64 value) { position_ = value; update(); }
void Timeline::setSelection(qint64 start, qint64 end) {
    start_ = std::clamp(start, qint64(0), duration_);
    end_ = std::clamp(end, qint64(0), duration_);
    emit selectionChanged(start_, end_); update();
}
int Timeline::xAt(qint64 value) const {
    return 16 + (duration_ ? int(double(value) / duration_ * (width() - 32)) : 0);
}
qint64 Timeline::at(double x) const {
    return qRound64(std::clamp((x - 16) / (width() - 32), 0.0, 1.0) * duration_);
}
void Timeline::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen); p.setBrush(QColor("#292f3d"));
    p.drawRoundedRect(QRectF(16, 30, width() - 32, 28), 5, 5);
    if (end_ > start_) {
        p.setBrush(QColor("#245b57"));
        p.drawRect(QRectF(xAt(start_), 30, xAt(end_) - xAt(start_), 28));
    }
    p.setPen(QColor("#677083"));
    const int ticks = std::clamp((width() - 32) / 105, 2, 8);
    for (int i = 0; i <= ticks; ++i) {
        int x = 16 + (width() - 32) * i / ticks;
        p.drawLine(x, 62, x, 67);
        qint64 value = duration_ * i / ticks;
        const auto label = duration_ < 60000 ? QString::number(value / 1000.0, 'f', 1) + "s" : timeText(value).left(8);
        int labelWidth = p.fontMetrics().horizontalAdvance(label) + 8;
        p.drawText(QRect(std::clamp(x - labelWidth / 2, 0, width() - labelWidth), 70, labelWidth, 20), Qt::AlignCenter, label);
    }
    if (!duration_) return;
    for (auto marker : {qMakePair(start_, QString("IN")), qMakePair(end_, QString("OUT"))}) {
        int x = xAt(marker.first);
        p.setPen(QPen(QColor("#63e6c6"), 2)); p.drawLine(x, 27, x, 60);
        p.drawText(QRect(std::clamp(x - 18, 0, width() - 36), 5, 36, 20), Qt::AlignCenter, marker.second);
    }
    p.setPen(QPen(QColor("#ffffff"), 2)); p.drawLine(xAt(position_), 25, xAt(position_), 63);
}
void Timeline::select(QMouseEvent *e) {
    if (!duration_) return;
    qint64 value = at(e->position().x());
    if (e->modifiers() & Qt::ShiftModifier) { emit seekRequested(value); return; }
    if (e->buttons() & Qt::LeftButton) start_ = value;
    else if (e->buttons() & Qt::RightButton) end_ = value;
    else return;
    emit selectionChanged(start_, end_); emit seekRequested(value); update();
}
void Timeline::mousePressEvent(QMouseEvent *e) { select(e); }
void Timeline::mouseMoveEvent(QMouseEvent *e) { select(e); }
void Timeline::mouseReleaseEvent(QMouseEvent *e) { e->accept(); }
