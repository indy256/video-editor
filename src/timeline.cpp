#include "timeline.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

namespace {
constexpr int trackInset = 16;
constexpr int trackTop = 28;
constexpr int trackHeight = 24;
constexpr int trackBottom = trackTop + trackHeight;
constexpr int handleWidth = 38;
constexpr int handleHeight = 20;
}

QString timeText(qint64 ms) {
    ms = std::max(qint64(0), ms);
    return QStringLiteral("%1:%2:%3.%4").arg(ms / 3600000, 2, 10, QChar('0'))
        .arg(ms / 60000 % 60, 2, 10, QChar('0')).arg(ms / 1000 % 60, 2, 10, QChar('0'))
        .arg(ms % 1000, 3, 10, QChar('0'));
}
Timeline::Timeline(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(108);
    setMinimumWidth(320);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setToolTip("Left-drag the IN or OUT handle\nShift-click: seek");
    setAccessibleName("Selection timeline");
}
void Timeline::setDuration(qint64 value) {
    dragTarget_ = DragTarget::None;
    duration_ = std::max(qint64(0), value);
    position_ = 0;
    setSelection(0, duration_);
}
void Timeline::setPosition(qint64 value) { position_ = value; update(); }
void Timeline::setSelection(qint64 start, qint64 end) {
    start_ = std::clamp(start, qint64(0), duration_);
    end_ = std::clamp(end, qint64(0), duration_);
    emit selectionChanged(start_, end_); update();
}
qreal Timeline::xAt(qint64 value) const {
    return trackInset + (duration_ ? qreal(value) / duration_ * (width() - 2 * trackInset) : 0);
}
qint64 Timeline::at(qreal x) const {
    return qRound64(std::clamp((x - trackInset) / (width() - 2 * trackInset), qreal(0), qreal(1)) * duration_);
}
QRect Timeline::handleRect(DragTarget marker) const {
    const bool isIn = marker == DragTarget::In;
    const int x = qRound(xAt(isIn ? start_ : end_));
    const int left = std::clamp(x - handleWidth / 2, 0, width() - handleWidth);
    return {left, isIn ? trackTop - handleHeight - 4 : trackBottom + 4, handleWidth, handleHeight};
}
Timeline::DragTarget Timeline::markerAt(const QPoint &point) const {
    if (!duration_) return DragTarget::None;
    if (handleRect(DragTarget::In).contains(point)) return DragTarget::In;
    if (handleRect(DragTarget::Out).contains(point)) return DragTarget::Out;
    if (point.y() < trackTop || point.y() > trackBottom) return DragTarget::None;

    const qreal toIn = qAbs(point.x() - xAt(start_));
    const qreal toOut = qAbs(point.x() - xAt(end_));
    if (qMin(toIn, toOut) > 6) return DragTarget::None;
    if (toIn == toOut) return point.y() < trackTop + trackHeight / 2 ? DragTarget::In : DragTarget::Out;
    return toIn < toOut ? DragTarget::In : DragTarget::Out;
}
void Timeline::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const int trackWidth = width() - 2 * trackInset;
    p.setPen(Qt::NoPen); p.setBrush(QColor("#292f3d"));
    p.drawRoundedRect(QRectF(trackInset, trackTop, trackWidth, trackHeight), 5, 5);
    if (end_ > start_) {
        p.setBrush(QColor("#245b57"));
        p.drawRect(QRectF(xAt(start_), trackTop, xAt(end_) - xAt(start_), trackHeight));
    }
    p.setPen(QColor("#677083"));
    const int ticks = std::clamp(trackWidth / 105, 2, 8);
    for (int i = 0; i <= ticks; ++i) {
        int x = trackInset + trackWidth * i / ticks;
        p.drawLine(x, 80, x, 85);
        qint64 value = duration_ * i / ticks;
        const auto label = duration_ < 60000 ? QString::number(value / 1000.0, 'f', 1) + "s" : timeText(value).left(8);
        int labelWidth = p.fontMetrics().horizontalAdvance(label) + 8;
        p.drawText(QRect(std::clamp(x - labelWidth / 2, 0, width() - labelWidth), 86, labelWidth, 20), Qt::AlignCenter, label);
    }
    if (!duration_) return;
    const QColor markerColor("#63e6c6");
    for (auto marker : {DragTarget::In, DragTarget::Out}) {
        const bool isIn = marker == DragTarget::In;
        const QRect handle = handleRect(marker);
        const qreal x = xAt(isIn ? start_ : end_);
        p.setPen(QPen(markerColor, 2));
        p.drawLine(QLineF(x, isIn ? handle.bottom() : trackTop, x, isIn ? trackBottom : handle.top()));
        p.setPen(Qt::NoPen); p.setBrush(markerColor);
        p.drawRoundedRect(handle, 4, 4);
        p.setPen(QColor("#102a26"));
        p.drawText(handle, Qt::AlignCenter, isIn ? "IN" : "OUT");
    }
    const qreal playhead = xAt(position_);
    p.setPen(QPen(QColor("#ffffff"), 2));
    p.drawLine(QLineF(playhead, trackTop + 2, playhead, trackBottom - 2));
}
void Timeline::updateDrag(qreal x) {
    const qint64 value = at(x - dragOffset_);
    switch (dragTarget_) {
    case DragTarget::In:
        if (value == start_) return;
        setSelection(value, end_);
        break;
    case DragTarget::Out:
        if (value == end_) return;
        setSelection(start_, value);
        break;
    case DragTarget::Seek:
        break;
    case DragTarget::None:
        return;
    }
    emit seekRequested(value);
}
void Timeline::mousePressEvent(QMouseEvent *e) {
    if (!duration_ || e->button() != Qt::LeftButton) {
        e->ignore();
        return;
    }
    if (e->modifiers() & Qt::ShiftModifier) {
        dragTarget_ = DragTarget::Seek;
        dragOffset_ = 0;
        updateDrag(e->position().x());
        return;
    }
    dragTarget_ = markerAt(e->position().toPoint());
    if (dragTarget_ != DragTarget::None) {
        const qint64 markerPosition = dragTarget_ == DragTarget::In ? start_ : end_;
        dragOffset_ = e->position().x() - xAt(markerPosition);
        setCursor(Qt::SizeHorCursor);
    }
}
void Timeline::mouseMoveEvent(QMouseEvent *e) {
    if (e->buttons() & Qt::LeftButton) updateDrag(e->position().x());
    else setCursor(markerAt(e->position().toPoint()) == DragTarget::None ? Qt::CrossCursor : Qt::SizeHorCursor);
}
void Timeline::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) {
        e->ignore();
        return;
    }
    if (dragTarget_ == DragTarget::In || dragTarget_ == DragTarget::Out) updateDrag(e->position().x());
    dragTarget_ = DragTarget::None;
    setCursor(markerAt(e->position().toPoint()) == DragTarget::None ? Qt::CrossCursor : Qt::SizeHorCursor);
}
