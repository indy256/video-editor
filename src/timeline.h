#pragma once
#include <QWidget>

enum class TimePrecision { Milliseconds, Tenths };
QString timeText(qint64 ms, TimePrecision precision = TimePrecision::Milliseconds);

class Timeline : public QWidget {
    Q_OBJECT
public:
    explicit Timeline(QWidget *parent = nullptr);
    qint64 duration() const { return duration_; }
    qint64 position() const { return position_; }
    qint64 start() const { return start_; }
    qint64 end() const { return end_; }
    void setDuration(qint64 value);
    void cancelDrag() { dragTarget_ = DragTarget::None; }
    void setPosition(qint64 value);
    void setSelection(qint64 start, qint64 end);
signals:
    void selectionChanged(qint64 start, qint64 end);
    void seekRequested(qint64 position);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    enum class DragTarget { None, In, Out, Seek };
    QRectF trackRect() const;
    qint64 at(qreal x) const;
    qreal xAt(qint64 value) const;
    QRect handleRect(DragTarget marker, qreal x) const;
    void updateDrag(qreal x);
    DragTarget dragTarget_ = DragTarget::None;
    qreal dragOffset_ = 0;
    qint64 lastDragPosition_ = -1;
    qint64 duration_ = 0, position_ = 0, start_ = 0, end_ = 0;
};
