#include "animations.h"

FillFade::FillFade() {
    firstRender = ros::Time::now();
    duration = ros::Duration(5);
}

void FillFade::renderFrame(QImage* image, const RenderData& data) {
    ros::Duration delta = data.timestamp - firstRender;

    double durationDelta = (double) (delta.toNSec() % duration.toNSec());
    double position = durationDelta / (double) duration.toNSec();

    QColor color = QColor::fromHsvF( position, 0.8, 0.3 );

    image->fill(color);
}


StarPath::StarPath() {
    duration = ros::Duration(3);
}

void StarPath::renderFrame(QImage* image, const RenderData& data) {

    QPainter painter;
    painter.begin( image );

    foreach(BlobInfo* blob, data.blobs){
        ros::Duration delta = data.timestamp - blob->timestamp;

        double durationDelta = (double) (delta.toNSec() % duration.toNSec());
        double position = durationDelta / (double) duration.toNSec();


        double widthPx = blob->bounds.x();
        double depthPx = blob->bounds.y();

        double centerXPx = blob->centroid.x();
        double centerYPx = blob->centroid.y();

        double radiusPx = qMax(widthPx, depthPx);

        QRectF bounds(centerXPx - (widthPx/2.0f),
                      centerYPx - (depthPx/2.0f),
                      radiusPx,
                      radiusPx);

        QBrush fillBrush;


        if(radiusPx > 5.5f){
            QConicalGradient conicalGrad(centerXPx,centerYPx, 0);
            conicalGrad.setColorAt(0, Qt::red);
            conicalGrad.setColorAt(90.0/360.0, Qt::green);
            conicalGrad.setColorAt(180.0/360.0, Qt::blue);
            conicalGrad.setColorAt(270.0/360.0, Qt::magenta);
            conicalGrad.setColorAt(360.0/360.0, Qt::yellow);

            fillBrush = QBrush( conicalGrad );

        }
        else {
            QRadialGradient radialGrad(QPointF(centerXPx,centerYPx), radiusPx);
            radialGrad.setColorAt(0, Qt::white);
            radialGrad.setColorAt(1.0f, Qt::black);

            fillBrush = QBrush( radialGrad );
        }

        QPainterPath fillPath;

        fillPath.addEllipse(bounds);

        painter.fillPath(fillPath, fillBrush);


    }
    painter.end();
}