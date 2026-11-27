#include "timeline.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

namespace {
constexpr int trackInset = 16;
constexpr int handleWidth = 38;
constexpr int labelHeight = 20;
constexpr int handleGap = 3;
constexpr int verticalPadding = 2;
constexpr int trackTop = verticalPadding + labelHeight + handleGap;
constexpr int trackHeight = 24;
constexpr int trackBottom = trackTop + trackHeight;
constexpr int outHandleTop = trackBottom + handleGap;
constexpr int tickTop = trackBottom + 1;
constexpr int tickBottom = tickTop + 2;
constexpr int tickLabelTop = outHandleTop + 1;
}

QString timeText(qint64 ms, TimePrecision precision) {
    ms = std::max(qint64(0), ms);
    const bool tenths = precision == TimePrecision::Tenths;
    const int millisecondsPerUnit = tenths ? 100 : 1;
    const int unitsPerSecond = 1000 / millisecondsPerUnit;
    const qint64 roundedUnits = ms / millisecondsPerUnit
        + (ms % millisecondsPerUnit + millisecondsPerUnit / 2) / millisecondsPerUnit;
    const qint64 seconds = roundedUnits / unitsPerSecond;
    return QStringLiteral("%1:%2:%3.%4").arg(seconds / 3600, 2, 10, QChar('0'))
        .arg(seconds / 60 % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'))
        .arg(roundedUnits % unitsPerSecond, tenths ? 1 : 3, 10, QChar('0'));
}
Timeline::Timeline(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(tickLabelTop + labelHeight + verticalPadding);
    setMinimumWidth(320);
    setToolTip("Left-click the track to move the closest marker\nLeft-drag an IN or OUT handle\nShift-click: seek");
    setAccessibleName("Selection timeline");
}
void Timeline::setDuration(qint64 value) {
    duration_ = std::max(qint64(0), value);
    if (!duration_) cancelDrag();
    position_ = 0;
    setSelection(0, duration_);
}
void Timeline::setPosition(qint64 value) { position_ = value; update(); }
void Timeline::setSelection(qint64 start, qint64 end) {
    start_ = std::clamp(start, qint64(0), duration_);
    end_ = std::clamp(end, qint64(0), duration_);
    emit selectionChanged(start_, end_); update();
}
QRectF Timeline::trackRect() const {
    return {trackInset, trackTop, qreal(width() - 2 * trackInset), trackHeight};
}
qreal Timeline::xAt(qint64 value) const {
    const QRectF track = trackRect();
    return track.left() + (duration_ ? qreal(value) / duration_ * track.width() : 0);
}
qint64 Timeline::at(qreal x) const {
    const QRectF track = trackRect();
    return qRound64(std::clamp((x - track.left()) / track.width(), qreal(0), qreal(1)) * duration_);
}
QRect Timeline::handleRect(DragTarget marker, qreal x) const {
    const int left = std::clamp(qRound(x) - handleWidth / 2, 0, width() - handleWidth);
    const int top = marker == DragTarget::In ? verticalPadding : outHandleTop;
    return {left, top, handleWidth, labelHeight};
}
void Timeline::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const QRectF track = trackRect();
    const int trackPixels = int(track.width());
    const qreal inX = xAt(start_), outX = xAt(end_);
    p.setPen(Qt::NoPen); p.setBrush(QColor("#292f3d"));
    p.drawRoundedRect(track, 5, 5);
    if (end_ > start_) {
        p.setBrush(QColor("#245b57"));
        p.drawRect(QRectF(inX, trackTop, outX - inX, trackHeight));
    }
    p.setPen(QColor("#677083"));
    const int ticks = std::clamp(trackPixels / 105, 2, 8);
    for (int i = 0; i <= ticks; ++i) {
        const int x = int(track.left()) + trackPixels * i / ticks;
        p.drawLine(x, tickTop, x, tickBottom);
        qint64 value = duration_ * i / ticks;
        const auto label = duration_ < 60000 ? QString::number(value / 1000.0, 'f', 1) + "s" : timeText(value).left(8);
        int labelWidth = p.fontMetrics().horizontalAdvance(label) + 8;
        p.drawText(QRect(std::clamp(x - labelWidth / 2, 0, width() - labelWidth),
                         tickLabelTop, labelWidth, labelHeight), Qt::AlignCenter, label);
    }
    if (!duration_) return;
    const QColor markerColor("#63e6c6");
    for (auto marker : {DragTarget::In, DragTarget::Out}) {
        const bool isIn = marker == DragTarget::In;
        const qreal x = isIn ? inX : outX;
        const QRect handle = handleRect(marker, x);
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
    const qint64 position = at(x - dragOffset_);
    if (position == lastDragPosition_) return;
    lastDragPosition_ = position;
    if (dragTarget_ == DragTarget::In && start_ != position) setSelection(position, end_);
    else if (dragTarget_ == DragTarget::Out && end_ != position) setSelection(start_, position);
    emit seekRequested(position);
}
void Timeline::mousePressEvent(QMouseEvent *event) {
    if (!duration_ || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    const QPointF point = event->position();
    dragOffset_ = 0;
    lastDragPosition_ = -1;
    if (event->modifiers() & Qt::ShiftModifier) {
        dragTarget_ = DragTarget::Seek;
    } else if (trackRect().contains(point)) {
        const qreal toIn = qAbs(point.x() - xAt(start_));
        const qreal toOut = qAbs(point.x() - xAt(end_));
        const bool favorIn = toIn == toOut ? point.y() < trackTop + trackHeight / 2 : toIn < toOut;
        dragTarget_ = favorIn ? DragTarget::In : DragTarget::Out;
    } else {
        dragTarget_ = DragTarget::None;
        const QPoint click = point.toPoint();
        for (auto marker : {DragTarget::In, DragTarget::Out}) {
            const qint64 markerPosition = marker == DragTarget::In ? start_ : end_;
            const qreal markerX = xAt(markerPosition);
            if (handleRect(marker, markerX).contains(click)) {
                dragTarget_ = marker;
                dragOffset_ = point.x() - markerX;
                if (position_ == markerPosition) lastDragPosition_ = markerPosition;
                break;
            }
        }
        if (dragTarget_ == DragTarget::None) event->ignore();
        return;
    }
    updateDrag(point.x());
}
void Timeline::mouseMoveEvent(QMouseEvent *event) {
    if (dragTarget_ != DragTarget::None && event->buttons().testFlag(Qt::LeftButton))
        updateDrag(event->position().x());
    else
        event->ignore();
}
void Timeline::mouseReleaseEvent(QMouseEvent *event) {
    if (dragTarget_ == DragTarget::None || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    updateDrag(event->position().x());
    cancelDrag();
}
